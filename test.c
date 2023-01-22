#include <stdio.h>
#include <assert.h>
#include "src/ble_midi_packet.h"

uint8_t note_on_msg[3] = {
	0x90, 0x69, 0x7f
};
uint8_t note_off_msg[3] = {
	0x80, 0x69, 0x7f
};

uint8_t tune_request_msg[3] = {
	0xf6, 0, 0
};

struct lol {
	const char* face;
};

struct lol face = {
	.face = "HEELO"
};

static void log_packet(struct ble_midi_packet *packet)
{
	printf("BLE MIDI packet (size %d, max %d): ", packet->size, packet->max_size);
  for (int i = 0; i < packet->size; i++)
	{
		printf("%02x ", ((uint8_t *)packet->bytes)[i]);
	}
	printf("\n");
}

void test_assert(int condition, const char* message) {
	if (!condition) {
		printf("❌ %s\n", message);
	} else {
		printf("✅ %s\n", message);
	}
}

void test_parse_note_on_note_off(int condition, const char* message) {
	struct ble_midi_packet packet;
	ble_midi_packet_reset(&packet);
	packet.max_size = 100;
	ble_midi_packet_append_msg(&packet, note_on_msg, 0, 0);
	ble_midi_packet_append_msg(&packet, note_off_msg, 0, 0);
}

void test_reset() {
	struct ble_midi_packet packet;
	ble_midi_packet_reset(&packet);
	packet.max_size = 100;
	test_assert(packet.size == 0, "Packet size should be zero after reset");
	test_assert(packet.prev_status_byte == 0, "prev_status_byte should be zero after reset");
	test_assert(packet.is_running_status == 0, "is_running_status should be zero after reset");
}

void test_simple_running_status() {
		struct ble_midi_packet packet;
		ble_midi_packet_reset(&packet);
		packet.max_size = 100;
		ble_midi_packet_append_msg(&packet, note_on_msg, 1000, 1);
		test_assert(packet.is_running_status == 0, "Should not be running status");
		test_assert(packet.prev_status_byte == note_on_msg[0], "Invalid prev status");
		ble_midi_packet_append_msg(&packet, note_off_msg, 1000, 1);
		test_assert(packet.is_running_status == 1, "Should be running status");
		ble_midi_packet_append_msg(&packet, note_on_msg, 1000, 1);
		test_assert(packet.is_running_status == 1, "Should be running status");
		ble_midi_packet_append_msg(&packet, note_off_msg, 1000, 1);
		test_assert(packet.is_running_status == 1, "Should be running status");
		test_assert(packet.size == 1+4+2+2+2, "unexpected running status package size");
		log_packet(&packet);
}

void test_running_status_with_rt() {
		struct ble_midi_packet packet;
		ble_midi_packet_reset(&packet);
		packet.max_size = 100;
		ble_midi_packet_append_msg(&packet, note_on_msg, 1000, 1);
		log_packet(&packet);
		test_assert(packet.size == 5, "Unexpected size");

		ble_midi_packet_append_msg(&packet, note_off_msg, 1000, 1);
		log_packet(&packet);
		test_assert(packet.size == 7, "Unexpected size");

		ble_midi_packet_append_msg(&packet, note_on_msg, 1000, 1);
		log_packet(&packet);
		test_assert(packet.size == 9, "Unexpected size");

		ble_midi_packet_append_msg(&packet, tune_request_msg, 1000, 1);
		log_packet(&packet);
		test_assert(packet.size == 11, "Unexpected size");

		ble_midi_packet_append_msg(&packet, note_on_msg, 1000, 1);
		log_packet(&packet);
		test_assert(packet.size == 14, "Unexpected size");

		ble_midi_packet_append_msg(&packet, note_off_msg, 1002, 1);
		log_packet(&packet);
		test_assert(packet.size == 17, "Unexpected size");
		ble_midi_packet_append_msg(&packet, tune_request_msg, 1002, 1);
		test_assert(packet.size == 19, "Unexpected size");

		ble_midi_packet_append_msg(&packet, tune_request_msg, 1002, 1);
		test_assert(packet.size == 21, "Unexpected size");

		log_packet(&packet);
		ble_midi_packet_append_msg(&packet, note_on_msg, 1003, 1);
		log_packet(&packet);
		test_assert(packet.size == 24, "Unexpected size");
		ble_midi_packet_append_msg(&packet, note_off_msg, 1003, 1);
		log_packet(&packet);
		test_assert(packet.size == 26, "Unexpected size");
}

void test_sysex()
{
	struct ble_midi_packet packet;
	ble_midi_packet_init(&packet);
	packet.max_size = 100;
	ble_midi_packet_start_sysex_msg(&packet, 100);
	log_packet(&packet);
	uint8_t data[3] = {1, 2, 3};
	int rc = ble_midi_packet_append_sysex_data(&packet, data, 3, 100);
	printf("rc %d\n", rc);
	log_packet(&packet);
	ble_midi_packet_end_sysex_msg(&packet, 100);
	log_packet(&packet);
}

int main(int argc, char *argv[]) {
	test_sysex();
	// test_reset();
	// test_simple_running_status();
	// test_running_status_with_rt();
}
