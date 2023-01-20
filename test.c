#include <stdio.h>
#include <assert.h>
#include "src/ble_midi_packet.h"

uint8_t note_on_msg[3] = {
	0x80, 0x69, 0x7f
};
uint8_t note_off_msg[3] = {
	0x90, 0x69, 0x7f
};

void test_assert(int condition, const char* message) {
	if (!condition) {
		printf("ASSERTION FAILED: %s\n", message);
	}
}

void test_parse_note_on_note_off(int condition, const char* message) {
	struct ble_midi_packet packet;
	ble_midi_packet_reset(&packet);
	ble_midi_packet_add_message(&packet, note_on_msg, 0, 0);
	ble_midi_packet_add_message(&packet, note_off_msg, 0, 0);
}

void test_reset() {
	struct ble_midi_packet packet;
	ble_midi_packet_reset(&packet);
	test_assert(packet.size == 0, "Packet size should be zero after reset");
	test_assert(packet.running_status == 0, "Running status should be zero after reset");
}

int main(int argc, char *argv[]) {
	test_reset();
}
