#ifndef _BLE_MIDI_H_
#define _BLE_MIDI_H_

#include <zephyr/zephyr.h>
#include <zephyr/bluetooth/uuid.h>
#include "ble_midi_packet.h"

#define BLE_MIDI_SERVICE_UUID \
	BT_UUID_128_ENCODE(0x03B80E5A, 0xEDE8, 0x4B33, 0xA751, 0x6CE34EC4C700)
#define BLE_MIDI_CHAR_UUID \
	BT_UUID_128_ENCODE(0x7772E5DB, 0x3868, 0x4112, 0xA1A9, 0xF2669D106BF3)

typedef void (*ble_midi_tx_available_cb_t)();
typedef void (*ble_midi_available_cb_t)(uint32_t is_available);

/** Callbacks set to NULL are ignored. */
struct ble_midi_callbacks {
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

/** Sends a non-sysex MIDI message. 3 zero padded bytes. */
int ble_midi_tx_msg(uint8_t* bytes);
int ble_midi_tx_sysex_msg(uint8_t* bytes, int num_bytes);

#ifdef CONFIG_BLE_MIDI_NRF_BATCH_TX
int ble_midi_tx_sysex_start();
int ble_midi_tx_sysex_data(uint8_t* bytes, int num_bytes);
int ble_midi_tx_sysex_end();
#endif

#endif