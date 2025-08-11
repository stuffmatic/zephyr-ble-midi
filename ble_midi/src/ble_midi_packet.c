#include "ble_midi_packet.h"

/* Returns the 6 high bits of a 13 bit BLE MIDI timestamp */
inline static uint8_t timestamp_high(uint16_t timestamp_ms)
{
	return 0x3f & (timestamp_ms >> 7);
}

/* Returns the 7 low bits of a 13 bit BLE MIDI timestamp */
inline static uint8_t timestamp_low(uint16_t timestamp_ms)
{
	return 0x7f & timestamp_ms;
}

inline static uint8_t header_byte(uint16_t timestamp_ms)
{
	return 0x80 | timestamp_high(timestamp_ms);
}

inline static uint8_t timestamp_byte(uint16_t timestamp_ms)
{
	return 0x80 | timestamp_low(timestamp_ms);
}

inline static uint16_t timestamp_ms(uint8_t timestamp_high, uint8_t timestamp_byte)
{
	return (timestamp_high << 7) | (0x7f & timestamp_byte);
}

inline static uint32_t is_status_byte(uint8_t byte)
{
	return byte >= 0x80;
}

inline static uint32_t is_data_byte(uint8_t byte)
{
	return byte < 0x80;
}

static uint32_t is_realtime_message(uint8_t status_byte)
{
	switch (status_byte) {
	case 0xf8: /* Timing Clock */
	case 0xfa: /* Start */
	case 0xfb: /* Continue */
	case 0xfc: /* Stop */
	case 0xfe: /* Active Sensing */
	case 0xff: /* System Reset */
		return 1;
	}

	return 0;
}

static uint32_t is_system_common_message(uint8_t status_byte)
{
	switch (status_byte) {
	case 0xf1: /* MIDI Time Code Quarter Frame */
	case 0xf3: /* Song Select */
	case 0xf2: /* Song Position Pointer */
	case 0xf6: /* Tune request */
		return 1;
	}

	return 0;
}

static uint32_t is_channel_message(uint8_t status_byte)
{
	uint8_t high_nibble = status_byte >> 4;
	switch (high_nibble) {
	case 0x8: /* Note off */
	case 0x9: /* Note on */
	case 0xa: /* Poly KeyPress */
	case 0xb: /* Control Change */
	case 0xe: /* PitchBend Change */
	case 0xc: /* Program Change */
	case 0xd: /* Channel Pressure */
		return 1;
	}

	return 0;
}

static uint8_t message_size(uint8_t status_byte)
{
	/* Channel message? */
	uint8_t high_nibble = status_byte >> 4;
	switch (high_nibble) {
	case 0x8: /* Note off */
	case 0x9: /* Note on */
	case 0xa: /* Poly KeyPress */
	case 0xb: /* Control Change */
	case 0xe: /* PitchBend Change */
		/* Three byte channel Voice Message */
		return 3;
	case 0xc: /* Program Change */
	case 0xd: /* Channel Pressure */
		/* Two byte channel Voice Message */
		return 2;
	default:
		break;
	}

	/* Real time message? */
	switch (status_byte) {
	case 0xf8: /* Timing Clock */
	case 0xfa: /* Start */
	case 0xfb: /* Continue */
	case 0xfc: /* Stop */
	case 0xfe: /* Active Sensing */
	case 0xff: /* System Reset */
		return 1;
	default:
		break;
	}

	/* System common message? */
	switch (status_byte) {
	case 0xf1: /* MIDI Time Code Quarter Frame */
	case 0xf3: /* Song Select */
		/* 2 byte System Common message */
		return 2;
	case 0xf2: /* Song Position Pointer */
		/* 3 byte System Common message */
		return 3;
	case 0xf6: /* Tune request */
		/* Single-byte System Common Message */
		return 1;
	default:
		break;
	}

	return 0;
}

void ble_midi_writer_init(struct ble_midi_writer_t *writer, int running_status_enabled,
			  int note_off_as_note_on)
{
	writer->tx_buf_max_size = BLE_MIDI_TX_PACKET_MAX_SIZE;
	writer->tx_buf_size = 0;
	writer->prev_running_status_byte = 0;
	writer->prev_status_byte = 0;
	writer->prev_timestamp = 0;
	writer->in_sysex_msg = 0;
	writer->note_off_as_note_on = note_off_as_note_on;
	writer->running_status_enabled = running_status_enabled;
}

void ble_midi_writer_reset(struct ble_midi_writer_t *writer)
{
	writer->tx_buf_size = 0;
	writer->prev_timestamp = 0;

	/* The end of a BLE packet cancels running status. */
	writer->prev_running_status_byte = 0;
	writer->prev_status_byte = 0;
}

enum ble_midi_packet_error_t ble_midi_writer_add_msg(struct ble_midi_writer_t *writer,
					      uint8_t *message_bytes, uint16_t timestamp)
{
	/* First, handle special case of a system real time message in a sysex message */
	if (writer->in_sysex_msg) {
		if (is_realtime_message(message_bytes[0])) {
			if (writer->tx_buf_max_size - writer->tx_buf_size >= 2) {
				writer->tx_buf[writer->tx_buf_size++] = timestamp_byte(timestamp);
				writer->tx_buf[writer->tx_buf_size++] = message_bytes[0];
				return BLE_MIDI_PACKET_SUCCESS;
			} else {
				return BLE_MIDI_PACKET_ERROR_PACKET_FULL;
			}
		} else {
			/* Only real time messages allowed in sysex data */
			return BLE_MIDI_PACKET_ERROR_INVALID_STATUS_BYTE;
		}
	}

	/* The following code appends a MIDI message to the BLE MIDI packet in two steps:
	   1. Compute a maximum of 5 bytes to append:
		 - packet header?
		 - message timestamp?
		 - 1-3 midi bytes
	   2. Append the bytes if there is room in the packet.
	*/
	uint8_t status_byte = message_bytes[0];
	uint8_t num_message_bytes = message_size(status_byte);
	if (num_message_bytes == 0) {
		return BLE_MIDI_PACKET_ERROR_INVALID_STATUS_BYTE;
	}

	uint8_t data_bytes[2] = {message_bytes[1], message_bytes[2]};

	if (!is_data_byte(data_bytes[0]) || !is_data_byte(data_bytes[1])) {
		return BLE_MIDI_PACKET_ERROR_INVALID_DATA_BYTE;
	}

	/* Use running status? */
	uint8_t prev_running_status_byte = writer->prev_running_status_byte;

	if (!writer->running_status_enabled) {
		prev_running_status_byte = 0;
	} else if (is_channel_message(status_byte)) {
		if ((status_byte >> 4) == 0x8 && writer->note_off_as_note_on) {
			/* This is a note off message. Represent it as a note
				on with velocity 0 to increase running status efficiency. */
			status_byte = 0x90 | (status_byte & 0xf);
			data_bytes[1] = 0; /* Velocity 0 */
		}

		prev_running_status_byte = status_byte;
	} else if (is_realtime_message(status_byte) || is_system_common_message(status_byte)) {
		/*
		   From the BLE MIDI spec:
		   System Common and System Real-Time messages do not cancel Running Status if
		   interspersed between Running Status MIDI messages. However, a timestamp byte
		   must precede the Running Status MIDI message that follows.
		*/
	} else {
		/* Cancel running status */
		prev_running_status_byte = 0;
	}

	uint8_t bytes_to_append[5] = {0, 0, 0, 0, 0};
	uint8_t num_bytes_to_append = 0;

	/* Add packet header byte? */
	if (writer->tx_buf_size == 0) {
		bytes_to_append[num_bytes_to_append] = header_byte(timestamp);
		num_bytes_to_append++;
	}

	/* Skip the message timestamp if we're in a running status sequence, the timestamp
	   hasn't changed and we're dealing with a channel message that was not preceded
	   by a system realtime/common message. */
	int is_running_status = status_byte == writer->prev_running_status_byte;
	int prev_msg_is_sys_rt_or_cmn = is_system_common_message(writer->prev_status_byte) ||
					is_realtime_message(writer->prev_status_byte);
	int timestamp_has_changed = timestamp != writer->prev_timestamp;
	int skip_msg_timestamp = is_running_status && !timestamp_has_changed &&
				 is_channel_message(status_byte) && !prev_msg_is_sys_rt_or_cmn;
	if (!skip_msg_timestamp) {
		bytes_to_append[num_bytes_to_append] = timestamp_byte(timestamp);
		num_bytes_to_append++;
	}

	/* Skip the status byte if we're in a running status sequence and this is a channel message
	 */
	int skip_status_byte = is_running_status && is_channel_message(status_byte);
	if (!skip_status_byte) {
		bytes_to_append[num_bytes_to_append] = status_byte;
		num_bytes_to_append++;
	}

	/* Add data bytes */
	uint8_t num_data_bytes = num_message_bytes - 1;
	for (int i = 0; i < num_data_bytes; i++) {
		bytes_to_append[num_bytes_to_append] = data_bytes[i];
		num_bytes_to_append++;
	}

	/* Append bytes to the BLE packet */
	if (writer->tx_buf_size + num_bytes_to_append <= writer->tx_buf_max_size) {
		for (int i = 0; i < num_bytes_to_append; i++) {
			writer->tx_buf[writer->tx_buf_size] = bytes_to_append[i];
			writer->tx_buf_size++;
		}

		/* Update packet state */
		writer->prev_status_byte = status_byte;
		writer->prev_running_status_byte = prev_running_status_byte;
		writer->prev_timestamp = timestamp;

		return BLE_MIDI_PACKET_SUCCESS;
	} else {
		return BLE_MIDI_PACKET_ERROR_PACKET_FULL;
	}
}

enum ble_midi_packet_error_t ble_midi_writer_add_sysex_msg(struct ble_midi_writer_t *writer,
						    uint8_t *bytes, uint32_t num_bytes,
						    uint16_t timestamp)
{
	/* The sysex message should have zero or more data bytes
	   between the start byte 0xf0 and end byte 0xf7. */
	if (num_bytes < 2) {
		return BLE_MIDI_PACKET_ERROR_INVALID_MESSAGE;
	}
	if (bytes[0] != 0xf0 || bytes[num_bytes - 1] != 0xf7) {
		return BLE_MIDI_PACKET_ERROR_INVALID_STATUS_BYTE;
	}
	for (int i = 1; i < num_bytes - 1; i++) {
		if (!is_data_byte(bytes[i])) {
			return BLE_MIDI_PACKET_ERROR_INVALID_DATA_BYTE;
		}
	}

	/* See if there's room in the packet */
	int num_bytes_to_append =
		num_bytes + 2; /* +2 timestamp bytes for sysex start/end status bytes */
	int add_packet_header = writer->tx_buf_size == 0;
	if (add_packet_header) {
		num_bytes_to_append++; /* Empty packet. A packet header byte is needed. */
	}
	if (writer->tx_buf_max_size - writer->tx_buf_size < num_bytes_to_append) {
		return BLE_MIDI_PACKET_ERROR_PACKET_FULL;
	}

	/* Add packet header? */
	if (add_packet_header) {
		writer->tx_buf[writer->tx_buf_size++] = header_byte(timestamp);
	}

	/* Add message bytes */
	for (int i = 0; i < num_bytes; i++) {
		if (i == 0 || i == num_bytes - 1) {
			/* Timestamp bytes for sysex start and end status bytes */
			writer->tx_buf[writer->tx_buf_size++] = timestamp_byte(timestamp);
		}
		writer->tx_buf[writer->tx_buf_size++] = bytes[i];
	}

	/* Cancel running status */
	writer->prev_running_status_byte = 0;

	return BLE_MIDI_PACKET_SUCCESS;
}

static enum ble_midi_packet_error_t append_status_byte_with_timestamp(struct ble_midi_writer_t *writer,
							       uint16_t timestamp, uint8_t status)
{
	int num_bytes_to_append = 2;
	int add_packet_header = writer->tx_buf_size == 0;
	if (add_packet_header) {
		/* Empty packet. Also add packet header. */
		num_bytes_to_append++;
	}
	if (writer->tx_buf_max_size - writer->tx_buf_size < num_bytes_to_append) {
		return BLE_MIDI_PACKET_ERROR_PACKET_FULL;
	}

	/* If we made it here, there's room in the packet for the sysex start message. */
	if (add_packet_header) {
		writer->tx_buf[writer->tx_buf_size++] = header_byte(timestamp);
	}

	writer->tx_buf[writer->tx_buf_size++] = timestamp_byte(timestamp);
	writer->tx_buf[writer->tx_buf_size++] = status;

	return BLE_MIDI_PACKET_SUCCESS;
}

enum ble_midi_packet_error_t ble_midi_writer_start_sysex_msg(struct ble_midi_writer_t *writer,
							     uint16_t timestamp)
{
	if (writer->in_sysex_msg) {
		return BLE_MIDI_PACKET_ERROR_ALREADY_IN_SYSEX_SEQUENCE;
	}
	enum ble_midi_packet_error_t result = append_status_byte_with_timestamp(writer, timestamp, 0xf0);
	if (result == BLE_MIDI_PACKET_SUCCESS) {
		writer->in_sysex_msg = 1;
		writer->prev_running_status_byte = 0;
	}
	return result;
}

enum ble_midi_packet_error_t ble_midi_writer_end_sysex_msg(struct ble_midi_writer_t *writer,
						    uint16_t timestamp)
{
	if (!writer->in_sysex_msg) {
		return BLE_MIDI_PACKET_ERROR_NOT_IN_SYSEX_SEQUENCE;
	}
	enum ble_midi_packet_error_t result = append_status_byte_with_timestamp(writer, timestamp, 0xf7);
	if (result == BLE_MIDI_PACKET_SUCCESS) {
		writer->in_sysex_msg = 0;
	}
	return result;
}

int ble_midi_writer_add_sysex_data(struct ble_midi_writer_t *writer, const uint8_t *data_bytes,
				   				   uint32_t num_data_bytes, uint16_t timestamp)
{
	// TODO: the writer->tx_buf_size > 0 check was added to make this code play nice with 
	// the new tx queue structure. This should probably be refactored somehow.
	if (!writer->in_sysex_msg && writer->tx_buf_size > 0) { 
		return BLE_MIDI_PACKET_ERROR_NOT_IN_SYSEX_SEQUENCE;
	}

	/* Validate data bytes */
	for (int i = 0; i < num_data_bytes; i++) {
		if (!is_data_byte(data_bytes[i])) {
			return BLE_MIDI_PACKET_ERROR_INVALID_DATA_BYTE;
		}
	}

	/* Add packet header? */
	if (writer->tx_buf_size == 0) {
		if (writer->tx_buf_max_size < 1) {
			/* Pathological case: not enough room for a packet header. */
			return BLE_MIDI_PACKET_ERROR_PACKET_FULL;
		}
		writer->tx_buf[writer->tx_buf_size++] = header_byte(timestamp);
	}

	/* Add data bytes until end of data or end of packet. */
	int num_bytes_left = writer->tx_buf_max_size - writer->tx_buf_size;
	int num_data_bytes_to_add =
		num_bytes_left < num_data_bytes ? num_bytes_left : num_data_bytes;
	for (int i = 0; i < num_data_bytes_to_add; i++) {
		writer->tx_buf[writer->tx_buf_size++] = data_bytes[i];
	}

	/* Return the number of data bytes added */
	return num_data_bytes_to_add;
}

struct ble_midi_parser_t {
	uint8_t in_sysex_msg;
	uint8_t running_status_byte;
	uint8_t prev_timestamp_byte;
	uint32_t read_pos;
	uint8_t *rx_buf;
	uint32_t rx_buf_size;
};

static enum ble_midi_packet_error_t ble_midi_parser_read(struct ble_midi_parser_t *parser, uint8_t *result,
						  uint32_t num_bytes)
{
	if (parser->read_pos <= parser->rx_buf_size - num_bytes) {
		for (int i = 0; i < num_bytes; i++) {
			result[i] = parser->rx_buf[parser->read_pos];
			parser->read_pos++;
		}
		return BLE_MIDI_PACKET_SUCCESS;
	} else {
		return BLE_MIDI_PACKET_ERROR_UNEXPECTED_END_OF_DATA;
	}
}

static enum ble_midi_packet_error_t is_sysex_continuation(struct ble_midi_parser_t *parser, int *is_sysex)
{
	*is_sysex = 0;

	/* A sysex continuation packet starts with
	  - a packet header
	  - 0 or more system realtime messages with timestamps
	  - a data byte */
	uint8_t header = 0;
	if (ble_midi_parser_read(parser, &header, 1)) {
		return BLE_MIDI_PACKET_ERROR_UNEXPECTED_END_OF_DATA;
	}

	while (parser->read_pos < parser->rx_buf_size) {
		uint8_t byte_1 = 0;
		if (ble_midi_parser_read(parser, &byte_1, 1)) {
			return BLE_MIDI_PACKET_ERROR_UNEXPECTED_END_OF_DATA;
		}
		if (is_data_byte(byte_1)) {
			*is_sysex = 1;
			return BLE_MIDI_PACKET_SUCCESS;
		}
		uint8_t byte_2 = 0;
		if (ble_midi_parser_read(parser, &byte_2, 1)) {
			return BLE_MIDI_PACKET_ERROR_UNEXPECTED_END_OF_DATA;
		}

		if (is_data_byte(byte_2)) {
			return BLE_MIDI_PACKET_ERROR_UNEXPECTED_DATA_BYTE;
		}
		if (is_realtime_message(byte_2)) {
			/* A system real time message. Keep going.  */
		} else {
			/* A status byte other than system real time.
			Not a sysex continuation packet */
			return BLE_MIDI_PACKET_SUCCESS;
		}
	}

	return BLE_MIDI_PACKET_SUCCESS;
}

enum ble_midi_packet_error_t ble_midi_parse_packet(uint8_t *rx_buf, uint32_t rx_buf_size,
					    struct ble_midi_parse_cb_t *cb)
{
	/* A parser instance keeping track of the parsing state. */
	struct ble_midi_parser_t parser = {.in_sysex_msg = 0,
					   .prev_timestamp_byte = 0,
					   .rx_buf = rx_buf,
					   .rx_buf_size = rx_buf_size,
					   .read_pos = 0,
					   .running_status_byte = 0};

	/* First, check if this is a sysex continuation packet. */
	int is_sysex_cont = 0;
	int res = is_sysex_continuation(&parser, &is_sysex_cont);
	if (res != BLE_MIDI_PACKET_SUCCESS) {
		return res;
	}
	parser.in_sysex_msg = is_sysex_cont;

	/* Reset read position and do the actual parsing */
	parser.read_pos = 0;

	/* Start actual parsing by reading packet header */
	uint8_t packet_header = 0;
	if (ble_midi_parser_read(&parser, &packet_header, 1)) {
		return BLE_MIDI_PACKET_ERROR_UNEXPECTED_END_OF_DATA;
	}
	uint8_t timestamp_high_bits = 0x3f & packet_header;

	if (!is_status_byte(packet_header)) {
		return BLE_MIDI_PACKET_ERROR_INVALID_HEADER_BYTE;
	}

	/* Parse messages */
	while (parser.read_pos < parser.rx_buf_size) {
		if (parser.in_sysex_msg) {
			/* Only data bytes or system real time with timestamp allowed in sysex
			 * messages */
			uint8_t byte = 0;
			if (ble_midi_parser_read(&parser, &byte, 1)) {
				return BLE_MIDI_PACKET_ERROR_UNEXPECTED_END_OF_DATA;
			}

			if (is_data_byte(byte)) {
				if (cb->sysex_data_cb) {
					cb->sysex_data_cb(byte);
				}
			} else {
				/* Assume we just read a timestamp byte. It may be followed by a
				  system real time status or sysex end */
				uint8_t status = 0;
				if (ble_midi_parser_read(&parser, &status, 1)) {
					return BLE_MIDI_PACKET_ERROR_UNEXPECTED_END_OF_DATA;
				}
				if (is_realtime_message(status)) {
					if (cb->midi_message_cb) {
						cb->midi_message_cb(
							&status, 1,
							timestamp_ms(timestamp_high_bits, byte));
					}
				} else if (status == 0xf7) {
					/* End of sysex */
					parser.in_sysex_msg = 0;
					if (cb->sysex_end_cb) {
						cb->sysex_end_cb(
							timestamp_ms(timestamp_high_bits, byte));
					}
				} else {
					/* Invalid status byte. Bail. */
					return BLE_MIDI_PACKET_ERROR_INVALID_STATUS_BYTE;
				}
			}
		} else {
			/*
			Expecting one of these cases:
			Case 1. data byte(s) (running status without timestamp)
			Case 2. timestamp byte + data byte(s) (running status with timestamp)
			Case 3. timestamp byte + status byte + zero or more data bytes (complete
			MIDI message with timestamp)
			*/
			uint8_t byte_0 = 0;
			if (ble_midi_parser_read(&parser, &byte_0, 1)) {
				return BLE_MIDI_PACKET_ERROR_UNEXPECTED_END_OF_DATA;
			}
			if (is_data_byte(byte_0)) {
				/* Case 1 - running status without timestamp byte.
				Read the rest of the data bytes. */
				uint8_t status_byte = parser.running_status_byte;
				uint8_t message_bytes[3] = {status_byte, byte_0, 0};
				uint8_t num_message_bytes = message_size(status_byte);
				uint8_t num_additional_data_bytes = num_message_bytes - 2;
				if (ble_midi_parser_read(&parser, &message_bytes[2],
							 num_additional_data_bytes)) {
					return BLE_MIDI_PACKET_ERROR_UNEXPECTED_END_OF_DATA;
				}
				if (cb->midi_message_cb) {
					cb->midi_message_cb(
						message_bytes, num_message_bytes,
						timestamp_ms(timestamp_high_bits,
							     parser.prev_timestamp_byte));
				}
			} else {
				/* Assume byte_0 is a timestamp byte. At least one byte should
				 * follow. */
				uint8_t byte_1 = 0;
				if (parser.prev_timestamp_byte > byte_0) {
					timestamp_high_bits = (timestamp_high_bits + 1) % 0x40;
				}
				parser.prev_timestamp_byte = byte_0;
				if (ble_midi_parser_read(&parser, &byte_1, 1)) {
					return BLE_MIDI_PACKET_ERROR_UNEXPECTED_END_OF_DATA;
				}

				if (is_data_byte(byte_1)) {
					/* Case 2 - running status with timestamp */
					uint8_t status_byte = parser.running_status_byte;
					uint8_t message_bytes[3] = {status_byte, byte_1, 0};
					uint8_t num_message_bytes = message_size(status_byte);
					uint8_t num_additional_data_bytes = num_message_bytes - 2;
					if (ble_midi_parser_read(&parser, &message_bytes[2],
								 num_additional_data_bytes)) {
						return BLE_MIDI_PACKET_ERROR_UNEXPECTED_END_OF_DATA;
					}
					if (cb->midi_message_cb) {
						cb->midi_message_cb(
							message_bytes, num_message_bytes,
							timestamp_ms(timestamp_high_bits, byte_0));
					}
				} else {
					/* Case 3 - full midi message with timestamp */

					/* byte_1 is the status byte of this message. update running
					 * status */
					if (is_channel_message(byte_1)) {
						parser.running_status_byte = byte_1;
					} else if (!is_realtime_message(byte_1) &&
						   !is_system_common_message(byte_1)) {
						parser.running_status_byte = 0;
					}

					if (byte_1 == 0xf0) {
						/* Sysex start */
						parser.in_sysex_msg = 1;
						if (cb->sysex_start_cb) {
							cb->sysex_start_cb(timestamp_ms(
								timestamp_high_bits, byte_0));
						}
					} else {
						/* Non-sysex message */
						uint8_t msg_size = message_size(byte_1);
						if (msg_size > 0) {
							uint8_t num_data_bytes = msg_size - 1;
							uint8_t message_bytes[3] = {byte_1, 0, 0};
							if (ble_midi_parser_read(&parser,
										 &message_bytes[1],
										 num_data_bytes)) {
								return BLE_MIDI_PACKET_ERROR_UNEXPECTED_END_OF_DATA;
							}
							if (cb->midi_message_cb) {
								cb->midi_message_cb(
									message_bytes,
									1 + num_data_bytes,
									timestamp_ms(
										timestamp_high_bits,
										byte_0));
							}
						} else {
							return BLE_MIDI_PACKET_ERROR_INVALID_STATUS_BYTE;
						}
					}
				}
			}
		}
	}

	return BLE_MIDI_PACKET_SUCCESS;
}