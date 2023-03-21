#ifndef _BLE_MIDI_H_
#define _BLE_MIDI_H_

#include <zephyr/bluetooth/uuid.h>

#define BLE_MIDI_SERVICE_UUID \
	BT_UUID_128_ENCODE(0x03B80E5A, 0xEDE8, 0x4B33, 0xA751, 0x6CE34EC4C700)
#define BLE_MIDI_CHAR_UUID \
	BT_UUID_128_ENCODE(0x7772E5DB, 0x3868, 0x4112, 0xA1A9, 0xF2669D106BF3)

typedef void (*ble_midi_tx_available_cb_t)();
typedef void (*ble_midi_available_cb_t)(uint32_t is_available);
/** Called when a non-sysex message has been parsed */
typedef void (*ble_midi_message_cb_t)(uint8_t *bytes, uint8_t num_bytes, uint16_t timestamp);
/** Called when a sysex message starts */
typedef void (*ble_midi_sysex_start_cb_t)(uint16_t timestamp);
/** Called when a sysex data byte has been received */
typedef void (*ble_midi_sysex_data_cb_t)(uint8_t data_byte);
/** Called when a sysex message ends */
typedef void (*ble_midi_sysex_end_cb_t)(uint16_t timestamp);

/** Callbacks set to NULL are ignored. */
struct ble_midi_callbacks
{
	ble_midi_available_cb_t available_cb;
	ble_midi_tx_available_cb_t tx_available_cb;
	ble_midi_message_cb_t midi_message_cb;
	ble_midi_sysex_start_cb_t sysex_start_cb;
	ble_midi_sysex_data_cb_t sysex_data_cb;
	ble_midi_sysex_end_cb_t sysex_end_cb;
};

/**
 * Call once.
 */
void ble_midi_init(struct ble_midi_callbacks *callbacks);

/** 
 * Sends a non-sysex MIDI message.
 * @param bytes A buffer of length 3 containing the message bytes to send. 
 */
int ble_midi_tx_msg(uint8_t *bytes);

int ble_midi_tx_sysex_start();
int ble_midi_tx_sysex_data(uint8_t *bytes, int num_bytes);
int ble_midi_tx_sysex_end();

#endif
