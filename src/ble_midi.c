#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include "ble_midi.h"
#include "ble_midi_packet.h"

#define BT_UUID_MIDI_SERVICE BT_UUID_DECLARE_128(BLE_MIDI_SERVICE_UUID)
#define BT_UUID_MIDI_CHRC BT_UUID_DECLARE_128(BLE_MIDI_CHAR_UUID)

#ifdef CONFIG_BLE_MIDI_SEND_RUNNING_STATUS
#define BLE_MIDI_RUNNING_STATUS_ENABLED 1
#else
#define BLE_MIDI_RUNNING_STATUS_ENABLED 0
#endif

static struct ble_midi_callbacks user_callbacks = {
    .midi_message_cb = NULL,
	.sysex_cb  = NULL,
    .available_cb = NULL,
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

static void dummy_midi_message_cb(uint8_t* bytes, uint8_t num_bytes, uint16_t timestamp) {

}

static void dummy_sysex_cb(uint8_t* bytes, uint8_t num_bytes, uint32_t sysex_ended) {

}

static ssize_t midi_write_cb(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     const void *buf, uint16_t len,
			     uint16_t offset, uint8_t flags)
{
    log_buffer("MIDI rx:", ((uint8_t *)buf)[offset], len);
    ble_midi_packet_parse(
        ((uint8_t *)buf)[offset],
        len,
        user_callbacks.midi_message_cb ? user_callbacks.midi_message_cb : dummy_midi_message_cb,
        user_callbacks.sysex_cb ? user_callbacks.sysex_cb : dummy_sysex_cb
    );
}

static void midi_ccc_cfg_changed(const struct bt_gatt_attr *attr,
				  uint16_t value)
{
    /* MIDI I/O characteristic notification has been turned on.
       Notify the user that BLE MIDI is available. */
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
	BT_LE_CONN_PARAM(INTERVAL_MIN, INTERVAL_MAX, 0, 36);
static struct bt_conn_le_phy_param *phy_param = BT_CONN_LE_PHY_PARAM_2M;

static void mtu_exchange_func(struct bt_conn *conn, uint8_t att_err,
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
}

static void on_connected(struct bt_conn *conn, uint8_t err)
{
    /* Connection established. Don't notify the user that BLE MIDI is
       available until MIDI I/O characteristic notification has been turned on. */

		int e;
		struct bt_conn_info info = {0};
		e = bt_conn_get_info(conn, &info);
		if (info.le.phy->rx_phy != BT_GAP_LE_PHY_2M || info.le.phy->tx_phy != BT_GAP_LE_PHY_2M) {
			e = bt_conn_le_phy_update(conn, phy_param);
		}
		if (info.le.interval != INTERVAL_MIN) {
			e = bt_conn_le_param_update(conn, conn_param);
		}

		// err = bt_conn_le_data_len_update(conn, conn_param->);
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    /* Device disconnected. Notify the user that BLE MIDI is not available. */
    if (user_callbacks.available_cb) {
        user_callbacks.available_cb(0);
    }
}

static bool le_param_req(struct bt_conn *conn, struct bt_le_conn_param *param)
{
	printk("Connection parameters update request received.\n");
	printk("Minimum interval: %d, Maximum interval: %d\n",
	       param->interval_min, param->interval_max);
	printk("Latency: %d, Timeout: %d\n", param->latency, param->timeout);

	return true;
}

static void le_param_updated(struct bt_conn *conn, uint16_t interval,
			     uint16_t latency, uint16_t timeout)
{
	printk("Connection parameters updated: interval: %dx1.25ms, latency: %d, timeout: %d\n",
	       interval, latency, timeout);
}

static void le_phy_updated(struct bt_conn *conn,
			   struct bt_conn_le_phy_info *param)
{
	printk("LE PHY updated: TX PHY %s, RX PHY %s\n",
	       phy2str(param->tx_phy), phy2str(param->rx_phy));
}

static void le_data_length_updated(struct bt_conn *conn,
				   struct bt_conn_le_data_len_info *info)
{
	printk("LE data len updated: TX (len: %d time: %d)"
	       " RX (len: %d time: %d)\n", info->tx_max_len,
	       info->tx_max_time, info->rx_max_len, info->rx_max_time);
}

BT_CONN_CB_DEFINE(ble_midi_conn_callbacks) = {
    .connected = on_connected,
    .disconnected = on_disconnected,
		.le_param_req = le_param_req,
		.le_param_updated = le_param_updated,
		.le_phy_updated = le_phy_updated,
		.le_data_len_updated = le_data_length_updated
};

void ble_midi_register_callbacks(struct ble_midi_callbacks *callbacks) {
    user_callbacks.available_cb = callbacks->available_cb;
    user_callbacks.sysex_cb = callbacks->sysex_cb;
    user_callbacks.midi_message_cb = callbacks->midi_message_cb;
}

int ble_midi_tx(uint8_t *bytes)
{
	struct ble_midi_packet packet;
	ble_midi_packet_reset(&packet);
	ble_midi_packet_add_message(&packet, bytes, 0, BLE_MIDI_RUNNING_STATUS_ENABLED);

    // USE notify cb
	int rc = bt_gatt_notify(NULL, &ble_midi_service.attrs[1], packet.bytes, packet.size);
    if (rc == 0) {
        // log_buffer("MIDI tx:", packet.bytes, packet.size);
    }

	return rc == -ENOTCONN ? 0 : rc;
}
