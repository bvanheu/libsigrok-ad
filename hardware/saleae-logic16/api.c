/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Marcus Comstedt <marcus@mc.pp.se>
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2012 Joel Holdsworth <joel@airwebreathe.org.uk>
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

#include <glib.h>
#include <libusb.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

#define LOGIC16_VID		0x21a9
#define LOGIC16_PID		0x1001

#define USB_INTERFACE		0
#define USB_CONFIGURATION	1
#define FX2_FIRMWARE		FIRMWARE_DIR "/saleae-logic16-fx2.fw"

#define MAX_RENUM_DELAY_MS	3000
#define NUM_SIMUL_TRANSFERS	32

SR_PRIV struct sr_dev_driver saleae_logic16_driver_info;
static struct sr_dev_driver *di = &saleae_logic16_driver_info;

static const int32_t hwopts[] = {
	SR_CONF_CONN,
};

static const int32_t hwcaps[] = {
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_SAMPLERATE,
	SR_CONF_VOLTAGE_THRESHOLD,

	/* These are really implemented in the driver, not the hardware. */
	SR_CONF_LIMIT_SAMPLES,
	SR_CONF_CONTINUOUS,
};

static const char *probe_names[] = {
	"0", "1", "2", "3", "4", "5", "6", "7", "8",
	"9", "10", "11", "12", "13", "14", "15",
	NULL,
};

static const struct {
	enum voltage_range range;
	gdouble low;
	gdouble high;
} volt_thresholds[] = {
	{ VOLTAGE_RANGE_18_33_V, 0.7, 1.4 },
	{ VOLTAGE_RANGE_5_V,     1.4, 3.6 },
};

static const uint64_t samplerates[] = {
	SR_KHZ(500),
	SR_MHZ(1),
	SR_MHZ(2),
	SR_MHZ(4),
	SR_MHZ(5),
	SR_MHZ(8),
	SR_MHZ(10),
	SR_KHZ(12500),
	SR_MHZ(16),
	SR_MHZ(25),
	SR_MHZ(32),
	SR_MHZ(40),
	SR_MHZ(80),
	SR_MHZ(100),
};

static int init(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static gboolean check_conf_profile(libusb_device *dev)
{
	struct libusb_device_descriptor des;
	struct libusb_device_handle *hdl;
	gboolean ret;
	unsigned char strdesc[64];

	hdl = NULL;
	ret = FALSE;
	while (!ret) {
		/* Assume the FW has not been loaded, unless proven wrong. */
		if (libusb_get_device_descriptor(dev, &des) != 0)
			break;

		if (libusb_open(dev, &hdl) != 0)
			break;

		if (libusb_get_string_descriptor_ascii(hdl,
		    des.iManufacturer, strdesc, sizeof(strdesc)) < 0)
			break;
		if (strcmp((const char *)strdesc, "Saleae LLC"))
			break;

		if (libusb_get_string_descriptor_ascii(hdl,
		    des.iProduct, strdesc, sizeof(strdesc)) < 0)
			break;
		if (strcmp((const char *)strdesc, "Logic S/16"))
			break;

		/* If we made it here, it must be a configured Logic16. */
		ret = TRUE;
	}
	if (hdl)
		libusb_close(hdl);

	return ret;
}

static GSList *scan(GSList *options)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_usb_dev_inst *usb;
	struct sr_probe *probe;
	struct sr_config *src;
	GSList *l, *devices, *conn_devices;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	int devcnt, ret, i, j;
	const char *conn;

	drvc = di->priv;

	conn = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	if (conn)
		conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
	else
		conn_devices = NULL;

	/* Find all Logic16 devices and upload firmware to them. */
	devices = NULL;
	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	for (i = 0; devlist[i]; i++) {
		if (conn) {
			usb = NULL;
			for (l = conn_devices; l; l = l->next) {
				usb = l->data;
				if (usb->bus == libusb_get_bus_number(devlist[i])
				    && usb->address == libusb_get_device_address(devlist[i]))
					break;
			}
			if (!l)
				/* This device matched none of the ones that
				 * matched the conn specification. */
				continue;
		}

		if ((ret = libusb_get_device_descriptor(devlist[i], &des)) != 0) {
			sr_warn("Failed to get device descriptor: %s.",
				libusb_error_name(ret));
			continue;
		}

		if (des.idVendor != LOGIC16_VID || des.idProduct != LOGIC16_PID)
			continue;

		devcnt = g_slist_length(drvc->instances);
		sdi = sr_dev_inst_new(devcnt, SR_ST_INITIALIZING,
				      "Saleae", "Logic16", NULL);
		if (!sdi)
			return NULL;
		sdi->driver = di;

		for (j = 0; probe_names[j]; j++) {
			if (!(probe = sr_probe_new(j, SR_PROBE_LOGIC, TRUE,
						   probe_names[j])))
				return NULL;
			sdi->probes = g_slist_append(sdi->probes, probe);
		}

		if (!(devc = g_try_malloc0(sizeof(struct dev_context))))
			return NULL;
		devc->selected_voltage_range = VOLTAGE_RANGE_18_33_V;
		sdi->priv = devc;
		drvc->instances = g_slist_append(drvc->instances, sdi);
		devices = g_slist_append(devices, sdi);

		if (check_conf_profile(devlist[i])) {
			/* Already has the firmware, so fix the new address. */
			sr_dbg("Found a Logic16 device.");
			sdi->status = SR_ST_INACTIVE;
			sdi->inst_type = SR_INST_USB;
			sdi->conn = sr_usb_dev_inst_new(
				libusb_get_bus_number(devlist[i]),
				libusb_get_device_address(devlist[i]), NULL);
		} else {
			if (ezusb_upload_firmware(devlist[i], USB_CONFIGURATION,
						  FX2_FIRMWARE) == SR_OK)
				/* Store when this device's FW was updated. */
				devc->fw_updated = g_get_monotonic_time();
			else
				sr_err("Firmware upload failed for "
				       "device %d.", devcnt);
			sdi->inst_type = SR_INST_USB;
			sdi->conn = sr_usb_dev_inst_new(
				libusb_get_bus_number(devlist[i]), 0xff, NULL);
		}
	}
	libusb_free_device_list(devlist, 1);
	g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);

	return devices;
}

static GSList *dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int dev_clear(void)
{
	return std_dev_clear(di, NULL);
}

static int logic16_dev_open(struct sr_dev_inst *sdi)
{
	libusb_device **devlist;
	struct sr_usb_dev_inst *usb;
	struct libusb_device_descriptor des;
	struct drv_context *drvc;
	int ret, skip, i, device_count;

	drvc = di->priv;
	usb = sdi->conn;

	if (sdi->status == SR_ST_ACTIVE)
		/* Device is already in use. */
		return SR_ERR;

	skip = 0;
	device_count = libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	if (device_count < 0) {
		sr_err("Failed to get device list: %s.",
		       libusb_error_name(device_count));
		return SR_ERR;
	}

	for (i = 0; i < device_count; i++) {
		if ((ret = libusb_get_device_descriptor(devlist[i], &des))) {
			sr_err("Failed to get device descriptor: %s.",
			       libusb_error_name(ret));
			continue;
		}

		if (des.idVendor != LOGIC16_VID || des.idProduct != LOGIC16_PID)
			continue;

		if (sdi->status == SR_ST_INITIALIZING) {
			if (skip != sdi->index) {
				/* Skip devices of this type that aren't the one we want. */
				skip += 1;
				continue;
			}
		} else if (sdi->status == SR_ST_INACTIVE) {
			/*
			 * This device is fully enumerated, so we need to find
			 * this device by vendor, product, bus and address.
			 */
			if (libusb_get_bus_number(devlist[i]) != usb->bus
			    || libusb_get_device_address(devlist[i]) != usb->address)
				/* This is not the one. */
				continue;
		}

		if (!(ret = libusb_open(devlist[i], &usb->devhdl))) {
			if (usb->address == 0xff)
				/*
				 * First time we touch this device after FW
				 * upload, so we don't know the address yet.
				 */
				usb->address = libusb_get_device_address(devlist[i]);
		} else {
			sr_err("Failed to open device: %s.",
			       libusb_error_name(ret));
			break;
		}

		ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE);
		if (ret == LIBUSB_ERROR_BUSY) {
			sr_err("Unable to claim USB interface. Another "
			       "program or driver has already claimed it.");
			break;
		} else if (ret == LIBUSB_ERROR_NO_DEVICE) {
			sr_err("Device has been disconnected.");
			break;
		} else if (ret != 0) {
			sr_err("Unable to claim interface: %s.",
			       libusb_error_name(ret));
			break;
		}

		if ((ret = logic16_init_device(sdi)) != SR_OK) {
			sr_err("Failed to init device.");
			break;
		}

		sdi->status = SR_ST_ACTIVE;
		sr_info("Opened device %d on %d.%d, interface %d.",
			sdi->index, usb->bus, usb->address, USB_INTERFACE);

		break;
	}
	libusb_free_device_list(devlist, 1);

	if (sdi->status != SR_ST_ACTIVE) {
		if (usb->devhdl) {
			libusb_release_interface(usb->devhdl, USB_INTERFACE);
			libusb_close(usb->devhdl);
			usb->devhdl = NULL;
		}
		return SR_ERR;
	}

	return SR_OK;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;
	int64_t timediff_us, timediff_ms;

	devc = sdi->priv;

	/*
	 * If the firmware was recently uploaded, wait up to MAX_RENUM_DELAY_MS
	 * milliseconds for the FX2 to renumerate.
	 */
	ret = SR_ERR;
	if (devc->fw_updated > 0) {
		sr_info("Waiting for device to reset.");
		/* Takes >= 300ms for the FX2 to be gone from the USB bus. */
		g_usleep(300 * 1000);
		timediff_ms = 0;
		while (timediff_ms < MAX_RENUM_DELAY_MS) {
			if ((ret = logic16_dev_open(sdi)) == SR_OK)
				break;
			g_usleep(100 * 1000);

			timediff_us = g_get_monotonic_time() - devc->fw_updated;
			timediff_ms = timediff_us / 1000;
			sr_spew("Waited %" PRIi64 "ms.", timediff_ms);
		}
		if (ret != SR_OK) {
			sr_err("Device failed to renumerate.");
			return SR_ERR;
		}
		sr_info("Device came back after %" PRIi64 "ms.", timediff_ms);
	} else {
		sr_info("Firmware upload was not needed.");
		ret = logic16_dev_open(sdi);
	}

	if (ret != SR_OK) {
		sr_err("Unable to open device.");
		return SR_ERR;
	}

	if (devc->cur_samplerate == 0) {
		/* Samplerate hasn't been set; default to the slowest one. */
		devc->cur_samplerate = samplerates[0];
	}

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;

	usb = sdi->conn;
	if (usb->devhdl == NULL)
		return SR_ERR;

	sr_info("Closing device %d on %d.%d interface %d.",
		sdi->index, usb->bus, usb->address, USB_INTERFACE);
	libusb_release_interface(usb->devhdl, USB_INTERFACE);
	libusb_close(usb->devhdl);
	usb->devhdl = NULL;
	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int cleanup(void)
{
	int ret;
	struct drv_context *drvc;

	if (!(drvc = di->priv))
		/* Can get called on an unused driver, doesn't matter. */
		return SR_OK;

	ret = dev_clear();
	g_free(drvc);
	di->priv = NULL;

	return ret;
}

static int config_get(int key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_probe_group *probe_group)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	GVariant *range[2];
	char str[128];
	int ret;
	unsigned int i;

	(void)probe_group;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_CONN:
		if (!sdi || !sdi->conn)
			return SR_ERR_ARG;
		usb = sdi->conn;
		if (usb->address == 255)
			/* Device still needs to re-enumerate after firmware
			 * upload, so we don't know its (future) address. */
			return SR_ERR;
		snprintf(str, 128, "%d.%d", usb->bus, usb->address);
		*data = g_variant_new_string(str);
		break;
	case SR_CONF_SAMPLERATE:
		if (!sdi)
			return SR_ERR;
		devc = sdi->priv;
		*data = g_variant_new_uint64(devc->cur_samplerate);
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		if (!sdi)
			return SR_ERR;
		devc = sdi->priv;
		ret = SR_ERR;
		for (i = 0; i < ARRAY_SIZE(volt_thresholds); i++) {
			if (devc->selected_voltage_range !=
			    volt_thresholds[i].range)
				continue;
			range[0] = g_variant_new_double(volt_thresholds[i].low);
			range[1] = g_variant_new_double(volt_thresholds[i].high);
			*data = g_variant_new_tuple(range, 2);
			ret = SR_OK;
			break;
		}
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(int key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_probe_group *probe_group)
{
	struct dev_context *devc;
	gdouble low, high;
	int ret;
	unsigned int i;

	(void)probe_group;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_SAMPLERATE:
		devc->cur_samplerate = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		g_variant_get(data, "(dd)", &low, &high);
		ret = SR_ERR_ARG;
		for (i = 0; i < ARRAY_SIZE(volt_thresholds); i++) {
			if (fabs(volt_thresholds[i].low - low) < 0.1 &&
			    fabs(volt_thresholds[i].high - high) < 0.1) {
				devc->selected_voltage_range =
					volt_thresholds[i].range;
				ret = SR_OK;
				break;
			}
		}
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_probe_group *probe_group)
{
	GVariant *gvar, *range[2];
	GVariantBuilder gvb;
	int ret;
	unsigned int i;

	(void)sdi;
	(void)probe_group;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwopts, ARRAY_SIZE(hwopts), sizeof(int32_t));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwcaps, ARRAY_SIZE(hwcaps), sizeof(int32_t));
		break;
	case SR_CONF_SAMPLERATE:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
		gvar = g_variant_new_fixed_array(G_VARIANT_TYPE("t"),
			samplerates, ARRAY_SIZE(samplerates), sizeof(uint64_t));
		g_variant_builder_add(&gvb, "{sv}", "samplerates", gvar);
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
		for (i = 0; i < ARRAY_SIZE(volt_thresholds); i++) {
			range[0] = g_variant_new_double(volt_thresholds[i].low);
			range[1] = g_variant_new_double(volt_thresholds[i].high);
			gvar = g_variant_new_tuple(range, 2);
			g_variant_builder_add_value(&gvb, gvar);
		}
		*data = g_variant_builder_end(&gvb);
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static void abort_acquisition(struct dev_context *devc)
{
	int i;

	devc->num_samples = -1;

	for (i = devc->num_transfers - 1; i >= 0; i--) {
		if (devc->transfers[i])
			libusb_cancel_transfer(devc->transfers[i]);
	}
}

static unsigned int bytes_per_ms(struct dev_context *devc)
{
	return devc->cur_samplerate * devc->num_channels / 8000;
}

static size_t get_buffer_size(struct dev_context *devc)
{
	size_t s;

	/*
	 * The buffer should be large enough to hold 10ms of data and
	 * a multiple of 512.
	 */
	s = 10 * bytes_per_ms(devc);
	return (s + 511) & ~511;
}

static unsigned int get_number_of_transfers(struct dev_context *devc)
{
	unsigned int n;

	/* Total buffer size should be able to hold about 500ms of data. */
	n = 500 * bytes_per_ms(devc) / get_buffer_size(devc);

	if (n > NUM_SIMUL_TRANSFERS)
		return NUM_SIMUL_TRANSFERS;

	return n;
}

static unsigned int get_timeout(struct dev_context *devc)
{
	size_t total_size;
	unsigned int timeout;

	total_size = get_buffer_size(devc) * get_number_of_transfers(devc);
	timeout = total_size / bytes_per_ms(devc);
	return timeout + timeout / 4; /* Leave a headroom of 25% percent. */
}

static int configure_probes(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_probe *probe;
	GSList *l;
	uint16_t probe_bit;

	devc = sdi->priv;

	devc->cur_channels = 0;
	devc->num_channels = 0;
	for (l = sdi->probes; l; l = l->next) {
		probe = (struct sr_probe *)l->data;
		if (probe->enabled == FALSE)
			continue;

		probe_bit = 1 << (probe->index);

		devc->cur_channels |= probe_bit;

#ifdef WORDS_BIGENDIAN
		/*
		 * Output logic data should be stored in little endian format.
		 * To speed things up during conversion, do the switcharoo
		 * here instead.
		 */
		probe_bit = 1 << (probe->index ^ 8);
#endif

		devc->channel_masks[devc->num_channels++] = probe_bit;
	}

	return SR_OK;
}

static int receive_data(int fd, int revents, void *cb_data)
{
	struct timeval tv;
	struct dev_context *devc;
	struct drv_context *drvc;
	const struct sr_dev_inst *sdi;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	drvc = di->priv;
	devc = sdi->priv;

	tv.tv_sec = tv.tv_usec = 0;
	libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);

	if (devc->num_samples == -2) {
		logic16_abort_acquisition(sdi);
		abort_acquisition(devc);
	}

	return TRUE;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc;
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;
	struct libusb_transfer *transfer;
	unsigned int i, timeout, num_transfers;
	int ret;
	unsigned char *buf;
	size_t size, convsize;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	drvc = di->priv;
	devc = sdi->priv;
	usb = sdi->conn;

	/* Configures devc->cur_channels. */
	if (configure_probes(sdi) != SR_OK) {
		sr_err("Failed to configure probes.");
		return SR_ERR;
	}

	devc->cb_data = cb_data;
	devc->num_samples = 0;
	devc->empty_transfer_count = 0;
	devc->cur_channel = 0;
	memset(devc->channel_data, 0, sizeof(devc->channel_data));

	timeout = get_timeout(devc);
	num_transfers = get_number_of_transfers(devc);
	size = get_buffer_size(devc);
	convsize = (size / devc->num_channels + 2) * 16;
	devc->submitted_transfers = 0;

	devc->convbuffer_size = convsize;
	if (!(devc->convbuffer = g_try_malloc(convsize))) {
		sr_err("Conversion buffer malloc failed.");
		return SR_ERR_MALLOC;
	}

	devc->transfers = g_try_malloc0(sizeof(*devc->transfers) * num_transfers);
	if (!devc->transfers) {
		sr_err("USB transfers malloc failed.");
		g_free(devc->convbuffer);
		return SR_ERR_MALLOC;
	}

	if ((ret = logic16_setup_acquisition(sdi, devc->cur_samplerate,
					     devc->cur_channels)) != SR_OK) {
		g_free(devc->transfers);
		g_free(devc->convbuffer);
		return ret;
	}

	devc->num_transfers = num_transfers;
	for (i = 0; i < num_transfers; i++) {
		if (!(buf = g_try_malloc(size))) {
			sr_err("USB transfer buffer malloc failed.");
			if (devc->submitted_transfers)
				abort_acquisition(devc);
			else {
				g_free(devc->transfers);
				g_free(devc->convbuffer);
			}
			return SR_ERR_MALLOC;
		}
		transfer = libusb_alloc_transfer(0);
		libusb_fill_bulk_transfer(transfer, usb->devhdl,
				2 | LIBUSB_ENDPOINT_IN, buf, size,
				logic16_receive_transfer, devc, timeout);
		if ((ret = libusb_submit_transfer(transfer)) != 0) {
			sr_err("Failed to submit transfer: %s.",
			       libusb_error_name(ret));
			libusb_free_transfer(transfer);
			g_free(buf);
			abort_acquisition(devc);
			return SR_ERR;
		}
		devc->transfers[i] = transfer;
		devc->submitted_transfers++;
	}

	devc->ctx = drvc->sr_ctx;

	usb_source_add(devc->ctx, timeout, receive_data, (void *)sdi);

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	if ((ret = logic16_start_acquisition(sdi)) != SR_OK) {
		abort_acquisition(devc);
		return ret;
	}

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	int ret;

	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	ret = logic16_abort_acquisition(sdi);

	abort_acquisition(sdi->priv);

	return ret;
}

SR_PRIV struct sr_dev_driver saleae_logic16_driver_info = {
	.name = "saleae-logic16",
	.longname = "Saleae Logic16",
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
