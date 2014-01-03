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

#ifndef LIBSIGROK_HARDWARE_AGILENT_DMM_AGILENT_DMM_H
#define LIBSIGROK_HARDWARE_AGILENT_DMM_AGILENT_DMM_H

#define LOG_PREFIX "agilent-dmm"

#define AGDMM_BUFSIZE  256

/* Supported models */
enum {
	AGILENT_U1231A = 1,
	AGILENT_U1232A,
	AGILENT_U1233A,
	AGILENT_U1251A,
	AGILENT_U1252A,
	AGILENT_U1253A,
};

/* Supported device profiles */
struct agdmm_profile {
	int model;
	const char *modelname;
	const struct agdmm_job *jobs;
	const struct agdmm_recv *recvs;
};

/* Private, per-device-instance driver context. */
struct dev_context {
	const struct agdmm_profile *profile;
	uint64_t limit_samples;
	uint64_t limit_msec;

	/* Opaque pointer passed in by the frontend. */
	void *cb_data;

	/* Runtime. */
	uint64_t num_samples;
	int64_t jobqueue[8];
	unsigned char buf[AGDMM_BUFSIZE];
	int buflen;
	int cur_mq;
	int cur_unit;
	int cur_mqflags;
	int cur_divider;
	int cur_acdc;
	int mode_tempaux;
	int mode_continuity;
};

struct agdmm_job {
	int interval;
	int (*send) (const struct sr_dev_inst *sdi);
};

struct agdmm_recv {
	const char *recv_regex;
	int (*recv) (const struct sr_dev_inst *sdi, GMatchInfo *match);
};

SR_PRIV int agdmm_receive_data(int fd, int revents, void *cb_data);

#endif
