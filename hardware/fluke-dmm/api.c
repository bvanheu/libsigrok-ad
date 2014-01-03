/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "fluke-dmm.h"

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

SR_PRIV struct sr_dev_driver flukedmm_driver_info;
static struct sr_dev_driver *di = &flukedmm_driver_info;

static char *scan_conn[] = {
	/* 287/289 */
	"115200/8n1",
	/* 187/189 */
	"9600/8n1",
	/* Scopemeter 190 series */
	"1200/8n1",
	NULL
};

static const struct flukedmm_profile supported_flukedmm[] = {
	{ FLUKE_187, "187", 100, 1000 },
	{ FLUKE_287, "287", 100, 1000 },
	{ FLUKE_190, "199B", 1000, 3500 },
};

static int dev_clear(void)
{
	return std_dev_clear(di, NULL);
}

static int init(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static GSList *fluke_scan(const char *conn, const char *serialcomm)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_probe *probe;
	struct sr_serial_dev_inst *serial;
	GSList *devices;
	int retry, len, i, s;
	char buf[128], *b, **tokens;

	if (!(serial = sr_serial_dev_inst_new(conn, serialcomm)))
		return NULL;

	if (serial_open(serial, SERIAL_RDWR | SERIAL_NONBLOCK) != SR_OK)
		return NULL;

	drvc = di->priv;
	b = buf;
	retry = 0;
	devices = NULL;
	/* We'll try the discovery sequence three times in case the device
	 * is not in an idle state when we send ID. */
	while (!devices && retry < 3) {
		retry++;
		serial_flush(serial);
		if (serial_write(serial, "ID\r", 3) == -1) {
			sr_err("Unable to send ID string: %s.",
			       strerror(errno));
			continue;
		}

		/* Response is first a CMD_ACK byte (ASCII '0' for OK,
		 * or '1' to signify an error. */
		len = 128;
		serial_readline(serial, &b, &len, 150);
		if (len != 1)
			continue;
		if (buf[0] != '0')
			continue;

		/* If CMD_ACK was OK, ID string follows. */
		len = 128;
		serial_readline(serial, &b, &len, 850);
		if (len < 10)
			continue;
		if (strcspn(buf, ",") < 15)
			/* Looks like it's comma-separated. */
			tokens = g_strsplit(buf, ",", 3);
		else
			/* Fluke 199B, at least, uses semicolon. */
			tokens = g_strsplit(buf, ";", 3);
		if (!strncmp("FLUKE", tokens[0], 5)
				&& tokens[1] && tokens[2]) {
			for (i = 0; supported_flukedmm[i].model; i++) {
				if (strcmp(supported_flukedmm[i].modelname, tokens[0] + 6))
					continue;
				/* Skip leading spaces in version number. */
				for (s = 0; tokens[1][s] == ' '; s++);
				if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE, "Fluke",
						tokens[0] + 6, tokens[1] + s)))
					return NULL;
				if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
					sr_err("Device context malloc failed.");
					return NULL;
				}
				devc->profile = &supported_flukedmm[i];
				sdi->inst_type = SR_INST_SERIAL;
				sdi->conn = serial;
				sdi->priv = devc;
				sdi->driver = di;
				if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "P1")))
					return NULL;
				sdi->probes = g_slist_append(sdi->probes, probe);
				drvc->instances = g_slist_append(drvc->instances, sdi);
				devices = g_slist_append(devices, sdi);
				break;
			}
		}
		g_strfreev(tokens);
		if (devices)
			/* Found one. */
			break;
	}
	serial_close(serial);
	if (!devices)
		sr_serial_dev_inst_free(serial);

	return devices;
}

static GSList *scan(GSList *options)
{
	struct sr_config *src;
	GSList *l, *devices;
	int i;
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
		devices = fluke_scan(conn, serialcomm);
	} else {
		for (i = 0; scan_conn[i]; i++) {
			if ((devices = fluke_scan(conn, scan_conn[i])))
				break;
			/* The Scopemeter 199B, at least, requires this
			 * after all the 115k/9.6k confusion. */
			g_usleep(5000);
		}
	}

	return devices;
}

static GSList *dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int cleanup(void)
{
	return dev_clear();
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
	case SR_CONF_LIMIT_MSEC:
		/* TODO: not yet implemented */
		if (g_variant_get_uint64(data) == 0) {
			sr_err("LIMIT_MSEC can't be 0.");
			return SR_ERR;
		}
		devc->limit_msec = g_variant_get_uint64(data);
		sr_dbg("Setting time limit to %" PRIu64 "ms.",
		       devc->limit_msec);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		sr_dbg("Setting sample limit to %" PRIu64 ".",
		       devc->limit_samples);
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

static int dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
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

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	/* Poll every 100ms, or whenever some data comes in. */
	serial = sdi->conn;
	serial_source_add(serial, G_IO_IN, 50, fluke_receive_data, (void *)sdi);

	if (serial_write(serial, "QM\r", 3) == -1) {
		sr_err("Unable to send QM: %s.", strerror(errno));
		return SR_ERR;
	}
	devc->cmd_sent_at = g_get_monotonic_time() / 1000;
	devc->expect_response = TRUE;

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	return std_serial_dev_acquisition_stop(sdi, cb_data, std_serial_dev_close,
			sdi->conn, LOG_PREFIX);
}

SR_PRIV struct sr_dev_driver flukedmm_driver_info = {
	.name = "fluke-dmm",
	.longname = "Fluke 18x/28x series DMMs",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = dev_list,
	.dev_clear = dev_clear,
	.config_get = NULL,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = std_serial_dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};
