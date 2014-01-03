/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include "config.h" /* Needed for PACKAGE_STRING and others. */
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "output/gnuplot"

struct context {
	unsigned int num_enabled_probes;
	unsigned int unitsize;
	char *header;
	uint8_t *old_sample;
};

static const char *gnuplot_header = "\
# Sample data in space-separated columns format usable by gnuplot\n\
#\n\
# Generated by: %s on %s%s\
# Period: %s\n\
#\n\
# Column\tProbe\n\
# -------------------------------------\
----------------------------------------\n\
# 0\t\tSample counter (for internal gnuplot purposes)\n%s\n";

static const char *gnuplot_header_comment = "\
# Comment: Acquisition with %d/%d probes at %s\n";

static int init(struct sr_output *o)
{
	struct context *ctx;
	struct sr_probe *probe;
	GSList *l;
	GVariant *gvar;
	uint64_t samplerate;
	unsigned int i;
	int num_probes;
	char *c, *frequency_s;
	char wbuf[1000], comment[128];
	time_t t;

	if (!o) {
		sr_err("%s: o was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!o->sdi) {
		sr_err("%s: o->sdi was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!(ctx = g_try_malloc0(sizeof(struct context)))) {
		sr_err("%s: ctx malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	o->internal = ctx;
	ctx->num_enabled_probes = 0;
	for (l = o->sdi->probes; l; l = l->next) {
		probe = l->data;
		if (probe->type != SR_PROBE_LOGIC)
			continue;
		if (!probe->enabled)
			continue;
		ctx->num_enabled_probes++;
	}
	ctx->unitsize = (ctx->num_enabled_probes + 7) / 8;

	num_probes = g_slist_length(o->sdi->probes);
	comment[0] = '\0';
	samplerate = 0;
	if (sr_config_get(o->sdi->driver, o->sdi, NULL, SR_CONF_SAMPLERATE,
			&gvar) == SR_OK) {
		samplerate = g_variant_get_uint64(gvar);
		g_variant_unref(gvar);
		if (!(frequency_s = sr_samplerate_string(samplerate))) {
			sr_err("%s: sr_samplerate_string failed", __func__);
			g_free(ctx);
			return SR_ERR;
		}
		snprintf(comment, 127, gnuplot_header_comment,
			ctx->num_enabled_probes, num_probes, frequency_s);
		g_free(frequency_s);
	}

	/* Columns / channels */
	wbuf[0] = '\0';
	for (i = 0, l = o->sdi->probes; l; l = l->next, i++) {
		probe = l->data;
		if (probe->type != SR_PROBE_LOGIC)
			continue;
		if (!probe->enabled)
			continue;
		c = (char *)&wbuf + strlen((const char *)&wbuf);
		sprintf(c, "# %d\t\t%s\n", i + 1, probe->name);
	}

	if (!(frequency_s = sr_period_string(samplerate))) {
		sr_err("%s: sr_period_string failed", __func__);
		g_free(ctx);
		return SR_ERR;
	}

	t = time(NULL);
	ctx->header = g_strdup_printf(gnuplot_header, PACKAGE_STRING,
			ctime(&t), comment, frequency_s, (char *)&wbuf);
	g_free(frequency_s);

	if (!(ctx->old_sample = g_try_malloc0(ctx->unitsize))) {
		sr_err("%s: ctx->old_sample malloc failed", __func__);
		g_free(ctx->header);
		g_free(ctx);
		return SR_ERR_MALLOC;
	}

	return 0;
}

static int event(struct sr_output *o, int event_type, uint8_t **data_out,
		 uint64_t *length_out)
{
	if (!o) {
		sr_err("%s: o was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!data_out) {
		sr_err("%s: data_out was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!length_out) {
		sr_err("%s: length_out was NULL", __func__);
		return SR_ERR_ARG;
	}

	switch (event_type) {
	case SR_DF_TRIGGER:
		/* TODO: Can a trigger mark be in a gnuplot data file? */
		break;
	case SR_DF_END:
		g_free(o->internal);
		o->internal = NULL;
		break;
	default:
		sr_err("%s: unsupported event type: %d", __func__, event_type);
		break;
	}

	*data_out = NULL;
	*length_out = 0;

	return SR_OK;
}

static int data(struct sr_output *o, const uint8_t *data_in,
		uint64_t length_in, uint8_t **data_out, uint64_t *length_out)
{
	struct context *ctx;
	unsigned int max_linelen, outsize, p, curbit, i;
	const uint8_t *sample;
	static uint64_t samplecount = 0;
	uint8_t *outbuf, *c;

	if (!o) {
		sr_err("%s: o was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!o->internal) {
		sr_err("%s: o->internal was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!data_in) {
		sr_err("%s: data_in was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!data_out) {
		sr_err("%s: data_out was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!length_out) {
		sr_err("%s: length_out was NULL", __func__);
		return SR_ERR_ARG;
	}

	ctx = o->internal;
	max_linelen = 16 + ctx->num_enabled_probes * 2;
	outsize = length_in / ctx->unitsize * max_linelen;
	if (ctx->header)
		outsize += strlen(ctx->header);

	if (!(outbuf = g_try_malloc0(outsize))) {
		sr_err("%s: outbuf malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	outbuf[0] = '\0';
	if (ctx->header) {
		/* The header is still here, this must be the first packet. */
		strncpy((char *)outbuf, ctx->header, outsize);
		g_free(ctx->header);
		ctx->header = NULL;
	}

	for (i = 0; i <= length_in - ctx->unitsize; i += ctx->unitsize) {

		sample = data_in + i;

		/*
		 * Don't output the same samples multiple times. However, make
		 * sure to output at least the first and last sample.
		 */
		if (samplecount++ != 0 &&
				!memcmp(sample, ctx->old_sample, ctx->unitsize)) {
			if (i != (length_in - ctx->unitsize))
				continue;
		}
		memcpy(ctx->old_sample, sample, ctx->unitsize);

		/* The first column is a counter (needed for gnuplot). */
		c = outbuf + strlen((const char *)outbuf);
		sprintf((char *)c, "%" PRIu64 "\t", samplecount++);

		/* The next columns are the values of all channels. */
		for (p = 0; p < ctx->num_enabled_probes; p++) {
			curbit = (sample[p / 8] & ((uint8_t) (1 << (p % 8)))) >> (p % 8);
			c = outbuf + strlen((const char *)outbuf);
			sprintf((char *)c, "%d ", curbit);
		}

		c = outbuf + strlen((const char *)outbuf);
		sprintf((char *)c, "\n");
	}

	*data_out = outbuf;
	*length_out = strlen((const char *)outbuf);

	return SR_OK;
}

SR_PRIV struct sr_output_format output_gnuplot = {
	.id = "gnuplot",
	.description = "Gnuplot",
	.df_type = SR_DF_LOGIC,
	.init = init,
	.data = data,
	.event = event,
};