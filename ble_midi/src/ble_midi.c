#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <ble_midi/ble_midi.h>
#include "ble_midi_packet.h"
#include "ble_midi_context.h"
#include "conn_event_trigger.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ble_midi, CONFIG_BLE_MIDI_LOG_LEVEL);

#ifndef CONFIG_BLE_MIDI_TX_MODE_SINGLE_MSG
#include <zephyr/sys/ring_buffer.h>
#endif /* !CONFIG_BLE_MIDI_TX_MODE_SINGLE_MSG */

static uint16_t timestamp_ms()
{
	return k_ticks_to_ms_near64(k_uptime_ticks()) & 0x1FFF;
}

struct ble_midi_context context;

int send_packet(uint8_t *bytes, int num_bytes);

void on_ready_state_changed(ble_midi_ready_state_t state) {
	LOG_INF("ready state: %d", state);
	if (context.user_callbacks.ready_cb) {
		context.user_callbacks.ready_cb(state);
	}
}

/************* BLE SERVICE CALLBACKS **************/

static ssize_t midi_read_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			    uint16_t len, uint16_t offset)
{
	/* Respond with empty payload as per section 3 of the spec. */
	LOG_INF("Got read request, responding with empty payload");
	return 0;
}

static ssize_t midi_write_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			     uint16_t len, uint16_t offset, uint8_t flags)
{
	struct ble_midi_parse_cb_t parse_cb = {.midi_message_cb = context.user_callbacks.midi_message_cb,
					       .sysex_start_cb = context.user_callbacks.sysex_start_cb,
					       .sysex_data_cb = context.user_callbacks.sysex_data_cb,
					       .sysex_end_cb = context.user_callbacks.sysex_end_cb};
	/* log_buffer("MIDI rx:", &((uint8_t *)buf)[offset], len); */
	
	enum ble_midi_packet_error_t rc = ble_midi_parse_packet(&((uint8_t *)buf)[offset], len, &parse_cb);
	if (rc != BLE_MIDI_PACKET_SUCCESS) {
		LOG_ERR("ble_midi_parse_packet returned error %d", rc);
	}
}

static void midi_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{	
	int notification_enabled = value == BT_GATT_CCC_NOTIFY;
	LOG_INF("I/O characteristic notification enabled: %d", notification_enabled);

	/* MIDI I/O characteristic notification has been turned on/off.
	   Notify the user that BLE MIDI is ready/not ready. */
	on_ready_state_changed(notification_enabled ? BLE_MIDI_STATE_READY : BLE_MIDI_STATE_CONNECTED);
}

#define BT_UUID_MIDI_SERVICE BT_UUID_DECLARE_128(BLE_MIDI_SERVICE_UUID)
#define BT_UUID_MIDI_CHRC    BT_UUID_DECLARE_128(BLE_MIDI_CHAR_UUID)

#define BLE_MIDI_GATT_ATTRIBUTES BT_GATT_PRIMARY_SERVICE(BT_UUID_MIDI_SERVICE), \
		       BT_GATT_CHARACTERISTIC(BT_UUID_MIDI_CHRC, \
					      			  BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY | \
						      	      BT_GATT_CHRC_WRITE_WITHOUT_RESP, \ 
					      			  BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, \
									  midi_read_cb, \
					      			  midi_write_cb, NULL), \
		       BT_GATT_CCC(midi_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)

#ifdef CONFIG_BT_GATT_DYNAMIC_DB
static struct bt_gatt_attr gatt_attributes[] = {
	BLE_MIDI_GATT_ATTRIBUTES
};
static struct bt_gatt_service ble_midi_gatt_service = BT_GATT_SERVICE(gatt_attributes);
// TODO: use bt_conn_cb_register/bt_conn_cb_unregister when the API becomes available in nrf connect sdk.
// https://docs.zephyrproject.org/apidoc/latest/group__bt__conn.html#gad2f90b34390e3c3697fd455ae4ef5f31
static int ble_midi_service_is_registered = 0;
#else
static const int ble_midi_service_is_registered = 1;
BT_GATT_SERVICE_DEFINE(ble_midi_gatt_service, BLE_MIDI_GATT_ATTRIBUTES); 
#endif

/********* Buffered tx stuff **********/

#ifndef CONFIG_BLE_MIDI_TX_MODE_SINGLE_MSG
atomic_t has_tx_data = ATOMIC_INIT(0x00);
atomic_t waiting_for_notif_buf = ATOMIC_INIT(0x00);
RING_BUF_DECLARE(tx_queue_fifo, CONFIG_BLE_MIDI_TX_FIFO_SIZE);

int fifo_peek(uint8_t *bytes, int num_bytes) {
    return ring_buf_peek(&tx_queue_fifo, bytes, num_bytes);
}

int fifo_read(int num_bytes) {
    return ring_buf_get(&tx_queue_fifo, NULL, num_bytes);
}

int fifo_get_free_space()
{
    return ring_buf_space_get(&tx_queue_fifo);
}

int fifo_is_empty()
{
    return ring_buf_is_empty(&tx_queue_fifo);
}

int fifo_clear()
{
    ring_buf_reset(&tx_queue_fifo);
	return 0;
}

int fifo_write(const uint8_t *bytes, int num_bytes)
{
    return ring_buf_put(&tx_queue_fifo, bytes, num_bytes);
}

void notify_has_data(int has_data)
{
    has_data ? atomic_set_bit(&has_tx_data, 0) : atomic_clear_bit(&has_tx_data, 0);
}

static struct tx_queue_callbacks tx_queue_callbacks = {
    .fifo_peek = fifo_peek,
    .fifo_read = fifo_read,
    .fifo_get_free_space = fifo_get_free_space,
    .fifo_is_empty = fifo_is_empty,
    .fifo_clear = fifo_clear,
    .fifo_write = fifo_write,
    .ble_timestamp = timestamp_ms,
	.notify_has_data = notify_has_data
};

/* A work item handler for sending the contents of pending tx packets */
static void tx_pending_packets_work_cb(struct k_work *w)
{
	// Read any pending FIFO messages.
	tx_queue_read_from_fifo(&context.tx_queue);
	
	// Attempt to send pending BLE MIDI tx packets, 
	// stopping if the BLE stack buffer queue is full.
	struct ble_midi_writer_t* packet = tx_queue_first_tx_packet(&context.tx_queue);
	while (packet) {
		int send_result = send_packet(packet->tx_buf, packet->tx_buf_size);
		if (send_result == 0) {
			tx_queue_on_tx_packet_sent(&context.tx_queue);
		} 
		else if (send_result == -ENOMEM) {
			// BLE stack buffer queue is full. Retry this packet later
			atomic_set_bit(&waiting_for_notif_buf, 0);
			break;
		} else {
			// Something else went wrong.
			LOG_ERR("send_packet returned %d", send_result);
			break; // TODO: is this the right thing to do?
		}
		packet = tx_queue_first_tx_packet(&context.tx_queue);
	}
}
K_WORK_DEFINE(tx_pending_packets_work, tx_pending_packets_work_cb);

/* Called just before each BLE connection event. */
static void radio_notif_handler(void)
{
	/* If there is data to send, submit a work item to send it. */
	int has_fifo_data = !ring_buf_is_empty(&tx_queue_fifo);
	int has_ble_tx_packets = atomic_test_bit(&has_tx_data, 0);
	int waiting_for_notify_buffers = atomic_test_bit(&waiting_for_notif_buf, 0);
	if (!waiting_for_notify_buffers && (has_ble_tx_packets || has_fifo_data)) {
		k_work_submit(&tx_pending_packets_work);
	}
}

/* A work item handler that reads chunks from the tx queue FIFO */
static void tx_queue_fifo_work_cb(struct k_work *w);
static K_WORK_DEFINE(tx_queue_fifo_work, tx_queue_fifo_work_cb);
static void tx_queue_fifo_work_cb(struct k_work *w)
{
	tx_queue_read_from_fifo(&context.tx_queue);

	// k_sleep(K_MSEC(10)); // simulate long running work item. for testing re-submission logic.

	int unfinished_work_count = atomic_get(&context.pending_tx_queue_fifo_work_count);

	if (unfinished_work_count > 1) {
		// There are more work items waiting. Resubmit.
		int submit_result = k_work_submit(&tx_queue_fifo_work);
	}
	atomic_dec(&context.pending_tx_queue_fifo_work_count);
}

static void submit_tx_queue_fifo_work() {
	/* ... and submit a work item for adding the message to the next outgoing packet. */
	// TODO: return error code https://docs.zephyrproject.org/apidoc/latest/group__workqueue__apis.html#ga5353e76f73db070614f50d06d292d05c
	atomic_inc(&context.pending_tx_queue_fifo_work_count);
	int submit_result = k_work_submit(&tx_queue_fifo_work); 
}

#endif /* CONFIG_BLE_MIDI_TX_MODE_CONN_EVENT */

static void on_notify_done(struct bt_conn *conn, void *user_data)
{
#ifndef CONFIG_BLE_MIDI_TX_MODE_SINGLE_MSG
	atomic_clear_bit(&waiting_for_notif_buf, 0);
#endif

	if (context.user_callbacks.tx_done_cb) {
		context.user_callbacks.tx_done_cb();
	}
}

int send_packet(uint8_t *bytes, int num_bytes)
{
	struct bt_gatt_notify_params notify_params = {
		.attr = &ble_midi_gatt_service.attrs[1],
		.data = bytes,
		.len = num_bytes,
		.func = on_notify_done,
	};
	return bt_gatt_notify_cb(NULL, &notify_params);
	// return rc == -ENOTCONN ? 0 : rc; // TODO: what does this do? ignores failures if not connected?
}

#define INTERVAL_MIN 0x6 /* 7.5 ms */
#define INTERVAL_MAX 0x6 /* 7.5 ms */
static struct bt_le_conn_param *conn_param = BT_LE_CONN_PARAM(INTERVAL_MIN, INTERVAL_MAX, 0, 200);

void mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	/* From the BLE MIDI spec:
	 * "In transmitting MIDI data over Bluetooth, a series of MIDI messages of
	 * various sizes must be encoded into packets no larger than the negotiated
	 * MTU minus 3 bytes (typically 20 bytes or larger.)"
	 */
	int actual_mtu = bt_gatt_get_mtu(conn);
	int tx_buf_size_new = actual_mtu - 3;
	struct bt_conn_info info = {0};
	int e = bt_conn_get_info(conn, &info);

	int tx_buf_max_size = tx_buf_size_new > BLE_MIDI_TX_PACKET_MAX_SIZE
				      ? BLE_MIDI_TX_PACKET_MAX_SIZE
				      : tx_buf_size_new;

#ifdef CONFIG_BLE_MIDI_TX_MODE_SINGLE_MSG
	if (context.tx_writer.tx_buf_size > tx_buf_size_new) {
		// TODO: handle this somehow, e.g by decreasing size _after_ having sent the packet
		// TODO: warn about this also when doing buffered TX
		LOG_WRN("Lowering tx_buf_size from %d to %d", context.tx_writer.tx_buf_size,
			tx_buf_size_new);
	}
	context.tx_writer.tx_buf_max_size = tx_buf_max_size;
#else
	tx_queue_fifo_add_tx_packet_size(&context.tx_queue, tx_buf_max_size);
	submit_tx_queue_fifo_work();
#endif

	LOG_INF("MTU updated: tx %d, rx %d (actual %d), setting tx_buf_max_size to %d", tx, rx, actual_mtu,
		tx_buf_max_size);
}

static struct bt_gatt_cb gatt_callbacks = {.att_mtu_updated =
						   mtu_updated}; // TODO: must this be global?

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	if (!ble_midi_service_is_registered) {
		return;
	}

	on_ready_state_changed(BLE_MIDI_STATE_CONNECTED);

	int tx_running_status = 0;
	#ifdef CONFIG_BLE_MIDI_SEND_RUNNING_STATUS
	tx_running_status = 1;
	#endif
	int tx_note_off_as_note_on = 0;
	#ifdef CONFIG_BLE_MIDI_SEND_NOTE_OFF_AS_NOTE_ON
	tx_note_off_as_note_on = 1
	#endif
	ble_midi_context_reset(&context, tx_running_status, tx_note_off_as_note_on);

	LOG_INF("tx_running_status %d, tx_note_off_as_note_on %d", tx_running_status,
		tx_note_off_as_note_on);

#if CONFIG_BLE_MIDI_TX_MODE_CONN_EVENT || CONFIG_BLE_MIDI_TX_MODE_CONN_EVENT_LEGACY
	conn_event_trigger_set_enabled(conn, 1);
#endif

	/* Request smallest possible connection interval, if not already set.
	   NOTE: The actual update request is sent after 5 seconds as required
		 by the Bluetooth Core specification spec. See BT_CONN_PARAM_UPDATE_TIMEOUT. */
	struct bt_conn_info info = {0};
	int e = bt_conn_get_info(conn, &info);
	if (info.le.interval > INTERVAL_MIN) {
		e = bt_conn_le_param_update(conn, conn_param);
		LOG_INF("Got conn. interval %d ms, requesting interval %d ms with error %d",
			BT_CONN_INTERVAL_TO_MS(info.le.interval), BT_CONN_INTERVAL_TO_MS(INTERVAL_MIN), e);
	}
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	if (!ble_midi_service_is_registered) {
		return;
	}

	LOG_INF("Device disconnected, reason %d", reason);
	
	#if CONFIG_BLE_MIDI_TX_MODE_CONN_EVENT || CONFIG_BLE_MIDI_TX_MODE_CONN_EVENT_LEGACY
	conn_event_trigger_set_enabled(conn, 0);
	#endif

	/* Device disconnected. Notify the user that BLE MIDI is not available. */
	on_ready_state_changed(BLE_MIDI_STATE_NOT_CONNECTED);
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency,
			     uint16_t timeout)
{
	if (!ble_midi_service_is_registered) {
		return;
	}

	LOG_INF("Conn. params changed: interval: %d ms, latency: %d, timeout: %d",
		BT_CONN_INTERVAL_TO_MS(interval), latency, timeout);
#if CONFIG_BLE_MIDI_TX_MODE_CONN_EVENT || CONFIG_BLE_MIDI_TX_MODE_CONN_EVENT_LEGACY
	conn_event_trigger_refresh_conn_interval(conn);
#endif
}

BT_CONN_CB_DEFINE(ble_midi_conn_callbacks) = {.connected = on_connected,
					      .disconnected = on_disconnected,
					      .le_param_updated = le_param_updated};

enum ble_midi_error_t ble_midi_init(struct ble_midi_callbacks *callbacks)
{
	if (context.is_initialized) {
		return BLE_MIDI_ALREADY_INITIALIZED;
	}

#ifdef CONFIG_BT_GATT_DYNAMIC_DB
	int register_result = ble_midi_service_register();
	if (register_result) {
		LOG_ERR("ble_midi_service_register failed with error %d", register_result);
		return BLE_MIDI_SERVICE_REGISTRATION_ERROR;
	}
	LOG_INF("Registered BLE MIDI service");
#endif

	ble_midi_context_init(&context);

	bt_gatt_cb_register(&gatt_callbacks);
	context.user_callbacks.ready_cb = callbacks->ready_cb;
	context.user_callbacks.tx_done_cb = callbacks->tx_done_cb;
	context.user_callbacks.midi_message_cb = callbacks->midi_message_cb;
	context.user_callbacks.sysex_start_cb = callbacks->sysex_start_cb;
	context.user_callbacks.sysex_data_cb = callbacks->sysex_data_cb;
	context.user_callbacks.sysex_end_cb = callbacks->sysex_end_cb;

#ifndef CONFIG_BLE_MIDI_TX_MODE_SINGLE_MSG
	tx_queue_set_callbacks(&context.tx_queue, &tx_queue_callbacks);
	conn_event_trigger_init(radio_notif_handler); // TODO: return error
#endif
	LOG_INF("Initialized BLE MIDI");

	return BLE_MIDI_SUCCESS;
}

enum ble_midi_error_t ble_midi_tx_msg(uint8_t *bytes)
{
#ifdef CONFIG_BLE_MIDI_TX_MODE_SINGLE_MSG
	/* Send a single message packet. */
	ble_midi_writer_reset(&context.tx_writer);
	ble_midi_writer_add_msg(&context.tx_writer, bytes, timestamp_ms());
	return send_packet(context.tx_writer.tx_buf, context.tx_writer.tx_buf_size);
#else
	int add_result = tx_queue_fifo_add_msg(&context.tx_queue, bytes);
	if (add_result == TX_QUEUE_SUCCESS) {
		submit_tx_queue_fifo_work();
	}
	return add_result == TX_QUEUE_SUCCESS ? BLE_MIDI_SUCCESS : BLE_MIDI_TX_FIFO_FULL;
#endif
}

enum ble_midi_error_t ble_midi_tx_sysex_start()
{
#ifdef CONFIG_BLE_MIDI_TX_MODE_SINGLE_MSG
	ble_midi_writer_reset(&context.tx_writer);
	ble_midi_writer_start_sysex_msg(&context.tx_writer, timestamp_ms());
	return send_packet(context.tx_writer.tx_buf, context.tx_writer.tx_buf_size);
#else
	int add_result = tx_queue_fifo_add_sysex_start(&context.tx_queue); 
	if (add_result == TX_QUEUE_SUCCESS) {
		submit_tx_queue_fifo_work();
	}
	return add_result == TX_QUEUE_SUCCESS ? BLE_MIDI_SUCCESS : BLE_MIDI_TX_FIFO_FULL;
#endif
}

enum ble_midi_error_t ble_midi_tx_sysex_end()
{
	#ifdef CONFIG_BLE_MIDI_TX_MODE_SINGLE_MSG
	ble_midi_writer_reset(&context.tx_writer);
	ble_midi_writer_end_sysex_msg(&context.tx_writer, timestamp_ms());
	return send_packet(context.tx_writer.tx_buf, context.tx_writer.tx_buf_size);
#else
	int add_result = tx_queue_fifo_add_sysex_end(&context.tx_queue);
	if (add_result == TX_QUEUE_SUCCESS) {
		submit_tx_queue_fifo_work();
	}
	return add_result == TX_QUEUE_SUCCESS ? BLE_MIDI_SUCCESS : BLE_MIDI_TX_FIFO_FULL;
#endif
}

int ble_midi_tx_sysex_data(uint8_t *bytes, int num_bytes)
{
	if (num_bytes <= 0) {
		return BLE_MIDI_INVALID_ARGUMENT;
	}
	
#ifdef CONFIG_BLE_MIDI_TX_MODE_SINGLE_MSG
	ble_midi_writer_reset(&context.tx_writer);
	int add_result =
		ble_midi_writer_add_sysex_data(&context.tx_writer, bytes, num_bytes, timestamp_ms());
	if (add_result < 0) {
		LOG_ERR("ble_midi_writer_add_sysex_data failed with error %d", add_result);
		// TODO: better error code here
		return add_result;
	}

	int send_rc = send_packet(context.tx_writer.tx_buf, add_result);
	if (send_rc == 0) {
		return add_result; /* Return number of sent bytes on success */
	}
	return BLE_MIDI_SUCCESS; // TODO: report errors
#else
	int add_result = tx_queue_fifo_add_sysex_data(&context.tx_queue, bytes, num_bytes);
	if (add_result > 0) {
		submit_tx_queue_fifo_work();
	}
	return add_result > 0 ? add_result : BLE_MIDI_TX_FIFO_FULL;
#endif
}

#ifdef CONFIG_BLE_MIDI_TX_MODE_MANUAL
/**
 * 
 */
void ble_midi_tx_flush()
{
	/* Manually invoke radio notification handler */
	radio_notif_handler();
}
#endif

#ifdef CONFIG_BT_GATT_DYNAMIC_DB

int ble_midi_service_register() {
	int register_result = bt_gatt_service_register(&ble_midi_gatt_service);
	if (register_result == 0) {
		ble_midi_service_is_registered = 1;
	}
	return register_result;
}

int ble_midi_service_unregister() {
	int unregister_result = bt_gatt_service_unregister(&ble_midi_gatt_service);
	if (unregister_result == 0) {
		ble_midi_service_is_registered = 0;
	}
	return unregister_result;
}
#endif