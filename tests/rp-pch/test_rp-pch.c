#include <assert.h>
#include <glib.h>
#include <glib/gvariant.h>
#include "libsigrok-internal.h"
#include "../../src/hardware/rp-pch/protocol-rp.h"  // Unit under test


// gboolean reset_rp_device(struct sr_serial_dev_inst *serial);
// gboolean send_rp_start_capture(const struct sr_dev_inst *sdi);
// bufferstatus_t read_rp_data_block (struct sr_dev_inst *sdi);
// gboolean send_rp_stop_capture(struct sr_dev_inst *sdi, uint32_t block_limit);

sr_receive_data_callback sample_data_receive;

GSList *scan(struct sr_dev_driver *di, GSList * options);


struct sr_dev_inst *sdi = NULL;

static const char * const conn = "conn";
static const char * const serialcomm = "serial";
enum {
	TRIGGER_CHANNEL_COUNT = 5,
};
static const int triggers[TRIGGER_CHANNEL_COUNT] = { 
	SR_TRIGGER_EDGE,
	SR_TRIGGER_FALLING,
	SR_TRIGGER_RISING,
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
};
static struct sr_channel channels[CHANNEL_COUNT];
static struct sr_trigger_stage stage;
static struct sr_trigger_match matches[TRIGGER_CHANNEL_COUNT];


int main ( int argc, const char **argv) {
	(void)argv;
	(void)argc;
	struct sr_dev_driver di;
	printf("\nTest running...\n");
	GSList *scan_options = NULL;
	GVariant *limit_dword = NULL;
	struct sr_config config_conn, config_conn2;
	config_conn.key = SR_CONF_CONN;
	config_conn.data = g_variant_ref_sink(g_variant_new_string(conn));
	scan_options = g_slist_append(scan_options, &config_conn);
	config_conn2.key = SR_CONF_SERIALCOMM;
	config_conn2.data = g_variant_ref_sink(g_variant_new_string(serialcomm));
	scan_options = g_slist_append(scan_options, &config_conn2);

	GSList *scan_result = driver_info->scan(&di, scan_options);
	assert(scan_result == NULL);
	struct sr_serial_dev_inst *serial = sr_serial_dev_inst_new(conn, serialcomm);

	// Configure capture length = 80 samples (10 dwords)
	limit_dword = g_variant_ref_sink(g_variant_new_uint64(80));
	driver_info->config_set(SR_CONF_LIMIT_SAMPLES, limit_dword, sdi, NULL);
	g_variant_unref(limit_dword);

	for (int i = 0; i < CHANNEL_COUNT; ++i) {
		channels[i].index = i;
		channels[i].type = SR_CHANNEL_LOGIC;
		channels[i].enabled = TRUE;
	}


	serial_open(serial, SERIAL_RDWR);
	sdi->conn = serial;
	sdi->session = g_malloc(sizeof(struct sr_session));
	sdi->session->trigger = NULL;  // Null triggerring condition

	sr_dbg("\nSampling test #1");
	assert(driver_info->dev_acquisition_start(sdi) == SR_OK);
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // BoB #0 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // Block #0 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // BoB #1 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // Block #1 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // BoB #2 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // Block #2 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // AllDone received
	assert(driver_info->dev_acquisition_stop(sdi) == SR_OK);

	// Define the triggerring condition
	sdi->session->trigger = g_malloc(sizeof(struct sr_trigger));
	sdi->session->trigger->name = NULL;
	sdi->session->trigger->stages = NULL;
	stage.stage = 0;
	sdi->session->trigger->stages = g_slist_append(sdi->session->trigger->stages, &stage);
	stage.matches = NULL;
	for (int i = 0; i < TRIGGER_CHANNEL_COUNT; ++i) {
		matches[i].channel = channels+i;
		matches[i].match = triggers[i];
		stage.matches = g_slist_append(stage.matches, matches+i);
	}
	sr_dbg("\nSampling test #2");
	assert(driver_info->dev_acquisition_start(sdi) == SR_OK);
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // BoB #0 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // Block #0 part 1 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // Block #0 part 2 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // BoB #1 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // Block #1 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // BoB #2 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // Block #2 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // AllDone received
	assert(driver_info->dev_acquisition_stop(sdi) == SR_OK);

	// Configure 50% capture ratio - expects 5 dwords before trigger
	// (but trigger will still happen 2 dwords after start)
	limit_dword = g_variant_ref_sink(g_variant_new_uint64(50));
	driver_info->config_set(SR_CONF_CAPTURE_RATIO, limit_dword, sdi, NULL);
	g_variant_unref(limit_dword);
	
	sr_dbg("\nSampling test #3");
	assert(driver_info->dev_acquisition_start(sdi) == SR_OK);
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // BoB #0 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // Block #0 part 1 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // Block #0 part 2 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // BoB #1 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // Block #1 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // BoB #2 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // Block #2 part 1 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // Block #2 part 2 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // AllDone received
	assert(driver_info->dev_acquisition_stop(sdi) == SR_OK);

	sr_dbg("\nSampling test #4");
	assert(driver_info->dev_acquisition_start(sdi) == SR_OK);
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // BoB #0 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // Block #0 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // BoB #1 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // Block #1 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // BoB #2 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // Block #2 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // BoB #3 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // Block #3 received
	assert(sample_data_receive(0, 0, sdi) == TRUE);  // AllDone received
	assert(driver_info->dev_acquisition_stop(sdi) == SR_OK);

	sr_dbg("\nSampling test #5");
	assert(driver_info->dev_acquisition_start(sdi) == SR_OK);
	assert(sample_data_receive(0, 0, sdi) == FALSE);  // BoB error received
	assert(sample_data_receive(0, 0, sdi) == FALSE);  // Block error ignored
	assert(driver_info->dev_acquisition_stop(sdi) == SR_OK);

	check_next_event(COMPLETED, NULL, NULL, 0); // End of test
	return 0;
}

const char * const event_names[] = {
	"OPEN_SERIAL",
	"CLOSE_SERIAL",
	"WRITE_BLOCKING",
	"READ_BLOCKING",
	"SESSION_SEND",
	"SCAN_COMPLETED",
	"SESSION_STOP",
	"SEND_DF_HEADER",
	"SEND_DF_TRIGGER",
	"SEND_DF_END",
	"CHANNEL_NEW",
	"COMPLETED",
};

struct test_event_t {
	enum event_type_t event_type;
	const char *text;
	const uint8_t *data;
	int data_length;
};

struct test_event_t scenario[] = {
	{ OPEN_SERIAL, "conn/serial", NULL, 0 },
	{ WRITE_BLOCKING, NULL, (const uint8_t*)"*", 1 },
	{ WRITE_BLOCKING, NULL, (const uint8_t*)"B", 1 },
	{ WRITE_BLOCKING, NULL, (const uint8_t*)"*", 1 },
	{ WRITE_BLOCKING, NULL, (const uint8_t*)"C", 1 },
	{ CHANNEL_NEW, "D0", NULL, 0 },
	{ CHANNEL_NEW, "D1", NULL, 0 },
	{ CHANNEL_NEW, "D2", NULL, 0 },
	{ CHANNEL_NEW, "D3", NULL, 0 },
	{ CHANNEL_NEW, "D4", NULL, 0 },
	{ CHANNEL_NEW, "D5", NULL, 0 },
	{ CHANNEL_NEW, "D6", NULL, 0 },
	{ CHANNEL_NEW, "D7", NULL, 0 },
	{ CLOSE_SERIAL, "conn/serial", NULL, 0 },
	{ SCAN_COMPLETED, NULL, NULL, 0 },
	{ OPEN_SERIAL, "conn/serial", NULL, 0 },
	
	//-- First sampling with no trigger condition
	{ WRITE_BLOCKING, NULL, (const uint8_t*)"*", 1 },
	{ WRITE_BLOCKING, NULL, (const uint8_t*)"D", 1 },
	{ WRITE_BLOCKING, NULL, (const uint8_t*)"A>00001388", 10 },  // Start sampling 5000Hz
	{ SEND_DF_HEADER, NULL, NULL, 0 },
	{ WRITE_BLOCKING, NULL, (const uint8_t*)"|S00000003", 10 },   // Stop at block 3
	{ SEND_DF_TRIGGER, NULL, NULL, 0 },
	{ SESSION_SEND, NULL, (const uint8_t*)"ABCDEFGH" "IJKLMNOP" "QRSTUVWX" "YZ[\\]^_@", 32 },   // Block 1
	{ SESSION_SEND, NULL, (const uint8_t*)"@`BbDdFf" "AaCcEeGg" "HhJjLlNn" "IiKkMmOo", 32 },   // Block 2
	{ SESSION_SEND, NULL, (const uint8_t*)"@ABCDEFG" "HIJKLMNO", 16 }, // Half of block 3
	{ SESSION_STOP, NULL, NULL, 0 },  // End of session
	{ SEND_DF_END, NULL, NULL, 0 },  // Closing sampled data	

	//-- Second sampling with enought trigger data but pretrig less that block size
	{ WRITE_BLOCKING, NULL, (const uint8_t*)"*", 1 },
	{ WRITE_BLOCKING, NULL, (const uint8_t*)"E", 1 },
	{ WRITE_BLOCKING, NULL, (const uint8_t*)"A>00001388", 10 },  // Start sampling 5000Hz
	{ SEND_DF_HEADER, NULL, NULL, 0 },
	{ WRITE_BLOCKING, NULL, (const uint8_t*)"|S00000003", 10 },   // Stop at block 3
	{ SEND_DF_TRIGGER, NULL, NULL, 0 },
	{ SESSION_SEND, NULL, (const uint8_t*)"IJKLMNOP", 8 },   // Pre-trigger buffer
	{ SESSION_SEND, NULL, (const uint8_t*)"QRSTUVWX" "YZ[\\]^_@", 16 }, // Remaining of first block post trigger
	{ SESSION_SEND, NULL, (const uint8_t*)"@ABCDEFG" "HIJKLMNO" "PQRSTUVW" "XYZ[\\]^_", 32 }, // Bloc index 1
	{ SESSION_SEND, NULL, (const uint8_t*)"@`BbDdFf" "AaCcEeGg" "HhJjLlNn", 24 },  // Bloc index 2 that fit
	{ SESSION_STOP, NULL, NULL, 0 },  // End of session
	{ SEND_DF_END, NULL, NULL, 0 },  // Closing sampled data

	//-- Third sampling with too few trigger data
	{ WRITE_BLOCKING, NULL, (const uint8_t*)"*", 1 },
	{ WRITE_BLOCKING, NULL, (const uint8_t*)"F", 1 },
	{ WRITE_BLOCKING, NULL, (const uint8_t*)"A>00001388", 10 },  // Start sampling 5000Hz
	{ SEND_DF_HEADER, NULL, NULL, 0 },
	{ WRITE_BLOCKING, NULL, (const uint8_t*)"|S00000003", 10 },   // Stop at block 3
	{ SEND_DF_TRIGGER, NULL, NULL, 0 },
	{ SESSION_SEND, NULL, (const uint8_t*)"ABCDEFGH" "IJKLMNOP", 16 },   // Pre-trigger buffer
	{ SESSION_SEND, NULL, (const uint8_t*)"QRSTUVWX" "YZ[\\]^_@", 16 }, // Remaining of first block post trigger
	{ SESSION_SEND, NULL, (const uint8_t*)"@ABCDEFG" "HIJKLMNO" "PQRSTUVW" "XYZ[\\]^_", 32 }, // Bloc index 1
	{ SESSION_SEND, NULL, (const uint8_t*)"@`BbDdFf" "AaCcEeGg", 16 },  // Bloc index 2 that fit
	{ SESSION_STOP, NULL, NULL, 0 },  // End of session
	{ SEND_DF_END, NULL, NULL, 0 },  // Closing sampled data

	//-- Fourth sampling with enough trigger data but with buffer smaller than trigger data size
	{ WRITE_BLOCKING, NULL, (const uint8_t*)"*", 1 },
	{ WRITE_BLOCKING, NULL, (const uint8_t*)"G", 1 },
	{ WRITE_BLOCKING, NULL, (const uint8_t*)"A>00001388", 10 },  // Start sampling 5000Hz
	{ SEND_DF_HEADER, NULL, NULL, 0 },
	{ WRITE_BLOCKING, NULL, (const uint8_t*)"|S00000005", 10 },   // Stop at block 5
	{ SEND_DF_TRIGGER, NULL, NULL, 0 },
	{ SESSION_SEND, NULL, (const uint8_t*)"AaCcEeGg" "HhJjLlNn" "IiKkMmOo" "ABCDEFGH" "IJKLMNOP", 40 },   // Pre-trigger buffer 
	{ SESSION_SEND, NULL, (const uint8_t*)"QRSTUVWX" "YZ[\\]^_@", 16 }, // Remaining of first block post trigger
	{ SESSION_SEND, NULL, (const uint8_t*)"@ABCDEFG" "HIJKLMNO" "PQRSTUVW", 24 }, // Part of bloc 3 that fits
	{ SESSION_STOP, NULL, NULL, 0 },  // End of session
	{ SEND_DF_END, NULL, NULL, 0 },  // Closing sampled data	
	
	//-- Fifth sampling with trigger but but responded with error
	{ WRITE_BLOCKING, NULL, (const uint8_t*)"*", 1 },
	{ WRITE_BLOCKING, NULL, (const uint8_t*)"G", 1 },
	{ WRITE_BLOCKING, NULL, (const uint8_t*)"A>00001388", 10 },  // Start sampling 5000Hz
	{ SEND_DF_HEADER, NULL, NULL, 0 },
	{ SEND_DF_END, NULL, NULL, 0 },  // Closing sampled data	
};


//-- Null char may be included in string by prefixing string with a null char and a null count char
//-- "\0\0" = 3 null chars (only 2 explicit) => empty string
//-- "\0\1\0" = 1 null char to ignore and that is the only character of the string (the string contains only a null char)
//-- "\0\2a\0b\0c"  ==> 5 characters {'a', null, 'b', null, 'c'} including 2 ignored null char
const char *received_data_scenario [] = {
	"#*PCHv1.x",   // Garbage character before announce, incorrect announce no replay char (x)
	"\0\0",        // No more data will force sending a second announce
	"*PCHv1.C",    // Announce should match
	"*PCH",
	"v1.D",        // Announce should match
	"\0\5BoB\0" "\0\0\0\0",  // Marker of block 0
	"ABCDEFGH" "IJKLMNOP" "QRSTUVWX" "YZ[\\]^_@",   // Content of Block 0 unfragmented
	"\0\4BoB\0" "\1\0\0\0",  // Marker of block 1 (little endian)
	"@`BbDdFf" "AaCcEeGg" "HhJjLlNn" "IiKkMmOo",  // Unfragmented block 1
	"\0\4BoB\0" "\2\0\0\0",  // Marker of block 2
	"@ABCDEFG" "HIJKLMNO" "PQRSTUVW" "XYZ[\\]^_",  // Unfragmented block 2
	"\0\1AllDone\0",
	"\0\0",        // No more data probe has stop sending

	//-- Second sampling with immediately too much trigger data
	"*PCHv1.E",        // Announce should match
	"\0\5BoB\0" "\0\0\0\0",  // Marker of block 0
	"ABCDEFGH" "IJKLMNOP",   // Content of Block 0 fragmented
	"QRSTUVWX" "YZ[\\]^_@",  // End of content of Block 0
	"\0\4BoB\0" "\1\0\0\0",  // Marker of block 1 (little endian)
	"@ABCDEFG" "HIJKLMNO" "PQRSTUVW" "XYZ[\\]^_",  // Unfragmented block 1
	"\0\4BoB\0" "\2\0\0\0",  // Marker of block 2
	"@`BbDdFf" "AaCcEeGg" "HhJjLlNn" "IiKkMmOo",
	"\0\1AllDone\0",
	"\0\0",        // No more data probe has stop sending

	//-- Third sampling with too few trigger data
	"*PCHv1.F",        // Announce should match
	"\0\5BoB\0" "\0\0\0\0",  // Marker of block 0
	"ABCDEFGH" "IJKLMNOP",   // Content of Block 0 fragmented
	"\0\4" "QRSTUVWX" "YZ[\\]^_@" "BoB\0" "\1\0\0\0",  // End of content of Block 0 + Marker of block 1 (little endian)
	"@ABCDEFG" "HIJKLMNO" "PQRSTUVW" "XYZ[\\]^_",  // Unfragmented block 1
	"\0\4" "BoB\0" "\2\0\0\0" "@`BbDdFf" "AaCcEeGg",  // Marker of block 2 + beginning of block 2
	"\0\1" "HhJjLlNn" "IiKkMmOo" "AllDone\0",
	"\0\0",        // No more data probe has stop sending
	
	//-- Fourth sampling with enough trigger data but with buffer smaller than trigger data size
	"*PCHv1.G",        // Announce should match
	"\0\5BoB\0" "\0\0\0\0",  // Marker of block 0
	"PpRrTtVv" "XxZz\\|^~" "QqSsUuWw" "Yy[{]}_\x7F",  // Not triggering block
	"\0\4BoB\0" "\1\0\0\0",  // Marker of block 1
	"@`BbDdFf" "AaCcEeGg" "HhJjLlNn" "IiKkMmOo",  // Again not triggering block
	"\0\4BoB\0" "\2\0\0\0",  // Marker of block 2
	"@ABCDEFG" "HIJKLMNO" "PQRSTUVW" "XYZ[\\]^_",  // Trigerring block
	"\0\4BoB\0" "\3\0\0\0",  // Marker of block 3
	"@ABCDEFG" "HIJKLMNO" "PQRSTUVW" "XYZ[\\]^_",  // data block 3
	"\0\1AllDone\0",
	"\0\0",        // No more data probe has stop sending

	//-- Fifth sampling with trigger data but error returned immediately
	"*PCHv1.H",        // Announce should match
	"\0\1BoB\0" "\xFF\xFF\xFF\xFF",  // Error block 0
	"PpRrTtVv" "XxZz\\|^~" "QqSsUuWw" "Yy[{]}_\x7F",  // Random data
	"\0\0",        // No more data


	// "\0\4BoB\0" "\3\0\0\0",  // Marker of block 3
	// "PpRrTtVv" "XxZz\\|^~" "QqSsUuWw" "Yy[{]}_\x7F",
};
const int DATA_SCENARIO_LENGTH = sizeof(received_data_scenario) / sizeof(uint32_t*);


#define SCENARIO_LENGTH (sizeof(scenario)/sizeof(struct test_event_t))

#define MAX_DATA_LENGTH 32

const char * get_step_event(int step) {
	return event_names[scenario[step].event_type];
}

const char * data_image(const uint8_t *data, int data_length, char *buffer) {
	if (data_length > MAX_DATA_LENGTH) {
		data_length = MAX_DATA_LENGTH;
	}
	buffer[0] = '\0';
	for (int i = 0; i < data_length; ++i) {
		snprintf(buffer+(i*2), 3, "%02X", data[i]);
	}
	return buffer;
}

int serial_source_add(struct sr_session *session, struct sr_serial_dev_inst *serial, int events, int timeout, sr_receive_data_callback cb, void *cb_data) {
	sample_data_receive = cb;
	return SR_OK;
}


static char data_text1[MAX_DATA_LENGTH*2+1];
static char data_text2[MAX_DATA_LENGTH*2+1];

int result_step = 0;

void check_next_event(enum event_type_t event_type, const char *text, const uint8_t *data, int length) {
	int status = 1;
	if (result_step >= SCENARIO_LENGTH) {
		if (event_type == COMPLETED) {
			// Completed OK
			printf("Test Completed: %lld tests OK\n", SCENARIO_LENGTH);
			status = 0;
		}
		else {
			printf("Error: Test still running after end has been reached\n");
		}
	}
	else if (scenario[result_step].event_type == DONT_CARE) {
		status = -1; // What ever event reveived, this is OK
	}
	else if (event_type != scenario[result_step].event_type) {
		// printf("Wrong event %s at step %ld: was expecting %s\n", event_names[event_type], step, get_step_event(step));
		printf("Wrong event %s at step %d: was expecting %s\n", event_names[event_type], result_step, get_step_event(result_step));
	}
	else if ((text == NULL) != (scenario[result_step].text == NULL)) {
		if (text == NULL) {
			printf("At step %d, event %s expects text '%s' but no string has been provided\n", result_step, get_step_event(result_step), scenario[result_step].text);
		}
		else {
			printf("At step %d, event %s expects no text but some string has been passed\n", result_step, get_step_event(result_step));
		}
	}
	else if ((text != NULL) && (strcmp(text, scenario[result_step].text) != 0)) {
		printf("At step %d, event %s expects text '%s' but got '%s'\n", result_step, get_step_event(result_step), scenario[result_step].text, text);
	}
	else if ((data == NULL) != (scenario[result_step].data == NULL)) {
		if (data == NULL) {
			printf("At step %d, event %s expects data %s but no data has been provided\n", result_step, get_step_event(result_step), data_image(scenario[result_step].data, scenario[result_step].data_length, data_text1));
		}
		else {
			printf("At step %d, event %s expects no data but some data has been passed\n", result_step, get_step_event(result_step));
		}
	}
	else if (length != scenario[result_step].data_length) {
		printf("At step %d, event %s received wrong data length: %d bytes expected got %d, \n", result_step, get_step_event(result_step), scenario[result_step].data_length, length);
		if (data != NULL) {
			printf("Expected data: '%s', got: '%s'\n", data_image(scenario[result_step].data, scenario[result_step].data_length, data_text1), data_image(data, length, data_text2));
		}
	}
	else {
		status = -1; // Check success
		sr_dbg("%s OK", event_names[event_type]);
		++result_step;
	}
	if (status >= 0) {
		exit(status);
	}
}


