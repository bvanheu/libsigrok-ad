/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2012 Alexandru Gagniuc <mr.nuke.me@gmail.com>
 * Copyright (C) 2012 Uwe Hermann <uwe@hermann-uwe.de>
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

static const int32_t hwopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const int32_t hwcaps[] = {
	SR_CONF_MULTIMETER,
	SR_CONF_LIMIT_SAMPLES,
	SR_CONF_LIMIT_MSEC,
	SR_CONF_CONTINUOUS,
};

SR_PRIV struct sr_dev_driver bbcgm_m2110_driver_info;
SR_PRIV struct sr_dev_driver digitek_dt4000zc_driver_info;
SR_PRIV struct sr_dev_driver tekpower_tp4000zc_driver_info;
SR_PRIV struct sr_dev_driver metex_me31_driver_info;
SR_PRIV struct sr_dev_driver peaktech_3410_driver_info;
SR_PRIV struct sr_dev_driver mastech_mas345_driver_info;
SR_PRIV struct sr_dev_driver va_va18b_driver_info;
SR_PRIV struct sr_dev_driver va_va40b_driver_info;
SR_PRIV struct sr_dev_driver metex_m3640d_driver_info;
SR_PRIV struct sr_dev_driver metex_m4650cr_driver_info;
SR_PRIV struct sr_dev_driver peaktech_4370_driver_info;
SR_PRIV struct sr_dev_driver pce_pce_dm32_driver_info;
SR_PRIV struct sr_dev_driver radioshack_22_168_driver_info;
SR_PRIV struct sr_dev_driver radioshack_22_805_driver_info;
SR_PRIV struct sr_dev_driver radioshack_22_812_driver_info;
SR_PRIV struct sr_dev_driver tecpel_dmm_8061_ser_driver_info;
SR_PRIV struct sr_dev_driver voltcraft_m3650d_driver_info;
SR_PRIV struct sr_dev_driver voltcraft_m4650cr_driver_info;
SR_PRIV struct sr_dev_driver voltcraft_vc820_ser_driver_info;
SR_PRIV struct sr_dev_driver voltcraft_vc830_ser_driver_info;
SR_PRIV struct sr_dev_driver voltcraft_vc840_ser_driver_info;
SR_PRIV struct sr_dev_driver uni_t_ut60a_ser_driver_info;
SR_PRIV struct sr_dev_driver uni_t_ut60e_ser_driver_info;
SR_PRIV struct sr_dev_driver uni_t_ut61b_ser_driver_info;
SR_PRIV struct sr_dev_driver uni_t_ut61c_ser_driver_info;
SR_PRIV struct sr_dev_driver uni_t_ut61d_ser_driver_info;
SR_PRIV struct sr_dev_driver uni_t_ut61e_ser_driver_info;
SR_PRIV struct sr_dev_driver iso_tech_idm103n_driver_info;

SR_PRIV struct dmm_info dmms[] = {
	{
		"BBC Goertz Metrawatt", "M2110", "1200/7n2", 1200,
		BBCGM_M2110_PACKET_SIZE, NULL,
		sr_m2110_packet_valid, sr_m2110_parse,
		NULL,
		&bbcgm_m2110_driver_info, receive_data_BBCGM_M2110,
	},
	{
		"Digitek", "DT4000ZC", "2400/8n1/dtr=1", 2400,
		FS9721_PACKET_SIZE, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_10_temp_c,
		&digitek_dt4000zc_driver_info, receive_data_DIGITEK_DT4000ZC,
	},
	{
		"TekPower", "TP4000ZC", "2400/8n1/dtr=1", 2400,
		FS9721_PACKET_SIZE, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_10_temp_c,
		&tekpower_tp4000zc_driver_info, receive_data_TEKPOWER_TP4000ZC,
	},
	{
		"Metex", "ME-31", "600/7n2/rts=0/dtr=1", 600,
		METEX14_PACKET_SIZE, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL,
		&metex_me31_driver_info, receive_data_METEX_ME31,
	},
	{
		"Peaktech", "3410", "600/7n2/rts=0/dtr=1", 600,
		METEX14_PACKET_SIZE, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL,
		&peaktech_3410_driver_info, receive_data_PEAKTECH_3410,
	},
	{
		"MASTECH", "MAS345", "600/7n2/rts=0/dtr=1", 600,
		METEX14_PACKET_SIZE, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL,
		&mastech_mas345_driver_info, receive_data_MASTECH_MAS345,
	},
	{
		"V&A", "VA18B", "2400/8n1", 2400,
		FS9721_PACKET_SIZE, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_01_temp_c,
		&va_va18b_driver_info, receive_data_VA_VA18B,
	},
	{
		"V&A", "VA40B", "2400/8n1", 2400,
		FS9721_PACKET_SIZE, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_max_c_min,
		&va_va40b_driver_info, receive_data_VA_VA40B,
	},
	{
		"Metex", "M-3640D", "1200/7n2/rts=0/dtr=1", 1200,
		METEX14_PACKET_SIZE, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL,
		&metex_m3640d_driver_info, receive_data_METEX_M3640D,
	},
	{
		"Metex", "M-4650CR", "1200/7n2/rts=0/dtr=1", 1200,
		METEX14_PACKET_SIZE, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL,
		&metex_m4650cr_driver_info, receive_data_METEX_M4650CR,
	},
	{
		"PeakTech", "4370", "1200/7n2/rts=0/dtr=1", 1200,
		METEX14_PACKET_SIZE, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL,
		&peaktech_4370_driver_info, receive_data_PEAKTECH_4370,
	},
	{
		"PCE", "PCE-DM32", "2400/8n1", 2400,
		FS9721_PACKET_SIZE, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_01_10_temp_f_c,
		&pce_pce_dm32_driver_info, receive_data_PCE_PCE_DM32,
	},
	{
		"RadioShack", "22-168", "1200/7n2/rts=0/dtr=1", 1200,
		METEX14_PACKET_SIZE, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL,
		&radioshack_22_168_driver_info, receive_data_RADIOSHACK_22_168,
	},
	{
		"RadioShack", "22-805", "600/7n2/rts=0/dtr=1", 600,
		METEX14_PACKET_SIZE, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL,
		&radioshack_22_805_driver_info, receive_data_RADIOSHACK_22_805,
	},
	{
		"RadioShack", "22-812", "4800/8n1/rts=0/dtr=1", 4800,
		RS9LCD_PACKET_SIZE, NULL,
		sr_rs9lcd_packet_valid, sr_rs9lcd_parse,
		NULL,
		&radioshack_22_812_driver_info, receive_data_RADIOSHACK_22_812,
	},
	{
		"Tecpel", "DMM-8061 (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		2400, FS9721_PACKET_SIZE, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_00_temp_c,
		&tecpel_dmm_8061_ser_driver_info,
		receive_data_TECPEL_DMM_8061_SER,
	},
	{
		"Voltcraft", "M-3650D", "1200/7n2/rts=0/dtr=1", 1200,
		METEX14_PACKET_SIZE, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL,
		&voltcraft_m3650d_driver_info, receive_data_VOLTCRAFT_M3650D,
	},
	{
		"Voltcraft", "M-4650CR", "1200/7n2/rts=0/dtr=1", 1200,
		METEX14_PACKET_SIZE, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL,
		&voltcraft_m4650cr_driver_info, receive_data_VOLTCRAFT_M4650CR,
	},
	{
		"Voltcraft", "VC-820 (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		2400, FS9721_PACKET_SIZE, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		NULL,
		&voltcraft_vc820_ser_driver_info,
		receive_data_VOLTCRAFT_VC820_SER,
	},
	{
		/*
		 * Note: The VC830 doesn't set the 'volt' and 'diode' bits of
		 * the FS9922 protocol. Instead, it only sets the user-defined
		 * bit "z1" to indicate "diode mode" and "voltage".
		 */
		"Voltcraft", "VC-830 (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		2400, FS9922_PACKET_SIZE, NULL,
		sr_fs9922_packet_valid, sr_fs9922_parse,
		&sr_fs9922_z1_diode,
		&voltcraft_vc830_ser_driver_info,
		receive_data_VOLTCRAFT_VC830_SER,
	},
	{
		"Voltcraft", "VC-840 (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		2400, FS9721_PACKET_SIZE, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_00_temp_c,
		&voltcraft_vc840_ser_driver_info,
		receive_data_VOLTCRAFT_VC840_SER,
	},
	{
		"UNI-T", "UT60A (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		2400, FS9721_PACKET_SIZE, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		NULL,
		&uni_t_ut60a_ser_driver_info,
		receive_data_UNI_T_UT60A_SER,
	},
	{
		"UNI-T", "UT60E (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		2400, FS9721_PACKET_SIZE, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_00_temp_c,
		&uni_t_ut60e_ser_driver_info,
		receive_data_UNI_T_UT60E_SER,
	},
	{
		"UNI-T", "UT61B (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		2400, FS9922_PACKET_SIZE, NULL,
		sr_fs9922_packet_valid, sr_fs9922_parse, NULL,
		&uni_t_ut61b_ser_driver_info, receive_data_UNI_T_UT61B_SER,
	},
	{
		"UNI-T", "UT61C (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		2400, FS9922_PACKET_SIZE, NULL,
		sr_fs9922_packet_valid, sr_fs9922_parse, NULL,
		&uni_t_ut61c_ser_driver_info, receive_data_UNI_T_UT61C_SER,
	},
	{
		"UNI-T", "UT61D (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		2400, FS9922_PACKET_SIZE, NULL,
		sr_fs9922_packet_valid, sr_fs9922_parse, NULL,
		&uni_t_ut61d_ser_driver_info, receive_data_UNI_T_UT61D_SER,
	},
	{
		/* Note: ES51922 baudrate is actually 19230! */
		"UNI-T", "UT61E (UT-D02 cable)", "19200/7o1/rts=0/dtr=1",
		19200, ES519XX_14B_PACKET_SIZE, NULL,
		sr_es519xx_19200_14b_packet_valid, sr_es519xx_19200_14b_parse, NULL,
		&uni_t_ut61e_ser_driver_info, receive_data_UNI_T_UT61E_SER,
	},
	{
		"ISO-TECH", "IDM103N", "2400/7o1/rts=0/dtr=1",
		2400, ES519XX_11B_PACKET_SIZE, NULL,
		sr_es519xx_2400_11b_packet_valid, sr_es519xx_2400_11b_parse, NULL,
		&iso_tech_idm103n_driver_info, receive_data_ISO_TECH_IDM103N,
	},
};

static int dev_clear(int dmm)
{
	return std_dev_clear(dmms[dmm].di, NULL);
}

static int init(struct sr_context *sr_ctx, int dmm)
{
	sr_dbg("Selected '%s' subdriver.", dmms[dmm].di->name);

	return std_init(sr_ctx, dmms[dmm].di, LOG_PREFIX);
}

static GSList *sdmm_scan(const char *conn, const char *serialcomm, int dmm)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_probe *probe;
	struct sr_serial_dev_inst *serial;
	GSList *devices;
	int dropped, ret;
	size_t len;
	uint8_t buf[128];

	if (!(serial = sr_serial_dev_inst_new(conn, serialcomm)))
		return NULL;

	if (serial_open(serial, SERIAL_RDWR | SERIAL_NONBLOCK) != SR_OK)
		return NULL;

	sr_info("Probing serial port %s.", conn);

	drvc = dmms[dmm].di->priv;
	devices = NULL;
	serial_flush(serial);

	/* Request a packet if the DMM requires this. */
	if (dmms[dmm].packet_request) {
		if ((ret = dmms[dmm].packet_request(serial)) < 0) {
			sr_err("Failed to request packet: %d.", ret);
			return FALSE;
		}
	}

	/*
	 * There's no way to get an ID from the multimeter. It just sends data
	 * periodically (or upon request), so the best we can do is check if
	 * the packets match the expected format.
	 */

	/* Let's get a bit of data and see if we can find a packet. */
	len = sizeof(buf);
	ret = serial_stream_detect(serial, buf, &len, dmms[dmm].packet_size,
				   dmms[dmm].packet_valid, 3000,
				   dmms[dmm].baudrate);
	if (ret != SR_OK)
		goto scan_cleanup;

	/*
	 * If we dropped more than two packets worth of data, something is
	 * wrong. We shouldn't quit however, since the dropped bytes might be
	 * just zeroes at the beginning of the stream. Those can occur as a
	 * combination of the nonstandard cable that ships with some devices
	 * and the serial port or USB to serial adapter.
	 */
	dropped = len - dmms[dmm].packet_size;
	if (dropped > 2 * dmms[dmm].packet_size)
		sr_warn("Had to drop too much data.");

	sr_info("Found device on port %s.", conn);

	if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE, dmms[dmm].vendor,
				    dmms[dmm].device, "")))
		goto scan_cleanup;

	if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
		sr_err("Device context malloc failed.");
		goto scan_cleanup;
	}

	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;

	sdi->priv = devc;
	sdi->driver = dmms[dmm].di;
	if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "P1")))
		goto scan_cleanup;
	sdi->probes = g_slist_append(sdi->probes, probe);
	drvc->instances = g_slist_append(drvc->instances, sdi);
	devices = g_slist_append(devices, sdi);

scan_cleanup:
	serial_close(serial);

	return devices;
}

static GSList *scan(GSList *options, int dmm)
{
	struct sr_config *src;
	GSList *l, *devices;
	const char *conn, *serialcomm;

	conn = serialcomm = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		case SR_CONF_SERIALCOMM:
			serialcomm = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	if (!conn)
		return NULL;

	if (serialcomm) {
		/* Use the provided comm specs. */
		devices = sdmm_scan(conn, serialcomm, dmm);
	} else {
		/* Try the default. */
		devices = sdmm_scan(conn, dmms[dmm].conn, dmm);
	}

	return devices;
}

static GSList *dev_list(int dmm)
{
	return ((struct drv_context *)(dmms[dmm].di->priv))->instances;
}

static int cleanup(int dmm)
{
	return dev_clear(dmm);
}

static int config_set(int id, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_probe_group *probe_group)
{
	struct dev_context *devc;

	(void)probe_group;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	switch (id) {
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		sr_dbg("Setting sample limit to %" PRIu64 ".",
		       devc->limit_samples);
		break;
	case SR_CONF_LIMIT_MSEC:
		devc->limit_msec = g_variant_get_uint64(data);
		sr_dbg("Setting time limit to %" PRIu64 "ms.",
		       devc->limit_msec);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_probe_group *probe_group)
{
	(void)sdi;
	(void)probe_group;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwopts, ARRAY_SIZE(hwopts), sizeof(int32_t));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwcaps, ARRAY_SIZE(hwcaps), sizeof(int32_t));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi,
				    void *cb_data, int dmm)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	devc->cb_data = cb_data;

	/*
	 * Reset the number of samples to take. If we've already collected our
	 * quota, but we start a new session, and don't reset this, we'll just
	 * quit without acquiring any new samples.
	 */
	devc->num_samples = 0;
	devc->starttime = g_get_monotonic_time();

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	/* Poll every 50ms, or whenever some data comes in. */
	serial = sdi->conn;
	serial_source_add(serial, G_IO_IN, 50,
		      dmms[dmm].receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	return std_serial_dev_acquisition_stop(sdi, cb_data, std_serial_dev_close,
			sdi->conn, LOG_PREFIX);
}

/* Driver-specific API function wrappers */
#define HW_INIT(X) \
static int init_##X(struct sr_context *sr_ctx) { return init(sr_ctx, X); }
#define HW_CLEANUP(X) \
static int cleanup_##X(void) { return cleanup(X); }
#define HW_SCAN(X) \
static GSList *scan_##X(GSList *options) { return scan(options, X); }
#define HW_DEV_LIST(X) \
static GSList *dev_list_##X(void) { return dev_list(X); }
#define HW_DEV_CLEAR(X) \
static int dev_clear_##X(void) { return dev_clear(X); }
#define HW_DEV_ACQUISITION_START(X) \
static int dev_acquisition_start_##X(const struct sr_dev_inst *sdi, \
void *cb_data) { return dev_acquisition_start(sdi, cb_data, X); }

/* Driver structs and API function wrappers */
#define DRV(ID, ID_UPPER, NAME, LONGNAME) \
HW_INIT(ID_UPPER) \
HW_CLEANUP(ID_UPPER) \
HW_SCAN(ID_UPPER) \
HW_DEV_LIST(ID_UPPER) \
HW_DEV_CLEAR(ID_UPPER) \
HW_DEV_ACQUISITION_START(ID_UPPER) \
SR_PRIV struct sr_dev_driver ID##_driver_info = { \
	.name = NAME, \
	.longname = LONGNAME, \
	.api_version = 1, \
	.init = init_##ID_UPPER, \
	.cleanup = cleanup_##ID_UPPER, \
	.scan = scan_##ID_UPPER, \
	.dev_list = dev_list_##ID_UPPER, \
	.dev_clear = dev_clear_##ID_UPPER, \
	.config_get = NULL, \
	.config_set = config_set, \
	.config_list = config_list, \
	.dev_open = std_serial_dev_open, \
	.dev_close = std_serial_dev_close, \
	.dev_acquisition_start = dev_acquisition_start_##ID_UPPER, \
	.dev_acquisition_stop = dev_acquisition_stop, \
	.priv = NULL, \
};

DRV(bbcgm_m2110, BBCGM_M2110, "bbcgm-m2110", "BBC Goertz Metrawatt M2110")
DRV(digitek_dt4000zc, DIGITEK_DT4000ZC, "digitek-dt4000zc", "Digitek DT4000ZC")
DRV(tekpower_tp4000zc, TEKPOWER_TP4000ZC, "tekpower-tp4000zc", "TekPower TP4000ZC")
DRV(metex_me31, METEX_ME31, "metex-me31", "Metex ME-31")
DRV(peaktech_3410, PEAKTECH_3410, "peaktech-3410", "PeakTech 3410")
DRV(mastech_mas345, MASTECH_MAS345, "mastech-mas345", "MASTECH MAS345")
DRV(va_va18b, VA_VA18B, "va-va18b", "V&A VA18B")
DRV(va_va40b, VA_VA40B, "va-va40b", "V&A VA40B")
DRV(metex_m3640d, METEX_M3640D, "metex-m3640d", "Metex M-3640D")
DRV(metex_m4650cr, METEX_M4650CR, "metex-m4650cr", "Metex M-4650CR")
DRV(peaktech_4370, PEAKTECH_4370, "peaktech-4370", "PeakTech 4370")
DRV(pce_pce_dm32, PCE_PCE_DM32, "pce-pce-dm32", "PCE PCE-DM32")
DRV(radioshack_22_168, RADIOSHACK_22_168, "radioshack-22-168", "RadioShack 22-168")
DRV(radioshack_22_805, RADIOSHACK_22_805, "radioshack-22-805", "RadioShack 22-805")
DRV(radioshack_22_812, RADIOSHACK_22_812, "radioshack-22-812", "RadioShack 22-812")
DRV(tecpel_dmm_8061_ser, TECPEL_DMM_8061_SER, "tecpel-dmm-8061-ser", "Tecpel DMM-8061 (UT-D02 cable)")
DRV(voltcraft_m3650d, VOLTCRAFT_M3650D, "voltcraft-m3650d", "Voltcraft M-3650D")
DRV(voltcraft_m4650cr, VOLTCRAFT_M4650CR, "voltcraft-m4650cr", "Voltcraft M-4650CR")
DRV(voltcraft_vc820_ser, VOLTCRAFT_VC820_SER, "voltcraft-vc820-ser", "Voltcraft VC-820 (UT-D02 cable)")
DRV(voltcraft_vc830_ser, VOLTCRAFT_VC830_SER, "voltcraft-vc830-ser", "Voltcraft VC-830 (UT-D02 cable)")
DRV(voltcraft_vc840_ser, VOLTCRAFT_VC840_SER, "voltcraft-vc840-ser", "Voltcraft VC-840 (UT-D02 cable)")
DRV(uni_t_ut60a_ser, UNI_T_UT60A_SER, "uni-t-ut60a-ser", "UNI-T UT60A (UT-D02 cable)")
DRV(uni_t_ut60e_ser, UNI_T_UT60E_SER, "uni-t-ut60e-ser", "UNI-T UT60E (UT-D02 cable)")
DRV(uni_t_ut61b_ser, UNI_T_UT61B_SER, "uni-t-ut61b-ser", "UNI-T UT61B (UT-D02 cable)")
DRV(uni_t_ut61c_ser, UNI_T_UT61C_SER, "uni-t-ut61c-ser", "UNI-T UT61C (UT-D02 cable)")
DRV(uni_t_ut61d_ser, UNI_T_UT61D_SER, "uni-t-ut61d-ser", "UNI-T UT61D (UT-D02 cable)")
DRV(uni_t_ut61e_ser, UNI_T_UT61E_SER, "uni-t-ut61e-ser", "UNI-T UT61E (UT-D02 cable)")
DRV(iso_tech_idm103n, ISO_TECH_IDM103N, "iso-tech-idm103n", "ISO-TECH IDM103N")
