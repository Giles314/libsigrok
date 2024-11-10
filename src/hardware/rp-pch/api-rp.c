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
#include <config.h>
#include <fcntl.h>
#include <glib.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol-rp.h"

/* Baud rate is really a don't care because we run USB CDC, dtr must be 1.
 * flow should be zero since we don't use xon/xoff */
#define SERIALCOMM "115200/8n1/dtr=1/rts=0/flow=0"

/* Use the force_detect scan option as a way to pass user information to the
 * device the string must use only 0-9,a-z,A-Z,'.','=' and '-'* and be less than
 * 60 characters */

static const uint32_t scanopts[] = {
	SR_CONF_CONN,		/* Required OS name for the port, i.e. /dev/ttyACM0 */
	SR_CONF_SERIALCOMM,	/* Optional config of the port, i.e. 115200/8n1 */
//	SR_CONF_FORCE_DETECT
};

/* Sample rate can either provide a std_gvar_samplerates_steps or a
 * std_gvar_samplerates. The latter is just a long list of every supported rate.
 * For the steps, pulseview/pv/toolbars/mainbar.cpp will do a min,max,step. If
 * step is 1 then it provides a 1,2,5,10 select otherwise it allows a spin box.
 * Going with the full list because while the spin box is more flexible, it is
 * harder to read */
static const uint64_t samplerates[] = {
	SR_KHZ(1),
	SR_KHZ(2),
	SR_KHZ(5),
	SR_KHZ(10),
	SR_KHZ(20),
	SR_KHZ(25),
	SR_KHZ(50),
	SR_KHZ(100),
	SR_KHZ(200),
	SR_KHZ(250),
	SR_KHZ(500),
	SR_MHZ(1),
	SR_MHZ(2),
	SR_MHZ(4),
	SR_MHZ(8),
	SR_MHZ(16),
	SR_MHZ(32),
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
	SR_TRIGGER_EDGE,
};


static const uint32_t devopts[] = {
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};


static struct sr_dev_driver rp_pch_driver_info;

static int check_trigger (struct dev_context *sdc, const uint8_t *buf, int len);


static GSList *scan(struct sr_dev_driver *di, GSList * options)
{
	struct sr_config *src;
	struct sr_dev_inst *sdi;
	struct sr_serial_dev_inst *serial;
	struct dev_context *devc;
	GSList *l;
	int i;
	gchar *channel_name;

	//-- Get scan options strings
	const char *conn = NULL;
	const char *serialcomm = NULL;
    // const char *force_detect = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		case SR_CONF_SERIALCOMM:
			serialcomm = g_variant_get_string(src->data, NULL);
			break;
		// case SR_CONF_FORCE_DETECT:
		// 	force_detect = g_variant_get_string(src->data, NULL);
		// 	sr_info("Force detect string %s", force_detect);
		// 	break;
		}
	}
	if (!conn)
		return NULL;

	if (!serialcomm)
		serialcomm = SERIALCOMM;

	serial = sr_serial_dev_inst_new(conn, serialcomm);
	sr_info("Opening %s.", conn);
	if ((serial_open(serial, SERIAL_RDWR) != SR_OK)) {
		sr_err("Failed to open serial probe");
		return NULL;
	}

   if (!reset_rp_device(serial)) {
		sr_err("Failed to reset serial probe");
		serial_close(serial);
		return NULL;
   }

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("PCH");
	sdi->model = g_strdup("RP-PCH");
	sdi->version = g_strdup("00");
	sdi->conn = serial;
	sdi->driver = &rp_pch_driver_info;
	sdi->inst_type = SR_INST_SERIAL;
	sdi->serial_num = g_strdup("N/A");


	devc = g_malloc0(sizeof(struct dev_context));
	devc->chan_mask = ((1 << CHANNEL_COUNT) - 1);


	for (i = 0; i < CHANNEL_COUNT; i++) {
		/* Name digital channels */
		channel_name = g_strdup_printf("D%d", i);
		sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE, channel_name);
		g_free(channel_name);
	}


	devc->cbuf_wrptr = 0;

	devc->d_data_buf = NULL;
	devc->sample_rate = 5000;
	devc->capture_ratio = 10;
	devc->rxstate = RX_IDLE;
	/*Set an initial value as various code relies on an inital value. */
	devc->limit_samples = 1000;

	sdi->priv = devc;

	serial_close(serial);
	return std_scan_complete(di, g_slist_append(NULL, sdi));
}


/* Note that on the initial driver load we pull all values into local storage.
 * Thus gets can return local data, but sets have to issue commands to device. */
static int config_set(uint32_t key, GVariant * data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	int ret;
	(void) cg;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;
	ret = SR_OK;

	sr_dbg("Got config_set key %d \n", key);
	switch (key) {
	case SR_CONF_SAMPLERATE:
		devc->sample_rate = g_variant_get_uint64(data);
		sr_dbg("config_set sr %" PRIu64 "\n", devc->sample_rate);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		sr_dbg("config_set slimit %" PRIu64 "\n", devc->limit_samples);
		break;
	case SR_CONF_CAPTURE_RATIO:
		devc->capture_ratio = g_variant_get_uint64(data);
		break;

	default:
		sr_err("ERROR: config_set given undefined key %d\n", key);
		ret = SR_ERR_NA;
	}

	return ret;
}


static int config_get(uint32_t key, GVariant ** data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	sr_dbg("config_get given key %d", key);

	(void) cg;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;
	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->sample_rate);
		sr_spew("sample rate get of %" PRIu64 "", devc->sample_rate);
		break;
	case SR_CONF_CAPTURE_RATIO:
		if (!sdi)
			return SR_ERR;
		devc = sdi->priv;
		*data = g_variant_new_uint64(devc->capture_ratio);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		sr_spew("config_get limit_samples of %" PRIu64, devc->limit_samples);
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	default:
		sr_spew("unsupported config_get key %d", key);
		return SR_ERR_NA;
	}
	return SR_OK;
}


static int config_list(uint32_t key, GVariant ** data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	(void) cg;

	/* Scan or device options are the only ones that can be called without a
	 * defined instance */
	if ((key == SR_CONF_SCAN_OPTIONS) || (key == SR_CONF_DEVICE_OPTIONS)) {
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	}

	if (!sdi) {
		sr_err("ERROR: Call to config list with null sdi");
		return SR_ERR_ARG;
	}

	sr_dbg("Start config_list with key %X", key);
	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates));
		break;
	/* This must be set to get SW trigger support */
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	case SR_CONF_LIMIT_SAMPLES:
		/* Really this limit is up to the memory capacity of the host,
		 * and users that pick huge values deserve what they get.
		 * But setting this limit to prevent really crazy things. */
		*data = std_gvar_tuple_u64(1LL, 0x1000000LL);
		break;
	default:
		sr_dbg("Reached default statement of config_list");

		return SR_ERR_NA;
	}

	return SR_OK;
}


static void copy_to_pretrigger(struct dev_context *devc, int len) {
	if (devc->pretrig_entries > 0) {
		if ((uint32_t)len >= devc->pretrig_entries) {
			//-- The buffer is more than sufficient to fill up the pretrigger
			devc->pretrig_filled = TRUE;
			devc->pretrig_wr_ptr = 0;
			memcpy(devc->pretrig_buf, devc->d_data_buf + (len - devc->pretrig_entries), devc->pretrig_entries);
		}
		else if (len + devc->pretrig_wr_ptr >= devc->pretrig_entries) {
			//-- Will cause the buffer to loop
			//-- Copy first the first end to the end of the buffer
			int first_part_len = devc->pretrig_entries - devc->pretrig_wr_ptr;
			devc->pretrig_filled = TRUE;
			memcpy(devc->pretrig_buf + devc->pretrig_wr_ptr, devc->d_data_buf, first_part_len);
			//-- Copy second part at beginning of the buffer
			devc->pretrig_wr_ptr = len - first_part_len;
			memcpy(devc->pretrig_buf, devc->d_data_buf + first_part_len, devc->pretrig_wr_ptr);
		}
		else {
			// Too few data to overflow the buffer
			// Simply copy the data
			memcpy(devc->pretrig_buf + devc->pretrig_wr_ptr, devc->d_data_buf, len);
			devc->pretrig_wr_ptr += len;
		}
	}
}



/* This callback function is mapped from api.c with serial_source_add and is
 * created after a capture has been setup and is responsible for querying the
 * device trigger status, downloading data and forwarding packets */
static int rp_pch_receive(int fd, int revents, void *cb_data) {
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	int len;
	int trigger_offset = 0;
	(void) fd;

	sdi = cb_data;
	if (!sdi)
		return TRUE;

	devc = sdi->priv;
	if (!devc)
		return TRUE;

	if (devc->rxstate != RX_ACTIVE) {
		/* This condition is normal operation and expected to happen
		 * but printed as information */
		sr_dbg("Reached non active state in receive %d", devc->rxstate);
		/* Don't return - we may be waiting for a final bytecnt */
	}

	if (devc->rxstate == RX_IDLE) {
		/* This is the normal end condition where we do one more receive
		 * to make sure we get the full byte_cnt */
		sr_dbg("Reached idle state in receive %d", devc->rxstate);
		return FALSE;
	}

	/* Return true if it is some kind of event we don't handle */
	if (!(revents == G_IO_IN || revents == 0))
		return TRUE;

	/* Fill the buffer */
	bufferstatus_t status = read_rp_data_block(sdi);
	switch (status) {
		case BUFFER_RECEIVED:
			//-- Get packet ready to send the buffer
			packet.type = SR_DF_LOGIC;
			packet.payload = &logic;
			/* The number of bytes required to fit all of the channels */
			logic.unitsize = SAMPLE_SIZE;

			if (!devc->trigger_fired) {
				//-- Not yet triggered
				//-- Check if trigger has happen in received data
				trigger_offset = check_trigger (devc, devc->d_data_buf, DATA_BLOCK_SIZE);
				//-- Round trigger to dword
				
				if (trigger_offset >= 0) {
					trigger_offset &= ROUND_MASK;

					//-- Trigger has been found within these samples
					copy_to_pretrigger(devc, trigger_offset);
					devc->trigger_fired = TRUE;

					//-- If any pretrig data send them
					if (devc->pretrig_filled || (devc->pretrig_wr_ptr > 0)) {
						//-- Send pre-trigger buffer
						if (devc->pretrig_filled) {
							devc->sent_samples = devc->pretrig_entries;
							if (devc->pretrig_wr_ptr > 0) {
								//-- Start first by the second part of the buffer since it has cycled
								logic.length = devc->pretrig_entries - devc->pretrig_wr_ptr;
								logic.data = devc->pretrig_buf + devc->pretrig_wr_ptr;
								sr_session_send(sdi, &packet);
							}
						}
						else {
							devc->sent_samples = devc->pretrig_wr_ptr;
						}
						//-- Send beginning of the pretrig buffer
						logic.length = (devc->pretrig_wr_ptr == 0) ? DATA_BLOCK_SIZE : devc->pretrig_wr_ptr;
						logic.data = devc->pretrig_buf;
						sr_session_send(sdi, &packet);
					}
				}
				else {
					copy_to_pretrigger(devc, DATA_BLOCK_SIZE);
				}
			}

			//-- Check again trigger status as it may have changed above
			if (devc->trigger_fired) {
				/* Update the count of data received with the new buffer */
				len = DATA_BLOCK_SIZE - trigger_offset;
				devc->sent_samples += len;
				if (devc->sent_samples > devc->limit_samples) {
					len -= (devc->sent_samples - devc->limit_samples);
					devc->sent_samples = devc->limit_samples;
					if (devc->rxstate == RX_ACTIVE) {
						return sr_dev_acquisition_stop(sdi) == SR_OK;
					}
				}
				if (len > 0) {
					/* The total length of the array sent */
					logic.length = len;
					logic.data = devc->d_data_buf + trigger_offset;
					sr_session_send(sdi, &packet);
				}
			}
			break;

		case ACQ_COMPLETED: 
			devc->rxstate = RX_IDLE;
			break;

		case INVALID_DATA:
			devc->rxstate = RX_ABORT;
			sdi->driver->dev_acquisition_stop(sdi);
			return FALSE;

		case PARTIAL_DATA:
			break;
	}

	return TRUE;
}


static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;
	struct dev_context *devc;
	struct sr_trigger *trigger;

	serial = sdi->conn;

	devc = sdi->priv;
	sr_dbg("Enter acq start");

	devc->d_data_buf = g_malloc(SERIAL_BUFFER_SIZE);
	if (!(devc->d_data_buf)) {
		sr_err("ERROR: serial buffer malloc fail");
		return SR_ERR_MALLOC;
	}

	/* Get device in idle state */
	if (! reset_rp_device(serial)) {
		sr_err("ERROR: failed to reset device");
		return SR_ERR_MALLOC;
	}


	if (! send_rp_sample_rate(sdi)) {
		sr_err("Failed to set sample rate");
		return SR_ERR;
	}

	devc->sent_samples = 0;
	devc->buffer_number = 0;
	devc->wrptr = 0;
	devc->cbuf_wrptr = 0;

	devc->d_data_buf = g_malloc(SERIAL_BUFFER_SIZE);
	if (!(devc->d_data_buf)) {
		sr_err("ERROR: logic buffer malloc fail");
		return SR_ERR_MALLOC;
	}

	devc->trigger_fired = FALSE;
	devc->pretrig_entries = (devc->capture_ratio * devc->limit_samples) / 100;
	devc->pretrig_entries += ROUND_COUNT - 1;
	devc->pretrig_entries &= ROUND_MASK;

	if (devc->pretrig_entries > 0) {
		devc->pretrig_buf = g_malloc(devc->pretrig_entries * SAMPLE_SIZE);
		devc->pretrig_wr_ptr = 0;
		devc->pretrig_filled = FALSE;
	}
	/* While the driver supports the passing of trigger info to the device
	 * it has been found that the sw overhead of supporting triggering and
	 * pretrigger buffer entries etc.. ends up slowing the cores down enough
	 * that the effective continous sample rate isn't much higher than that of
	 * sending untriggered samples across USB.  Thus this code will remain but
	 * likely may not be used by the device, unless HW based triggers are
	 * implemented */
	devc->mask_change = 0;   // Don't care
	devc->mask_state  = 0;   // Don't care
	devc->expect_state = 0;  // Default to 0
	trigger = sr_session_trigger_get(sdi->session);
	if (trigger) {
		if (g_slist_length(trigger->stages) > 1)
			return SR_ERR_NA;

		struct sr_trigger_stage *stage;
		struct sr_trigger_match *match;
		stage = g_slist_nth_data(trigger->stages, 0);
		if (!stage)
			return SR_ERR_ARG;

		for (GSList* l = stage->matches; l; l = l->next) {
			match = l->data;
			if (!match->match)
				continue;
			if (!match->channel->enabled)
				continue;
			int bit = 1 << match->channel->index;
			switch(match->match) {
			case SR_TRIGGER_ZERO:
				devc->mask_state   |= bit;
				break;
			case SR_TRIGGER_ONE:
				devc->mask_state   |= bit;
				devc->expect_state |= bit;
				break;
			case SR_TRIGGER_RISING:
				devc->mask_state   |= bit;
				devc->mask_change  |= bit;
				devc->expect_state |= bit;
				break;
			case SR_TRIGGER_FALLING:
				devc->mask_state   |= bit;
				devc->mask_change  |= bit;
				break;
			case SR_TRIGGER_EDGE:
				devc->mask_change  |= bit;
				break;
			default:
				break;
			}
		}
		sr_info("Trigger value State %02llx %02llx Change %02llx", devc->expect_state, devc->mask_state, devc->mask_change);

		devc->trigger_fired = FALSE;

		sr_info("Entering sw triggered mode");
		/* Post the receive before starting the device to ensure we are ready
		 * to receive data ASAP */
		serial_source_add(sdi->session, serial, G_IO_IN, 200, rp_pch_receive, (void*)sdi);

	} else {
		devc->trigger_fired = TRUE;
		devc->pretrig_entries = 0;
		sr_info("Entering fixed sample mode");
		serial_source_add(sdi->session, serial, G_IO_IN, 200, rp_pch_receive, (void*)sdi);
	}

	if (!send_rp_start_capture(sdi)) {
		return SR_ERR;
	}

	std_session_send_df_header(sdi);

	if (devc->trigger_fired)
		std_session_send_df_trigger(sdi);

	/* Keep this at the end as we don't want to be RX_ACTIVE unless everything
	 * is ok */
	devc->rxstate = RX_ACTIVE;

	return SR_OK;
}


#define BACK_ROL_COUNT  ((ROUND_COUNT-1) * CHAR_BIT)

// mask_state  : bit = 0 : don't care state
// mask_change : bit = 0 : don't care previous state
// expect_state : current state expected (ignored when mask_state = 0)
// state=1, change=0, expect=1 => cond: High
// state=1, change=0, expect=0 => cond: Low
// state=0, change=1, expect=x => cond: Change
// state=1, change=1, expect=1 => cond: Raising
// state=1, change=1, expect=0 => cond: Falling
// state=0, change=0, expect=x => cond: don't care
static int check_trigger (struct dev_context *sdc, const uint8_t *buf, int len) {
	const uint64_t * const batch_buf = (uint64_t *)buf;
	const int              batch_len = len / ROUND_COUNT;
	int       found       = -1;
	int       cur_byte    = 0;

	for (int i = 0; (i < batch_len) && (found < 0); ++i) {
#ifdef __BIG_ENDIAN__
		uint64_t prev_dword = buf[i];
		uint8_t  swap       = last_dword & 0xFF;
		last_dword &= ~0xFF;
		last_dword |= prev;
		prev = swap;
		uint64_t last_dword = (last_dword << CHAR_BIT) || (last_dword >> BACK_ROL_COUNT);
#else
		uint64_t cur_dword  = batch_buf[i];
		uint64_t prev_dword = (cur_dword << CHAR_BIT) || (cur_dword >> BACK_ROL_COUNT);
		uint8_t  swap       = prev_dword & 0xFF;
		prev_dword &= ~0xFF;
		prev_dword |= sdc->previous_sample;
		sdc->previous_sample = swap;
#endif
		uint64_t match = ((sdc->expect_state ^ cur_dword) & sdc->mask_state) | ((sdc->previous_sample ^ cur_dword) & sdc->mask_change);
		for (int j = 0; j < ROUND_COUNT; j++, cur_byte++) {
#ifdef __BIG_ENDIAN__
			match = (match << CHAR_BIT) | (match >> BACK_ROL_COUNT);
#endif
			if ((match & 0xFF) == 0) {
				found = cur_byte;
				break;
			}
#ifndef __BIG_ENDIAN__
			match >>= CHAR_BIT;
#endif
		}
	}
	return found;
}

/* This function is called either by the protocol code if we reached all of the
 * samples or an error condition, and also by the user clicking stop in
 * pulseview. It must always be called for any acquistion that was started to
 * free memory. */
static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int len;
	devc = sdi->priv;
	serial = sdi->conn;

	sr_dbg("At dev_acquisition_stop");

	std_session_send_df_end(sdi);

	if (devc->rxstate != RX_IDLE) {
		if (!send_rp_stop_capture(sdi)) {
			sr_err("Failed sending plus to stop device stream");
		}
	}
	devc->rxstate = RX_IDLE;

	/* Drain data from device so that it doesn't confuse subsequent commands */
	do {
		len = serial_read_blocking(serial, devc->d_data_buf, SERIAL_BUFFER_SIZE, 100);
		if (len)
			sr_err("Dropping %d device bytes", len);
	} while (len > 0);

	if (devc->d_data_buf) {
		g_free(devc->d_data_buf);
		devc->d_data_buf = NULL;
	}

	sr_serial_dev_inst_free(sdi->conn);
	
	sr_session_stop(sdi->session);

	serial_source_remove(sdi->session, serial);

	return SR_OK;
}

static struct sr_dev_driver rp_pch_driver_info = {
	.name = "rp-pch",
	.longname = "RP PCH",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = std_serial_dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};

SR_REGISTER_DEV_DRIVER(rp_pch_driver_info);
