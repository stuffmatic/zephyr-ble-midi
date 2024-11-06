#ifndef _BLE_MIDI_PACKET_H_
#define _BLE_MIDI_PACKET_H_

#include <stdint.h>

enum ble_midi_packet_error_t {
	BLE_MIDI_PACKET_SUCCESS = 0,
	BLE_MIDI_PACKET_ERROR_PACKET_FULL = -1,
	BLE_MIDI_PACKET_ERROR_ALREADY_IN_SYSEX_SEQUENCE = -2,
	BLE_MIDI_PACKET_ERROR_NOT_IN_SYSEX_SEQUENCE = -3,
	BLE_MIDI_PACKET_ERROR_INVALID_DATA_BYTE = -4,
	BLE_MIDI_PACKET_ERROR_INVALID_STATUS_BYTE = -5,
	BLE_MIDI_PACKET_ERROR_INVALID_MESSAGE = -6,
	BLE_MIDI_PACKET_ERROR_UNEXPECTED_END_OF_DATA = -7,
	BLE_MIDI_PACKET_ERROR_UNEXPECTED_DATA_BYTE = -8,
	BLE_MIDI_PACKET_ERROR_UNEXPECTED_STATUS_BYTE = -9,
	BLE_MIDI_PACKET_ERROR_INVALID_HEADER_BYTE = -10
};

/** Called when a non-sysex message has been parsed */
typedef void (*ble_midi_message_cb_t)(uint8_t *bytes, uint8_t num_bytes, uint16_t timestamp);
/** Called when a sysex message starts */
typedef void (*ble_midi_sysex_start_cb_t)(uint16_t timestamp);
/** Called when a sysex data byte has been received */
typedef void (*ble_midi_sysex_data_cb_t)(uint8_t data_byte);
/** Called when a sysex message ends */
typedef void (*ble_midi_sysex_end_cb_t)(uint16_t timestamp);

/**
 * BLE MIDI packet parsing callbacks.
 * A callback that is set to NULL is ignored.
 */
struct ble_midi_parse_cb_t {
	ble_midi_message_cb_t midi_message_cb;
	ble_midi_sysex_data_cb_t sysex_data_cb;
	ble_midi_sysex_start_cb_t sysex_start_cb;
	ble_midi_sysex_end_cb_t sysex_end_cb;
};

/**
 * Parses an entire BLE MIDI packet.
 */
enum ble_midi_packet_error_t ble_midi_parse_packet(uint8_t *rx_buf, uint32_t rx_buf_size,
					    struct ble_midi_parse_cb_t *cb);

#ifdef CONFIG_BLE_MIDI_TX_PACKET_MAX_SIZE
#define BLE_MIDI_TX_PACKET_MAX_SIZE CONFIG_BLE_MIDI_TX_PACKET_MAX_SIZE
#else
#define BLE_MIDI_TX_PACKET_MAX_SIZE 64
#endif

/**
 * Keeps track of the state when writing BLE MIDI packets.
 */
struct ble_midi_writer_t {
	/* The packet to send. */
	uint8_t tx_buf[BLE_MIDI_TX_PACKET_MAX_SIZE];
	/* Current maximum packet size. Must not be greater than BLE_MIDI_TX_PACKET_MAX_SIZE */
	uint16_t tx_buf_max_size;
	/* Current packet size. Must not be greater than tx_buf_max_size. */
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
	/* Indicates if note off messages should be represented as zero velocity note on messages.
	 */
	int note_off_as_note_on;
};

/* Called once before using the writer. */
void ble_midi_writer_init(struct ble_midi_writer_t *writer, int running_status_enabled,
			  int note_off_as_note_on);

/* Called after finishing writing a packet. */
void ble_midi_writer_reset(struct ble_midi_writer_t *writer);

/* Append a non-sysex MIDI message. */
enum ble_midi_packet_error_t ble_midi_writer_add_msg(struct ble_midi_writer_t *writer,
					      uint8_t *bytes,	 /* 3 bytes, zero padded */
					      uint16_t timestamp /* 13 bit, wrapped ms timestamp */
);

/* Append an entire sysex MIDI message. Fails if the message does not fit into the packet. */
enum ble_midi_packet_error_t ble_midi_writer_add_sysex_msg(struct ble_midi_writer_t *writer,
						    uint8_t *bytes, uint32_t num_bytes,
						    uint16_t timestamp);

/* Start a sysex message, possibly spanning multiple messages. */
enum ble_midi_packet_error_t ble_midi_writer_start_sysex_msg(struct ble_midi_writer_t *writer,
						      uint16_t timestamp);

/**
 * Append data bytes to an ongoing sysex message, possibly spanning multiple packets.
 * The chunk must not contain sysex start/end and system real time status bytes.
 * Returns a non-negative value on success, indicating the number of bytes written to the packet.
 * Negative return values correspond to error codes from the ble_midi_packet_error_t enum.
 */
int ble_midi_writer_add_sysex_data(struct ble_midi_writer_t *writer, uint8_t *data_bytes,
				   uint32_t num_data_bytes, uint16_t timestamp);

/* End a sysex message, possibly spanning multiple messages. */
enum ble_midi_packet_error_t ble_midi_writer_end_sysex_msg(struct ble_midi_writer_t *writer,
						    uint16_t timestamp);

#endif