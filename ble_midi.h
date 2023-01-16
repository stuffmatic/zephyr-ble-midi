#ifndef _BLE_MIDI_H_
#define _BLE_MIDI_H_

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

typedef void (*ble_midi_message_cb)(uint8_t* bytes, uint8_t num_bytes, uint16_t timestamp);
typedef void (*ble_midi_sysex_cb)(uint8_t* bytes, uint8_t num_bytes, uint32_t sysex_ended);
typedef void (*ble_midi_available_cb)(uint32_t is_available);
struct ble_midi_callbacks {
	ble_midi_message_cb midi_message_cb;
	ble_midi_sysex_cb sysex_cb;
	ble_midi_available_cb available_cb;
};

void ble_midi_parse_packet(uint8_t* packet, uint32_t num_bytes, struct ble_midi_callbacks *callbacks);

#define BLE_MIDI_PACKET_MAX_SIZE 128
struct ble_midi_out_packet {
  uint8_t bytes[BLE_MIDI_PACKET_MAX_SIZE];
  uint8_t running_status;
  uint32_t size;
  uint32_t max_size;
};

void ble_midi_out_packet_reset(struct ble_midi_out_packet *packet);
uint32_t ble_midi_out_packet_add_message(
  struct ble_midi_out_packet *packet,
  uint8_t* bytes, /* 3 bytes, zero padded */
  uint16_t timestamp /* 13 bit, wrapped ms timestamp */
);

#endif