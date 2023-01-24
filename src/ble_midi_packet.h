#ifndef _BLE_MIDI_PACKET_H_
#define _BLE_MIDI_PACKET_H_

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

typedef void (*midi_message_cb_t)(uint8_t *bytes, uint8_t num_bytes, uint16_t timestamp);
typedef void (*sysex_data_cb_t)(uint8_t data_byte);
typedef void (*sysex_start_cb_t)(uint16_t timestamp);
typedef void (*sysex_end_cb_t)(uint16_t timestamp);

/**
 * Parses an entire BLE MIDI packet.
 */
int ble_midi_parse_packet(
    uint8_t *rx_packet,
    uint32_t num_bytes,
    midi_message_cb_t message_cb,
		sysex_start_cb_t sysex_start_cb,
    sysex_data_cb_t sysex_data_cb,
		sysex_end_cb_t sysex_end_cb
);

/**
 * Keeps track of the state when writing BLE MIDI packets.
 */
struct ble_midi_writer_t
{
	/* The packet to send. */
	uint8_t tx_buf[BLE_MIDI_TX_PACKET_MAX_SIZE];
	/* Current maximum packet size. Must not be greater than BLE_MIDI_TX_PACKET_MAX_SIZE */
	uint16_t tx_buf_max_size;
	/* Current packet size. Must not be greater than max_size. */
	uint16_t tx_buf_size;
	/* Status byte of the previous message */
	uint8_t prev_status_byte;
	/* Status byte of the previous non-realtime/common message. Used for running status. */
	uint8_t prev_running_status_byte;
	/* The timestamp of the previously added message. Used to determine if timestamp bytes
	   should be added to running status messages. */
	uint16_t prev_timestamp;
	/* Non-zero if sysex writing is in progress */
	uint8_t in_sysex_msg;
	/* Indicates if running status should be used. */
	int running_status_enabled;
	/* Indicates if note off messages should be represented as zero velocity note on messages. */
	int note_off_as_note_on;
};

/* Called once before using the writer. */
void ble_midi_writer_init(struct ble_midi_writer_t *writer,
		  										int running_status_enabled,
				  								int note_off_as_note_on);

/* Called before writing the next packet. */
void ble_midi_writer_reset(struct ble_midi_writer_t *writer);

/* Append a non-sysex MIDI message. */
int ble_midi_writer_add_msg(
    struct ble_midi_writer_t *writer,
    uint8_t *bytes,	/* 3 bytes, zero padded */
    uint16_t timestamp /* 13 bit, wrapped ms timestamp */
);

/* Append an entire sysex MIDI message. Fails if the message does not fit into the packet. */
int ble_midi_writer_add_sysex_msg(
    struct ble_midi_writer_t *writer,
    uint8_t *bytes,
    uint32_t num_bytes,
    uint16_t timestamp);

/* Start a sysex message, possibly spanning multiple messages. */
int ble_midi_writer_start_sysex_msg(struct ble_midi_writer_t *writer, uint16_t timestamp);

/**
 * Append data bytes to an ongoing sysex message, possibly spanning multiple packets.
 * The chunk must not contain sysex start/end and system real time status bytes.
 * Returns the number of bytes appended.
 */
int ble_midi_writer_add_sysex_data(
    struct ble_midi_writer_t *writer,
    uint8_t *data_bytes,
    uint32_t num_data_bytes,
    uint16_t timestamp);

/* End a sysex message, possibly spanning multiple messages. */
int ble_midi_writer_end_sysex_msg(struct ble_midi_writer_t *writer, uint16_t timestamp);

#endif