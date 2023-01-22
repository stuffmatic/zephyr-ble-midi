#include "ble_midi_packet.h"
/*

byte_equals(index, value)
byte_has_high_bit_set(index)

if (header >= 0x80) {
  if (!byte_has_high_bit_set(1) || byte_equals(1, value)) {

  }
  else {
    Sysex start or continuation. Once a SysEx transfer has begun, only System Real-Time messages are allowed to precede its completion as follows:
    Only status bytes allowed are
    * timestamp byte + F0 (end of sysex)
    * timestamp byte + system real time
      (A System Real-Time message interrupting a yet unterminated SysEx message must be
      preceded by its own timestamp byte)
  }
} else {
  error: invalid header
}

*/

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
	switch (status_byte)
	{
	case 0xf7: /* Sysex start */
	case 0xf0: /* Sysex end */
		return 1;
	}

	return 0;
}

uint32_t is_realtime_message(uint8_t status_byte)
{
	switch (status_byte)
	{
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
	switch (status_byte)
	{
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
	switch (high_nibble)
	{
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
	switch (high_nibble)
	{
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
	switch (status_byte)
	{
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
	switch (status_byte)
	{
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

void ble_midi_packet_parse(
    uint8_t *packet, uint32_t num_bytes, midi_message_cb message_cb, sysex_cb sysex_cb)
{
	if (num_bytes < 2)
	{
		/* Empty packet */
		return;
	}

	uint8_t header = packet[0];
	if (!(header & 0x80))
	{
		/* Invalid header byte */
		return;
	}
	if (header & 0x40)
	{
		/* Warning: expected reserved byte to be zero */
	}
	uint8_t timestamp_high = header & 0x3f;

	uint8_t second_byte = packet[1];
	if (second_byte & 0x80)
	{
	}
	else
	{
		/* Sysex continuation packet */
	}

	uint8_t running_status = 0;
	uint32_t read_idx = 1;
	while (read_idx < num_bytes)
	{
		break;
	}
}

void ble_midi_packet_init(struct ble_midi_packet *packet)
{
	packet->max_size = 0;
	packet->size = 0;
	packet->prev_status_byte = 0;
	packet->next_rs_message_needs_timestamp = 0;
	packet->is_running_status = 0;
	packet->prev_timestamp = 0;
	packet->in_sysex_sequence = 0;
}

void ble_midi_packet_reset(struct ble_midi_packet *packet)
{
	packet->size = 0;
	packet->prev_timestamp = 0;

	/* The end of a BLE packet cancels running status. */
	packet->prev_status_byte = 0;
	packet->next_rs_message_needs_timestamp = 0;
	packet->is_running_status = 0;
}

int ble_midi_packet_append_msg(struct ble_midi_packet *packet, uint8_t *message_bytes, uint16_t timestamp, int running_status_enabled)
{
	/* First, handle special case of system real time message in a sysex sequence */
	if (packet->in_sysex_sequence)
	{
		if (is_realtime_message(message_bytes[0]))
		{
			if (packet->max_size - packet->size >= 2)
			{
				packet->bytes[packet->size] = message_timestamp(timestamp);
				packet->size++;
				packet->bytes[packet->size] = message_bytes[0];
				packet->size++;
				return BLE_MIDI_SUCCESS;
			}
			else
			{
				return BLE_MIDI_ERROR_PACKET_FULL;
			}
		}
		else
		{
			/* Only real time messages allowed in sysex data */
			return BLE_MIDI_ERROR_INVALID_STATUS_BYTE;
		}
	}

	/* The following code appends a MIDI message to the BLE MIDI packet in two steps:
	   1. Compute a maximum of 5 bytes to append
	     - packet header
	     - message timestamp
	     - 3 midi bytes
	   2. Append the bytes if there is room in the packet.
	*/

	uint8_t status_byte = message_bytes[0];
	uint8_t num_message_bytes = message_size(status_byte);
	if (num_message_bytes == 0)
	{
		return BLE_MIDI_ERROR_INVALID_STATUS_BYTE;
	}

	uint8_t data_bytes[2] = {message_bytes[1], message_bytes[2]};
	for (int i = 0; i < num_message_bytes - 1; i++)
	{
		if (!is_data_byte(data_bytes[i]))
		{
			return BLE_MIDI_ERROR_INVALID_DATA_BYTE;
		}
	}

	/* Use running status? */
	uint8_t prev_status_byte = packet->prev_status_byte;
	uint8_t next_rs_message_needs_timestamp = packet->next_rs_message_needs_timestamp;
	uint8_t is_running_status = packet->is_running_status;
	if (!running_status_enabled) {
		is_running_status = 0;
	}
	else if (is_channel_message(status_byte))
	{
		if ((status_byte >> 4) == 0x8)
		{
			/* This is a note off message. Represent it as a note
			    on with velocity 0 to increase running status efficiency. */
			status_byte = 0x90 | (status_byte & 0xf);
			data_bytes[1] = 0; /* Velocity 0 */
		}
		if (status_byte == prev_status_byte)
		{
			/* Start or continue running status sequence */
			is_running_status = 1;
		}
		else
		{
			/* Cancel running status */
			is_running_status = 0;
		}
		prev_status_byte = status_byte;
	}
	else if (is_realtime_message(status_byte) || is_system_common_message(status_byte))
	{
		/*
		   From the BLE MIDI spec:
		   System Common and System Real-Time messages do not cancel Running Status if
		   interspersed between Running Status MIDI messages. However, a timestamp byte
		   must precede the Running Status MIDI message that follows.
		*/
		if (is_running_status)
		{
			next_rs_message_needs_timestamp = 1;
		}
	}
	else
	{
		/* Cancel running status */
		is_running_status = 0;
		prev_status_byte = 0;
	}

	uint8_t bytes_to_append[5] = {0, 0, 0, 0, 0 };
	uint8_t num_bytes_to_append = 0;

	/* Add packet header byte? */
	if (packet->size == 0)
	{
		bytes_to_append[num_bytes_to_append] = packet_header(timestamp);
		num_bytes_to_append++;
	}

	/* Skip the message timestamp if we're in a running status sequence, the timestamp
	   hasn't changed and we're dealing with a channel message that was not preceded
	   by a system realtime/common message. */
	uint16_t prev_timestamp = packet->prev_timestamp;
	int skip_msg_timestamp = is_running_status && timestamp == prev_timestamp && is_channel_message(status_byte) && !next_rs_message_needs_timestamp;
	if (!skip_msg_timestamp)
	{
		bytes_to_append[num_bytes_to_append] = message_timestamp(timestamp);
		num_bytes_to_append++;
	}

	if (next_rs_message_needs_timestamp && is_channel_message(status_byte))
	{
		/* Just added a required timestamp byte for a running status channel
		   message following a system common/real time message. */
		next_rs_message_needs_timestamp = 0;
	}

	/* Skip the status byte if we're in a running status sequence and this is a channel message */
	int skip_status_byte = is_running_status && is_channel_message(status_byte);
	if (!skip_status_byte)
	{
		bytes_to_append[num_bytes_to_append] = status_byte;
		num_bytes_to_append++;
	}

	/* Add data bytes */
	uint8_t num_data_bytes = num_message_bytes - 1;
	for (int i = 0; i < num_data_bytes; i++)
	{
		bytes_to_append[num_bytes_to_append] = data_bytes[i];
		num_bytes_to_append++;
	}

	/* Append bytes to the BLE packet */
	if (packet->size + num_bytes_to_append <= packet->max_size)
	{
		for (int i = 0; i < num_bytes_to_append; i++)
		{
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
	else
	{
		return BLE_MIDI_ERROR_PACKET_FULL;
	}
}

int ble_midi_packet_append_sysex_msg(
    struct ble_midi_packet *packet,
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
		if (i == 0 && bytes[i] != 0xf7)
		{
			return BLE_MIDI_ERROR_INVALID_STATUS_BYTE;
		}
		else if (i == num_bytes - 1 && bytes[i] != 0xf0)
		{
			return BLE_MIDI_ERROR_INVALID_STATUS_BYTE;
		}
		else if (!is_data_byte(bytes[i]))
		{
			return BLE_MIDI_ERROR_INVALID_DATA_BYTE;
		}
	}

	/* See if there's room in the packet */
	int num_bytes_to_append = num_bytes + 2;
	if (packet->size == 0)
	{
		num_bytes_to_append++;
	}
	if (packet->max_size - packet->size < num_bytes_to_append)
	{
		return BLE_MIDI_ERROR_PACKET_FULL;
	}

	/* Add packet header? */
	if (packet->size == 0)
	{
		packet->bytes[packet->size] = packet_header(timestamp);
		packet->size++;
	}

	/* Add message bytes */
	for (int i = 0; i < num_bytes; i++) {
		if (i == 0 || i == num_bytes - 1)
		{
			packet->bytes[packet->size] = message_timestamp(timestamp);
			packet->size++;
		}
		packet->bytes[packet->size] = bytes[i];
		packet->size++;
	}

	return BLE_MIDI_SUCCESS;
}

int append_sysex_status(struct ble_midi_packet *packet, uint16_t timestamp, uint8_t status)
{
	if (packet->in_sysex_sequence && status == 0xf7) {
		return BLE_MIDI_ERROR_IN_SYSEX_SEQUENCE;
	}
	else if (!packet->in_sysex_sequence && status == 0xf0) {
		return BLE_MIDI_ERROR_NOT_IN_SYSEX_SEQUENCE;
	}
	int num_bytes_to_append = 2;
	if (packet->size == 0) {
		num_bytes_to_append++;
	}
	if (packet->max_size - packet->size < num_bytes_to_append) {
		return BLE_MIDI_ERROR_PACKET_FULL;
	}
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

int ble_midi_packet_start_sysex_msg(struct ble_midi_packet *packet, uint16_t timestamp)
{
	int result = append_sysex_status(packet, timestamp, 0xf7);
	if (result == BLE_MIDI_SUCCESS) {
		packet->in_sysex_sequence = 1;
	}
	return result;
}

int ble_midi_packet_end_sysex_msg(struct ble_midi_packet *packet, uint16_t timestamp)
{
	int result = append_sysex_status(packet, timestamp, 0xf0);
	if (result == BLE_MIDI_SUCCESS) {
		packet->in_sysex_sequence = 0;
	}
	return result;
}

int ble_midi_packet_append_sysex_data(struct ble_midi_packet *packet, uint8_t *data_bytes, uint32_t num_data_bytes, uint16_t timestamp)
{
	if (!packet->in_sysex_sequence) {
		return BLE_MIDI_ERROR_NOT_IN_SYSEX_SEQUENCE;
	}

	/* Validate data bytes */
	for (int i = 0; i < num_data_bytes; i++)
	{
		if (!is_data_byte(data_bytes[i]))
		{
			/* Invalid data byte */
			return BLE_MIDI_ERROR_INVALID_DATA_BYTE;
		}
	}

	if (packet->size == 0) {
		if (packet->max_size < 1) {
			return BLE_MIDI_ERROR_PACKET_FULL;
		}
		packet->bytes[packet->size] = packet_header(timestamp);
		packet->size++;
	}

	int num_bytes_left = packet->max_size - packet->size;
	int num_data_bytes_to_add = num_bytes_left < num_data_bytes ? num_bytes_left : num_data_bytes;
	for (int i = 0; i < num_data_bytes_to_add; i++) {
		packet->bytes[packet->size] = data_bytes[i];
		packet->size++;
	}

	return num_data_bytes_to_add;
}