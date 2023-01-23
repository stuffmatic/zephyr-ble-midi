#include <stdio.h>
#include <assert.h>
#include "src/ble_midi_packet.h"

static void log_buffer(uint8_t *bytes, int num_bytes)
{
	for (int i = 0; i < num_bytes; i++)
	{
		printf("%02x ", ((uint8_t *)bytes)[i]);
	}
	printf("\n");
}

static void log_packet(struct ble_midi_packet *packet)
{
	printf("BLE MIDI packet (size %d, max %d): ", packet->size, packet->max_size);
	log_buffer(packet->bytes, packet->size);
}

void run_serialization_test(const char *desc, int use_running_status, uint16_t packet_max_size, uint8_t messages[][4], int num_messages, uint8_t expected_payload[], int expected_payload_size)
{
	struct ble_midi_packet packet;
	ble_midi_packet_init(&packet);
	packet.max_size = packet_max_size;

	for (int i = 0; i < num_messages; i++)
	{
		ble_midi_packet_append_msg(&packet, messages[i], messages[i][3], use_running_status);
	}
	int error = expected_payload_size != packet.size;

	if (!error)
	{
		for (int i = 0; i < expected_payload_size; i++)
		{
			if (expected_payload[i] != packet.bytes[i])
			{
				error = 1;
				break;
			}
		}
	}

	printf("%s '%s':\n", error ? "❌" : "✅", desc);
	printf("   expected payload: ");
	log_buffer(expected_payload, expected_payload_size);
	printf("   actual payload:   ");
	log_buffer(packet.bytes, packet.size);
}

void test_running_status_with_rt()
{
	uint8_t messages[][4] = {
	    {0x90, 0x69, 0x7f, 10},
	    {0x80, 0x69, 0x7f, 10},
	    {0x90, 0x69, 0x7f, 10},
	    {0x80, 0x69, 0x7f, 11},
	    {0x90, 0x69, 0x7f, 11},
	    {0xf6, 0, 0, 11},
	    {0x80, 0x69, 0x7f, 11},
	    {0x90, 0x69, 0x7f, 11},
	};

	uint8_t expected_payload[] = {
	    0x80,		    // packet header
	    0x8a, 0x90, 0x69, 0x7f, // note on w timestamp
	    0x69, 0x00,		    // note off, running status, no timestamp
	    0x69, 0x7f,		    // note on, running status, no timestamp
	    0x8b, 0x69, 0x00,	    // note off, running status, with timestamp
	    0x69, 0x7f,		    // note on, running status, no timestamp
	    0x8b, 0xf6,		    // rt with timestamp
	    0x8b, 0x69, 0x00,	    // running status with timestamp
	    0x69, 0x7f		    // running status, no timestamp
	};

	run_serialization_test(
	    "Running status with real time msg",
	    1,
	    100,
	    messages,
	    sizeof(messages) / 4,
	    expected_payload,
	    sizeof(expected_payload));
}

void test_running_status_disabled()
{
	uint8_t messages[][4] = {
	    {0x90, 0x69, 0x7f, 10},
	    {0x80, 0x69, 0x7f, 10},
	    {0x90, 0x69, 0x7f, 10},
	    {0x80, 0x69, 0x7f, 11},
	    {0x90, 0x69, 0x7f, 11},
	    {0xf6, 0, 0, 11},
	    {0x80, 0x69, 0x7f, 11}
	};

	uint8_t expected_payload[] = {
	    0x80, // packet header
	    0x8a, 0x90, 0x69, 0x7f, // note on w timestamp
	    0x8a, 0x80, 0x69, 0x7f, // note off w timestamp
	    0x8a, 0x90, 0x69, 0x7f, // note on w timestamp
	    0x8b, 0x80, 0x69, 0x7f, // note off w timestamp
	    0x8b, 0x90, 0x69, 0x7f, // note on w timestamp
	    0x8b, 0xf6, // system rt w timestamp
	    0x8b, 0x80, 0x69, 0x7f,  // note off w timestamp
	};

	run_serialization_test(
	    "Running status disabled",
	    0,
	    100,
	    messages,
	    sizeof(messages) / 4,
	    expected_payload,
	    sizeof(expected_payload));
}

void test_full_packet()
{
	uint8_t messages[][4] = {
	    {0xb0, 0x12, 0x34, 10},
	    {0xe0, 0x12, 0x34, 10},
	    {0xb0, 0x12, 0x34, 10},
	    {0xe0, 0x12, 0x34, 10},
	    {0xb0, 0x12, 0x34, 10},
	    {0xe0, 0x12, 0x34, 10}
	};

	uint8_t expected_payload[] = {
	    0x80, // packet header
	    0x8a, 0xb0, 0x12, 0x34,
	    0x8a, 0xe0, 0x12, 0x34,
	    0x8a, 0xb0, 0x12, 0x34,
	    0x8a, 0xe0, 0x12, 0x34,
	    0x8a, 0xb0, 0x12, 0x34,
	    // 0x8a, 0xe0, 0x12, 0x34 <- should not fit in the packet
	};

	run_serialization_test(
	    "No room left in packet",
	    0,
	    22,
	    messages,
	    sizeof(messages) / 4,
	    expected_payload,
	    sizeof(expected_payload));
}

void test_rt_in_sysex() {
	struct ble_midi_packet packet;
	ble_midi_packet_init(&packet);
	packet.max_size = 100;

	/* Control change */
	uint8_t msg[] = { 0xb0, 0x12, 0x23 };
	ble_midi_packet_append_msg(&packet, msg, 200, 0);
	/* sysex start */
	ble_midi_packet_start_sysex_msg(&packet, 210);
	/* sysex data */
	uint8_t sysex_data[] = { 0x01, 0x02, 0x03, 0x04 };
	ble_midi_packet_append_sysex_data(&packet, sysex_data, 4, 210);
	/* attempt to add note on in sysex message. should not work. */
	uint8_t note_on[] = { 0x90, 0x69, 0x7f };
	int result = ble_midi_packet_append_msg(&packet, note_on, 220, 0);
	/* system real time in sysex message */
	uint8_t rt[] = { 0xfe };
	result = ble_midi_packet_append_msg(&packet, rt, 220, 0);
	/* sysex end */
	ble_midi_packet_end_sysex_msg(&packet, 210);
	/* note on */
	result = ble_midi_packet_append_msg(&packet, note_on, 220, 0);

	uint8_t expected_payload[] = {
		0x83, // packet header
		0xc8, 0xb0, 0x12, 0x23, // control change
		0xd2, 0xf7, // sysex start
		0x01, 0x02, 0x03, 0x04, // sysex data
		0xdc, 0xfe, // system rt
		0xd2, 0xf0, // sysex end
		0xdc, 0x90, 0x69, 0x7f // note on
	};
	log_packet(&packet);
}

int main(int argc, char *argv[])
{
	test_running_status_with_rt();
	test_running_status_disabled();
	test_full_packet();
	test_rt_in_sysex();
}
