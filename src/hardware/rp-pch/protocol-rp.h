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

#ifndef LIBSIGROK_HARDWARE_RASPBERRYPI_PICO_PROTOCOL_H
#define LIBSIGROK_HARDWARE_RASPBERRYPI_PICO_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

/* This is used by sr_dbg/log etc to indicate where a printout came from */
#define LOG_PREFIX "pchrp"

/* Number of bytes between markers */
// #define MRK_STRIDE 128

/* Channel counts */
#define CHANNEL_COUNT 8
#define SAMPLE_SIZE   ((CHANNEL_COUNT+CHAR_BIT-1)/CHAR_BIT)

#define ROUND_COUNT        8
#define ROUND_MASK         (~(ROUND_COUNT-1))
#define DATA_BLOCK_SIZE    0x8000L
#define EOB_MARKER_SIZE    ROUND_COUNT
/* Size of serial data buffer */
#define SERIAL_BUFFER_SIZE (DATA_BLOCK_SIZE + EOB_MARKER_SIZE)


typedef enum rxstate {
	RX_IDLE = 0,		/* Not receiving */
	RX_ACTIVE = 1,		/* Receiving data */
	RX_STOPPED = 2,		/* Received stop marker, waiting for byte cnt */
	RX_ABORT = 3,		/* Received aborted marker or other error */
} rxstate_t;


typedef enum bufferstatus {
	BUFFER_RECEIVED,
	PARTIAL_DATA,
	ACQ_COMPLETED,
	INVALID_DATA,
} bufferstatus_t;



struct dev_context {
	/* Configuration Parameters
	 * It is up to the user to understand sample rates and serial download speed
	 * etc and do the right thing. i.e. don't expect continuous streaming
	 * bandwidth greater than serial link speed etc... */
	/* The number of samples the user expects to see. */
	uint64_t limit_samples;
	uint64_t sample_rate;
	/* Number of samples that have been received and processed */
	uint32_t num_samples;
	/* Masks of enabled channels based on user input */
	uint32_t chan_mask;
	/* Channel groups - each analog channel is its own group */
	// struct sr_channel_group *digital_group;
	/* % ratio of pre-trigger to post trigger samples */
	uint64_t capture_ratio;

	/* Tracking/status once started */
	/* Samples sent to the session */
	uint32_t sent_samples;
	/* Buffer number to detect lost info */
	uint32_t buffer_number;
	/* For SW-based triggering we put the device into continuous transmit and
	 * stop when we detect a sample and capture all the samples we need.
	 * trigger_fired is thus set when the sw trigger logic detects a trigger.
	 * For non triggered modes we send a start and a number of samples and the
	 * device transmits that much. trigger_fired is set immediately at the
	 * start. */
	gboolean trigger_fired;
	gboolean pretrig_filled;
	/* Keep previous sample value to check trigger change conditions */
	uint8_t  previous_sample;
	rxstate_t rxstate;

	/* Serial Related */
	/* Serial data buffer */
	// unsigned char *buffer;
	/* Size of incoming serial buffer*/
	//uint32_t serial_buffer_size;
	/* Current byte in serial read stream that is being processed */
	uint32_t ser_rdptr;
	/* Write pointer into the serial input buffer */
	uint32_t wrptr;

	/* Buffering Related */
	/* Parsed serial read data is split into each channels dedicated buffer
	 * for analog */
	// float *a_data_bufs[MAX_ANALOG_CHANNELS];
	/* Digital samples are stored packed together since cli/pulseview want it
	 * that way */
	uint8_t *d_data_buf;
	/* Write pointer for the the per channel data buffers */
	uint32_t cbuf_wrptr;
	/* Size of packet data buffers for each channel */
	//uint32_t sample_buf_size;

	// /* RLE related*/
	// /* Previous sample values to duplicate for rle */
	// float a_last[MAX_ANALOG_CHANNELS];
	// uint8_t d_last[4];

	/* SW trigger related */
	//struct soft_trigger_logic *stl;
	uint64_t mask_state;
	uint64_t mask_change; 
	uint64_t expect_state;

	/* Maximum number of entries to store pre-trigger */
	uint32_t pretrig_entries;
	uint32_t pretrig_wr_ptr;
	uint8_t *pretrig_buf;
};

gboolean reset_rp_device(struct sr_serial_dev_inst *serial);
gboolean send_rp_sample_rate(const struct sr_dev_inst *sdi);
gboolean send_rp_start_capture(const struct sr_dev_inst *sdi);
bufferstatus_t read_rp_data_block (struct sr_dev_inst *sdi);
gboolean send_rp_stop_capture(struct sr_dev_inst *sdi);

#endif
