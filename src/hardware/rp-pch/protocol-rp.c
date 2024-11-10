/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2022 Shawn Walker <ac0bi00@gmail.com>
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
#define _GNU_SOURCE

#include <config.h>
#include <errno.h>
#include <glib.h>
#include <math.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol-rp.h"


//==================================================================
// Protocol with the probe
//==================================================================
// Reset:
//   Host: '*'
//   Probe: Interrupt doing anything and: '*$#{PCH.v1}'  
//
// SampleRate:
//   Host: 'R' Hz-rate-decimal-9-digits '#'
//   Probe: ' :'
//
// Acquisition:
//   Host: 'A>'
//   Probe: '/^' repeat (8-bit-data * 4096) ';' Block-number-low-byte block-number-high-byte '.'
//
// Stop Acq:
//   Host: 'S$'
//   Probe: '=|'

static const unsigned ACK_LENGTH = 8;
static const uint8_t * const RESET_ANNOUNCE    = (const uint8_t *)"*PCHv1.";  // followed by no replay random uint8_t
static const char *    const SAMPLE_RATE_CMD   = "R%09llu#";
static const uint8_t *       SAMPLE_RATE_ACK   = (const uint8_t *)"RATEACK";  //-- including ending null char
static const unsigned CMD_LENGTH = 2;
static const char *    const START_ACQ_CMD     = "A>";
static const uint8_t *       START_ACQ_ACK     = (const uint8_t *)"STARTAC";  //-- including ending null char
static const char *    const STOP_ACQ_CMD      = "=|";
static const uint8_t * const STOP_ACQ_ACK      = (const uint8_t *)"ENDACK.";  //-- including ending null char
static const unsigned EOB_LENGTH = 4;
static const uint8_t *       EOB_MARKER        = (const uint8_t *)"EOB";  //-- including ending null char & preceeded by buffer number in little endian

static uint8_t  noreplay_random = 0;

static int send_serial_str(struct sr_serial_dev_inst *serial, const char *str)
{
	int len = strlen(str);
	if ((len > 15) || (len < 1)) {
		sr_err("ERROR: Serial string len %d invalid ", len);
		return SR_ERR;
	}

	/* 100ms timeout. With USB CDC serial we can't define the timeout based
	 * on link rate, so just pick something large as we shouldn't normally
	 * see them */
	if (serial_write_blocking(serial, str, len, 100) != len) {
		sr_err("ERROR: Serial str write failed");
		return SR_ERR;
	}

	return SR_OK;
}

static int send_serial_char(struct sr_serial_dev_inst *serial, char ch)
{
	char buf[1];
	buf[0] = ch;

	if (serial_write_blocking(serial, buf, 1, 100) != 1) {	/* 100ms */
		sr_err("ERROR: Serial char write failed");
		return SR_ERR;
	}

	return SR_OK;
}

/* Issue a command that expects a string return that is less than 30 characters.
 * Returns the length of string */
static int send_serial_w_resp(struct sr_serial_dev_inst *serial, const char *str, uint8_t *resp, size_t cnt)
{
	int num_read, i;
	send_serial_str(serial, str);

	/* Using the serial_read_blocking function when reading a response of
	 * unknown length requires a long worst case timeout to always be taken.
	 * So, instead loop waiting for a first byte, and then a final small delay
	 * for the rest. */
	for (i = 0; i < 1000; i++) {	/* wait up to 1 second in ms increments */
		num_read = serial_read_blocking(serial, resp, cnt, 1);
		if (num_read > 0)
			break;
	}

	/* Since the serial port is USB CDC we can't calculate timeouts based on
	 * baud rate but even if the response is split between two USB transfers,
	 * 10ms should be plenty. */
	num_read += serial_read_blocking(serial, resp + num_read, cnt - num_read,
		10);
	if (num_read < 1) {
		sr_err("ERROR: Serial_w_resp failed (%d).", num_read);
		return -1;
	} else
		return num_read;
}


/**
 * @brief Reset the device and wait for device reset acknowledge
 * 
 * @param serial 
 * @param conn 
 * @param serialcomm 
 * @return gboolean indicate success
 */

gboolean reset_rp_device(struct sr_serial_dev_inst *serial)
{
	const int BUF_LENGTH = 32;
	time_t cur_time;

	if (noreplay_random == 0) {
		noreplay_random = (uint8_t)time(&cur_time);
	}
	noreplay_random ^= 0x5500AA11;
	noreplay_random ^= noreplay_random >> 9;
	noreplay_random ^= noreplay_random << 7;

	int len;
	unsigned keep = 0;
	char buf[BUF_LENGTH];
	gboolean result = FALSE;
	uint8_t random_byte = (uint8_t)noreplay_random;

	send_serial_char(serial, '*');
	send_serial_char(serial, random_byte);
	g_usleep(10000);
	do {
		sr_warn("Drain reads");
		//-- Waiting for RESET_ANNOUNCE
		len = serial_read_blocking(serial, buf + keep, BUF_LENGTH - keep, 100);
		if (len)
		{
			char * match_start = buf;

			result = FALSE;
			if (keep == 0) {
				//-- No match yet, try to find one
				match_start = memchr(buf, *RESET_ANNOUNCE, len);
				if (match_start != NULL) {
					//-- match beginning is found keep next chars
					keep = len - (match_start - buf);
					len = 0; //-- All useful read data are kept. no more data
				}
				else {
					// Leave non null len to indicate that
					// though we have not found expected data
					// we have at least received data
					sr_dbg("Dropping %d serial data", len);
				}
			}

			if (keep > 0) {
				keep += len; //-- Add new read data to what we were keeping
				if (keep >= ACK_LENGTH) {
					//-- We have enough data to check device annouce
					if ((memcmp(match_start, RESET_ANNOUNCE, ACK_LENGTH-1) == 0) 
					 && ((uint8_t)(match_start[ACK_LENGTH-1]) == random_byte)) {
						//-- This is matching. Normally there should be nothing after that
						keep -= ACK_LENGTH;
						result = (keep == 0);
						match_start += ACK_LENGTH; //-- next match if any may not start before this point
					}
					else {
						//-- Try to find a match at next char
						match_start++;
						keep--;
					}
					if (!result && (match_start != buf)) {
						memmove(buf, match_start, keep);
					}
				}
			}
		}
		else {
			//-- No data received before the timeout
			//-- Forget previous received data and exit loop
			keep = 0;
		}
	} while ((len > 0) || (keep > 0));
	sr_warn("Drain reads done");
	return result;
}


/**
 * @brief Send sample rate and wait for device acknowledgement
 * 
 * @param sdi 
 * @return SR_PRIV 
 */
gboolean send_rp_sample_rate(const struct sr_dev_inst *sdi) {
	struct dev_context * const devc = sdi->priv;
	char tmpstr[16];
	int  num_read;

	snprintf(tmpstr, sizeof(tmpstr), SAMPLE_RATE_CMD, devc->sample_rate);
	num_read = send_serial_w_resp(sdi->conn, tmpstr, devc->d_data_buf, strlen(tmpstr));
	if (num_read != sizeof(SAMPLE_RATE_ACK)) {
		sr_err("Failed to read probe sample rate command acknowledgement");
		return FALSE;
	}
	if (memcmp(devc->d_data_buf, SAMPLE_RATE_ACK, ACK_LENGTH) != 0) {
		sr_err("Invalid probe answer to sample rate command");
		return FALSE;
	}
	return TRUE;
}



gboolean send_rp_start_capture(const struct sr_dev_inst *sdi) {
	struct dev_context * const devc = sdi->priv;
	unsigned  num_read;

	num_read = send_serial_w_resp(sdi->conn, START_ACQ_CMD, devc->d_data_buf, ACK_LENGTH);
	if (num_read != ACK_LENGTH) {
		sr_err("Failed to read probe start acquisition command acknowledgement");
		return FALSE;
	}
	if (memcmp(devc->d_data_buf, START_ACQ_ACK, ACK_LENGTH) != 0) {
		sr_err("Invalid probe answer to start acquisition command");
		return FALSE;
	}
	return TRUE;
}


bufferstatus_t read_rp_data_block (struct sr_dev_inst *sdi) {
	struct dev_context * const devc = sdi->priv;
	uint32_t buf_number;

	/* Fill the buffer, note the end may have partial slices */
	int bytes_rem = SERIAL_BUFFER_SIZE - devc->wrptr;

	devc->wrptr += serial_read_blocking(sdi->conn, devc->d_data_buf + devc->wrptr, bytes_rem, 10);

	if (devc->wrptr == SERIAL_BUFFER_SIZE) {
		if (memcmp(devc->d_data_buf + SERIAL_BUFFER_SIZE - EOB_LENGTH, EOB_MARKER, EOB_LENGTH) != 0) {
			sr_err("ERROR: Sample buffer badly formatted");
			return INVALID_DATA;
		}
		//-- little endlian
		buf_number = ((uint32_t)(devc->d_data_buf[SERIAL_BUFFER_SIZE-1]) << 24) + 
						((uint32_t)(devc->d_data_buf[SERIAL_BUFFER_SIZE-2]) << 16) + 
						((uint32_t)(devc->d_data_buf[SERIAL_BUFFER_SIZE-3]) << 8) + 
						devc->d_data_buf[SERIAL_BUFFER_SIZE-1];
		if (buf_number != devc->buffer_number) {
			sr_err("ERROR: buffer number is not correct: found=%08x, expected=%08x", buf_number, devc->buffer_number);
			return INVALID_DATA;
		}
		devc->buffer_number++;
		devc->wrptr = 0;
		return BUFFER_RECEIVED;
	}
	else if ((devc->wrptr == ACK_LENGTH)
	 && (memcmp(devc->d_data_buf, STOP_ACQ_ACK, ACK_LENGTH) == 0)) {
		devc->wrptr = 0;
		return ACQ_COMPLETED;
	}
	return PARTIAL_DATA;
}

gboolean send_rp_stop_capture(struct sr_dev_inst *sdi) {
	int  num_sent = serial_write_blocking(sdi->conn, STOP_ACQ_CMD, CMD_LENGTH, 30);
	if (num_sent != sizeof(STOP_ACQ_CMD)) {
		sr_err("Failed to send probe stop acquisition command");
		return FALSE;
	}
	return TRUE;
}

