#include <zephyr/zephyr.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/bluetooth.h>
#include "ble_midi.h"

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN)};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BLE_MIDI_SERVICE_UUID),
};

#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);

static int ble_midi_is_available = 0;

static void ble_midi_available(int is_available) {
	gpio_pin_set_dt(&led0, is_available);
	if (!is_available) {
		gpio_pin_set_dt(&led1, 0);
	}
	ble_midi_is_available = is_available;
}

static void ble_midi_message(uint8_t* message) {
	gpio_pin_toggle_dt(&led1);
}

static void ble_midi_sysex(uint8_t* bytes, uint8_t num_bytes, uint32_t sysex_ended) {

}

static struct ble_midi_callbacks midi_callbacks = {
	.available_cb = ble_midi_available,
	.midi_message_cb = ble_midi_message,
	.sysex_cb = ble_midi_sysex
};

void main(void)
{
	/* Set up LEDs */
	/* Turn on LED 0 when the device is online */
	gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE);
	gpio_pin_set_dt(&led0, 0);
	/* Toggle LED 1 on oncoming MIDI events */
	gpio_pin_configure_dt(&led1, GPIO_OUTPUT_ACTIVE);
	gpio_pin_set_dt(&led1, 0);

	ble_midi_register_callbacks(&midi_callbacks);
	uint32_t err = bt_enable(NULL);
	int ad_err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	printk("bt_le_adv_start %d\n", ad_err);
	__ASSERT(err == 0, "bt_enable failed");

	bool is_note_on = false;
	uint8_t note_num = 69;
	uint8_t note_vel = 127;
	while (1)
	{
		if (ble_midi_is_available) {
			uint8_t midi_bytes[3] = {is_note_on ? 0x90 : 0x80, note_num, note_vel};
			ble_midi_tx(midi_bytes);
			is_note_on = !is_note_on;
		}
		k_msleep(500);
	}
}