#ifndef _BLE_MIDI_PACKET_H_
#define _BLE_MIDI_PACKET_H_

#include <stdint.h>
// TODO: only define these when testing
//typedef unsigned char uint8_t;
//typedef unsigned short uint16_t;
//typedef unsigned int uint32_t;

typedef void (*midi_message_cb)(uint8_t* bytes, uint8_t num_bytes, uint16_t timestamp);
typedef void (*sysex_cb)(uint8_t* bytes, uint8_t num_bytes, uint32_t sysex_ended);

void ble_midi_packet_parse(
  uint8_t* packet, uint32_t num_bytes, midi_message_cb message_cb, sysex_cb sysex_cb
);

#define BLE_MIDI_PACKET_MAX_SIZE 128
struct ble_midi_packet {
  uint8_t bytes[BLE_MIDI_PACKET_MAX_SIZE];
  uint8_t running_status;
  uint32_t size;
  uint32_t max_size;
};

void ble_midi_packet_reset(struct ble_midi_packet *packet);
uint32_t ble_midi_packet_add_message(
  struct ble_midi_packet *packet,
  uint8_t* bytes, /* 3 bytes, zero padded */
  uint16_t timestamp, /* 13 bit, wrapped ms timestamp */
  int running_status_enabled
);

#endif