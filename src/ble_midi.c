#include <zephyr/zephyr.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include "ble_midi.h"
#include "ble_midi_packet.h"

#ifdef CONFIG_BLE_MIDI_NRF_BATCH_TX
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <sys/ring_buffer.h>
#include <mpsl_radio_notification.h>
atomic_t has_tx_data = ATOMIC_INIT(0x00);
struct k_work_q work_q;
RING_BUF_DECLARE(msg_ringbuf, 128);
RING_BUF_DECLARE(sysex_ringbuf, 128);

#endif

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

static ssize_t midi_read_cb(struct bt_conn *conn,
			    const struct bt_gatt_attr *attr,
			    void *buf, uint16_t len,
			    uint16_t offset)
{
	/* Respond with empty payload as per section 3 of the spec. */
	return 0;
}

static void log_buffer(const char* tag, uint8_t* bytes, uint32_t num_bytes)
{
    printk("%s ", tag);
    for (int i = 0; i < num_bytes; i++)
	{
		printk("%02x ", ((uint8_t *)bytes)[i]);
	}
	printk("\n");
}

static ssize_t midi_write_cb(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     const void *buf, uint16_t len,
			     uint16_t offset, uint8_t flags)
{
		struct ble_midi_parse_cb_t parse_cb = {
			.midi_message_cb = user_callbacks.midi_message_cb,
			.sysex_start_cb = user_callbacks.sysex_start_cb,
			.sysex_data_cb = user_callbacks.sysex_data_cb,
			.sysex_end_cb = user_callbacks.sysex_end_cb
		};
    // log_buffer("MIDI rx:", &((uint8_t *)buf)[offset], len);
    ble_midi_parse_packet(
        &((uint8_t *)buf)[offset],
        len,
        &parse_cb
    );
}

static void midi_ccc_cfg_changed(const struct bt_gatt_attr *attr,
				  uint16_t value)
{
    /* MIDI I/O characteristic notification has been turned on.
       Notify the user that BLE MIDI is available. */
		printk("midi_ccc_cfg_changed %d\n", value);
	if (user_callbacks.available_cb) {
		user_callbacks.available_cb(value == BT_GATT_CCC_NOTIFY);
	}
}

static const char *phy2str(uint8_t phy)
{
	switch (phy) {
	case 0: return "No packets";
	case BT_GAP_LE_PHY_1M: return "LE 1M";
	case BT_GAP_LE_PHY_2M: return "LE 2M";
	case BT_GAP_LE_PHY_CODED: return "LE Coded";
	default: return "Unknown";
	}
}

#define BT_UUID_MIDI_SERVICE BT_UUID_DECLARE_128(BLE_MIDI_SERVICE_UUID)
#define BT_UUID_MIDI_CHRC BT_UUID_DECLARE_128(BLE_MIDI_CHAR_UUID)

BT_GATT_SERVICE_DEFINE(ble_midi_service,
		       BT_GATT_PRIMARY_SERVICE(BT_UUID_MIDI_SERVICE),
		       BT_GATT_CHARACTERISTIC(BT_UUID_MIDI_CHRC,
					      BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
					      BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
					      midi_read_cb, midi_write_cb, NULL),
			BT_GATT_CCC(midi_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
		);

#define INTERVAL_MIN	0x6	/* 7.5 ms */
#define INTERVAL_MAX	0x6	/* 7.5 ms */
static struct bt_le_conn_param *conn_param =
	BT_LE_CONN_PARAM(INTERVAL_MIN, INTERVAL_MAX, 0, 200);
// static struct bt_conn_le_phy_param *phy_param = BT_CONN_LE_PHY_PARAM_2M;

/* static void mtu_exchange_func(struct bt_conn *conn, uint8_t att_err,
			  struct bt_gatt_exchange_params *params)
{
	struct bt_conn_info info = {0};
	int err;

	printk("MTU exchange %s\n", att_err == 0 ? "successful" : "failed");

	err = bt_conn_get_info(conn, &info);
	if (err) {
		printk("Failed to get connection info %d\n", err);
		return;
	}
} */

void mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	struct bt_conn_info info = {0};
	int e = bt_conn_get_info(conn, &info);
	if (tx_writer.tx_buf_size > tx) {
		// TODO: warn(lowering packet max size)
	}
	// TODO: From the spec: In transmitting MIDI data over Bluetooth, a series of MIDI messages of various sizes must be encoded into packets no larger than the negotiated MTU minus 3 bytes (typically 20 bytes or larger.)
	int tx_buf_max_size = tx > BLE_MIDI_TX_PACKET_MAX_SIZE ? BLE_MIDI_TX_PACKET_MAX_SIZE : tx;
	tx_writer.tx_buf_max_size = tx_buf_max_size;
	sysex_tx_writer.tx_buf_max_size = tx_buf_max_size;
	printk("Updated MTU: TX: %d RX: %d bytes.\n", tx, rx);
}

static struct bt_gatt_cb gatt_callbacks = {.att_mtu_updated = mtu_updated};

static void on_connected(struct bt_conn *conn, uint8_t err)
{
		/* Request smallest possible connection interval, if not already set.
		   NOTE: The actual update request is sent after 5 seconds as required
			 by the Bluetooth Core specification spec. See BT_CONN_PARAM_UPDATE_TIMEOUT. */
		struct bt_conn_info info = {0};
		int e = bt_conn_get_info(conn, &info);
		if (info.le.interval != INTERVAL_MIN) {
			e = bt_conn_le_param_update(conn, conn_param);
			printk("bt_conn_le_param_update rc %d\n", e);
		}

		int use_running_status = CONFIG_BLE_MIDI_SEND_RUNNING_STATUS;
		int note_off_as_note_on = CONFIG_BLE_MIDI_SEND_NOTE_OFF_AS_NOTE_ON;
		ble_midi_writer_init(&sysex_tx_writer, use_running_status, note_off_as_note_on);
		ble_midi_writer_init(&tx_writer, use_running_status, note_off_as_note_on);

		irq_enable(TEMP_IRQn);
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    /* Device disconnected. Notify the user that BLE MIDI is not available. */
    if (user_callbacks.available_cb) {
      user_callbacks.available_cb(0);
    }
		irq_disable(TEMP_IRQn);
}

static bool le_param_req(struct bt_conn *conn, struct bt_le_conn_param *param)
{
	printk("Connection parameters update request received.\n");
	printk("Minimum interval: %d, Maximum interval: %d\n",
	       param->interval_min, param->interval_max);
	printk("Latency: %d, Timeout: %d\n", param->latency, param->timeout);
	if (param->interval_max != INTERVAL_MAX) {
			int e = bt_conn_le_param_update(conn, conn_param);
			printk("bt_conn_le_param_update rc %d\n", e);
		}

	return true;
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval,
			     uint16_t latency, uint16_t timeout)
{
	printk("Connection parameters updated: interval: %dx1.25ms, latency: %d, timeout: %d\n",
	       interval, latency, timeout);
}

/*
static void le_phy_updated(struct bt_conn *conn,
			   struct bt_conn_le_phy_info *param)
{
	printk("LE PHY updated: TX PHY %s, RX PHY %s\n",
	       phy2str(param->tx_phy), phy2str(param->rx_phy));
} */

/*static void le_data_length_updated(struct bt_conn *conn,
				   struct bt_conn_le_data_len_info *info)
{
	printk("LE data len updated: TX (len: %d time: %d)"
	       " RX (len: %d time: %d)\n", info->tx_max_len,
	       info->tx_max_time, info->rx_max_len, info->rx_max_time);
}*/

uint16_t timestamp_ms() {
	return k_ticks_to_ms_near64(k_uptime_ticks()) & 0x1FFF;
}

#ifdef CONFIG_BLE_MIDI_NRF_BATCH_TX

#define RADIO_NOTIF_PRIORITY 1
static void radio_notif_work_cb(struct k_work * w)
{
	// printk("sending packet at %d\n", timestamp_ms());
	send_packet(tx_writer.tx_buf, tx_writer.tx_buf_size);
	ble_midi_writer_reset(&tx_writer);
	atomic_clear_bit(&has_tx_data, 0);
}

static void midi_msg_work_cb(struct k_work * w)
{
	uint8_t msg[3];
	ring_buf_get(&msg_ringbuf, msg, 3);
	uint16_t timestamp = timestamp_ms();
	// printk("adding msg at %d\n", timestamp);
	ble_midi_writer_add_msg(&tx_writer, msg, timestamp);
	atomic_set_bit(&has_tx_data, 0);
}

K_WORK_DEFINE(radio_notif_work, radio_notif_work_cb);
K_WORK_DEFINE(midi_msg_work, midi_msg_work_cb);
static void radio_notif_handler(void)
{
	if (atomic_test_bit(&has_tx_data, 0)) {
		k_work_submit(&radio_notif_work);
	}
}

static void radio_notif_setup()
{
	int rc = mpsl_radio_notification_cfg_set(
		MPSL_RADIO_NOTIFICATION_TYPE_INT_ON_ACTIVE,
		MPSL_RADIO_NOTIFICATION_DISTANCE_420US, TEMP_IRQn);
	IRQ_CONNECT(TEMP_IRQn, RADIO_NOTIF_PRIORITY, radio_notif_handler, NULL,
		    0);
	__ASSERT(rc == 0, "mpsl_radio_notification_cfg_set failed");
}

SYS_INIT(radio_notif_setup, POST_KERNEL, 45);

#endif

BT_CONN_CB_DEFINE(ble_midi_conn_callbacks) = {
    .connected = on_connected,
    .disconnected = on_disconnected,
		.le_param_req = le_param_req,
		.le_param_updated = le_param_updated,
		// .le_phy_updated = le_phy_updated
		// .le_data_len_updated = le_data_length_updated
};

void ble_midi_init(struct ble_midi_callbacks *callbacks) {
		bt_gatt_cb_register(&gatt_callbacks);
    user_callbacks.available_cb = callbacks->available_cb;
		user_callbacks.tx_available_cb = callbacks->tx_available_cb;
    user_callbacks.midi_message_cb = callbacks->midi_message_cb;
    user_callbacks.sysex_start_cb = callbacks->sysex_start_cb;
		user_callbacks.sysex_data_cb = callbacks->sysex_data_cb;
		user_callbacks.sysex_end_cb = callbacks->sysex_end_cb;
}

static void on_notify_done(struct bt_conn *conn, void *user_data)
{
	/* BLE MIDI packet sent. */
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

int ble_midi_tx_msg(uint8_t *bytes)
{
	#ifdef CONFIG_BLE_MIDI_NRF_BATCH_TX
	ring_buf_put(&msg_ringbuf, bytes, 3);
	k_work_submit(&midi_msg_work);
	#else
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

int ble_midi_tx_sysex_data(uint8_t* bytes, int num_bytes)
{
	ble_midi_writer_reset(&sysex_tx_writer);
	sysex_tx_writer.in_sysex_msg = 1; // TODO: remove in_sysex_msg flag?
	int add_rc = ble_midi_writer_add_sysex_data(&sysex_tx_writer, bytes, num_bytes, timestamp_ms());
	if (add_rc < 0) { // TODO: how to interpret 0?
		printk("ble_midi_writer_add_sysex_data add_rc %d\n", add_rc);
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

