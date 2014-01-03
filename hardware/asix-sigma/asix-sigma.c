/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010-2012 Håvard Espeland <gus@ping.uio.no>,
 * Copyright (C) 2010 Martin Stensgård <mastensg@ping.uio.no>
 * Copyright (C) 2010 Carl Henrik Lunde <chlunde@ping.uio.no>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * ASIX SIGMA/SIGMA2 logic analyzer driver
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <ftdi.h>
#include <string.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "asix-sigma.h"

#define USB_VENDOR			0xa600
#define USB_PRODUCT			0xa000
#define USB_DESCRIPTION			"ASIX SIGMA"
#define USB_VENDOR_NAME			"ASIX"
#define USB_MODEL_NAME			"SIGMA"
#define USB_MODEL_VERSION		""
#define TRIGGER_TYPE 			"rf10"
#define NUM_PROBES			16

SR_PRIV struct sr_dev_driver asix_sigma_driver_info;
static struct sr_dev_driver *di = &asix_sigma_driver_info;
static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data);

static const uint64_t samplerates[] = {
	SR_KHZ(200),
	SR_KHZ(250),
	SR_KHZ(500),
	SR_MHZ(1),
	SR_MHZ(5),
	SR_MHZ(10),
	SR_MHZ(25),
	SR_MHZ(50),
	SR_MHZ(100),
	SR_MHZ(200),
};

/*
 * Probe numbers seem to go from 1-16, according to this image:
 * http://tools.asix.net/img/sigma_sigmacab_pins_720.jpg
 * (the cable has two additional GND pins, and a TI and TO pin)
 */
static const char *probe_names[NUM_PROBES + 1] = {
	"1", "2", "3", "4", "5", "6", "7", "8",
	"9", "10", "11", "12", "13", "14", "15", "16",
	NULL,
};

static const int32_t hwcaps[] = {
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_SAMPLERATE,
	SR_CONF_CAPTURE_RATIO,
	SR_CONF_LIMIT_MSEC,
};

/* Force the FPGA to reboot. */
static uint8_t suicide[] = {
	0x84, 0x84, 0x88, 0x84, 0x88, 0x84, 0x88, 0x84,
};

/* Prepare to upload firmware (FPGA specific). */
static uint8_t init_array[] = {
	0x03, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
};

/* Initialize the logic analyzer mode. */
static uint8_t logic_mode_start[] = {
	0x00, 0x40, 0x0f, 0x25, 0x35, 0x40,
	0x2a, 0x3a, 0x40, 0x03, 0x20, 0x38,
};

static const char *firmware_files[] = {
	"asix-sigma-50.fw",	/* 50 MHz, supports 8 bit fractions */
	"asix-sigma-100.fw",	/* 100 MHz */
	"asix-sigma-200.fw",	/* 200 MHz */
	"asix-sigma-50sync.fw",	/* Synchronous clock from pin */
	"asix-sigma-phasor.fw",	/* Frequency counter */
};

static int sigma_read(void *buf, size_t size, struct dev_context *devc)
{
	int ret;

	ret = ftdi_read_data(&devc->ftdic, (unsigned char *)buf, size);
	if (ret < 0) {
		sr_err("ftdi_read_data failed: %s",
		       ftdi_get_error_string(&devc->ftdic));
	}

	return ret;
}

static int sigma_write(void *buf, size_t size, struct dev_context *devc)
{
	int ret;

	ret = ftdi_write_data(&devc->ftdic, (unsigned char *)buf, size);
	if (ret < 0) {
		sr_err("ftdi_write_data failed: %s",
		       ftdi_get_error_string(&devc->ftdic));
	} else if ((size_t) ret != size) {
		sr_err("ftdi_write_data did not complete write.");
	}

	return ret;
}

static int sigma_write_register(uint8_t reg, uint8_t *data, size_t len,
				struct dev_context *devc)
{
	size_t i;
	uint8_t buf[len + 2];
	int idx = 0;

	buf[idx++] = REG_ADDR_LOW | (reg & 0xf);
	buf[idx++] = REG_ADDR_HIGH | (reg >> 4);

	for (i = 0; i < len; ++i) {
		buf[idx++] = REG_DATA_LOW | (data[i] & 0xf);
		buf[idx++] = REG_DATA_HIGH_WRITE | (data[i] >> 4);
	}

	return sigma_write(buf, idx, devc);
}

static int sigma_set_register(uint8_t reg, uint8_t value, struct dev_context *devc)
{
	return sigma_write_register(reg, &value, 1, devc);
}

static int sigma_read_register(uint8_t reg, uint8_t *data, size_t len,
			       struct dev_context *devc)
{
	uint8_t buf[3];

	buf[0] = REG_ADDR_LOW | (reg & 0xf);
	buf[1] = REG_ADDR_HIGH | (reg >> 4);
	buf[2] = REG_READ_ADDR;

	sigma_write(buf, sizeof(buf), devc);

	return sigma_read(data, len, devc);
}

static uint8_t sigma_get_register(uint8_t reg, struct dev_context *devc)
{
	uint8_t value;

	if (1 != sigma_read_register(reg, &value, 1, devc)) {
		sr_err("sigma_get_register: 1 byte expected");
		return 0;
	}

	return value;
}

static int sigma_read_pos(uint32_t *stoppos, uint32_t *triggerpos,
			  struct dev_context *devc)
{
	uint8_t buf[] = {
		REG_ADDR_LOW | READ_TRIGGER_POS_LOW,

		REG_READ_ADDR | NEXT_REG,
		REG_READ_ADDR | NEXT_REG,
		REG_READ_ADDR | NEXT_REG,
		REG_READ_ADDR | NEXT_REG,
		REG_READ_ADDR | NEXT_REG,
		REG_READ_ADDR | NEXT_REG,
	};
	uint8_t result[6];

	sigma_write(buf, sizeof(buf), devc);

	sigma_read(result, sizeof(result), devc);

	*triggerpos = result[0] | (result[1] << 8) | (result[2] << 16);
	*stoppos = result[3] | (result[4] << 8) | (result[5] << 16);

	/* Not really sure why this must be done, but according to spec. */
	if ((--*stoppos & 0x1ff) == 0x1ff)
		stoppos -= 64;

	if ((*--triggerpos & 0x1ff) == 0x1ff)
		triggerpos -= 64;

	return 1;
}

static int sigma_read_dram(uint16_t startchunk, size_t numchunks,
			   uint8_t *data, struct dev_context *devc)
{
	size_t i;
	uint8_t buf[4096];
	int idx = 0;

	/* Send the startchunk. Index start with 1. */
	buf[0] = startchunk >> 8;
	buf[1] = startchunk & 0xff;
	sigma_write_register(WRITE_MEMROW, buf, 2, devc);

	/* Read the DRAM. */
	buf[idx++] = REG_DRAM_BLOCK;
	buf[idx++] = REG_DRAM_WAIT_ACK;

	for (i = 0; i < numchunks; ++i) {
		/* Alternate bit to copy from DRAM to cache. */
		if (i != (numchunks - 1))
			buf[idx++] = REG_DRAM_BLOCK | (((i + 1) % 2) << 4);

		buf[idx++] = REG_DRAM_BLOCK_DATA | ((i % 2) << 4);

		if (i != (numchunks - 1))
			buf[idx++] = REG_DRAM_WAIT_ACK;
	}

	sigma_write(buf, idx, devc);

	return sigma_read(data, numchunks * CHUNK_SIZE, devc);
}

/* Upload trigger look-up tables to Sigma. */
static int sigma_write_trigger_lut(struct triggerlut *lut, struct dev_context *devc)
{
	int i;
	uint8_t tmp[2];
	uint16_t bit;

	/* Transpose the table and send to Sigma. */
	for (i = 0; i < 16; ++i) {
		bit = 1 << i;

		tmp[0] = tmp[1] = 0;

		if (lut->m2d[0] & bit)
			tmp[0] |= 0x01;
		if (lut->m2d[1] & bit)
			tmp[0] |= 0x02;
		if (lut->m2d[2] & bit)
			tmp[0] |= 0x04;
		if (lut->m2d[3] & bit)
			tmp[0] |= 0x08;

		if (lut->m3 & bit)
			tmp[0] |= 0x10;
		if (lut->m3s & bit)
			tmp[0] |= 0x20;
		if (lut->m4 & bit)
			tmp[0] |= 0x40;

		if (lut->m0d[0] & bit)
			tmp[1] |= 0x01;
		if (lut->m0d[1] & bit)
			tmp[1] |= 0x02;
		if (lut->m0d[2] & bit)
			tmp[1] |= 0x04;
		if (lut->m0d[3] & bit)
			tmp[1] |= 0x08;

		if (lut->m1d[0] & bit)
			tmp[1] |= 0x10;
		if (lut->m1d[1] & bit)
			tmp[1] |= 0x20;
		if (lut->m1d[2] & bit)
			tmp[1] |= 0x40;
		if (lut->m1d[3] & bit)
			tmp[1] |= 0x80;

		sigma_write_register(WRITE_TRIGGER_SELECT0, tmp, sizeof(tmp),
				     devc);
		sigma_set_register(WRITE_TRIGGER_SELECT1, 0x30 | i, devc);
	}

	/* Send the parameters */
	sigma_write_register(WRITE_TRIGGER_SELECT0, (uint8_t *) &lut->params,
			     sizeof(lut->params), devc);

	return SR_OK;
}

/* Generate the bitbang stream for programming the FPGA. */
static int bin2bitbang(const char *filename,
		       unsigned char **buf, size_t *buf_size)
{
	FILE *f;
	unsigned long file_size;
	unsigned long offset = 0;
	unsigned char *p;
	uint8_t *firmware;
	unsigned long fwsize = 0;
	const int buffer_size = 65536;
	size_t i;
	int c, bit, v;
	uint32_t imm = 0x3f6df2ab;

	f = g_fopen(filename, "rb");
	if (!f) {
		sr_err("g_fopen(\"%s\", \"rb\")", filename);
		return SR_ERR;
	}

	if (-1 == fseek(f, 0, SEEK_END)) {
		sr_err("fseek on %s failed", filename);
		fclose(f);
		return SR_ERR;
	}

	file_size = ftell(f);

	fseek(f, 0, SEEK_SET);

	if (!(firmware = g_try_malloc(buffer_size))) {
		sr_err("%s: firmware malloc failed", __func__);
		fclose(f);
		return SR_ERR_MALLOC;
	}

	while ((c = getc(f)) != EOF) {
		imm = (imm + 0xa853753) % 177 + (imm * 0x8034052);
		firmware[fwsize++] = c ^ imm;
	}
	fclose(f);

	if(fwsize != file_size) {
	    sr_err("%s: Error reading firmware", filename);
	    fclose(f);
	    g_free(firmware);
	    return SR_ERR;
	}

	*buf_size = fwsize * 2 * 8;

	*buf = p = (unsigned char *)g_try_malloc(*buf_size);
	if (!p) {
		sr_err("%s: buf/p malloc failed", __func__);
		g_free(firmware);
		return SR_ERR_MALLOC;
	}

	for (i = 0; i < fwsize; ++i) {
		for (bit = 7; bit >= 0; --bit) {
			v = firmware[i] & 1 << bit ? 0x40 : 0x00;
			p[offset++] = v | 0x01;
			p[offset++] = v;
		}
	}

	g_free(firmware);

	if (offset != *buf_size) {
		g_free(*buf);
		sr_err("Error reading firmware %s "
		       "offset=%ld, file_size=%ld, buf_size=%zd.",
		       filename, offset, file_size, *buf_size);

		return SR_ERR;
	}

	return SR_OK;
}

static void clear_helper(void *priv)
{
	struct dev_context *devc;

	devc = priv;

	ftdi_deinit(&devc->ftdic);
}

static int dev_clear(void)
{
	return std_dev_clear(di, clear_helper);
}

static int init(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static GSList *scan(GSList *options)
{
	struct sr_dev_inst *sdi;
	struct sr_probe *probe;
	struct drv_context *drvc;
	struct dev_context *devc;
	GSList *devices;
	struct ftdi_device_list *devlist;
	char serial_txt[10];
	uint32_t serial;
	int ret, i;

	(void)options;

	drvc = di->priv;

	devices = NULL;

	if (!(devc = g_try_malloc(sizeof(struct dev_context)))) {
		sr_err("%s: devc malloc failed", __func__);
		return NULL;
	}

	ftdi_init(&devc->ftdic);

	/* Look for SIGMAs. */

	if ((ret = ftdi_usb_find_all(&devc->ftdic, &devlist,
	    USB_VENDOR, USB_PRODUCT)) <= 0) {
		if (ret < 0)
			sr_err("ftdi_usb_find_all(): %d", ret);
		goto free;
	}

	/* Make sure it's a version 1 or 2 SIGMA. */
	ftdi_usb_get_strings(&devc->ftdic, devlist->dev, NULL, 0, NULL, 0,
			     serial_txt, sizeof(serial_txt));
	sscanf(serial_txt, "%x", &serial);

	if (serial < 0xa6010000 || serial > 0xa602ffff) {
		sr_err("Only SIGMA and SIGMA2 are supported "
		       "in this version of libsigrok.");
		goto free;
	}

	sr_info("Found ASIX SIGMA - Serial: %s", serial_txt);

	devc->cur_samplerate = 0;
	devc->period_ps = 0;
	devc->limit_msec = 0;
	devc->cur_firmware = -1;
	devc->num_probes = 0;
	devc->samples_per_event = 0;
	devc->capture_ratio = 50;
	devc->use_triggers = 0;

	/* Register SIGMA device. */
	if (!(sdi = sr_dev_inst_new(0, SR_ST_INITIALIZING, USB_VENDOR_NAME,
				    USB_MODEL_NAME, USB_MODEL_VERSION))) {
		sr_err("%s: sdi was NULL", __func__);
		goto free;
	}
	sdi->driver = di;

	for (i = 0; probe_names[i]; i++) {
		if (!(probe = sr_probe_new(i, SR_PROBE_LOGIC, TRUE,
				probe_names[i])))
			return NULL;
		sdi->probes = g_slist_append(sdi->probes, probe);
	}

	devices = g_slist_append(devices, sdi);
	drvc->instances = g_slist_append(drvc->instances, sdi);
	sdi->priv = devc;

	/* We will open the device again when we need it. */
	ftdi_list_free(&devlist);

	return devices;

free:
	ftdi_deinit(&devc->ftdic);
	g_free(devc);
	return NULL;
}

static GSList *dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int upload_firmware(int firmware_idx, struct dev_context *devc)
{
	int ret;
	unsigned char *buf;
	unsigned char pins;
	size_t buf_size;
	unsigned char result[32];
	char firmware_path[128];

	/* Make sure it's an ASIX SIGMA. */
	if ((ret = ftdi_usb_open_desc(&devc->ftdic,
		USB_VENDOR, USB_PRODUCT, USB_DESCRIPTION, NULL)) < 0) {
		sr_err("ftdi_usb_open failed: %s",
		       ftdi_get_error_string(&devc->ftdic));
		return 0;
	}

	if ((ret = ftdi_set_bitmode(&devc->ftdic, 0xdf, BITMODE_BITBANG)) < 0) {
		sr_err("ftdi_set_bitmode failed: %s",
		       ftdi_get_error_string(&devc->ftdic));
		return 0;
	}

	/* Four times the speed of sigmalogan - Works well. */
	if ((ret = ftdi_set_baudrate(&devc->ftdic, 750000)) < 0) {
		sr_err("ftdi_set_baudrate failed: %s",
		       ftdi_get_error_string(&devc->ftdic));
		return 0;
	}

	/* Force the FPGA to reboot. */
	sigma_write(suicide, sizeof(suicide), devc);
	sigma_write(suicide, sizeof(suicide), devc);
	sigma_write(suicide, sizeof(suicide), devc);
	sigma_write(suicide, sizeof(suicide), devc);

	/* Prepare to upload firmware (FPGA specific). */
	sigma_write(init_array, sizeof(init_array), devc);

	ftdi_usb_purge_buffers(&devc->ftdic);

	/* Wait until the FPGA asserts INIT_B. */
	while (1) {
		ret = sigma_read(result, 1, devc);
		if (result[0] & 0x20)
			break;
	}

	/* Prepare firmware. */
	snprintf(firmware_path, sizeof(firmware_path), "%s/%s", FIRMWARE_DIR,
		 firmware_files[firmware_idx]);

	if ((ret = bin2bitbang(firmware_path, &buf, &buf_size)) != SR_OK) {
		sr_err("An error occured while reading the firmware: %s",
		       firmware_path);
		return ret;
	}

	/* Upload firmare. */
	sr_info("Uploading firmware file '%s'.", firmware_files[firmware_idx]);
	sigma_write(buf, buf_size, devc);

	g_free(buf);

	if ((ret = ftdi_set_bitmode(&devc->ftdic, 0x00, BITMODE_RESET)) < 0) {
		sr_err("ftdi_set_bitmode failed: %s",
		       ftdi_get_error_string(&devc->ftdic));
		return SR_ERR;
	}

	ftdi_usb_purge_buffers(&devc->ftdic);

	/* Discard garbage. */
	while (1 == sigma_read(&pins, 1, devc))
		;

	/* Initialize the logic analyzer mode. */
	sigma_write(logic_mode_start, sizeof(logic_mode_start), devc);

	/* Expect a 3 byte reply. */
	ret = sigma_read(result, 3, devc);
	if (ret != 3 ||
	    result[0] != 0xa6 || result[1] != 0x55 || result[2] != 0xaa) {
		sr_err("Configuration failed. Invalid reply received.");
		return SR_ERR;
	}

	devc->cur_firmware = firmware_idx;

	sr_info("Firmware uploaded.");

	return SR_OK;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;

	/* Make sure it's an ASIX SIGMA. */
	if ((ret = ftdi_usb_open_desc(&devc->ftdic,
		USB_VENDOR, USB_PRODUCT, USB_DESCRIPTION, NULL)) < 0) {

		sr_err("ftdi_usb_open failed: %s",
		       ftdi_get_error_string(&devc->ftdic));

		return 0;
	}

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int set_samplerate(const struct sr_dev_inst *sdi, uint64_t samplerate)
{
	struct dev_context *devc;
	unsigned int i;
	int ret;

	devc = sdi->priv;
	ret = SR_OK;

	for (i = 0; i < ARRAY_SIZE(samplerates); i++) {
		if (samplerates[i] == samplerate)
			break;
	}
	if (samplerates[i] == 0)
		return SR_ERR_SAMPLERATE;

	if (samplerate <= SR_MHZ(50)) {
		ret = upload_firmware(0, devc);
		devc->num_probes = 16;
	}
	if (samplerate == SR_MHZ(100)) {
		ret = upload_firmware(1, devc);
		devc->num_probes = 8;
	}
	else if (samplerate == SR_MHZ(200)) {
		ret = upload_firmware(2, devc);
		devc->num_probes = 4;
	}

	devc->cur_samplerate = samplerate;
	devc->period_ps = 1000000000000ULL / samplerate;
	devc->samples_per_event = 16 / devc->num_probes;
	devc->state.state = SIGMA_IDLE;

	return ret;
}

/*
 * In 100 and 200 MHz mode, only a single pin rising/falling can be
 * set as trigger. In other modes, two rising/falling triggers can be set,
 * in addition to value/mask trigger for any number of probes.
 *
 * The Sigma supports complex triggers using boolean expressions, but this
 * has not been implemented yet.
 */
static int configure_probes(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	const struct sr_probe *probe;
	const GSList *l;
	int trigger_set = 0;
	int probebit;

	memset(&devc->trigger, 0, sizeof(struct sigma_trigger));

	for (l = sdi->probes; l; l = l->next) {
		probe = (struct sr_probe *)l->data;
		probebit = 1 << (probe->index);

		if (!probe->enabled || !probe->trigger)
			continue;

		if (devc->cur_samplerate >= SR_MHZ(100)) {
			/* Fast trigger support. */
			if (trigger_set) {
				sr_err("Only a single pin trigger in 100 and "
				       "200MHz mode is supported.");
				return SR_ERR;
			}
			if (probe->trigger[0] == 'f')
				devc->trigger.fallingmask |= probebit;
			else if (probe->trigger[0] == 'r')
				devc->trigger.risingmask |= probebit;
			else {
				sr_err("Only rising/falling trigger in 100 "
				       "and 200MHz mode is supported.");
				return SR_ERR;
			}

			++trigger_set;
		} else {
			/* Simple trigger support (event). */
			if (probe->trigger[0] == '1') {
				devc->trigger.simplevalue |= probebit;
				devc->trigger.simplemask |= probebit;
			}
			else if (probe->trigger[0] == '0') {
				devc->trigger.simplevalue &= ~probebit;
				devc->trigger.simplemask |= probebit;
			}
			else if (probe->trigger[0] == 'f') {
				devc->trigger.fallingmask |= probebit;
				++trigger_set;
			}
			else if (probe->trigger[0] == 'r') {
				devc->trigger.risingmask |= probebit;
				++trigger_set;
			}

			/*
			 * Actually, Sigma supports 2 rising/falling triggers,
			 * but they are ORed and the current trigger syntax
			 * does not permit ORed triggers.
			 */
			if (trigger_set > 1) {
				sr_err("Only 1 rising/falling trigger "
				       "is supported.");
				return SR_ERR;
			}
		}

		if (trigger_set)
			devc->use_triggers = 1;
	}

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	/* TODO */
	if (sdi->status == SR_ST_ACTIVE)
		ftdi_usb_close(&devc->ftdic);

	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int cleanup(void)
{
	return dev_clear();
}

static int config_get(int id, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_probe_group *probe_group)
{
	struct dev_context *devc;

	(void)probe_group;

	switch (id) {
	case SR_CONF_SAMPLERATE:
		if (sdi) {
			devc = sdi->priv;
			*data = g_variant_new_uint64(devc->cur_samplerate);
		} else
			return SR_ERR;
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(int id, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_probe_group *probe_group)
{
	struct dev_context *devc;
	int ret;

	(void)probe_group;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	if (id == SR_CONF_SAMPLERATE) {
		ret = set_samplerate(sdi, g_variant_get_uint64(data));
	} else if (id == SR_CONF_LIMIT_MSEC) {
		devc->limit_msec = g_variant_get_uint64(data);
		if (devc->limit_msec > 0)
			ret = SR_OK;
		else
			ret = SR_ERR;
	} else if (id == SR_CONF_CAPTURE_RATIO) {
		devc->capture_ratio = g_variant_get_uint64(data);
		if (devc->capture_ratio < 0 || devc->capture_ratio > 100)
			ret = SR_ERR;
		else
			ret = SR_OK;
	} else {
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_probe_group *probe_group)
{
	GVariant *gvar;
	GVariantBuilder gvb;

	(void)sdi;
	(void)probe_group;

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwcaps, ARRAY_SIZE(hwcaps), sizeof(int32_t));
		break;
	case SR_CONF_SAMPLERATE:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
		gvar = g_variant_new_fixed_array(G_VARIANT_TYPE("t"), samplerates,
				ARRAY_SIZE(samplerates), sizeof(uint64_t));
		g_variant_builder_add(&gvb, "{sv}", "samplerates", gvar);
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_TRIGGER_TYPE:
		*data = g_variant_new_string(TRIGGER_TYPE);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

/* Software trigger to determine exact trigger position. */
static int get_trigger_offset(uint16_t *samples, uint16_t last_sample,
			      struct sigma_trigger *t)
{
	int i;

	for (i = 0; i < 8; ++i) {
		if (i > 0)
			last_sample = samples[i-1];

		/* Simple triggers. */
		if ((samples[i] & t->simplemask) != t->simplevalue)
			continue;

		/* Rising edge. */
		if ((last_sample & t->risingmask) != 0 || (samples[i] &
		    t->risingmask) != t->risingmask)
			continue;

		/* Falling edge. */
		if ((last_sample & t->fallingmask) != t->fallingmask ||
		    (samples[i] & t->fallingmask) != 0)
			continue;

		break;
	}

	/* If we did not match, return original trigger pos. */
	return i & 0x7;
}

/*
 * Decode chunk of 1024 bytes, 64 clusters, 7 events per cluster.
 * Each event is 20ns apart, and can contain multiple samples.
 *
 * For 200 MHz, events contain 4 samples for each channel, spread 5 ns apart.
 * For 100 MHz, events contain 2 samples for each channel, spread 10 ns apart.
 * For 50 MHz and below, events contain one sample for each channel,
 * spread 20 ns apart.
 */
static int decode_chunk_ts(uint8_t *buf, uint16_t *lastts,
			   uint16_t *lastsample, int triggerpos,
			   uint16_t limit_chunk, void *cb_data)
{
	struct sr_dev_inst *sdi = cb_data;
	struct dev_context *devc = sdi->priv;
	uint16_t tsdiff, ts;
	uint16_t samples[65536 * devc->samples_per_event];
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	int i, j, k, l, numpad, tosend;
	size_t n = 0, sent = 0;
	int clustersize = EVENTS_PER_CLUSTER * devc->samples_per_event;
	uint16_t *event;
	uint16_t cur_sample;
	int triggerts = -1;

	/* Check if trigger is in this chunk. */
	if (triggerpos != -1) {
		if (devc->cur_samplerate <= SR_MHZ(50))
			triggerpos -= EVENTS_PER_CLUSTER - 1;

		if (triggerpos < 0)
			triggerpos = 0;

		/* Find in which cluster the trigger occured. */
		triggerts = triggerpos / 7;
	}

	/* For each ts. */
	for (i = 0; i < 64; ++i) {
		ts = *(uint16_t *) &buf[i * 16];
		tsdiff = ts - *lastts;
		*lastts = ts;

		/* Decode partial chunk. */
		if (limit_chunk && ts > limit_chunk)
			return SR_OK;

		/* Pad last sample up to current point. */
		numpad = tsdiff * devc->samples_per_event - clustersize;
		if (numpad > 0) {
			for (j = 0; j < numpad; ++j)
				samples[j] = *lastsample;

			n = numpad;
		}

		/* Send samples between previous and this timestamp to sigrok. */
		sent = 0;
		while (sent < n) {
			tosend = MIN(2048, n - sent);

			packet.type = SR_DF_LOGIC;
			packet.payload = &logic;
			logic.length = tosend * sizeof(uint16_t);
			logic.unitsize = 2;
			logic.data = samples + sent;
			sr_session_send(devc->cb_data, &packet);

			sent += tosend;
		}
		n = 0;

		event = (uint16_t *) &buf[i * 16 + 2];
		cur_sample = 0;

		/* For each event in cluster. */
		for (j = 0; j < 7; ++j) {

			/* For each sample in event. */
			for (k = 0; k < devc->samples_per_event; ++k) {
				cur_sample = 0;

				/* For each probe. */
				for (l = 0; l < devc->num_probes; ++l)
					cur_sample |= (!!(event[j] & (1 << (l *
					   devc->samples_per_event + k)))) << l;

				samples[n++] = cur_sample;
			}
		}

		/* Send data up to trigger point (if triggered). */
		sent = 0;
		if (i == triggerts) {
			/*
			 * Trigger is not always accurate to sample because of
			 * pipeline delay. However, it always triggers before
			 * the actual event. We therefore look at the next
			 * samples to pinpoint the exact position of the trigger.
			 */
			tosend = get_trigger_offset(samples, *lastsample,
						    &devc->trigger);

			if (tosend > 0) {
				packet.type = SR_DF_LOGIC;
				packet.payload = &logic;
				logic.length = tosend * sizeof(uint16_t);
				logic.unitsize = 2;
				logic.data = samples;
				sr_session_send(devc->cb_data, &packet);

				sent += tosend;
			}

			/* Only send trigger if explicitly enabled. */
			if (devc->use_triggers) {
				packet.type = SR_DF_TRIGGER;
				sr_session_send(devc->cb_data, &packet);
			}
		}

		/* Send rest of the chunk to sigrok. */
		tosend = n - sent;

		if (tosend > 0) {
			packet.type = SR_DF_LOGIC;
			packet.payload = &logic;
			logic.length = tosend * sizeof(uint16_t);
			logic.unitsize = 2;
			logic.data = samples + sent;
			sr_session_send(devc->cb_data, &packet);
		}

		*lastsample = samples[n - 1];
	}

	return SR_OK;
}

static int receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi = cb_data;
	struct dev_context *devc = sdi->priv;
	struct sr_datafeed_packet packet;
	const int chunks_per_read = 32;
	unsigned char buf[chunks_per_read * CHUNK_SIZE];
	int bufsz, numchunks, i, newchunks;
	uint64_t running_msec;
	struct timeval tv;

	(void)fd;
	(void)revents;

	/* Get the current position. */
	sigma_read_pos(&devc->state.stoppos, &devc->state.triggerpos, devc);

	numchunks = (devc->state.stoppos + 511) / 512;

	if (devc->state.state == SIGMA_IDLE)
		return TRUE;

	if (devc->state.state == SIGMA_CAPTURE) {
		/* Check if the timer has expired, or memory is full. */
		gettimeofday(&tv, 0);
		running_msec = (tv.tv_sec - devc->start_tv.tv_sec) * 1000 +
			(tv.tv_usec - devc->start_tv.tv_usec) / 1000;

		if (running_msec < devc->limit_msec && numchunks < 32767)
			return TRUE; /* While capturing... */
		else
			dev_acquisition_stop(sdi, sdi);

	}

	if (devc->state.state == SIGMA_DOWNLOAD) {
		if (devc->state.chunks_downloaded >= numchunks) {
			/* End of samples. */
			packet.type = SR_DF_END;
			sr_session_send(devc->cb_data, &packet);

			devc->state.state = SIGMA_IDLE;

			return TRUE;
		}

		newchunks = MIN(chunks_per_read,
				numchunks - devc->state.chunks_downloaded);

		sr_info("Downloading sample data: %.0f %%.",
			100.0 * devc->state.chunks_downloaded / numchunks);

		bufsz = sigma_read_dram(devc->state.chunks_downloaded,
					newchunks, buf, devc);
		/* TODO: Check bufsz. For now, just avoid compiler warnings. */
		(void)bufsz;

		/* Find first ts. */
		if (devc->state.chunks_downloaded == 0) {
			devc->state.lastts = RL16(buf) - 1;
			devc->state.lastsample = 0;
		}

		/* Decode chunks and send them to sigrok. */
		for (i = 0; i < newchunks; ++i) {
			int limit_chunk = 0;

			/* The last chunk may potentially be only in part. */
			if (devc->state.chunks_downloaded == numchunks - 1) {
				/* Find the last valid timestamp */
				limit_chunk = devc->state.stoppos % 512 + devc->state.lastts;
			}

			if (devc->state.chunks_downloaded + i == devc->state.triggerchunk)
				decode_chunk_ts(buf + (i * CHUNK_SIZE),
						&devc->state.lastts,
						&devc->state.lastsample,
						devc->state.triggerpos & 0x1ff,
						limit_chunk, sdi);
			else
				decode_chunk_ts(buf + (i * CHUNK_SIZE),
						&devc->state.lastts,
						&devc->state.lastsample,
						-1, limit_chunk, sdi);

			++devc->state.chunks_downloaded;
		}
	}

	return TRUE;
}

/* Build a LUT entry used by the trigger functions. */
static void build_lut_entry(uint16_t value, uint16_t mask, uint16_t *entry)
{
	int i, j, k, bit;

	/* For each quad probe. */
	for (i = 0; i < 4; ++i) {
		entry[i] = 0xffff;

		/* For each bit in LUT. */
		for (j = 0; j < 16; ++j)

			/* For each probe in quad. */
			for (k = 0; k < 4; ++k) {
				bit = 1 << (i * 4 + k);

				/* Set bit in entry */
				if ((mask & bit) &&
				    ((!(value & bit)) !=
				    (!(j & (1 << k)))))
					entry[i] &= ~(1 << j);
			}
	}
}

/* Add a logical function to LUT mask. */
static void add_trigger_function(enum triggerop oper, enum triggerfunc func,
				 int index, int neg, uint16_t *mask)
{
	int i, j;
	int x[2][2], tmp, a, b, aset, bset, rset;

	memset(x, 0, 4 * sizeof(int));

	/* Trigger detect condition. */
	switch (oper) {
	case OP_LEVEL:
		x[0][1] = 1;
		x[1][1] = 1;
		break;
	case OP_NOT:
		x[0][0] = 1;
		x[1][0] = 1;
		break;
	case OP_RISE:
		x[0][1] = 1;
		break;
	case OP_FALL:
		x[1][0] = 1;
		break;
	case OP_RISEFALL:
		x[0][1] = 1;
		x[1][0] = 1;
		break;
	case OP_NOTRISE:
		x[1][1] = 1;
		x[0][0] = 1;
		x[1][0] = 1;
		break;
	case OP_NOTFALL:
		x[1][1] = 1;
		x[0][0] = 1;
		x[0][1] = 1;
		break;
	case OP_NOTRISEFALL:
		x[1][1] = 1;
		x[0][0] = 1;
		break;
	}

	/* Transpose if neg is set. */
	if (neg) {
		for (i = 0; i < 2; ++i) {
			for (j = 0; j < 2; ++j) {
				tmp = x[i][j];
				x[i][j] = x[1-i][1-j];
				x[1-i][1-j] = tmp;
			}
		}
	}

	/* Update mask with function. */
	for (i = 0; i < 16; ++i) {
		a = (i >> (2 * index + 0)) & 1;
		b = (i >> (2 * index + 1)) & 1;

		aset = (*mask >> i) & 1;
		bset = x[b][a];

		if (func == FUNC_AND || func == FUNC_NAND)
			rset = aset & bset;
		else if (func == FUNC_OR || func == FUNC_NOR)
			rset = aset | bset;
		else if (func == FUNC_XOR || func == FUNC_NXOR)
			rset = aset ^ bset;

		if (func == FUNC_NAND || func == FUNC_NOR || func == FUNC_NXOR)
			rset = !rset;

		*mask &= ~(1 << i);

		if (rset)
			*mask |= 1 << i;
	}
}

/*
 * Build trigger LUTs used by 50 MHz and lower sample rates for supporting
 * simple pin change and state triggers. Only two transitions (rise/fall) can be
 * set at any time, but a full mask and value can be set (0/1).
 */
static int build_basic_trigger(struct triggerlut *lut, struct dev_context *devc)
{
	int i,j;
	uint16_t masks[2] = { 0, 0 };

	memset(lut, 0, sizeof(struct triggerlut));

	/* Contant for simple triggers. */
	lut->m4 = 0xa000;

	/* Value/mask trigger support. */
	build_lut_entry(devc->trigger.simplevalue, devc->trigger.simplemask,
			lut->m2d);

	/* Rise/fall trigger support. */
	for (i = 0, j = 0; i < 16; ++i) {
		if (devc->trigger.risingmask & (1 << i) ||
		    devc->trigger.fallingmask & (1 << i))
			masks[j++] = 1 << i;
	}

	build_lut_entry(masks[0], masks[0], lut->m0d);
	build_lut_entry(masks[1], masks[1], lut->m1d);

	/* Add glue logic */
	if (masks[0] || masks[1]) {
		/* Transition trigger. */
		if (masks[0] & devc->trigger.risingmask)
			add_trigger_function(OP_RISE, FUNC_OR, 0, 0, &lut->m3);
		if (masks[0] & devc->trigger.fallingmask)
			add_trigger_function(OP_FALL, FUNC_OR, 0, 0, &lut->m3);
		if (masks[1] & devc->trigger.risingmask)
			add_trigger_function(OP_RISE, FUNC_OR, 1, 0, &lut->m3);
		if (masks[1] & devc->trigger.fallingmask)
			add_trigger_function(OP_FALL, FUNC_OR, 1, 0, &lut->m3);
	} else {
		/* Only value/mask trigger. */
		lut->m3 = 0xffff;
	}

	/* Triggertype: event. */
	lut->params.selres = 3;

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc;
	struct clockselect_50 clockselect;
	int frac, triggerpin, ret;
	uint8_t triggerselect = 0;
	struct triggerinout triggerinout_conf;
	struct triggerlut lut;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	if (configure_probes(sdi) != SR_OK) {
		sr_err("Failed to configure probes.");
		return SR_ERR;
	}

	/* If the samplerate has not been set, default to 200 kHz. */
	if (devc->cur_firmware == -1) {
		if ((ret = set_samplerate(sdi, SR_KHZ(200))) != SR_OK)
			return ret;
	}

	/* Enter trigger programming mode. */
	sigma_set_register(WRITE_TRIGGER_SELECT1, 0x20, devc);

	/* 100 and 200 MHz mode. */
	if (devc->cur_samplerate >= SR_MHZ(100)) {
		sigma_set_register(WRITE_TRIGGER_SELECT1, 0x81, devc);

		/* Find which pin to trigger on from mask. */
		for (triggerpin = 0; triggerpin < 8; ++triggerpin)
			if ((devc->trigger.risingmask | devc->trigger.fallingmask) &
			    (1 << triggerpin))
				break;

		/* Set trigger pin and light LED on trigger. */
		triggerselect = (1 << LEDSEL1) | (triggerpin & 0x7);

		/* Default rising edge. */
		if (devc->trigger.fallingmask)
			triggerselect |= 1 << 3;

	/* All other modes. */
	} else if (devc->cur_samplerate <= SR_MHZ(50)) {
		build_basic_trigger(&lut, devc);

		sigma_write_trigger_lut(&lut, devc);

		triggerselect = (1 << LEDSEL1) | (1 << LEDSEL0);
	}

	/* Setup trigger in and out pins to default values. */
	memset(&triggerinout_conf, 0, sizeof(struct triggerinout));
	triggerinout_conf.trgout_bytrigger = 1;
	triggerinout_conf.trgout_enable = 1;

	sigma_write_register(WRITE_TRIGGER_OPTION,
			     (uint8_t *) &triggerinout_conf,
			     sizeof(struct triggerinout), devc);

	/* Go back to normal mode. */
	sigma_set_register(WRITE_TRIGGER_SELECT1, triggerselect, devc);

	/* Set clock select register. */
	if (devc->cur_samplerate == SR_MHZ(200))
		/* Enable 4 probes. */
		sigma_set_register(WRITE_CLOCK_SELECT, 0xf0, devc);
	else if (devc->cur_samplerate == SR_MHZ(100))
		/* Enable 8 probes. */
		sigma_set_register(WRITE_CLOCK_SELECT, 0x00, devc);
	else {
		/*
		 * 50 MHz mode (or fraction thereof). Any fraction down to
		 * 50 MHz / 256 can be used, but is not supported by sigrok API.
		 */
		frac = SR_MHZ(50) / devc->cur_samplerate - 1;

		clockselect.async = 0;
		clockselect.fraction = frac;
		clockselect.disabled_probes = 0;

		sigma_write_register(WRITE_CLOCK_SELECT,
				     (uint8_t *) &clockselect,
				     sizeof(clockselect), devc);
	}

	/* Setup maximum post trigger time. */
	sigma_set_register(WRITE_POST_TRIGGER,
			   (devc->capture_ratio * 255) / 100, devc);

	/* Start acqusition. */
	gettimeofday(&devc->start_tv, 0);
	sigma_set_register(WRITE_MODE, 0x0d, devc);

	devc->cb_data = cb_data;

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	/* Add capture source. */
	sr_source_add(0, G_IO_IN, 10, receive_data, (void *)sdi);

	devc->state.state = SIGMA_CAPTURE;

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc;
	uint8_t modestatus;

	(void)cb_data;

	sr_source_remove(0);

	if (!(devc = sdi->priv)) {
		sr_err("%s: sdi->priv was NULL", __func__);
		return SR_ERR_BUG;
	}

	/* Stop acquisition. */
	sigma_set_register(WRITE_MODE, 0x11, devc);

	/* Set SDRAM Read Enable. */
	sigma_set_register(WRITE_MODE, 0x02, devc);

	/* Get the current position. */
	sigma_read_pos(&devc->state.stoppos, &devc->state.triggerpos, devc);

	/* Check if trigger has fired. */
	modestatus = sigma_get_register(READ_MODE, devc);
	if (modestatus & 0x20)
		devc->state.triggerchunk = devc->state.triggerpos / 512;
	else
		devc->state.triggerchunk = -1;

	devc->state.chunks_downloaded = 0;

	devc->state.state = SIGMA_DOWNLOAD;

	return SR_OK;
}

SR_PRIV struct sr_dev_driver asix_sigma_driver_info = {
	.name = "asix-sigma",
	.longname = "ASIX SIGMA/SIGMA2",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};
