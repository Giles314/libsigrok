#include "../../src/hardware/rp-pch/protocol-rp.h"  // Unit under test
#include <assert.h>
#include "libsigrok-internal.h"


struct sr_serial_dev_inst *sr_serial_dev_inst_new(const char *port, const char *serialcomm) {
	struct sr_serial_dev_inst *result = g_malloc(sizeof(struct sr_serial_dev_inst));
	result->port = g_strdup(port);
	result->serialcomm = (serialcomm) ? g_strdup(serialcomm) : NULL;
	result->rx_chunk_cb_data = NULL;
	return result;
}

static int serial_check(struct sr_serial_dev_inst *serial, enum event_type_t event) {
	int part1 = strlen(serial->port);
	int length = part1+strlen(serial->serialcomm)+2;
	int status = (length < 32) ? SR_OK : SR_ERR;
	if (status == SR_OK) {
		char *concat = g_malloc(length);
		strcpy(concat, serial->port);
		concat[part1] = '/';
		strcpy(concat+part1+1, serial->serialcomm);
		check_next_event(event, concat, NULL, 0);
		g_free(concat);
		serial->rx_chunk_cb_data = serial;
	}
	return status;
}

int serial_open(struct sr_serial_dev_inst *serial, int flags) {
	assert(serial->rx_chunk_cb_data == NULL);
	int result = serial_check(serial, OPEN_SERIAL);
	if (result == SR_OK) {
		serial->rx_chunk_cb_data = serial;
	}
	return result;
}

int serial_close(struct sr_serial_dev_inst *serial) {
	assert(serial->rx_chunk_cb_data != NULL);
	int result = serial_check(serial, CLOSE_SERIAL);
	if (result == SR_OK) {
		serial->rx_chunk_cb_data = NULL;
	}
	return result;
}

int serial_flush(struct sr_serial_dev_inst *serial) {
	assert(serial->rx_chunk_cb_data != NULL);
	int result = serial_check(serial, CLOSE_SERIAL);
	return result;
}

int serial_write_blocking(struct sr_serial_dev_inst *serial, const void *buf, int count, unsigned int timeout_ms) {
	assert(serial->rx_chunk_cb_data != NULL);
	int result = count;
	if (count > 32) {
		result = 32;
	}
	check_next_event(WRITE_BLOCKING, NULL, buf, result);
	return result;
}

static int scenario_cursor = 0;
static int remaining = 0;

int serial_read_blocking(struct sr_serial_dev_inst *serial, void *buf, int count, unsigned int timeout_ms) {
	static const char* at = NULL;

	assert(serial->rx_chunk_cb_data != NULL);
	if (scenario_cursor >= DATA_SCENARIO_LENGTH) {
		printf("Scenario data exhausted before end of test\n");
		exit(1);
	}
	if (remaining == 0) {
		// Load data of next scenario step
		at = received_data_scenario[scenario_cursor];
		uint8_t ignored_null_count = 0;
		//-- Compute length of data to send
		remaining = strlen(at);
		if (remaining == 0) {
			++at;
			ignored_null_count = *at;  // Next char indicates the number of null char to include in string 
			++at;
			assert(ignored_null_count < 10);  // It is assumed we don't need to include more than 9 null chars in string
			for (int i = 0; ; ++i) {
				remaining += strlen(at + remaining); // add count of characters after the ignored null
				if (i >= ignored_null_count) {
					break;
				}
				remaining ++;  // add one null to ignore
			}
		}
	}
	int result = remaining;  // Send all
	remaining = result - count;   // How many bytes remaining if we send maximum bytes
	if (remaining > 0) {
		// It will remain some byte so don't send all
		result = count;
	}
	else {
		// Avoid to keep negative number of remaining bytes
		remaining = 0;
	}
	memcpy(buf, at, result);
	if (remaining == 0) {
		// All data from this step has been sent, go to next step
		scenario_cursor++;
	}
	else {
		at += result;
	}
	return result;
}

size_t serial_has_receive_data(struct sr_serial_dev_inst *serial) {
	assert(serial->rx_chunk_cb_data != NULL);
	assert(FALSE);
}

int serial_drain(struct sr_serial_dev_inst *serial) {
	assert(serial->rx_chunk_cb_data != NULL);
	return SR_OK;
}



struct sr_channel *sr_channel_new(struct sr_dev_inst *sdi, int index, int type, gboolean enabled, const char *name) {
	check_next_event(CHANNEL_NEW, name, NULL, 0);
	return NULL;
}

int sr_session_send(const struct sr_dev_inst *sdi, const struct sr_datafeed_packet *packet) {
	assert(packet->type==SR_DF_LOGIC);
	struct sr_datafeed_logic const *logic = packet->payload;
	check_next_event(SESSION_SEND, NULL, logic->data, logic->length);
	return SR_OK;
}

int std_init(struct sr_dev_driver *di, struct sr_context *sr_ctx) {
	assert(di);
	// struct drv_context *drvc;
	// drvc = g_malloc0(sizeof(struct drv_context));
	// drvc->sr_ctx = sr_ctx;
	// di->context = drvc;
	return SR_OK;
}

int sr_session_stop(struct sr_session *session) {
	check_next_event(SESSION_STOP, NULL, NULL, 0);
	return SR_OK;
}

struct sr_trigger *sr_session_trigger_get(struct sr_session *session) {
	return session->trigger;
}


int std_session_send_df_header(const struct sr_dev_inst *sdi) {
	assert(sdi);
	check_next_event(SEND_DF_HEADER, NULL, NULL, 0);
	return SR_OK;
}
int std_session_send_df_trigger(const struct sr_dev_inst *sdi){
	assert(sdi);
	check_next_event(SEND_DF_TRIGGER, NULL, NULL, 0);
	return SR_OK;
}
int std_session_send_df_end(const struct sr_dev_inst *sdi){
	assert(sdi);
	check_next_event(SEND_DF_END, NULL, NULL, 0);
	return SR_OK;	
}

GSList *std_scan_complete(struct sr_dev_driver *di, GSList *devices) {
	sdi = (struct sr_dev_inst*)devices->data;
	check_next_event(SCAN_COMPLETED, NULL, NULL, 0);
	return NULL;
}

int std_dev_clear(const struct sr_dev_driver *driver) {
	assert(FALSE); //should not be called because this is only called by sigrok
	return SR_OK;
}
GSList *std_dev_list(const struct sr_dev_driver *di) {
	assert(FALSE); //should not be called because this is only called by sigrok
	return NULL;
}
int std_cleanup(const struct sr_dev_driver *di){
	assert(FALSE); //should not be called because this is only called by sigrok
	return SR_OK;
}
int std_serial_dev_open(struct sr_dev_inst *sdi){
	assert(FALSE); //should not be called because this is only called by sigrok
	return SR_OK;
}
int std_serial_dev_close(struct sr_dev_inst *sdi) {
	assert(FALSE); //should not be called because this is only called by sigrok
	return SR_OK;
}

void printfln(const char *format, ...){
    va_list args;
    va_start(args, format);
	vprintf(format, args);
	va_end(args);
	printf("\n");
}

