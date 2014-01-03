/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Aurelien Jacobs <aurel@gnuage.org>
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

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include "protocol.h"

#define STX  0x02
#define ETX  0x03
#define EOT  0x04
#define LF   0x0A
#define CR   0x0D

static gboolean teleinfo_control_check(char *label, char *data, char control)
{
	int sum = 0;
	while (*label)
		sum += *label++;
	sum += ' ';
	while (*data)
		sum += *data++;
	return ((sum & 0x3F) + ' ') == control;
}

static gint teleinfo_probe_compare(gconstpointer a, gconstpointer b)
{
	const struct sr_probe *probe = a;
	const char *name = b;
	return strcmp(probe->name, name);
}

static struct sr_probe *teleinfo_find_probe(struct sr_dev_inst *sdi,
                                            const char *name)
{
	GSList *elem = g_slist_find_custom(sdi->probes, name,
	                                   teleinfo_probe_compare);
	return elem ? elem->data : NULL;
}

static void teleinfo_send_value(struct sr_dev_inst *sdi, const char *probe_name,
                                float value, int mq, int unit)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_probe *probe;

	devc = sdi->priv;
	probe = teleinfo_find_probe(sdi, probe_name);

	if (!probe || !probe->enabled)
		return;

	memset(&analog, 0, sizeof(struct sr_datafeed_analog));
	analog.probes = g_slist_append(analog.probes, probe);
	analog.num_samples = 1;
	analog.mq = mq;
	analog.unit = unit;
	analog.data = &value;

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(devc->session_cb_data, &packet);
	g_slist_free(analog.probes);
}

static void teleinfo_handle_mesurement(struct sr_dev_inst *sdi,
                                       const char *label, const char *data,
                                       char *optarif)
{
	struct dev_context *devc;
	int v = atoi(data);

	if (!sdi || !(devc = sdi->priv)) {
		if (optarif && !strcmp(label, "OPTARIF"))
			strcpy(optarif, data);
		return;
	}

	if (!strcmp(label, "ADCO")) {
		devc->num_samples++;
	} else if (!strcmp(label, "BASE")) {
		teleinfo_send_value(sdi, "BASE", v, SR_MQ_POWER, SR_UNIT_WATT_HOUR);
	} else if (!strcmp(label, "HCHP")) {
		teleinfo_send_value(sdi, "HP"  , v, SR_MQ_POWER, SR_UNIT_WATT_HOUR);
	} else if (!strcmp(label, "HCHC")) {
		teleinfo_send_value(sdi, "HC"  , v, SR_MQ_POWER, SR_UNIT_WATT_HOUR);
	} else if (!strcmp(label, "EJPHN")) {
		teleinfo_send_value(sdi, "HN"  , v, SR_MQ_POWER, SR_UNIT_WATT_HOUR);
	} else if (!strcmp(label, "EJPHPM")) {
		teleinfo_send_value(sdi, "HPM" , v, SR_MQ_POWER, SR_UNIT_WATT_HOUR);
	} else if (!strcmp(label, "BBRHPJB")) {
		teleinfo_send_value(sdi, "HPJB", v, SR_MQ_POWER, SR_UNIT_WATT_HOUR);
	} else if (!strcmp(label, "BBRHPJW")) {
		teleinfo_send_value(sdi, "HPJW", v, SR_MQ_POWER, SR_UNIT_WATT_HOUR);
	} else if (!strcmp(label, "BBRHPJR")) {
		teleinfo_send_value(sdi, "HPJR", v, SR_MQ_POWER, SR_UNIT_WATT_HOUR);
	} else if (!strcmp(label, "BBRHCJB")) {
		teleinfo_send_value(sdi, "HCJB", v, SR_MQ_POWER, SR_UNIT_WATT_HOUR);
	} else if (!strcmp(label, "BBRHCJW")) {
		teleinfo_send_value(sdi, "HCJW", v, SR_MQ_POWER, SR_UNIT_WATT_HOUR);
	} else if (!strcmp(label, "BBRHCJR")) {
		teleinfo_send_value(sdi, "HCJR", v, SR_MQ_POWER, SR_UNIT_WATT_HOUR);
	} else if (!strcmp(label, "IINST")) {
		teleinfo_send_value(sdi, "IINST", v, SR_MQ_CURRENT, SR_UNIT_AMPERE);
	} else if (!strcmp(label, "PAPP")) {
		teleinfo_send_value(sdi, "PAPP", v, SR_MQ_POWER, SR_UNIT_VOLT_AMPERE);
	}
}

static gboolean teleinfo_parse_group(struct sr_dev_inst *sdi,
                                     const uint8_t *group, char *optarif)
{
	char label[9], data[13], control, cr;
	const char *str = (const char *)group;
	if (sscanf(str, "\x0A%8s %13s %c%c", label, data, &control, &cr) != 4
	    || cr != CR)
		return FALSE;
	if (!teleinfo_control_check(label, data, control))
		return FALSE;
	teleinfo_handle_mesurement(sdi, label, data, optarif);
	return TRUE;
}

static const uint8_t *teleinfo_parse_data(struct sr_dev_inst *sdi,
                                          const uint8_t *buf, int len,
                                          char *optarif)
{
	const uint8_t *group_start, *group_end;

	group_start = memchr(buf, LF, len);
	if (!group_start)
		return NULL;

	group_end = memchr(group_start, CR, len - (group_start - buf));
	if (!group_end)
		return NULL;

	teleinfo_parse_group(sdi, group_start, optarif);
	return group_end + 1;
}

SR_PRIV int teleinfo_get_optarif(const uint8_t *buf)
{
	const uint8_t *ptr = buf;
	char optarif[5] = { 0 };

	while ((ptr = teleinfo_parse_data(NULL, ptr, 292-(ptr-buf), optarif)));
	if (!strcmp(optarif, "BASE"))
		return OPTARIF_BASE;
	else if (!strcmp(optarif, "HC.."))
		return OPTARIF_HC;
	else if (!strcmp(optarif, "EJP."))
		return OPTARIF_EJP;
	else if (!strncmp(optarif, "BBR", 3))
		return OPTARIF_BBR;
	return OPTARIF_NONE;
}

SR_PRIV gboolean teleinfo_packet_valid(const uint8_t *buf)
{
	return !!teleinfo_get_optarif(buf);
}

SR_PRIV int teleinfo_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	const uint8_t *ptr, *next_ptr, *end_ptr;
	int len;
	int64_t time;

	(void)fd;

	if (!(sdi = cb_data) || !(devc = sdi->priv) || revents != G_IO_IN)
		return TRUE;
	serial = sdi->conn;

	/* Try to get as much data as the buffer can hold. */
	len = TELEINFO_BUF_SIZE - devc->buf_len;
	len = serial_read(serial, devc->buf + devc->buf_len, len);
	if (len < 1) {
		sr_err("Serial port read error: %d.", len);
		return FALSE;
	}
	devc->buf_len += len;

	/* Now look for packets in that data. */
	ptr = devc->buf;
	end_ptr = ptr + devc->buf_len;
	while ((next_ptr = teleinfo_parse_data(sdi, ptr, end_ptr - ptr, NULL)))
		ptr = next_ptr;

	/* If we have any data left, move it to the beginning of our buffer. */
	memmove(devc->buf, ptr, end_ptr - ptr);
	devc->buf_len -= ptr - devc->buf;

	/* If buffer is full and no valid packet was found, wipe buffer. */
	if (devc->buf_len >= TELEINFO_BUF_SIZE) {
		devc->buf_len = 0;
		return FALSE;
	}

	if (devc->limit_samples && devc->num_samples >= devc->limit_samples) {
		sr_info("Requested number of samples reached.");
		sdi->driver->dev_acquisition_stop(sdi, devc->session_cb_data);
		return TRUE;
	}

	if (devc->limit_msec) {
		time = (g_get_monotonic_time() - devc->start_time) / 1000;
		if (time > (int64_t)devc->limit_msec) {
			sr_info("Requested time limit reached.");
			sdi->driver->dev_acquisition_stop(sdi, devc->session_cb_data);
			return TRUE;
		}
	}
	return TRUE;
}
