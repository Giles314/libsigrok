#ifndef STUB_INTERNAL
#define STUB_INTERNAL

//  Test Stub for libsigrok-internal.h
// Stub implementation is in test_rp-pch.c
//---------------------------------------

//#include <stddef.h>
#include <glib.h>
#include <stdint.h>
#include <libsigrok/libsigrok.h>

// Stub for libsigrok_internal.h

void printfln(const char *format, ...);

#define sr_err printfln
#define sr_warn printfln
#define sr_info printfln
#define sr_dbg printfln
#define sr_spew printfln


enum event_type_t {
	OPEN_SERIAL,
	CLOSE_SERIAL,
	WRITE_BLOCKING,
	READ_BLOCKING,
	SESSION_SEND,
	SCAN_COMPLETED,
	SESSION_STOP,
	SEND_DF_HEADER,
	SEND_DF_TRIGGER,
	SEND_DF_END,
	CHANNEL_NEW,
	COMPLETED,
	DONT_CARE,
};

extern struct sr_dev_inst *sdi;
extern const char *received_data_scenario[];
extern const int DATA_SCENARIO_LENGTH;
extern int result_step;



void check_next_event(enum event_type_t event_type, const char *text, const uint8_t *data, int length);

struct sr_context {
	void *resource_cb_data;
};
struct drv_context {
	/** sigrok context */
	struct sr_context *sr_ctx;
};

/** Device instance data */
struct sr_dev_inst {
	/** Device driver. */
	struct sr_dev_driver *driver;
	/** Device instance status. SR_ST_NOT_FOUND, etc. */
	int status;
	/** Device instance type. SR_INST_USB, etc. */
	int inst_type;
	/** Device vendor. */
	char *vendor;
	/** Device model. */
	char *model;
	/** Device version. */
	char *version;
	/** Serial number. */
	char *serial_num;
	/** Connection string to uniquely identify devices. */
	char *connection_id;
	// /** List of channels. */
	// GSList *channels;
	// /** List of sr_channel_group structs */
	// GSList *channel_groups;
	/** Device instance connection data (used?) */
	void *conn;
	/** Device instance private data (used?) */
	void *priv;
	/** Session to which this device is currently assigned. */
	struct sr_session *session;
};

struct sr_serial_dev_inst {
	/** Port name, e.g. '/dev/tty42'. */
	char *port;
	/** Comm params for serial_set_paramstr(). */
	char *serialcomm;
	// struct ser_lib_functions *lib_funcs;
	// struct {
	// 	int bit_rate;
	// 	int data_bits;
	// 	int parity_bits;
	// 	int stop_bits;
	// } comm_params;
	// GString *rcv_buffer;
	// void *rx_chunk_cb_data;
	// struct sp_port *sp_data;
	// struct sr_tcp_dev_inst *tcp_dev;
	void *rx_chunk_cb_data;  // Used to track the port status: NULL=closed else open 
};

struct sr_session {
	/** Context this session exists in. */
	struct sr_context *ctx;
	// /** List of struct sr_dev_inst pointers. */
	// GSList *devs;
	// /** List of struct sr_dev_inst pointers owned by this session. */
	// GSList *owned_devs;
	// /** List of struct datafeed_callback pointers. */
	// GSList *datafeed_callbacks;
	// GSList *transforms;
	struct sr_trigger *trigger;

	// /** Callback to invoke on session stop. */
	// sr_session_stopped_callback stopped_callback;
	// /** User data to be passed to the session stop callback. */
	// void *stopped_cb_data;

	// /** Mutex protecting the main context pointer. */
	// GMutex main_mutex;
	// /** Context of the session main loop. */
	// GMainContext *main_context;

	// /** Registered event sources for this session. */
	// GHashTable *event_sources;
	// /** Session main loop. */
	// GMainLoop *main_loop;
	// /** ID of idle source for dispatching the session stop notification. */
	// unsigned int stop_check_id;
	/** Whether the session has been started. */
	gboolean running;
};


enum {
	SERIAL_RDWR = 1,
	SERIAL_RDONLY = 2,
};

/** Scan options supported by a driver. */
#define SR_CONF_SCAN_OPTIONS 0x7FFF0000

/** Device options for a particular device. */
#define SR_CONF_DEVICE_OPTIONS 0x7FFF0001

#define STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts) ({assert(key == SR_CONF_SCAN_OPTIONS); \
										assert(scanopts!=NULL); assert(drvopts!=NULL); assert(devopts!=NULL); 0;})
#define std_gvar_samplerates(x) ((GVariant*)&(x))
#define std_gvar_array_i32(x) ((GVariant*)&(x))
#define std_gvar_tuple_u64(x,y) ({int64_t *z = g_malloc(sizeof(int64_t));*z=y;(GVariant*)z;})
#define ARRAY_AND_SIZE(x) (x)
#define SR_REGISTER_DEV_DRIVER(x)  struct sr_dev_driver *driver_info = &x
extern struct sr_dev_driver *driver_info;


typedef int (*sr_receive_data_callback)(int fd, int revents, void *cb_data);
extern sr_receive_data_callback sample_data_receive;


struct sr_serial_dev_inst *sr_serial_dev_inst_new(const char *port, const char *serialcomm);

int serial_open(struct sr_serial_dev_inst *serial, int flags);

int serial_close(struct sr_serial_dev_inst *serial);
int serial_flush(struct sr_serial_dev_inst *serial);
int serial_drain(struct sr_serial_dev_inst *serial);
size_t serial_has_receive_data(struct sr_serial_dev_inst *serial);

int serial_write_blocking(struct sr_serial_dev_inst *serial, const void *buf, int count, unsigned int timeout_ms);

int serial_read_blocking(struct sr_serial_dev_inst *serial, void *buf, int count, unsigned int timeout_ms);

struct sr_channel *sr_channel_new(struct sr_dev_inst *sdi, int index, int type, gboolean enabled, const char *name);

int sr_session_send(const struct sr_dev_inst *sdi, const struct sr_datafeed_packet *packet);

typedef int (*sr_receive_data_callback)(int fd, int revents, void *cb_data);
int serial_source_add(struct sr_session *session, struct sr_serial_dev_inst *serial, int events, int timeout, sr_receive_data_callback cb, void *cb_data);


int std_init(struct sr_dev_driver *di, struct sr_context *sr_ctx);
int std_cleanup(const struct sr_dev_driver *di);
int std_session_send_df_header(const struct sr_dev_inst *sdi);
int std_session_send_df_trigger(const struct sr_dev_inst *sdi);
int std_session_send_df_end(const struct sr_dev_inst *sdi);

int std_dev_clear(const struct sr_dev_driver *driver);
GSList *std_dev_list(const struct sr_dev_driver *di);
int std_serial_dev_open(struct sr_dev_inst *sdi);
int std_serial_dev_close(struct sr_dev_inst *sdi);
GSList *std_scan_complete(struct sr_dev_driver *di, GSList *devices);

#endif // STUB_INTERNAL