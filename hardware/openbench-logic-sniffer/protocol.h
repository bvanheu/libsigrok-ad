/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
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

#ifndef LIBSIGROK_HARDWARE_OPENBENCH_LOGIC_SNIFFER_PROTOCOL_H
#define LIBSIGROK_HARDWARE_OPENBENCH_LOGIC_SNIFFER_PROTOCOL_H

#include <stdint.h>
#include <string.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "ols"

#define NUM_PROBES             32
#define NUM_TRIGGER_STAGES     4
#define TRIGGER_TYPE           "01"
#define SERIAL_SPEED           B115200
#define CLOCK_RATE             SR_MHZ(100)
#define MIN_NUM_SAMPLES        4
#define DEFAULT_SAMPLERATE     SR_KHZ(200)

/* Command opcodes */
#define CMD_RESET                  0x00
#define CMD_RUN                    0x01
#define CMD_TESTMODE               0x03
#define CMD_ID                     0x02
#define CMD_METADATA               0x04
#define CMD_SET_FLAGS              0x82
#define CMD_SET_DIVIDER            0x80
#define CMD_CAPTURE_SIZE           0x81
#define CMD_SET_TRIGGER_MASK_0     0xc0
#define CMD_SET_TRIGGER_MASK_1     0xc4
#define CMD_SET_TRIGGER_MASK_2     0xc8
#define CMD_SET_TRIGGER_MASK_3     0xcc
#define CMD_SET_TRIGGER_VALUE_0    0xc1
#define CMD_SET_TRIGGER_VALUE_1    0xc5
#define CMD_SET_TRIGGER_VALUE_2    0xc9
#define CMD_SET_TRIGGER_VALUE_3    0xcd
#define CMD_SET_TRIGGER_CONFIG_0   0xc2
#define CMD_SET_TRIGGER_CONFIG_1   0xc6
#define CMD_SET_TRIGGER_CONFIG_2   0xca
#define CMD_SET_TRIGGER_CONFIG_3   0xce

/* Bitmasks for CMD_FLAGS */
#define FLAG_DEMUX                 0x01
#define FLAG_FILTER                0x02
#define FLAG_CHANNELGROUP_1        0x04
#define FLAG_CHANNELGROUP_2        0x08
#define FLAG_CHANNELGROUP_3        0x10
#define FLAG_CHANNELGROUP_4        0x20
#define FLAG_CLOCK_EXTERNAL        0x40
#define FLAG_CLOCK_INVERTED        0x80
#define FLAG_RLE                   0x0100
#define FLAG_SWAP_PROBES           0x0200
#define FLAG_EXTERNAL_TEST_MODE    0x0400
#define FLAG_INTERNAL_TEST_MODE    0x0800

/* Private, per-device-instance driver context. */
struct dev_context {
	/* Fixed device settings */
	int max_probes;
	uint32_t max_samples;
	uint32_t max_samplerate;
	uint32_t protocol_version;

	/* Acquisition settings */
	uint64_t cur_samplerate;
	uint32_t cur_samplerate_divider;
	uint64_t limit_samples;
	int capture_ratio;
	int trigger_at;
	uint32_t probe_mask;
	uint32_t trigger_mask[4];
	uint32_t trigger_value[4];
	int num_stages;
	uint32_t flag_reg;

	/* Operational states */
	unsigned int num_transfers;
	unsigned int num_samples;
	int num_bytes;

	/* Temporary variables */
	unsigned int rle_count;
	unsigned char sample[4];
	unsigned char tmp_sample[4];
	unsigned char *raw_sample_buf;
};


SR_PRIV extern const char *ols_probe_names[NUM_PROBES + 1];

SR_PRIV int send_shortcommand(struct sr_serial_dev_inst *serial,
		uint8_t command);
SR_PRIV int send_longcommand(struct sr_serial_dev_inst *serial,
		uint8_t command, uint32_t data);
SR_PRIV int ols_configure_probes(const struct sr_dev_inst *sdi);
SR_PRIV uint32_t reverse16(uint32_t in);
SR_PRIV uint32_t reverse32(uint32_t in);
SR_PRIV struct dev_context *ols_dev_new(void);
SR_PRIV struct sr_dev_inst *get_metadata(struct sr_serial_dev_inst *serial);
SR_PRIV int ols_set_samplerate(const struct sr_dev_inst *sdi,
		uint64_t samplerate);
SR_PRIV void abort_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int ols_receive_data(int fd, int revents, void *cb_data);

#endif
