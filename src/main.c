#include <zephyr/zephyr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#define BLE_MIDI_SERVICE_UUID \
	BT_UUID_128_ENCODE(0x03B80E5A, 0xEDE8, 0x4B33, 0xA751, 0x6CE34EC4C700)

#define BLE_MIDI_CHAR_UUID \
	BT_UUID_128_ENCODE(0x7772E5DB, 0x3868, 0x4112, 0xA1A9, 0xF2669D106BF3)

#define BT_UUID_MIDI_SERVICE BT_UUID_DECLARE_128(BLE_MIDI_SERVICE_UUID)
#define BT_UUID_MIDI_CHRC BT_UUID_DECLARE_128(BLE_MIDI_CHAR_UUID)

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN)};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BLE_MIDI_SERVICE_UUID),
};

static ssize_t midi_read_cb(struct bt_conn *conn,
			    const struct bt_gatt_attr *attr,
			    void *buf, uint16_t len,
			    uint16_t offset)
{
	/* Respond with empty payload as per section 3 of the spec. */
	return 0;
}

static ssize_t midi_write_cb(struct bt_conn *conn,
			     const struct bt_gatt_attr *attr,
			     const void *buf, uint16_t len,
			     uint16_t offset, uint8_t flags)
{
	printk("MIDI rx: ");
	for (int i = 0; i < len; i++)
	{
		printk("%02x ", ((uint8_t *)buf)[offset + i]);
	}
	printk("\n");
}

BT_GATT_SERVICE_DEFINE(ble_midi_service,
		       BT_GATT_PRIMARY_SERVICE(BT_UUID_MIDI_SERVICE),
		       BT_GATT_CHARACTERISTIC(BT_UUID_MIDI_CHRC,
					      BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
					      BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
					      midi_read_cb, midi_write_cb, NULL),
			BT_GATT_CCC(NULL,     BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
		);



static void ble_midi_tx(uint8_t *bytes, uint32_t num_bytes)
{
	static uint8_t payload[5] = {
		0xb7, 0xf1, 0x8b, 0x46, 0x00
	};

	int rc = bt_gatt_notify(NULL, &ble_midi_service.attrs[1], payload, sizeof(payload));
	printk("ble_midi_tx %d\n", rc);
	return rc == -ENOTCONN ? 0 : rc;

}

static void bt_ready_cb(int err)
{
	printk("bt_ready_cb %d\n", err);
}

static void on_connected(struct bt_conn *conn, uint8_t err)
{
	printk("on_connected\n");
}
static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("on_disconnected\n");
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = on_connected,
    .disconnected = on_disconnected,
};

void main(void)
{
	uint32_t err = bt_enable(bt_ready_cb);
	int ad_err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	printk("bt_le_adv_start %d\n", ad_err);
	__ASSERT(err == 0, "bt_enable failed");

	bool is_note_on = false;
	uint8_t note_num = 69;
	uint8_t note_vel = 127;
	while (1)
	{
		uint8_t midi_bytes[3] = {is_note_on ? 0x90 : 0x80, note_num, note_vel};
		ble_midi_tx(midi_bytes, 3);
		is_note_on = !is_note_on;
		k_msleep(500);
	}
}