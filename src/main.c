#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/ring_buffer.h>
#include <ble_midi/ble_midi.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ble_midi_sample);

static void log_sysex_transfer_time(int is_tx, int num_bytes, int time_ms) {
	float bytes_per_s = time_ms == 0 ? 0 : (float)num_bytes / (0.001 * time_ms);
	LOG_INF("sysex %s done | %d bytes in %d ms | %d bytes/s", is_tx ? "tx" : "rx",
		num_bytes, (int)time_ms, (int)bytes_per_s);
}

/************************ App state ************************/
K_MSGQ_DEFINE(button_event_q, sizeof(uint8_t), 128, 4);

#define SYSEX_TX_MESSAGE_SIZE	20000
#define SYSEX_TX_MAX_CHUNK_SIZE 130
static uint8_t sysex_tx_chunk[SYSEX_TX_MAX_CHUNK_SIZE];

struct sample_app_state_t {
	int is_connected;
	int ble_midi_is_ready;
	int sysex_rx_data_byte_count;
	int sysex_tx_data_byte_count;
	int64_t sysex_tx_start_time_ms;
	int sysex_tx_in_progress;
	int64_t sysex_rx_start_time_ms;
	int sysex_rx_in_progress;
	int button_states[4];
};

static struct sample_app_state_t sample_app_state = {
							 .is_connected = 0,
							 .ble_midi_is_ready = 0,
						     .sysex_tx_data_byte_count = 0,
						     .sysex_tx_in_progress = 0,
							 .sysex_rx_in_progress = 0,
							 .sysex_tx_start_time_ms = 0,
							 .sysex_tx_start_time_ms = 0,
						     .sysex_rx_data_byte_count = 0,
						     .button_states = {0, 0, 0, 0}};

/************************ LEDs ************************/

#define LED_COUNT	     4
/* Turn on this LED when the BLE MIDI is perihperal is connected */
#define LED_CONNECTED	 0
/* Turn on this LED when the BLE MIDI service is ready. */
#define LED_READY	     1
/* Toggle this LED when receiving sysex messages */
#define LED_RX_SYSEX	 2
/* Toggle this LED when receiving non-sysex messages */
#define LED_RX_NON_SYSEX 3

static const struct gpio_dt_spec leds[LED_COUNT] = {GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios),
						    GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios),
						    GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios),
							GPIO_DT_SPEC_GET(DT_ALIAS(led3), gpios)};

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
		LOG_INF("MIDI rx | %02x %02x %02x | %d bytes", msg_bytes[0], msg_bytes[1], msg_bytes[2], msg_byte_count);
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
static void ble_midi_ready_cb(ble_midi_ready_state_t state)
{
	int is_connected = state != BLE_MIDI_STATE_NOT_CONNECTED;
	int is_ready = state == BLE_MIDI_STATE_READY;
	
	gpio_pin_set_dt(&leds[LED_CONNECTED], is_connected);
	gpio_pin_set_dt(&leds[LED_READY], is_ready);

	if (!is_ready) {
		gpio_pin_set_dt(&leds[LED_RX_NON_SYSEX], 0);
		gpio_pin_set_dt(&leds[LED_RX_SYSEX], 0);
	}

	sample_app_state.is_connected = is_connected;
	sample_app_state.ble_midi_is_ready = is_ready;
}

static void tx_done_cb()
{
	if (sample_app_state.sysex_tx_in_progress) {
		if (sample_app_state.sysex_tx_data_byte_count == SYSEX_TX_MESSAGE_SIZE) {
			u_int64_t dt_ms = k_uptime_get() - sample_app_state.sysex_tx_start_time_ms;
			log_sysex_transfer_time(1, SYSEX_TX_MESSAGE_SIZE + 2, dt_ms);
			ble_midi_tx_sysex_end();
			sample_app_state.sysex_tx_in_progress = 0;
		} else {
#ifndef CONFIG_BLE_MIDI_TX_MODE_SINGLE_MSG
			// Fill tx fifo. TODO: fix contents
			while (true) {
				int num_bytes_left_to_send = SYSEX_TX_MESSAGE_SIZE - sample_app_state.sysex_tx_data_byte_count;
				int num_bytes_to_send = num_bytes_left_to_send > SYSEX_TX_MAX_CHUNK_SIZE ? SYSEX_TX_MAX_CHUNK_SIZE : num_bytes_left_to_send;
				if (num_bytes_to_send > 0) {
					int result = ble_midi_tx_sysex_data(sysex_tx_chunk, num_bytes_to_send);
					if (result == BLE_MIDI_TX_FIFO_FULL) {
						break;
					}
					sample_app_state.sysex_tx_data_byte_count += result;
				} else {
					break;
				}
			}
#else
			for (int i = 0; i < SYSEX_TX_MAX_CHUNK_SIZE; i++) {
				sysex_tx_chunk[i] = (sample_app_state.sysex_tx_data_byte_count + i) % 128;
			}
			int num_bytes_left =
				SYSEX_TX_MESSAGE_SIZE - sample_app_state.sysex_tx_data_byte_count;
			int num_bytes_to_send = num_bytes_left < SYSEX_TX_MAX_CHUNK_SIZE
							? num_bytes_left
							: SYSEX_TX_MAX_CHUNK_SIZE;
			int num_bytes_sent = ble_midi_tx_sysex_data(sysex_tx_chunk, num_bytes_to_send);
			sample_app_state.sysex_tx_data_byte_count += num_bytes_sent;
#endif
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
	sample_app_state.sysex_rx_start_time_ms = k_uptime_get();
	// printk("rx sysex start, t %d\n", timestamp);
}
/** Called when a sysex data byte has been received */
static void ble_midi_sysex_data_cb(uint8_t data_byte)
{
	sample_app_state.sysex_rx_data_byte_count += 1;
}
/** Called when a sysex message ends */
static void ble_midi_sysex_end_cb(uint16_t timestamp)
{
	u_int64_t dt_ms = k_uptime_get() - sample_app_state.sysex_rx_start_time_ms;
	log_sysex_transfer_time(0, sample_app_state.sysex_rx_data_byte_count + 2, dt_ms);
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

int main(void)
{
	init_leds();
	init_buttons();

	for (int i = 0; i < SYSEX_TX_MAX_CHUNK_SIZE; i++) {
		sysex_tx_chunk[i] = i % 128;
	}

	uint32_t err = bt_enable(NULL);
	__ASSERT(err == 0, "bt_enable failed");

	/* Must be called after bt_enable */
	struct ble_midi_callbacks midi_callbacks = {.ready_cb = ble_midi_ready_cb,
						    .tx_done_cb = tx_done_cb,
						    .midi_message_cb = ble_midi_message_cb,
						    .sysex_start_cb = ble_midi_sysex_start_cb,
						    .sysex_data_cb = ble_midi_sysex_data_cb,
						    .sysex_end_cb = ble_midi_sysex_end_cb};
	ble_midi_init(&midi_callbacks);

	int ad_err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	// printk("bt_le_adv_start %d\n", ad_err);

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
				sample_app_state.sysex_tx_start_time_ms = k_uptime_get();
				ble_midi_tx_sysex_start();
			} else if (button_idx == BUTTON_MANUAL_TX_FLUSH) {
				#ifdef CONFIG_BLE_MIDI_TX_MODE_MANUAL
				ble_midi_tx_flush();
				#endif
			}
		}
	}
}