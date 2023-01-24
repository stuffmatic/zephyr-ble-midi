#ifndef _ble_midi_packet_H_
#define _ble_midi_packet_H_

#include <stdint.h>

#define BLE_MIDI_SUCCESS 0
#define BLE_MIDI_ERROR_PACKET_FULL -1
#define BLE_MIDI_ERROR_IN_SYSEX_SEQUENCE -2
#define BLE_MIDI_ERROR_NOT_IN_SYSEX_SEQUENCE -3
#define BLE_MIDI_ERROR_INVALID_DATA_BYTE -4
#define BLE_MIDI_ERROR_INVALID_STATUS_BYTE -5
#define BLE_MIDI_ERROR_INVALID_MESSAGE -6
#define BLE_MIDI_ERROR_UNEXPECTED_END_OF_DATA -7
#define BLE_MIDI_ERROR_UNEXPECTED_DATA_BYTE -8
#define BLE_MIDI_ERROR_UNEXPECTED_STATUS_BYTE -9

#define BLE_MIDI_TX_PACKET_MAX_SIZE 247 // TODO: MTU - 3 according to spec

struct ble_midi_packet_t
{
	/* The payload to send. */
	uint8_t bytes[BLE_MIDI_TX_PACKET_MAX_SIZE];
	/* Current maximum payload size. Must not be greater than BLE_MIDI_TX_PACKET_MAX_SIZE */
	uint16_t max_size;
	/* Current payload size. Must not be greater than max_size. */
	uint16_t size;
	/* Status byte of the previous MIDI message. Used for running status. */
	uint8_t prev_status_byte;
	/* Indicates if the next running status message should be preceded by a
	   timestamp byte, which is required for running status messages following
	   system real time or system common messages (which don't cancel running status).
	   */
	uint8_t next_rs_message_needs_timestamp;
	/* Indicates if we're in a running status sequence. Used to keep track of
	   running status in the presence of system common or system real time messages.  */
	uint8_t is_running_status;
	/* The timestamp of the previously added message. Used to determine if timestamp bytes
	   should be added to running status messages. */
	uint16_t prev_timestamp;
	/* */
	uint8_t in_sysex_msg;
	int running_status_enabled;
	int note_off_as_note_on;
};

void ble_midi_packet_init(struct ble_midi_packet_t *packet,
		  										int running_status_enabled,
				  								int note_off_as_note_on);

void ble_midi_packet_reset(struct ble_midi_packet_t *packet);

/* Append a non-sysex MIDI message. */
int ble_midi_packet_add_msg(
    struct ble_midi_packet_t *packet,
    uint8_t *bytes,	/* 3 bytes, zero padded */
    uint16_t timestamp /* 13 bit, wrapped ms timestamp */
);

/* Append an entire sysex MIDI message. Fails if the message does not fit into the packet. */
int ble_midi_packet_add_sysex_msg(
    struct ble_midi_packet_t *packet,
    uint8_t *bytes,
    uint32_t num_bytes,
    uint16_t timestamp);

/* Start a sysex message, possibly spanning multiple messages. */
int ble_midi_packet_start_sysex_msg(struct ble_midi_packet_t *packet, uint16_t timestamp);

/**
 * Append data bytes to an ongoing sysex message, possibly spanning multiple packets.
 * The chunk must not contain sysex start/end and system real time status bytes.
 * Returns the number of bytes appended.
 */
int ble_midi_packet_add_sysex_data(
    struct ble_midi_packet_t *packet,
    uint8_t *data_bytes,
    uint32_t num_data_bytes,
    uint16_t timestamp);

/* End a sysex message, possibly spanning multiple messages. */
int ble_midi_packet_end_sysex_msg(struct ble_midi_packet_t *packet, uint16_t timestamp);

typedef void (*midi_message_cb_t)(uint8_t *bytes, uint8_t num_bytes, uint16_t timestamp);
typedef void (*sysex_data_cb_t)(uint8_t data_byte);
typedef void (*sysex_start_cb_t)(uint16_t timestamp);
typedef void (*sysex_end_cb_t)(uint16_t timestamp);

int ble_midi_parse_packet(
    uint8_t *packet,
    uint32_t num_bytes,
    midi_message_cb_t message_cb,
		sysex_start_cb_t sysex_start_cb,
    sysex_data_cb_t sysex_data_cb,
		sysex_end_cb_t sysex_end_cb
);

#endif