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
#include <assert.h>
// #include <time.h>
// #include <unistd.h>
#ifndef UNIT_TEST
#include <libsigrok/libsigrok.h>
#endif
#include "libsigrok-internal.h"
#include "protocol-rp.h"


//==================================================================
// Protocol with the probe
//==================================================================
// Reset:
//   Host: '*'
//   Probe: Interrupt doing anything and: '*PCHv1.' + non-null byte for no replay
//
// Start sampling:
//   Host: 'A>' Hz-rate-hexa-8-digits
//   Probe: 'BoB'<null> <Block-number-low-byte> <Block-number-byte2> <Block-number-byte3> <block-number-high-byte> repeat 4096 * (8-bit-data)
//
// Stop sampling:
//   Host: '|S' <last-bock-id-unsigned-hexa-8-digits>
//   Probe: 'AllDone'<null> only after last BoB have been sent

static const uint8_t * const RESET_ANNOUNCE    = (const uint8_t *)"*PCHv1.";  //-- followed by no replay random uint8_t
static const uint8_t * const STOP_SAMP_ACK     = (const uint8_t *)"AllDone";  //-- including ending null char
static const uint8_t * const BOB_MARKER        = (const uint8_t *)"BoB";      //-- including ending null char & followed by 32-bit buffer number in little endian

static const unsigned        CMD_LENGTH        = 2;
static const char *    const START_SAMP_CMD    = "A>";                        //-- followed by rate indicated in hertz by 8 following hexa characters (up to 4GHz+)
static const char *    const STOP_SAMP_CMD     = "|S";                        //-- followed by 8 char hex to specify the number of the block after last one to send


static uint8_t  noreplay_value = 'A';

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

// /* Issue a command that expects a string return that is equal to cnt characters maximum.
//  * Returns the actual length of string */
// static int send_serial_w_resp(struct sr_serial_dev_inst *serial, const char *str, uint8_t *resp, size_t cnt)
// {
//     int num_read, i;
//     send_serial_str(serial, str);

//     /* Using the serial_read_blocking function when reading a response of
//      * unknown length requires a long worst case timeout to always be taken.
//      * So, instead loop waiting for a first byte, and then a final small delay
//      * for the rest. */
//     for (i = 0; i < 1000; i++) {	/* wait up to 1 second in ms increments */
//         num_read = serial_read_blocking(serial, resp, cnt, 1);
//         if (num_read > 0)
//             break;
//     }

//     /* Since the serial port is USB CDC we can't calculate timeouts based on
//      * baud rate but even if the response is split between two USB transfers,
//      * 10ms should be plenty. */
//     num_read += serial_read_blocking(serial, resp + num_read, cnt - num_read,
//         10);
//     if (num_read < 1) {
//         sr_err("ERROR: Serial_w_resp failed (%d).", num_read);
//         return -1;
//     } else
//         return num_read;
// }


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
    int len;
    unsigned keep = 0;
    uint8_t  buf[MARKER_SIZE];
    gboolean result = FALSE;
    int trials = 0;
    do {
        // Send Reset command
        noreplay_value++; // Change the value each time it is sent so response to one reset can be paired only with right query
        send_serial_char(serial, '*');
        send_serial_char(serial, noreplay_value);
        g_usleep(10000);
        for(;;) {
            sr_warn("Drain reads");
            // Waiting for corresponding RESET_ANNOUNCE
            // Request to fill buffer. 100ms should be sufficient
            len = serial_read_blocking(serial, buf + keep, MARKER_SIZE - keep, 100);
            if (len)
            {
                // We got a response from probe
                len += keep;
                if (len == MARKER_SIZE) {
                    // And length is sufficient to contain the announce
                    // So check whether it start with the first announce character
                    uint8_t *match_start = memchr(buf, *RESET_ANNOUNCE, MARKER_SIZE);
                    if (match_start) {
                        // OK we have found the start of the announce
                        if (match_start == buf) {
                            // Good, it is actually at the start of the received buffer
                            if ((memcmp(RESET_ANNOUNCE, buf, MARKER_SIZE-1) == 0) && (buf[MARKER_SIZE-1] == noreplay_value)) {
                                // Bingo, the full annouce matches, as well as the noreply byte
                                result = TRUE; // Reset is OK
                                break;         // We are done
                            }
                            // Buffer does not match, drop first announce character but keep rest of buffer
                            // (the expected announce may start somewhere inside the remaining bytes)
                            keep = (MARKER_SIZE - 1);
                            ++match_start;
                        }
                        else {
                            // Ooops. Start of announce is in the middle of the buffer
                            // Keep only data from the announce start
                            keep = (MARKER_SIZE - 1) - (match_start - buf) + 1;
                        }
                        assert(keep > 0); 
                        // A part of the buffer needs to be kept. 
                        // Move it at beginning of the buffer
                        memmove(buf, match_start, keep);
                    }
                    else {
                        // The announce does not start in this buffer
                        // So drop it completly
                        keep = 0;
                    }
                    sr_dbg("Dropping %d serial data bytes", MARKER_SIZE-keep);
                }
                else {
                    // The buffer is not completly filled
                    // so keep it entirely to get remaining bytes
                    keep = len;
                }
            }
            else {
                // Nothing has been read in 100ms
                // The probe seems not responding
                // Just in case it would not have understood the reset
                // Try to send it again
                break;
            }
        };
        // This trial is completed (successfully or not)
        ++trials;
    }
    // Retry a new time if we have not been successfull or if retry limit has been reached
    while (!result && (trials < 5));
    sr_warn("Drain reads done");
    return result;
}




static const char *COMMAND_FORMAT = "%s%08X";
static const int   FULL_COMMAND_LENGTH = CMD_LENGTH+8+1; // The command length + length of 8 hex digit of 32bit value  + null Terminator

/**
 * @brief Send command
 * 
 * @param sdi The device instance data
 * @param command The 2 character command
 * @param value the command 32bit parameter value
 * 
 * @return SR_PRIV 
 */
static gboolean send_rp_command(const struct sr_dev_inst *sdi, const char *command, int32_t value) {
    char tmpstr[FULL_COMMAND_LENGTH];

    snprintf(tmpstr, FULL_COMMAND_LENGTH, COMMAND_FORMAT, command, value);
    if (send_serial_str(sdi->conn, tmpstr) != SR_OK) {
        sr_err("Failed to send probe %s acquisition command", tmpstr);
        return FALSE;
    }

    return TRUE;
}


/**
 * @brief Send sample start command
 * 
 * @param sdi 
 * @return SR_PRIV 
 */
gboolean send_rp_start_capture(const struct sr_dev_inst *sdi) {
    struct dev_context * const devc = sdi->priv;
    return send_rp_command(sdi, START_SAMP_CMD, devc->sample_rate);
}


bufferstatus_t read_rp_data_block (const struct sr_dev_inst *sdi) {
    bufferstatus_t result = INVALID_DATA;

    struct dev_context * const devc = sdi->priv;

    /* Fill the buffer with a marker or a data block */
    uint32_t buffer_size = (devc->data_expected) ? DATA_BLOCK_SIZE : MARKER_SIZE;
    //  The buffer may contain partial slice
    int bytes_rem = buffer_size - devc->wrptr;

    devc->wrptr += serial_read_blocking(sdi->conn, devc->d_data_buf + devc->wrptr, bytes_rem, 10);

    if (devc->wrptr < buffer_size) {
        result = PARTIAL_DATA;
    }

    else {
        devc->wrptr = 0;  // Buffer is full get ready for next data

        if (devc->data_expected) {
            devc->data_expected = FALSE;
            sr_err("buffer %08x received", devc->buffer_number);
            devc->buffer_number++;
            result = BUFFER_RECEIVED;
        }
        
        else if (memcmp(devc->d_data_buf, BOB_MARKER, BOB_LENGTH) == 0) {
            uint32_t buf_number;
            //-- Buffer number is sent as little endian by probe
            buf_number = (devc->d_data_buf[BOB_LENGTH+3] << 24) + 
                            (devc->d_data_buf[BOB_LENGTH+2] << 16) + 
                            (devc->d_data_buf[BOB_LENGTH+1] << 8) + 
                            devc->d_data_buf[BOB_LENGTH];
            if (buf_number != devc->buffer_number) {
                sr_err("ERROR: buffer number is not correct: found=%08x, expected=%08x", buf_number, devc->buffer_number);
            }
            else {
                // The data block header is correct. Data is now expected.
                devc->data_expected = TRUE;
                result = PARTIAL_DATA;
            }
        }
        else if (memcmp(devc->d_data_buf, STOP_SAMP_ACK, MARKER_SIZE) == 0) {
            result = ACQ_COMPLETED;
        }
    }
    return result;
}


gboolean send_rp_stop_capture(const struct sr_dev_inst *sdi, uint32_t block_limit) {
    gboolean result = send_rp_command(sdi, STOP_SAMP_CMD, block_limit);
    if (! result) {
        sr_err("Failed sending stop device stream");
    }
    else {
        std_session_send_df_trigger(sdi);
    }
    return result;
}

