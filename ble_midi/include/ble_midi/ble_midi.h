#ifndef _BLE_MIDI_H_
#define _BLE_MIDI_H_

#include <zephyr/bluetooth/uuid.h>

/** UUID of the BLE MIDI service */
#define BLE_MIDI_SERVICE_UUID BT_UUID_128_ENCODE(0x03B80E5A, 0xEDE8, 0x4B33, 0xA751, 0x6CE34EC4C700)
/** UUID of the MIDI data I/O characteristic */
#define BLE_MIDI_CHAR_UUID    BT_UUID_128_ENCODE(0x7772E5DB, 0x3868, 0x4112, 0xA1A9, 0xF2669D106BF3)

enum ble_midi_error_t {
	BLE_MIDI_SUCCESS = 0,
	BLE_MIDI_ALREADY_INITIALIZED = -100,
	BLE_MIDI_TX_FIFO_FULL = -101,
	BLE_MIDI_INVALID_ARGUMENT = -102,
	BLE_MIDI_SERVICE_REGISTRATION_ERROR = -103,
	BLE_MIDI_NOT_CONNECTED = -104
};

typedef enum  {
	/* Not connected */
	BLE_MIDI_STATE_NOT_CONNECTED = 0,
	/* Connected but not ready to communicate. */
	BLE_MIDI_STATE_CONNECTED,
	/* Connected and ready to communicate. */
	BLE_MIDI_STATE_READY
} ble_midi_ready_state_t ;

/** 
 * Used to signal if the BLE MIDI service is ready. 
 * Only attempt to transmit data if state is BLE_MIDI_READY. 
 **/
typedef void (*ble_midi_ready_cb_t)(ble_midi_ready_state_t state);
/** Called when a BLE MIDI packet has just been sent. */
typedef void (*ble_midi_tx_done_cb_t)();
/** Called when a non-sysex message has been parsed */
typedef void (*ble_midi_message_cb_t)(uint8_t *bytes, uint8_t num_bytes, uint16_t timestamp);
/** Called when a sysex message starts */
typedef void (*ble_midi_sysex_start_cb_t)(uint16_t timestamp);
/** Called when a sysex data byte has been received */
typedef void (*ble_midi_sysex_data_cb_t)(uint8_t data_byte);
/** Called when a sysex message ends */
typedef void (*ble_midi_sysex_end_cb_t)(uint16_t timestamp);

/** Callbacks set to NULL are ignored. */
struct ble_midi_callbacks {
	ble_midi_ready_cb_t ready_cb;
	ble_midi_tx_done_cb_t tx_done_cb;
	ble_midi_message_cb_t midi_message_cb;
	ble_midi_sysex_start_cb_t sysex_start_cb;
	ble_midi_sysex_data_cb_t sysex_data_cb;
	ble_midi_sysex_end_cb_t sysex_end_cb;
};

/**
 * Initializes the BLE MIDI service. This should only be called once.
 */
enum ble_midi_error_t ble_midi_init(struct ble_midi_callbacks *callbacks);

/**
 * Sends a non-sysex MIDI message.
 * @param bytes A zero padded buffer of length 3 containing the message bytes to send.
 * @return 0 on success or a non-zero number on failure.
 */
enum ble_midi_error_t ble_midi_tx_msg(uint8_t *bytes);

/**
 * Start transmission of a sysex message.
 * @return 0 on success or a non-zero number on failure.
 */
enum ble_midi_error_t ble_midi_tx_sysex_start();

/**
 * Transmit sysex data bytes.
 * @param bytes The data bytes to send. Must have the high bit set to 0.
 * @param num_bytes The number of data bytes to send.
 * @return On success, the number of bytes written. A negative ble_midi_error_t value on error.
 */
int ble_midi_tx_sysex_data(uint8_t *bytes, int num_bytes);

/**
 * End transmission of a sysex message.
 * @return 0 on success or a non-zero number on failure.
 */
enum ble_midi_error_t ble_midi_tx_sysex_end();

#ifdef CONFIG_BLE_MIDI_TX_MODE_MANUAL
/**
 * Send buffered MIDI messages, if any.
 */
void ble_midi_tx_flush();
#endif // CONFIG_BLE_MIDI_TX_MODE_MANUAL

#ifdef CONFIG_BT_GATT_DYNAMIC_DB
/**
 * 
 */
int ble_midi_service_register();
/**
 * 
 */
int ble_midi_service_unregister();
#endif // CONFIG_BT_GATT_DYNAMIC_DB

#endif
