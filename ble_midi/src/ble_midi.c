#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <ble_midi/ble_midi.h>
#include "ble_midi_packet.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ble_midi, CONFIG_BLE_MIDI_LOG_LEVEL);

#ifdef CONFIG_BLE_MIDI_NRF_BATCH_TX
#include <zephyr/init.h>
#include <zephyr/sys/ring_buffer.h>
#include <mpsl_radio_notification.h>
#endif /* CONFIG_BLE_MIDI_NRF_BATCH_TX */

static uint16_t timestamp_ms()
{
	return k_ticks_to_ms_near64(k_uptime_ticks()) & 0x1FFF;
}

static void log_buffer(const char *tag, uint8_t *bytes, uint32_t num_bytes)
{
	printk("%s ", tag);
	for (int i = 0; i < num_bytes; i++) {
		printk("%02x ", ((uint8_t *)bytes)[i]);
	}
	printk("\n");
}

static int ble_midi_is_initialized = 0;
static struct ble_midi_writer_t tx_writer;
static struct ble_midi_writer_t sysex_tx_writer;
static struct ble_midi_callbacks user_callbacks = {
	.available_cb = NULL,
	.tx_available_cb = NULL,
	.midi_message_cb = NULL,
	.sysex_start_cb = NULL,
	.sysex_data_cb = NULL,
	.sysex_end_cb = NULL,
};

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
	struct ble_midi_parse_cb_t parse_cb = {.midi_message_cb = user_callbacks.midi_message_cb,
					       .sysex_start_cb = user_callbacks.sysex_start_cb,
					       .sysex_data_cb = user_callbacks.sysex_data_cb,
					       .sysex_end_cb = user_callbacks.sysex_end_cb};
	/* log_buffer("MIDI rx:", &((uint8_t *)buf)[offset], len); */
	enum ble_midi_error_t rc = ble_midi_parse_packet(&((uint8_t *)buf)[offset], len, &parse_cb);
	if (rc != BLE_MIDI_SUCCESS) {
		LOG_ERR("ble_midi_parse_packet returned error %d", rc);
	}
}

static void midi_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	int notification_enabled = value == BT_GATT_CCC_NOTIFY;
	LOG_INF("BLE MIDI characteristic notification enabled: %d", notification_enabled);

	/* MIDI I/O characteristic notification has been turned on.
			Notify the user that BLE MIDI is available. */
	if (user_callbacks.available_cb) {
		user_callbacks.available_cb(notification_enabled);
	}
}

#define BT_UUID_MIDI_SERVICE BT_UUID_DECLARE_128(BLE_MIDI_SERVICE_UUID)
#define BT_UUID_MIDI_CHRC    BT_UUID_DECLARE_128(BLE_MIDI_CHAR_UUID)

BT_GATT_SERVICE_DEFINE(ble_midi_service, BT_GATT_PRIMARY_SERVICE(BT_UUID_MIDI_SERVICE),
		       BT_GATT_CHARACTERISTIC(BT_UUID_MIDI_CHRC,
					      BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY |
						      BT_GATT_CHRC_WRITE_WITHOUT_RESP,
					      BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, midi_read_cb,
					      midi_write_cb, NULL),
		       BT_GATT_CCC(midi_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

/********* NRF BATCHED TX STUFF **********/

#ifdef CONFIG_BLE_MIDI_NRF_BATCH_TX
#define RADIO_NOTIF_PRIORITY 1
atomic_t has_tx_data = ATOMIC_INIT(0x00);
RING_BUF_DECLARE(msg_ringbuf, 128);
RING_BUF_DECLARE(sysex_ringbuf, 128);
struct k_work_q work_q; // <- TODO: use this?

/* A work item handler for sending the contents of the tx packet */
static void radio_notif_work_cb(struct k_work *w)
{
	/* Send tx packet */
	send_packet(tx_writer.tx_buf, tx_writer.tx_buf_size);
	/* Clear tx packet */
	ble_midi_writer_reset(&tx_writer);
	/* Indicate that there is no longer data to send */
	atomic_clear_bit(&has_tx_data, 0);
}
K_WORK_DEFINE(radio_notif_work, radio_notif_work_cb);

/* Called just before each BLE connection event. */
static void radio_notif_handler(void)
{
	/* If there is data to send, submit a work item to send it. */
	if (atomic_test_bit(&has_tx_data, 0)) {
		k_work_submit(&radio_notif_work);
	}
}

/**
 * Configures an interrupt that is triggered just before each BLE connection event.
 * Used to trigger transmission of BLE MIDI data accumulated between connection events.
 */
static void radio_notif_setup()
{
	int rc = mpsl_radio_notification_cfg_set(MPSL_RADIO_NOTIFICATION_TYPE_INT_ON_ACTIVE,
						 MPSL_RADIO_NOTIFICATION_DISTANCE_420US, TEMP_IRQn);
	if (rc == 0) {
		IRQ_CONNECT(TEMP_IRQn, RADIO_NOTIF_PRIORITY, radio_notif_handler, NULL, 0);
		LOG_INF("Finished setting up connection event interrupt");
	} else {
		LOG_ERR("mpsl_radio_notification_cfg_set failed with error %d", rc);
	}

	__ASSERT(rc == 0, "mpsl_radio_notification_cfg_set failed");
}

/* A work item handler that adds an enqueued MIDI message to the tx packet */
static void midi_msg_work_cb(struct k_work *w)
{
	/* Get the MIDI bytes to send. */
	uint8_t msg[3];
	ring_buf_get(&msg_ringbuf, msg, 3);
	/* Write the MIDI bytes to the tx packet */
	uint16_t timestamp = timestamp_ms();
	ble_midi_writer_add_msg(&tx_writer, msg, timestamp);
	/* Signal that there is data to send at the next connection event */
	atomic_set_bit(&has_tx_data, 0);
}
static K_WORK_DEFINE(midi_msg_work, midi_msg_work_cb);
#endif /* CONFIG_BLE_MIDI_NRF_BATCH_TX */

static void on_notify_done(struct bt_conn *conn, void *user_data)
{
	if (user_callbacks.tx_available_cb) {
		user_callbacks.tx_available_cb();
	}
}

int send_packet(uint8_t *bytes, int num_bytes)
{
	struct bt_gatt_notify_params notify_params = {
		.attr = &ble_midi_service.attrs[1],
		.data = bytes,
		.len = num_bytes,
		.func = on_notify_done,
	};
	int rc = bt_gatt_notify_cb(NULL, &notify_params);
	return rc == -ENOTCONN ? 0 : rc; // TODO: what does this do?
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
	int tx_buf_size_new = tx - 3;
	if (tx_writer.tx_buf_size > tx_buf_size_new) {
		LOG_WRN("Lowering tx_buf_size from %d to %d", tx_writer.tx_buf_size,
			tx_buf_size_new);
	}

	struct bt_conn_info info = {0};
	int e = bt_conn_get_info(conn, &info);

	int tx_buf_max_size = tx_buf_size_new > BLE_MIDI_TX_PACKET_MAX_SIZE
				      ? BLE_MIDI_TX_PACKET_MAX_SIZE
				      : tx_buf_size_new;
	tx_writer.tx_buf_max_size = tx_buf_max_size;
	sysex_tx_writer.tx_buf_max_size = tx_buf_max_size;
	LOG_INF("MTU changed to tx %d, rx %d, setting tx_buf_max_size to %d", tx, rx,
		tx_buf_max_size);
}

static struct bt_gatt_cb gatt_callbacks = {.att_mtu_updated =
						   mtu_updated}; // TODO: must this be global?

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	int tx_running_status = 0;
	#ifdef CONFIG_BLE_MIDI_SEND_RUNNING_STATUS
	tx_running_status = 1;
	#endif
	int tx_note_off_as_note_on = 0;
	#ifdef CONFIG_BLE_MIDI_SEND_NOTE_OFF_AS_NOTE_ON
	tx_note_off_as_note_on = 1
	#endif
	ble_midi_writer_init(&sysex_tx_writer, tx_running_status, tx_note_off_as_note_on);
	ble_midi_writer_init(&tx_writer, tx_running_status, tx_note_off_as_note_on);

	LOG_INF("tx_running_status %d, tx_note_off_as_note_on %d", tx_running_status,
		tx_note_off_as_note_on);

	/* Request smallest possible connection interval, if not already set.
	   NOTE: The actual update request is sent after 5 seconds as required
		 by the Bluetooth Core specification spec. See BT_CONN_PARAM_UPDATE_TIMEOUT. */
	struct bt_conn_info info = {0};
	int e = bt_conn_get_info(conn, &info);
	if (info.le.interval > INTERVAL_MIN) {
		e = bt_conn_le_param_update(conn, conn_param);
		LOG_INF("Got conn. interval %d, requested interval %d with error %d",
			info.le.interval, INTERVAL_MIN, e);
	}

#ifdef CONFIG_BLE_MIDI_NRF_BATCH_TX
	irq_enable(TEMP_IRQn);
#endif
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Device disconnected");
	/* Device disconnected. Notify the user that BLE MIDI is not available. */
	if (user_callbacks.available_cb) {
		user_callbacks.available_cb(0);
	}
#ifdef CONFIG_BLE_MIDI_NRF_BATCH_TX
	irq_disable(TEMP_IRQn);
#endif
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency,
			     uint16_t timeout)
{
	LOG_INF("Conn. params changed: interval: %d ms, latency: %d, timeout: %d",
		BT_CONN_INTERVAL_TO_MS(interval), latency, timeout);
}

BT_CONN_CB_DEFINE(ble_midi_conn_callbacks) = {.connected = on_connected,
					      .disconnected = on_disconnected,
					      .le_param_updated = le_param_updated};

void ble_midi_init(struct ble_midi_callbacks *callbacks)
{
	if (ble_midi_is_initialized) {
		return;
	}

	bt_gatt_cb_register(&gatt_callbacks);
	user_callbacks.available_cb = callbacks->available_cb;
	user_callbacks.tx_available_cb = callbacks->tx_available_cb;
	user_callbacks.midi_message_cb = callbacks->midi_message_cb;
	user_callbacks.sysex_start_cb = callbacks->sysex_start_cb;
	user_callbacks.sysex_data_cb = callbacks->sysex_data_cb;
	user_callbacks.sysex_end_cb = callbacks->sysex_end_cb;
#ifdef CONFIG_BLE_MIDI_NRF_BATCH_TX
	radio_notif_setup();
#endif
	LOG_INF("Initialized BLE MIDI");
	ble_midi_is_initialized = 1;
}

int ble_midi_tx_msg(uint8_t *bytes)
{
#ifdef CONFIG_BLE_MIDI_NRF_BATCH_TX
	/* Enqueue the MIDI message ... */
	ring_buf_put(&msg_ringbuf, bytes, 3);
	/* ... and submit a work item for adding the message to the next outgoing packet. */
	k_work_submit(&midi_msg_work);
#else
	/* Send a single message packet. */
	ble_midi_writer_reset(&tx_writer);
	ble_midi_writer_add_msg(&tx_writer, bytes, timestamp_ms());
	return send_packet(tx_writer.tx_buf, tx_writer.tx_buf_size);
#endif
}

int ble_midi_tx_sysex_start()
{
	ble_midi_writer_reset(&sysex_tx_writer);
	ble_midi_writer_start_sysex_msg(&sysex_tx_writer, timestamp_ms());
	return send_packet(sysex_tx_writer.tx_buf, sysex_tx_writer.tx_buf_size);
}

int ble_midi_tx_sysex_data(uint8_t *bytes, int num_bytes)
{
	ble_midi_writer_reset(&sysex_tx_writer);
	int add_rc =
		ble_midi_writer_add_sysex_data(&sysex_tx_writer, bytes, num_bytes, timestamp_ms());
	if (add_rc < 0) { // TODO: how to interpret 0?
		LOG_ERR("ble_midi_writer_add_sysex_data failed with error %d", add_rc);
		return -EINVAL;
	}
	int send_rc = send_packet(sysex_tx_writer.tx_buf, sysex_tx_writer.tx_buf_size);
	if (send_rc == 0) {
		return add_rc; /* Return number of sent bytes on success */
	}
	return send_rc;
}

int ble_midi_tx_sysex_end()
{
	ble_midi_writer_reset(&sysex_tx_writer);
	ble_midi_writer_end_sysex_msg(&sysex_tx_writer, timestamp_ms());
	return send_packet(sysex_tx_writer.tx_buf, sysex_tx_writer.tx_buf_size);
}
