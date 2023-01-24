#include "ble_midi_packet.h"

uint16_t timestamp_ms(uint8_t packet_header, uint8_t message_timestamp)
{
	// TODO
	return 1234;
}

uint32_t is_status_byte(uint8_t byte)
{
	return byte >= 0x80;
}

uint32_t is_data_byte(uint8_t byte)
{
	return byte < 0x80;
}

uint32_t is_sysex_message(uint8_t status_byte)
{
	switch (status_byte) {
	case 0xf7: /* Sysex start */
	case 0xf0: /* Sysex end */
		return 1;
	}

	return 0;
}

uint32_t is_realtime_message(uint8_t status_byte)
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

uint32_t is_system_common_message(uint8_t status_byte)
{
	/* System common message? */
	switch (status_byte) {
	case 0xf1: /* MIDI Time Code Quarter Frame */
	case 0xf3: /* Song Select */
	case 0xf2: /* Song Position Pointer */
	case 0xf6: /* Tune request */
		return 1;
	}

	return 0;
}

uint32_t is_channel_message(uint8_t status_byte)
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

uint8_t message_size(uint8_t status_byte)
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
	}

	return 0;
}

uint8_t packet_header(uint16_t timestamp)
{
	return 0x80 | ((timestamp >> 6) & 0xf);
}

uint8_t message_timestamp(uint16_t timestamp)
{
	return 0x80 | (0x7f & timestamp);
}

void ble_midi_packet_init(struct ble_midi_packet_t *packet,
											    int running_status_enabled,
				  								int note_off_as_note_on)
{
	packet->max_size = 0;
	packet->size = 0;
	packet->prev_status_byte = 0;
	packet->next_rs_message_needs_timestamp = 0;
	packet->is_running_status = 0;
	packet->prev_timestamp = 0;
	packet->in_sysex_msg = 0;
	packet->note_off_as_note_on = note_off_as_note_on;
	packet->running_status_enabled = running_status_enabled;
}

void ble_midi_packet_reset(struct ble_midi_packet_t *packet)
{
	packet->size = 0;
	packet->prev_timestamp = 0;

	/* The end of a BLE packet cancels running status. */
	packet->prev_status_byte = 0;
	packet->next_rs_message_needs_timestamp = 0;
	packet->is_running_status = 0;
}

int ble_midi_packet_add_msg(struct ble_midi_packet_t *packet,
														uint8_t *message_bytes, uint16_t timestamp)
{
	/* First, handle special case of system real time message in a sysex message */
	if (packet->in_sysex_msg) {
		if (is_realtime_message(message_bytes[0])) {
			if (packet->max_size - packet->size >= 2)
			{
				packet->bytes[packet->size] = message_timestamp(timestamp);
				packet->size++;
				packet->bytes[packet->size] = message_bytes[0];
				packet->size++;
				return BLE_MIDI_SUCCESS;
			}
			else {
				return BLE_MIDI_ERROR_PACKET_FULL;
			}
		}
		else {
			/* Only real time messages allowed in sysex data */
			return BLE_MIDI_ERROR_INVALID_STATUS_BYTE;
		}
	}

	/* The following code appends a MIDI message to the BLE MIDI packet in two steps:
	   1. Compute a maximum of 5 bytes to append
	     - packet header?
	     - message timestamp?
	     - 1-3 midi bytes
	   2. Append the bytes if there is room in the packet.
	*/
	uint8_t status_byte = message_bytes[0];
	uint8_t num_message_bytes = message_size(status_byte);
	if (num_message_bytes == 0) {
		return BLE_MIDI_ERROR_INVALID_STATUS_BYTE;
	}

	uint8_t data_bytes[2] = {message_bytes[1], message_bytes[2]};
	for (int i = 0; i < num_message_bytes - 1; i++) {
		if (!is_data_byte(data_bytes[i])) {
			return BLE_MIDI_ERROR_INVALID_DATA_BYTE;
		}
	}

	/* Use running status? */
	uint8_t prev_status_byte = packet->prev_status_byte;
	uint8_t next_rs_message_needs_timestamp = packet->next_rs_message_needs_timestamp;
	uint8_t is_running_status = packet->is_running_status;
	if (!packet->running_status_enabled) {
		is_running_status = 0;
	}
	else if (is_channel_message(status_byte)) {
		if ((status_byte >> 4) == 0x8 && packet->note_off_as_note_on) {
			/* This is a note off message. Represent it as a note
			    on with velocity 0 to increase running status efficiency. */
			status_byte = 0x90 | (status_byte & 0xf);
			data_bytes[1] = 0; /* Velocity 0 */
		}
		if (status_byte == prev_status_byte) {
			/* Start or continue running status sequence */
			is_running_status = 1;
		}
		else {
			/* Cancel running status */
			is_running_status = 0;
		}
		prev_status_byte = status_byte;
	}
	else if (is_realtime_message(status_byte) || is_system_common_message(status_byte)) {
		/*
		   From the BLE MIDI spec:
		   System Common and System Real-Time messages do not cancel Running Status if
		   interspersed between Running Status MIDI messages. However, a timestamp byte
		   must precede the Running Status MIDI message that follows.
		*/
		if (is_running_status) {
			next_rs_message_needs_timestamp = 1;
		}
	}
	else {
		/* Cancel running status */
		is_running_status = 0;
		prev_status_byte = 0;
	}

	uint8_t bytes_to_append[5] = {0, 0, 0, 0, 0 };
	uint8_t num_bytes_to_append = 0;

	/* Add packet header byte? */
	if (packet->size == 0) {
		bytes_to_append[num_bytes_to_append] = packet_header(timestamp);
		num_bytes_to_append++;
	}

	/* Skip the message timestamp if we're in a running status sequence, the timestamp
	   hasn't changed and we're dealing with a channel message that was not preceded
	   by a system realtime/common message. */
	uint16_t prev_timestamp = packet->prev_timestamp;
	int skip_msg_timestamp = is_running_status && timestamp == prev_timestamp && is_channel_message(status_byte) && !next_rs_message_needs_timestamp;
	if (!skip_msg_timestamp) {
		bytes_to_append[num_bytes_to_append] = message_timestamp(timestamp);
		num_bytes_to_append++;
	}

	if (next_rs_message_needs_timestamp && is_channel_message(status_byte)) {
		/* Just added a required timestamp byte for a running status channel
		   message following a system common/real time message. */
		next_rs_message_needs_timestamp = 0;
	}

	/* Skip the status byte if we're in a running status sequence and this is a channel message */
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
	if (packet->size + num_bytes_to_append <= packet->max_size) {
		for (int i = 0; i < num_bytes_to_append; i++) {
			packet->bytes[packet->size] = bytes_to_append[i];
			packet->size++;
		}

		/* Update packet state */
		packet->prev_status_byte = prev_status_byte;
		packet->prev_timestamp = timestamp;
		packet->next_rs_message_needs_timestamp = next_rs_message_needs_timestamp;
		packet->is_running_status = is_running_status;

		return BLE_MIDI_SUCCESS;
	}
	else {
		return BLE_MIDI_ERROR_PACKET_FULL;
	}
}

int ble_midi_packet_add_sysex_msg(
    struct ble_midi_packet_t *packet,
    uint8_t *bytes,
    uint32_t num_bytes,
    uint16_t timestamp
)
{
	/* Validate sysex message */
	if (num_bytes < 2) {
		return BLE_MIDI_ERROR_INVALID_MESSAGE;
	}
	for (int i = 0; i < num_bytes; i++) {
		if (i == 0) {
			if (bytes[i] != 0xf7) {
				return BLE_MIDI_ERROR_INVALID_STATUS_BYTE;
			}
		}
		else if (i == num_bytes - 1) {
			if (bytes[i] != 0xf0) {
				return BLE_MIDI_ERROR_INVALID_STATUS_BYTE;
			}
		}
		else if (!is_data_byte(bytes[i])) {
			return BLE_MIDI_ERROR_INVALID_DATA_BYTE;
		}
	}

	/* See if there's room in the packet */
	int num_bytes_to_append = num_bytes + 2; /* +2 for timestamp bytes */
	if (packet->size == 0) {
		num_bytes_to_append++;
	}
	if (packet->max_size - packet->size < num_bytes_to_append) {
		return BLE_MIDI_ERROR_PACKET_FULL;
	}

	/* Add packet header? */
	if (packet->size == 0) {
		packet->bytes[packet->size] = packet_header(timestamp);
		packet->size++;
	}

	/* Add message bytes */
	for (int i = 0; i < num_bytes; i++) {
		if (i == 0 || i == num_bytes - 1) {
			packet->bytes[packet->size] = message_timestamp(timestamp);
			packet->size++;
		}
		packet->bytes[packet->size] = bytes[i];
		packet->size++;
	}

	/* Cancel running status */
	packet->is_running_status = 0;
	packet->prev_status_byte = 0;

	return BLE_MIDI_SUCCESS;
}

int append_status_byte_with_timestamp(struct ble_midi_packet_t *packet, uint16_t timestamp, uint8_t status)
{
	int num_bytes_to_append = 2;
	if (packet->size == 0) {
		/* Also add packet header */
		num_bytes_to_append++;
	}
	if (packet->max_size - packet->size < num_bytes_to_append) {
		return BLE_MIDI_ERROR_PACKET_FULL;
	}

	/* If we made it here, there's room in the packet for the sysex start message. */
	if (packet->size == 0) {
		packet->bytes[packet->size] = packet_header(timestamp);
		packet->size++;
	}

	packet->bytes[packet->size] = message_timestamp(timestamp);
	packet->size++;
	packet->bytes[packet->size] = status;
	packet->size++;

	return BLE_MIDI_SUCCESS;
}

int ble_midi_packet_start_sysex_msg(struct ble_midi_packet_t *packet, uint16_t timestamp)
{
	if (packet->in_sysex_msg) {
		return BLE_MIDI_ERROR_IN_SYSEX_SEQUENCE;
	}
	int result = append_status_byte_with_timestamp(packet, timestamp, 0xf7);
	if (result == BLE_MIDI_SUCCESS) {
		packet->in_sysex_msg = 1;
		packet->is_running_status = 0;
		packet->prev_status_byte = 0;
	}
	return result;
}

int ble_midi_packet_end_sysex_msg(struct ble_midi_packet_t *packet, uint16_t timestamp)
{
	if (!packet->in_sysex_msg) {
		return BLE_MIDI_ERROR_NOT_IN_SYSEX_SEQUENCE;
	}
	int result = append_status_byte_with_timestamp(packet, timestamp, 0xf0);
	if (result == BLE_MIDI_SUCCESS) {
		packet->in_sysex_msg = 0;
	}
	return result;
}

int ble_midi_packet_add_sysex_data(struct ble_midi_packet_t *packet, uint8_t *data_bytes, uint32_t num_data_bytes, uint16_t timestamp)
{
	if (!packet->in_sysex_msg) {
		return BLE_MIDI_ERROR_NOT_IN_SYSEX_SEQUENCE;
	}

	/* Validate data bytes */
	for (int i = 0; i < num_data_bytes; i++)
	{
		if (!is_data_byte(data_bytes[i])) {
			return BLE_MIDI_ERROR_INVALID_DATA_BYTE;
		}
	}

	/* Add packet header */
	if (packet->size == 0) {
		if (packet->max_size < 1) {
			return BLE_MIDI_ERROR_PACKET_FULL;
		}
		packet->bytes[packet->size] = packet_header(timestamp);
		packet->size++;
	}

	/* Add data bytes until end of data or end of packet. */
	int num_bytes_left = packet->max_size - packet->size;
	int num_data_bytes_to_add = num_bytes_left < num_data_bytes ? num_bytes_left : num_data_bytes;
	for (int i = 0; i < num_data_bytes_to_add; i++) {
		packet->bytes[packet->size] = data_bytes[i];
		packet->size++;
	}

	/* Return the number of data bytes added */
	return num_data_bytes_to_add;
}

struct ble_midi_parser_t {
	uint8_t in_sysex_msg;
	uint8_t running_status_byte;
	uint16_t prev_timestamp_byte;
	uint32_t read_pos;
	uint8_t *packet;
	uint32_t packet_size;
};

static int ble_midi_parser_read(struct ble_midi_parser_t *parser, uint8_t* result, uint32_t num_bytes) {
	if (parser->read_pos <= parser->packet_size - num_bytes) {
		for (int i = 0; i < num_bytes; i++) {
			result[i] = parser->packet[parser->read_pos];
			parser->read_pos++;
		}
		return 0;
	} else {
		return 1;
	}
}

static int is_sysex_continuation(struct ble_midi_parser_t *parser, int *is_sysex)
{
	*is_sysex = 0;

	/* A sysex continuation packet starts with
	  - a packet header
		- 0 or more system realtime messages with timestamps
		- a data byte */
	uint8_t header = 0;
	if (ble_midi_parser_read(parser, &header, 1)) {
		return BLE_MIDI_ERROR_UNEXPECTED_END_OF_DATA;
	}

	while (parser->read_pos < parser->packet_size) {
		uint8_t byte_1 = 0;
		if (ble_midi_parser_read(parser, &byte_1, 1)) {
			return BLE_MIDI_ERROR_UNEXPECTED_END_OF_DATA;
		}
		if (is_data_byte(byte_1)) {
			*is_sysex = 1;
			return BLE_MIDI_SUCCESS;
		}
		uint8_t byte_2 = 0;
		if (ble_midi_parser_read(parser, &byte_2, 1)) {
			return BLE_MIDI_ERROR_UNEXPECTED_END_OF_DATA;
		}

		if (is_data_byte(byte_2)) {
			return BLE_MIDI_ERROR_UNEXPECTED_DATA_BYTE;
		}
		if (is_realtime_message(byte_2)) {
			/* A system real time message. Keep going.  */
		} else {
			/* A status byte other than system real time.
			Not a sysex continuation packet */
			return BLE_MIDI_SUCCESS;
		}
	}

	/* If we made it here, the packet consists of only real time messages
	and could be either a sysex continuation or a "regular" packet. Assume the latter. */
	*is_sysex = 0;

	return BLE_MIDI_SUCCESS;
}

int ble_midi_parse_packet(
    uint8_t *packet,
    uint32_t packet_size,
    midi_message_cb_t message_cb,
    sysex_start_cb_t sysex_start_cb,
    sysex_data_cb_t sysex_data_cb,
		sysex_end_cb_t sysex_end_cb
)
{
	/* Create a parser instance keeping track of the current state. */
	struct ble_midi_parser_t parser = {
		.in_sysex_msg = 0,
		.prev_timestamp_byte = 0,
		.packet = packet,
		.packet_size = packet_size,
		.read_pos = 0,
		.running_status_byte = 0
	};

	/* First, check if this is a sysex continuation packet, then start the actual parsing. */
	int is_sysex_cont = 0;
	int res = is_sysex_continuation(&parser, &is_sysex_cont);
	if (res != BLE_MIDI_SUCCESS) {
		return res;
	}
	parser.in_sysex_msg = is_sysex_cont;
	parser.read_pos = 0;

	/* Start actual parsing by reading packet header */
	uint8_t packet_header = 0;
	if (ble_midi_parser_read(&parser, &packet_header, 1)) {
		return BLE_MIDI_ERROR_UNEXPECTED_END_OF_DATA;
	}

	/* Parse messages */
	while (parser.read_pos < parser.packet_size) {
		if (parser.in_sysex_msg) {
			/* Only data bytes or system real time with timestamp allowed in sysex messages */
			uint8_t byte = 0;
			if (ble_midi_parser_read(&parser, &byte, 1)) {
				return BLE_MIDI_ERROR_UNEXPECTED_END_OF_DATA;
			}

			if (is_data_byte(byte)) {
				/*  */
				sysex_data_cb(byte);
			} else {
				/* Assume we just read a timestamp byte. It may be followed by a
				  system real time status or sysex end */
				uint8_t status = 0;
				if (ble_midi_parser_read(&parser, &status, 1)) {
					return BLE_MIDI_ERROR_UNEXPECTED_END_OF_DATA;
				}
				if (is_realtime_message(status)) {
					message_cb(&status, 1, timestamp_ms(packet_header, byte));
				} else if (status == 0xf0) {
					/* End of sysex */
					parser.in_sysex_msg = 0;
					sysex_end_cb(timestamp_ms(packet_header, byte));
				} else {
					/* Invalid status byte. Ignore. */
				}
			}
		}
		else {
			/*
			Expecting one of these cases:
			Case 1. data byte(s) (running status without timestamp)
			Case 2. timestamp byte + data byte(s) (running status with timestamp)
			Case 3. timestamp byte + status byte + zero or more data bytes (complete MIDI message with timestamp)
			*/
			uint8_t byte_0 = 0;
			if (ble_midi_parser_read(&parser, &byte_0, 1)) {
				return BLE_MIDI_ERROR_UNEXPECTED_END_OF_DATA;
			}
			if (is_data_byte(byte_0)) {
				/* Case 1 - running status without timestamp byte.
				Read the rest of the data bytes. */
				uint8_t status_byte = parser.running_status_byte;
				uint8_t message_bytes[3] = {status_byte, byte_0, 0};
				uint8_t num_message_bytes = message_size(status_byte);
				uint8_t num_additional_data_bytes = num_message_bytes - 2;
				if (ble_midi_parser_read(&parser, &message_bytes[2], num_additional_data_bytes)) {
					return BLE_MIDI_ERROR_UNEXPECTED_END_OF_DATA;
				}
				message_cb(message_bytes, num_message_bytes, timestamp_ms(packet_header, parser.prev_timestamp_byte));
			} else {
				/* Assume byte_0 is a timestamp byte. At least one byte should follow. */
				uint8_t byte_1 = 0;
				parser.prev_timestamp_byte = byte_0;
				if (ble_midi_parser_read(&parser, &byte_1, 1)) {
					return BLE_MIDI_ERROR_UNEXPECTED_END_OF_DATA;
				}

				if (is_data_byte(byte_1)) {
					/* Case 2 - running status with timestamp */
					uint8_t status_byte = parser.running_status_byte;
					uint8_t message_bytes[3] = {status_byte, byte_1, 0};
					uint8_t num_message_bytes = message_size(status_byte);
					uint8_t num_additional_data_bytes = num_message_bytes - 2;
					if (ble_midi_parser_read(&parser, &message_bytes[2], num_additional_data_bytes)) {
						return BLE_MIDI_ERROR_UNEXPECTED_END_OF_DATA;
					}
					message_cb(message_bytes, num_message_bytes, timestamp_ms(packet_header, byte_0));
				} else {
					/* Case 3 */
					/* byte_1 is the status byte of this message. update running status */
					if (is_channel_message(byte_1)) {
						parser.running_status_byte = byte_1;
					} else if (!is_realtime_message(byte_1) && !is_system_common_message(byte_1)) {
						parser.running_status_byte = 0;
					}

					if (byte_1 == 0xf7) {
						/* Sysex start */
						parser.in_sysex_msg = 1;
						sysex_start_cb(timestamp_ms(packet_header, byte_0));
					}
					else {
						uint8_t num_data_bytes = message_size(byte_1) - 1;
						if (num_data_bytes >= 0) {
							uint8_t message_bytes[3] = {byte_1, 0, 0};
							if (ble_midi_parser_read(&parser, &message_bytes[1], num_data_bytes)) {
								return BLE_MIDI_ERROR_UNEXPECTED_END_OF_DATA;
							}
							message_cb(message_bytes, 1 + num_data_bytes, timestamp_ms(packet_header, byte_0)); // TODO: timestamp
						} else {
							/* Ignore. TODO: how to handle? */
						}
					}
				}
			}
		}
	}

	return BLE_MIDI_SUCCESS;
}