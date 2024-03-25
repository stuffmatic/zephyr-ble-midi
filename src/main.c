#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/ring_buffer.h>
#include <ble_midi/ble_midi.h>

/************************ App state ************************/
K_MSGQ_DEFINE(button_event_q, sizeof(uint8_t), 128, 4);

#define SYSEX_TX_MESSAGE_SIZE	2000
#define SYSEX_TX_MAX_CHUNK_SIZE 240

struct sample_app_state_t {
	int ble_midi_is_available;
	int sysex_rx_data_byte_count;
	int sysex_tx_data_byte_count;
	int sysex_tx_in_progress;
	int button_states[4];
};

static struct sample_app_state_t sample_app_state = {.ble_midi_is_available = 0,
						     .sysex_tx_data_byte_count = 0,
						     .sysex_tx_in_progress = 0,
						     .sysex_rx_data_byte_count = 0,
						     .button_states = {0, 0, 0, 0}};

/************************ LEDs ************************/

#define LED_COUNT	 3
/* Turn on LED 0 when BLE MIDI is available */
#define LED_AVAILABLE	 0
/* Toggle LED 1 on received sysex messages */
#define LED_RX_SYSEX	 1
/* Toggle LED 2 when receiving non-sysex messages */
#define LED_RX_NON_SYSEX 2

static const struct gpio_dt_spec leds[LED_COUNT] = {GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios),
						    GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios),
						    GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios)};

static void init_leds()
{
	for (int i = 0; i < LED_COUNT; i++) {
		gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_ACTIVE);
		gpio_pin_set_dt(&leds[i], 0);
	}
}

/* Ringbuf/work queue */
RING_BUF_DECLARE(midi_msg_ringbuf, 128);
static void midi_msg_work_cb(struct k_work *w)
{
	uint8_t data[4] = {0, 0, 0, 0};
	while (ring_buf_get(&midi_msg_ringbuf, &data, 4) == 4) {
		uint8_t msg_byte_count = data[0];
		uint8_t *msg_bytes = &data[1];
		printk("incoming MIDI message ");
		for (int i = 0; i < msg_byte_count; i++) {
			printk("%02x ", msg_bytes[i]);
		}
		printk("\n");
		gpio_pin_toggle_dt(&leds[LED_RX_NON_SYSEX]);
	};
}
static K_WORK_DEFINE(midi_msg_work, midi_msg_work_cb);

/************************ Buttons ************************/

#define BUTTON_COUNT	      4
#define BUTTON_TX_NON_SYSEX   0
#define BUTTON_TX_SYSEX_SHORT 1
#define BUTTON_TX_SYSEX_LONG  2
#define BUTTON_MANUAL_TX_FLUSH 3
static const struct gpio_dt_spec buttons[BUTTON_COUNT] = {
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw0), gpios, {0}),
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw1), gpios, {0}),
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw2), gpios, {0}),
	GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw3), gpios, {0})};
static struct gpio_callback button_cb_data;

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	for (int i = 0; i < BUTTON_COUNT; i++) {
		if ((pins & BIT(buttons[i].pin)) != 0) {
			sample_app_state.button_states[i] = !sample_app_state.button_states[i];
			int button_down = sample_app_state.button_states[i];
			int button_event_code = i | (button_down << 2);
			k_msgq_put(&button_event_q, &button_event_code, K_NO_WAIT);
		}
	}
}

static void init_buttons()
{
	for (int i = 0; i < BUTTON_COUNT; i++) {
		__ASSERT_NO_MSG(device_is_ready(buttons[i].port));
		int ret = gpio_pin_configure_dt(&buttons[i], GPIO_INPUT);
		__ASSERT_NO_MSG(ret == 0);
		ret = gpio_pin_interrupt_configure_dt(&buttons[i], GPIO_INT_EDGE_BOTH);
		__ASSERT_NO_MSG(ret == 0);
	}

	gpio_init_callback(&button_cb_data, button_pressed,
			   BIT(buttons[0].pin) | BIT(buttons[1].pin) | BIT(buttons[2].pin) | BIT(buttons[3].pin));
	int ret = gpio_add_callback(buttons[0].port, &button_cb_data);
	__ASSERT_NO_MSG(ret == 0);
}

/****************** BLE MIDI callbacks ******************/
static void ble_midi_available_cb(int is_available)
{
	gpio_pin_set_dt(&leds[LED_AVAILABLE], is_available);
	if (!is_available) {
		gpio_pin_set_dt(&leds[LED_RX_NON_SYSEX], 0);
		gpio_pin_set_dt(&leds[LED_RX_SYSEX], 0);
	}
	sample_app_state.ble_midi_is_available = is_available;
}

static void tx_done_cb()
{
	if (sample_app_state.sysex_tx_in_progress) {
		if (sample_app_state.sysex_tx_data_byte_count == SYSEX_TX_MESSAGE_SIZE) {
			ble_midi_tx_sysex_end();
			sample_app_state.sysex_tx_in_progress = 0;
		} else {
			uint8_t chunk[SYSEX_TX_MAX_CHUNK_SIZE];
			for (int i = 0; i < SYSEX_TX_MAX_CHUNK_SIZE; i++) {
				chunk[i] = (sample_app_state.sysex_tx_data_byte_count + i) % 128;
			}
			int num_bytes_left =
				SYSEX_TX_MESSAGE_SIZE - sample_app_state.sysex_tx_data_byte_count;
			int num_bytes_to_send = num_bytes_left < SYSEX_TX_MAX_CHUNK_SIZE
							? num_bytes_left
							: SYSEX_TX_MAX_CHUNK_SIZE;
			int num_bytes_sent = ble_midi_tx_sysex_data(chunk, num_bytes_to_send);
			sample_app_state.sysex_tx_data_byte_count += num_bytes_sent;
		}
	}
}

/** Called when a non-sysex message has been parsed */
static void ble_midi_message_cb(uint8_t *bytes, uint8_t num_bytes, uint16_t timestamp)
{
	uint8_t data[4] = {num_bytes, 0, 0, 0};
	for (int i = 0; i < num_bytes; i++) {
		data[i + 1] = bytes[i];
	}
	uint32_t num_bytes_written = ring_buf_put(&midi_msg_ringbuf, &data, 4);
	__ASSERT(num_bytes_written == 4, "Failed to write to MIDI msg ringbuf");
	int submit_rc = k_work_submit(&midi_msg_work);
}

/** Called when a sysex message starts */
static void ble_midi_sysex_start_cb(uint16_t timestamp)
{
	// printk("rx sysex start, t %d\n", timestamp);
}
/** Called when a sysex data byte has been received */
static void ble_midi_sysex_data_cb(uint8_t data_byte)
{
	// printk("rx sysex byte %02x\n", data_byte);
}
/** Called when a sysex message ends */
static void ble_midi_sysex_end_cb(uint16_t timestamp)
{
	// printk("rx sysex end, t %d\n", timestamp);
	gpio_pin_toggle_dt(&leds[LED_RX_SYSEX]);
}

#define DEVICE_NAME	CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN)};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BLE_MIDI_SERVICE_UUID),
};

void main(void)
{
	init_leds();
	init_buttons();

	uint32_t err = bt_enable(NULL);
	__ASSERT(err == 0, "bt_enable failed");

	/* Must be called after bt_enable */
	struct ble_midi_callbacks midi_callbacks = {.available_cb = ble_midi_available_cb,
						    .tx_done_cb = tx_done_cb,
						    .midi_message_cb = ble_midi_message_cb,
						    .sysex_start_cb = ble_midi_sysex_start_cb,
						    .sysex_data_cb = ble_midi_sysex_data_cb,
						    .sysex_end_cb = ble_midi_sysex_end_cb};
	ble_midi_init(&midi_callbacks);

	int ad_err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	printk("bt_le_adv_start %d\n", ad_err);

	while (1) {
		/* Poll button events */
		int button_event_code = 0;
		k_msgq_get(&button_event_q, &button_event_code, K_FOREVER);

		int button_idx = button_event_code & 0x3;
		int button_down = button_event_code >> 2;
		if (!sample_app_state.sysex_tx_in_progress) {
			if (button_idx == BUTTON_TX_NON_SYSEX) {
				uint8_t status_byte =
					button_down ? 0x90 : 0x80; // Note on or note off?
				uint8_t chord_msgs[][3] = {
					{status_byte, 0x48, 0x7f},
					{status_byte, 0x4c, 0x7f},
					{status_byte, 0x4f, 0x7f},
				};
				ble_midi_tx_msg(&(chord_msgs[0][0]));
				ble_midi_tx_msg(&(chord_msgs[1][0]));
				ble_midi_tx_msg(&(chord_msgs[2][0]));
			} else if (button_idx == BUTTON_TX_SYSEX_SHORT && button_down) {
				uint8_t data_bytes[10] = {
					0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
				};
				ble_midi_tx_sysex_start();
				ble_midi_tx_sysex_data(data_bytes, 10);
				ble_midi_tx_sysex_end();
			} else if (button_idx == BUTTON_TX_SYSEX_LONG && button_down) {
				/* Send the first byte of a sysex message that is too large
					to be sent at once. Use the tx done callback to send the
					next chunk repeatedly until done. */
				sample_app_state.sysex_tx_in_progress = 1;
				sample_app_state.sysex_tx_data_byte_count = 0;
				ble_midi_tx_sysex_start();
			} else if (button_idx == BUTTON_MANUAL_TX_FLUSH) {
				#ifdef CONFIG_BLE_MIDI_TX_MODE_MANUAL
				ble_midi_tx_buffered_msgs();
				#endif
			}
		}
	}
}