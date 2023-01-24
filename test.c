#include <stdio.h>
#include <assert.h>
#include "src/ble_midi_packet.h"

typedef struct {
	uint8_t bytes[3];
	uint16_t timestamp;
} midi_msg_t;

static void log_buffer(uint8_t *bytes, int num_bytes)
{
	for (int i = 0; i < num_bytes; i++)
	{
		printf("%02x ", ((uint8_t *)bytes)[i]);
	}
	printf("\n");
}

void assert_midi_msg_equals(midi_msg_t *actual, midi_msg_t *expected)
{
	int equal = 1;
	for (int i = 0; i < 3; i++) {
		if (actual->bytes[i] != expected->bytes[i]) {
			equal = 0;
			break;
		}
	}

	/* Check if messages are equivalent. */
	uint8_t actual_high_nibble = actual->bytes[0] >> 4;
	uint8_t actual_second_data = actual->bytes[2];
	uint8_t expected_high_nibble = expected->bytes[0] >> 4;
	uint8_t expected_second_data = expected->bytes[2];

	int equivalent = actual_high_nibble == 0x8 && expected_high_nibble == 0x9 && expected_second_data == 0;
	equivalent = equivalent || (expected_high_nibble == 0x8 && actual_high_nibble == 0x9 && actual_second_data == 0);

	printf("%s actual %02x %02x %02x, expected %02x %02x %02x%s\n",
	equal || equivalent ? "✅" : "❌",
	actual->bytes[0], actual->bytes[1], actual->bytes[2],
	expected->bytes[0], expected->bytes[1], expected->bytes[2],
	equivalent ? " (equivalent)" : "");

}

void assert_payload_equals(
		struct ble_midi_packet_t *packet,
		uint8_t *expected_payload,
		int expected_payload_size,
		const char *desc)
{
	int error = expected_payload_size != packet->size;

	if (!error) {
		for (int i = 0; i < expected_payload_size; i++) {
			if (expected_payload[i] != packet->bytes[i]) {
				error = 1;
				break;
			}
		}
	}

	printf("%s %s\n", error ? "❌" : "✅", desc);
	printf("   expected payload: ");
	log_buffer(expected_payload, expected_payload_size);
	printf("   actual payload:   ");
	log_buffer(packet->bytes, packet->size);
	printf("\n");
}

void assert_error_code(int actual, int expected)
{
	if (actual != expected) {
		printf("❌ expected error code %d, got %d\n", expected, actual);
	}
}

void assert_equals(int actual, int expected)
{
	if (actual != expected) {
		printf("❌ expected %d, got %d\n", expected, actual);
	}
}

static int num_parsed_messages = 0;
static midi_msg_t parsed_messages[100];

void midi_message_cb(uint8_t *bytes, uint8_t num_bytes, uint16_t timestamp)
{
	for (int i = 0; i < 3; i++) {
		parsed_messages[num_parsed_messages].bytes[i] = i < num_bytes ? bytes[i] : 0;
		parsed_messages[num_parsed_messages].timestamp = timestamp;
	}
	num_parsed_messages++;
}
void sysex_data_cb(uint8_t data_byte)
{

}
void sysex_start_cb(uint16_t timestamp)
{

}
void sysex_end_cb(uint16_t timestamp)
{

}

void roundtrip_test(const char *desc,
										int use_running_status,
										uint16_t packet_max_size,
										midi_msg_t messages[],
										int num_messages,
										uint8_t expected_payload[],
										int expected_payload_size)
{
	/* Init tx packet with the prescribed size */
	struct ble_midi_packet_t packet;
	ble_midi_packet_init(&packet);
	packet.max_size = packet_max_size;

	/* Add messages to tx packet */
	for (int i = 0; i < num_messages; i++) {
		ble_midi_packet_add_msg(&packet, messages[i].bytes, messages[i].timestamp, use_running_status);
	}

	/* The tx packet payload should equal the reference payload */
	assert_payload_equals(&packet, expected_payload, expected_payload_size, desc);

	/* Parse tx payload. The resulting messages should equal the original messages.*/
	num_parsed_messages = 0;
	ble_midi_parse_packet(packet.bytes, packet.size, midi_message_cb, sysex_start_cb, sysex_data_cb, sysex_end_cb);
	int n = num_messages < num_parsed_messages ? num_messages : num_parsed_messages;
	for (int i = 0; i < n; i++) {
		assert_midi_msg_equals(&parsed_messages[i], &messages[i]);
	}
}

void test_running_status_with_one_rt()
{
	midi_msg_t messages[] = {
			{ .bytes = { 0x90, 0x69, 0x7f }, .timestamp = 10},
			{ .bytes = { 0x80, 0x69, 0x7f }, .timestamp = 10},
			{ .bytes = { 0x90, 0x69, 0x7f }, .timestamp = 10},
			{ .bytes = { 0x80, 0x69, 0x7f }, .timestamp = 11},
			{ .bytes = { 0x90, 0x69, 0x7f }, .timestamp = 11},
			{ .bytes = { 0xf6, 0, 0 }, .timestamp = 11},
			{ .bytes = { 0x80, 0x69, 0x7f }, .timestamp = 11},
			{ .bytes = { 0x90, 0x69, 0x7f }, .timestamp = 11},
	};

	uint8_t expected_payload[] = {
			0x80,										// packet header
			0x8a, 0x90, 0x69, 0x7f, // note on w timestamp
			0x69, 0x00,							// note off, running status, no timestamp
			0x69, 0x7f,							// note on, running status, no timestamp
			0x8b, 0x69, 0x00,				// note off, running status, with timestamp
			0x69, 0x7f,							// note on, running status, no timestamp
			0x8b, 0xf6,							// rt with timestamp
			0x8b, 0x69, 0x00,				// running status with timestamp
			0x69, 0x7f							// running status, no timestamp
	};

	roundtrip_test(
			"One system common message should not cancel running status",
			1,
			100,
			messages,
			sizeof(messages) / 4,
			expected_payload,
			sizeof(expected_payload));
}

void test_running_status_with_two_rt()
{
	midi_msg_t messages[] = {
			{ .bytes = { 0x90, 0x69, 0x7f }, .timestamp = 10},
			{ .bytes = { 0x80, 0x69, 0x7f }, .timestamp = 10},
			{ .bytes = { 0x90, 0x69, 0x7f }, .timestamp = 10},
			{ .bytes = { 0x80, 0x69, 0x7f }, .timestamp = 11},
			{ .bytes = { 0x90, 0x69, 0x7f }, .timestamp = 11},
			{ .bytes = { 0xf6, 0, 0 }, .timestamp = 11}, /* System common */
			{ .bytes = { 0xfe, 0, 0 }, .timestamp = 11}, /* System real time */
			{ .bytes = { 0x80, 0x69, 0x7f }, .timestamp = 11},
			{ .bytes = { 0x90, 0x69, 0x7f }, .timestamp = 11},
	};

	uint8_t expected_payload[] = {
			0x80,										/* packet header */
			0x8a, 0x90, 0x69, 0x7f, /* note on w timestamp */
			0x69, 0x00,							/* note off, running status, no timestamp */
			0x69, 0x7f,							/* note on, running status, no timestamp */
			0x8b, 0x69, 0x00,				/* note off, running status, with timestamp */
			0x69, 0x7f,							/* note on, running status, no timestamp */
			0x8b, 0xf6,							/* rt with timestamp */
			0x8b, 0xfe,							/* rt with timestamp */
			0x8b, 0x69, 0x00,				/* running status with timestamp */
			0x69, 0x7f							/* running status, no timestamp */
	};

	roundtrip_test(
			"Two consecutive real time/common messages should not cancel running status",
			1,
			100,
			messages,
			sizeof(messages) / 4,
			expected_payload,
			sizeof(expected_payload));
}

void test_running_status_disabled()
{
	midi_msg_t messages[] = {
			{ .bytes = { 0x90, 0x69, 0x7f }, .timestamp = 10},
			{ .bytes = { 0x80, 0x69, 0x7f }, .timestamp = 10},
			{ .bytes = { 0x90, 0x69, 0x7f }, .timestamp = 10},
			{ .bytes = { 0x80, 0x69, 0x7f }, .timestamp = 11},
			{ .bytes = { 0x90, 0x69, 0x7f }, .timestamp = 11},
			{ .bytes = { 0xf6, 0, 0 }, .timestamp = 11},
			{ .bytes = { 0x80, 0x69, 0x7f }, .timestamp = 11}};

	uint8_t expected_payload[] = {
			0x80,										/* packet header */
			0x8a, 0x90, 0x69, 0x7f, /* note on w timestamp */
			0x8a, 0x80, 0x69, 0x7f, /* note off w timestamp */
			0x8a, 0x90, 0x69, 0x7f, /* note on w timestamp */
			0x8b, 0x80, 0x69, 0x7f, /* note off w timestamp */
			0x8b, 0x90, 0x69, 0x7f, /* note on w timestamp */
			0x8b, 0xf6,							/* system rt w timestamp */
			0x8b, 0x80, 0x69, 0x7f, /* note off w timestamp */
	};

	roundtrip_test(
			"Timestamp and status bytes should be added for all messages when running status is disabled",
			0,
			100,
			messages,
			sizeof(messages) / sizeof(midi_msg_t),
			expected_payload,
			sizeof(expected_payload));
}

void test_full_packet()
{
	midi_msg_t messages[] = {
			{ .bytes = { 0xb0, 0x12, 0x34 }, .timestamp = 10},
			{ .bytes = { 0xe0, 0x12, 0x34 }, .timestamp = 10},
			{ .bytes = { 0xb0, 0x12, 0x34 }, .timestamp = 10},
			{ .bytes = { 0xe0, 0x12, 0x34 }, .timestamp = 10},
			{ .bytes = { 0xb0, 0x12, 0x34 }, .timestamp = 10},
			{ .bytes = { 0xe0, 0x12, 0x34 }, .timestamp = 10}
		};

	uint8_t expected_payload[] = {
			0x80, // packet header
			0x8a, 0xb0, 0x12, 0x34,
			0x8a, 0xe0, 0x12, 0x34,
			0x8a, 0xb0, 0x12, 0x34,
			0x8a, 0xe0, 0x12, 0x34,
			0x8a, 0xb0, 0x12, 0x34,
			/* 0x8a, 0xe0, 0x12, 0x34 <- should not fit in the packet */
	};

	roundtrip_test(
			"Message that doesn't fit in tx buffer should not be added",
			0,
			22,
			messages,
			sizeof(messages) / sizeof(midi_msg_t),
			expected_payload,
			sizeof(expected_payload));
}

void test_sysex_cancels_running_status()
{
	uint8_t note_on[] = {0x90, 0x69, 0x7f};
	uint8_t note_off[] = {0x80, 0x69, 0x7f};
	uint8_t sysex[] = {0xf7, 0x01, 0x02, 0x03, 0xf0};

	/* Test adding the whole sysex message in one go */
	struct ble_midi_packet_t packet_1;
	ble_midi_packet_init(&packet_1);
	packet_1.max_size = 100;
	assert_error_code(ble_midi_packet_add_msg(&packet_1, note_on, 100, 1), BLE_MIDI_SUCCESS);
	assert_error_code(ble_midi_packet_add_msg(&packet_1, note_off, 100, 1), BLE_MIDI_SUCCESS);
	assert_error_code(ble_midi_packet_add_sysex_msg(&packet_1, sysex, 5, 100), BLE_MIDI_SUCCESS);
	assert_error_code(ble_midi_packet_add_msg(&packet_1, note_on, 100, 1), BLE_MIDI_SUCCESS);

	/* Test adding the sysex message incrementally (should give the same result) */
	struct ble_midi_packet_t packet_2;
	ble_midi_packet_init(&packet_2);
	packet_2.max_size = 100;
	assert_error_code(ble_midi_packet_add_msg(&packet_2, note_on, 100, 1), BLE_MIDI_SUCCESS);
	assert_error_code(ble_midi_packet_add_msg(&packet_2, note_off, 100, 1), BLE_MIDI_SUCCESS);
	assert_error_code(ble_midi_packet_start_sysex_msg(&packet_2, 100), BLE_MIDI_SUCCESS);
	assert_equals(ble_midi_packet_add_sysex_data(&packet_2, &sysex[1], 3, 100), 3);
	assert_error_code(ble_midi_packet_end_sysex_msg(&packet_2, 100), BLE_MIDI_SUCCESS);
	assert_error_code(ble_midi_packet_add_msg(&packet_2, note_on, 100, 1), BLE_MIDI_SUCCESS);

	uint8_t expected_payload[] = {
			0x81,										// packet header
			0xe4, 0x90, 0x69, 0x7f, // note on
			0x69, 0x00,							// note off, running status
			0xe4, 0xf7,							// sysex start
			0x01, 0x02, 0x03,				// sysex data
			0xe4, 0xf0,							// sysex end
			0xe4, 0x90, 0x69, 0x7f	// note on
	};
	assert_payload_equals(&packet_1, expected_payload, sizeof(expected_payload),
												"Sysex message (whole) should cancel running status");
	assert_payload_equals(&packet_2, expected_payload, sizeof(expected_payload),
												"Sysex message (split) should cancel running status");
}

void test_packet_end_cancels_running_status()
{
	uint8_t note_on[] = {0x90, 0x69, 0x7f};
	uint8_t note_off[] = {0x80, 0x69, 0x7f};
	struct ble_midi_packet_t packet;
	ble_midi_packet_init(&packet);
	packet.max_size = 8;
	assert_error_code(ble_midi_packet_add_msg(&packet, note_on, 100, 1), BLE_MIDI_SUCCESS);
	assert_error_code(ble_midi_packet_add_msg(&packet, note_off, 100, 1), BLE_MIDI_SUCCESS);
	assert_error_code(ble_midi_packet_add_msg(&packet, note_on, 100, 1), BLE_MIDI_ERROR_PACKET_FULL);

	uint8_t expected_payload_1[] = {
		0x81, 0xe4, 0x90, 0x69, 0x7f, 0x69, 0x00
	};
	assert_payload_equals(&packet, expected_payload_1, sizeof(expected_payload_1),
	"Packet end should cancel running status");

	ble_midi_packet_reset(&packet);
	assert_error_code(ble_midi_packet_add_msg(&packet, note_on, 100, 1), BLE_MIDI_SUCCESS);
	assert_error_code(ble_midi_packet_add_msg(&packet, note_off, 100, 1), BLE_MIDI_SUCCESS);
	uint8_t expected_payload_2[] = {
		0x81, 0xe4, 0x90, 0x69, 0x7f, 0x69, 0x00
	};
	assert_payload_equals(&packet, expected_payload_2, sizeof(expected_payload_2),
	"Packet end should cancel running status");
}

void test_multi_packet_sysex()
{
		uint8_t sysex_data[10];
		for (int i = 0; i < sizeof(sysex_data); i++) {
			sysex_data[i] = i;
		}
		struct ble_midi_packet_t packet;
		ble_midi_packet_init(&packet);
		packet.max_size = 9;

		assert_error_code(ble_midi_packet_start_sysex_msg(&packet, 100), BLE_MIDI_SUCCESS);
		int num_bytes_added = ble_midi_packet_add_sysex_data(&packet, sysex_data, sizeof(sysex_data), 200);
		assert_equals(num_bytes_added, 6);
		uint8_t expected_payload_1[] = { 0x81, 0xe4, 0xf7, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05 };
		assert_payload_equals(&packet, expected_payload_1, sizeof(expected_payload_1),
		"Multi packet sysex message should be split on message boundary");

		ble_midi_packet_reset(&packet);
		num_bytes_added = ble_midi_packet_add_sysex_data(&packet, &sysex_data[num_bytes_added], sizeof(sysex_data) - num_bytes_added, 200);
		assert_equals(num_bytes_added, 4);
		assert_error_code(ble_midi_packet_end_sysex_msg(&packet, 100), BLE_MIDI_SUCCESS);
		uint8_t expected_payload_2[] = { 0x83, 0x06, 0x07, 0x08, 0x09, 0xe4, 0xf0 };
		assert_payload_equals(&packet, expected_payload_2, sizeof(expected_payload_2),
		"Remaining sysex message should be added to second packet.");
}

void test_rt_in_sysex()
{
	struct ble_midi_packet_t packet;
	ble_midi_packet_init(&packet);
	packet.max_size = 100;

	/* Control change */
	uint8_t msg[] = {0xb0, 0x12, 0x23};
	assert_error_code(ble_midi_packet_add_msg(&packet, msg, 200, 0), BLE_MIDI_SUCCESS);
	/* sysex start */
	assert_error_code(ble_midi_packet_start_sysex_msg(&packet, 210), BLE_MIDI_SUCCESS);
	/* sysex data */
	uint8_t sysex_data[] = {0x01, 0x02, 0x03, 0x04};
	assert_equals(ble_midi_packet_add_sysex_data(&packet, sysex_data, 4, 210), 4);
	/* attempt to add note on in sysex message. should not work. */
	uint8_t note_on[] = {0x90, 0x69, 0x7f};
	assert_error_code(ble_midi_packet_add_msg(&packet, note_on, 220, 0), BLE_MIDI_ERROR_INVALID_STATUS_BYTE);
	/* system real time in sysex message */
	uint8_t rt[] = {0xfe};
	assert_error_code(ble_midi_packet_add_msg(&packet, rt, 220, 0), BLE_MIDI_SUCCESS);
	/* sysex end */
	assert_error_code(ble_midi_packet_end_sysex_msg(&packet, 210), BLE_MIDI_SUCCESS);
	/* note on */
	assert_error_code(ble_midi_packet_add_msg(&packet, note_on, 220, 0), BLE_MIDI_SUCCESS);

	uint8_t expected_payload[] = {
			0x83,										// packet header
			0xc8, 0xb0, 0x12, 0x23, // control change
			0xd2, 0xf7,							// sysex start
			0x01, 0x02, 0x03, 0x04, // sysex data
			0xdc, 0xfe,							// system rt
			0xd2, 0xf0,							// sysex end
			0xdc, 0x90, 0x69, 0x7f	// note on
	};

	assert_payload_equals(&packet, expected_payload, sizeof(expected_payload),
												"Real time message should be allowed in sysex message");
}

int main(int argc, char *argv[])
{
	test_running_status_disabled();
	test_running_status_with_one_rt();
	test_running_status_with_two_rt();
	test_full_packet();
	test_sysex_cancels_running_status();
	test_packet_end_cancels_running_status();
	test_multi_packet_sysex();

	test_rt_in_sysex();
}
