/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2011 Håvard Espeland <gus@ping.uio.no>
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
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "text.h"

#define LOG_PREFIX "output/ascii"

SR_PRIV int init_ascii(struct sr_output *o)
{
	return init(o, DEFAULT_BPL_ASCII, MODE_ASCII);
}

SR_PRIV int data_ascii(struct sr_output *o, const uint8_t *data_in,
		       uint64_t length_in, uint8_t **data_out,
		       uint64_t *length_out)
{
	struct context *ctx;
	unsigned int outsize, offset, p;
	int max_linelen;
	const uint8_t *sample;
	uint8_t *outbuf;

	ctx = o->internal;
	max_linelen = SR_MAX_PROBENAME_LEN + 3 + ctx->samples_per_line
			+ ctx->samples_per_line / 8;
        /*
         * Calculate space needed for probes. Set aside 512 bytes for
         * extra output, e.g. trigger.
         */
	outsize = 512 + (1 + (length_in / ctx->unitsize) / ctx->samples_per_line)
            * (ctx->num_enabled_probes * max_linelen);

	if (!(outbuf = g_try_malloc0(outsize + 1))) {
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

	if (length_in >= ctx->unitsize) {
		for (offset = 0; offset <= length_in - ctx->unitsize;
		     offset += ctx->unitsize) {
			sample = data_in + offset;

			char tmpval[ctx->num_enabled_probes];

			for (p = 0; p < ctx->num_enabled_probes; p++) {
				uint8_t curbit = (sample[p / 8] & ((uint8_t) 1 << (p % 8)));
				uint8_t prevbit = (ctx->prevsample[p / 8] &
						((uint8_t) 1 << (p % 8)));

				if (curbit < prevbit && ctx->line_offset > 0) {
					ctx->linebuf[p * ctx->linebuf_len +
						ctx->line_offset-1] = '\\';
				}

				if (curbit > prevbit) {
					tmpval[p] = '/';
				} else {
					if (curbit)
						tmpval[p] = '"';
					else
						tmpval[p] = '.';
				}
			}

			/* End of line. */
			if (ctx->spl_cnt >= ctx->samples_per_line) {
				flush_linebufs(ctx, outbuf);
				ctx->line_offset = ctx->spl_cnt = 0;
				ctx->mark_trigger = -1;
			}

			for (p = 0; p < ctx->num_enabled_probes; p++) {
				ctx->linebuf[p * ctx->linebuf_len +
					     ctx->line_offset] = tmpval[p];
			}

			ctx->line_offset++;
			ctx->spl_cnt++;

			memcpy(ctx->prevsample, sample, ctx->unitsize);
		}
	} else {
		sr_info("Short buffer (length_in=%" PRIu64 ").", length_in);
	}

	*data_out = outbuf;
	*length_out = strlen((const char *)outbuf);

	return SR_OK;
}

SR_PRIV struct sr_output_format output_text_ascii = {
	.id = "ascii",
	.description = "ASCII",
	.df_type = SR_DF_LOGIC,
	.init = init_ascii,
	.data = data_ascii,
	.event = event,
	.cleanup = text_cleanup,
};
