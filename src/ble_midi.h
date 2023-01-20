#ifndef _BLE_MIDI_H_
#define _BLE_MIDI_H_

#include <zephyr/zephyr.h>
#include <zephyr/bluetooth/uuid.h>

#define BLE_MIDI_SERVICE_UUID \
	BT_UUID_128_ENCODE(0x03B80E5A, 0xEDE8, 0x4B33, 0xA751, 0x6CE34EC4C700)
#define BLE_MIDI_CHAR_UUID \
	BT_UUID_128_ENCODE(0x7772E5DB, 0x3868, 0x4112, 0xA1A9, 0xF2669D106BF3)

typedef void (*ble_midi_available_cb)(uint32_t is_available);
typedef void (*ble_midi_message_cb)(uint8_t* bytes, uint8_t num_bytes, uint16_t timestamp);
typedef void (*ble_midi_sysex_cb)(uint8_t* bytes, uint8_t num_bytes, uint32_t sysex_ended);

struct ble_midi_callbacks {
	ble_midi_message_cb midi_message_cb;
	ble_midi_sysex_cb sysex_cb;
  ble_midi_available_cb available_cb;
};

/**
 * Call once.
 */
void ble_midi_init(struct ble_midi_callbacks *callbacks);

/** 3 zero padded bytes. */
int ble_midi_tx(uint8_t* bytes);


#endif