/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 poljar (Damir Jelić) <poljarinho@gmail.com>
 * Copyright (C) 2013 Martin Ling <martin-sigrok@earth.li>
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

#include "libsigrok.h"
#include "libsigrok-internal.h"

#include <glib.h>
#include <string.h>

#define LOG_PREFIX "scpi_serial"

#define SCPI_READ_RETRIES 100
#define SCPI_READ_RETRY_TIMEOUT 10000

struct scpi_serial {
	struct sr_serial_dev_inst *serial;
	char last_character;
};

SR_PRIV int scpi_serial_open(void *priv)
{
	struct scpi_serial *sscpi = priv;
	struct sr_serial_dev_inst *serial = sscpi->serial;

	if (serial_open(serial, SERIAL_RDWR | SERIAL_NONBLOCK) != SR_OK)
		return SR_ERR;

	if (serial_flush(serial) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV int scpi_serial_source_add(void *priv, int events, int timeout,
			sr_receive_data_callback_t cb, void *cb_data)
{
	struct scpi_serial *sscpi = priv;
	struct sr_serial_dev_inst *serial = sscpi->serial;

	return serial_source_add(serial, events, timeout, cb, cb_data);
}

SR_PRIV int scpi_serial_source_remove(void *priv)
{
	struct scpi_serial *sscpi = priv;
	struct sr_serial_dev_inst *serial = sscpi->serial;

	return serial_source_remove(serial);
}

SR_PRIV int scpi_serial_send(void *priv, const char *command)
{
	int len, result, written;
	gchar *terminated_command;
	struct scpi_serial *sscpi = priv;
	struct sr_serial_dev_inst *serial = sscpi->serial;

	terminated_command = g_strconcat(command, "\n", NULL);
	len = strlen(terminated_command);
	written = 0;
	while (written < len) {
		result = serial_write(serial, terminated_command + written, len - written);
		if (result < 0) {
			sr_err("Error while sending SCPI command: '%s'.", command);
			g_free(terminated_command);
			return SR_ERR;
		}
		written += result;
	}

	g_free(terminated_command);

	sr_spew("Successfully sent SCPI command: '%s'.", command);

	return SR_OK;
}

SR_PRIV int scpi_serial_receive(void *priv, char **scpi_response)
{
	int len, ret;
	char buf[256];
	unsigned int i;
	GString *response;
	struct scpi_serial *sscpi = priv;
	struct sr_serial_dev_inst *serial = sscpi->serial;

	response = g_string_sized_new(1024);

	for (i = 0; i <= SCPI_READ_RETRIES; i++) {
		while ((len = serial_read(serial, buf, sizeof(buf))) > 0)
			response = g_string_append_len(response, buf, len);

		if (response->len > 0 &&
		    response->str[response->len-1] == '\n') {
			sr_spew("Fetched full SCPI response.");
			break;
		}

		g_usleep(SCPI_READ_RETRY_TIMEOUT);
	}

	if (response->len == 0) {
		sr_dbg("No SCPI response received.");
		g_string_free(response, TRUE);
		*scpi_response = NULL;
		return SR_ERR;
	} else if (response->str[response->len - 1] == '\n') {
		/*
		 * The SCPI response contains a LF ('\n') at the end and we
		 * don't need this so replace it with a '\0' and decrement
		 * the length.
		 */
		response->str[--response->len] = '\0';
		ret = SR_OK;
	} else {
		sr_warn("Incomplete SCPI response received!");
		ret = SR_ERR;
	}

	/* Minor optimization: steal the string instead of copying. */
	*scpi_response = response->str;

	/* A SCPI response can be quite large, print at most 50 characters. */
	sr_dbg("SCPI response received (length %d): '%.50s'",
	       response->len, response->str);

	g_string_free(response, FALSE);

	return ret;
}

SR_PRIV int scpi_serial_read_begin(void *priv)
{
	struct scpi_serial *sscpi = priv;

	sscpi->last_character = '\0';

	return SR_OK;
}

SR_PRIV int scpi_serial_read_data(void *priv, char *buf, int maxlen)
{
	struct scpi_serial *sscpi = priv;
	int ret;

	ret = serial_read(sscpi->serial, buf, maxlen);

	if (ret < 0)
		return ret;

	if (ret > 0) {
		sscpi->last_character = buf[ret - 1];
		if (sscpi->last_character == '\n')
			ret--;
	}

	return ret;
}

SR_PRIV int scpi_serial_read_complete(void *priv)
{
	struct scpi_serial *sscpi = priv;

	return (sscpi->last_character == '\n');
}

static int scpi_serial_close(void *priv)
{
	struct scpi_serial *sscpi = priv;
	struct sr_serial_dev_inst *serial = sscpi->serial;

	return serial_close(serial);
}

static void scpi_serial_free(void *priv)
{
	struct scpi_serial *sscpi = priv;
	struct sr_serial_dev_inst *serial = sscpi->serial;

	sr_serial_dev_inst_free(serial);
	g_free(sscpi);
}

SR_PRIV struct sr_scpi_dev_inst *scpi_serial_dev_inst_new(const char *port,
		const char *serialcomm)
{
	struct sr_scpi_dev_inst *scpi;
	struct scpi_serial *sscpi;
	struct sr_serial_dev_inst *serial;

	if (!(serial = sr_serial_dev_inst_new(port, serialcomm)))
		return NULL;

	sscpi = g_malloc(sizeof(struct scpi_serial));

	sscpi->serial = serial;

	scpi = g_malloc(sizeof(struct sr_scpi_dev_inst));

	scpi->open = scpi_serial_open;
	scpi->source_add = scpi_serial_source_add;
	scpi->source_remove = scpi_serial_source_remove;
	scpi->send = scpi_serial_send;
	scpi->read_begin = scpi_serial_read_begin;
	scpi->read_data = scpi_serial_read_data;
	scpi->read_complete = scpi_serial_read_complete;
	scpi->close = scpi_serial_close;
	scpi->free = scpi_serial_free;
	scpi->priv = sscpi;

	return scpi;
}
