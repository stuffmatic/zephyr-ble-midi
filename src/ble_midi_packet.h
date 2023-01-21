#ifndef _BLE_MIDI_PACKET_H_
#define _BLE_MIDI_PACKET_H_

#include <stdint.h>

#define BLE_MIDI_PACKET_MAX_SIZE 247

struct ble_midi_packet {
  /* The payload to send. */
  uint8_t bytes[BLE_MIDI_PACKET_MAX_SIZE];
  /* Current maximum payload size. Must not be greater than BLE_MIDI_PACKET_MAX_SIZE */
  uint16_t max_size;
  /* Current payload size. Must not be greater than max_size. */
  uint16_t size;
  /* Status byte of the previous MIDI message. Used for running status. */
  uint8_t prev_status_byte;
  /* Indicates if the next running status message should be preceded by a
     timestamp byte, which is required for running status messages following
     system real time or system common messages (which don't cancel running status). */
  uint8_t next_rs_message_needs_timestamp;
  /* Indicates if we're in a running status sequence. Used to keep track of
     running status in the presence of system common or system real time messages.  */
  uint8_t is_running_status;
  /* The timestamp of the previously added message. Used to determine if timestamp bytes
     should be added to running status messages. */
  uint16_t prev_timestamp;
};

void ble_midi_packet_reset(struct ble_midi_packet *packet);

uint32_t ble_midi_packet_add_message(
  struct ble_midi_packet *packet,
  uint8_t* bytes, /* 3 bytes, zero padded */
  uint16_t timestamp, /* 13 bit, wrapped ms timestamp */
  int running_status_enabled
);

typedef void (*midi_message_cb)(uint8_t* bytes, uint8_t num_bytes, uint16_t timestamp);

typedef void (*sysex_cb)(uint8_t* bytes, uint8_t num_bytes, uint32_t sysex_ended);

void ble_midi_packet_parse(
  uint8_t* packet, uint32_t num_bytes, midi_message_cb message_cb, sysex_cb sysex_cb
);

#endif