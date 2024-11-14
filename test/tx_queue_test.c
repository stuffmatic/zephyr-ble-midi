#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include "../ble_midi/src/tx_queue.h"

void assert_true(int condition, const char* message) {
    assert(condition && message);
}

void assert_eq(int a, int b, const char* message) {
    assert(a == b && message);
}

// Quick and dirty FIFO implementation for testing, just using an array copying data around
#define FIFO_MAX_CAPACITY 1024
struct fifo {
    uint8_t bytes[FIFO_MAX_CAPACITY];
    int capcity;
    int num_bytes;
};

static struct fifo fifo = {
    .num_bytes = 0,
    .capcity = 0
};

int fifo_peek(uint8_t *bytes, int num_bytes) {
    int num_bytes_to_peek = num_bytes > fifo.num_bytes ? fifo.num_bytes : num_bytes;
    memcpy(bytes, fifo.bytes, num_bytes_to_peek);
    return num_bytes_to_peek;
}

int fifo_read(int num_bytes) {
    int num_bytes_to_read = num_bytes > fifo.num_bytes ? fifo.num_bytes : num_bytes;
    
    for (int i = 0; i < fifo.num_bytes - num_bytes_to_read; i++) {
        fifo.bytes[i] = fifo.bytes[i + num_bytes_to_read];
    }

    fifo.num_bytes -= num_bytes_to_read;
    return num_bytes_to_read;
}

int fifo_get_free_space()
{
    return fifo.capcity - fifo.num_bytes;
}

int fifo_is_empty()
{
    return fifo.num_bytes == 0;
}

int fifo_clear()
{
    fifo.num_bytes = 0;
    return 0;
}

int fifo_write(const uint8_t *bytes, int num_bytes)
{
    int num_free_bytes = fifo_get_free_space();
    int num_bytes_to_write = num_bytes > num_free_bytes ? num_free_bytes : num_bytes;
    int write_pos = fifo.num_bytes;
    memcpy(&fifo.bytes[write_pos], bytes, num_bytes_to_write);
    fifo.num_bytes += num_bytes_to_write;
    return num_bytes_to_write;
}

void notify_has_data(int has_data) {

}

uint16_t ble_timestamp()
{
    printf("ble_timestamp\n");
	return 123;
}

static struct tx_queue_callbacks callbacks = {
    .fifo_peek = fifo_peek,
    .fifo_read = fifo_read,
    .fifo_get_free_space = fifo_get_free_space,
    .fifo_is_empty = fifo_is_empty,
    .fifo_clear = fifo_clear,
    .fifo_write = fifo_write,
    .ble_timestamp = ble_timestamp,
    .notify_has_data = notify_has_data
};
static int running_status_enabled = 0;
static int note_off_as_note_on = 0;

static void fifo_reset(int fifo_capacity) {
    fifo_clear();
    fifo.capcity = fifo_capacity;
    memset(fifo.bytes, 0, FIFO_MAX_CAPACITY);
}

void init_test_queue(struct tx_queue* queue, int max_packet_size, int fifo_capacity) {
    // Make sure the FIFO is empty
    fifo_reset(fifo_capacity);
    // Initialize the queue
	tx_queue_init(queue, &callbacks, running_status_enabled, note_off_as_note_on);
    // Add a set packet size request to the FIFO
    tx_queue_fifo_add_tx_packet_size(queue, max_packet_size);
    assert_true(queue->tx_packets->tx_buf_max_size == 0, "TX packet size should be 0 after reset");
    // Read request from FIFO, which should set the max packet size 
    tx_queue_read_from_fifo(queue);
    assert_true(queue->tx_packets->tx_buf_max_size == max_packet_size, "TX packet size should be set");
}

static enum tx_queue_error add_note_on(struct tx_queue* queue) {
    uint8_t bytes[3] = {
        0x90,
        0x60,
        0x7f
    };
    return tx_queue_fifo_add_msg(queue, bytes);
}

static void test_non_sysex_msgs() {
    // Use a small packet size so that tx packet fill up quickly
    // a ten byte packet can hold two note on messages (not using running status)
    int tx_packet_size = 10; 

    // Create and initialize the queue
    struct tx_queue queue;
    init_test_queue(&queue, tx_packet_size, 128);

    // Check that the queue is empty
    assert_true(!queue.has_tx_data, "Expected new queue to not have tx data");
    assert_true(queue.tx_packet_count == 1, "Expected new queue to have one active tx packet");

    // Push a note on message ...
    add_note_on(&queue);
    // ... and read it from the FIFO 
    tx_queue_read_from_fifo(&queue);
    assert_true(queue.has_tx_data, "Queue should have data");
    assert_true(queue.tx_packet_count == 1, "Queue should have one packet");

    // Push two more note on message ...
    add_note_on(&queue);
    add_note_on(&queue);
    // ... and read them from the FIFO 
    tx_queue_read_from_fifo(&queue);
    assert_true(queue.has_tx_data, "Queue should have data");
    assert_true(queue.tx_packet_count == 2, "Queue should have one packet");

    // The first packet in the queue should now hold two note on messages and the 
    // last (second packet) should hold one.
    struct ble_midi_writer_t* first_packet = tx_queue_first_tx_packet(&queue);
    struct ble_midi_writer_t* last_packet = tx_queue_last_tx_packet(&queue);
    assert_true(first_packet->tx_buf_size > last_packet->tx_buf_size, "first packet should be larger than last packet");

    // Mark first packet as sent
    tx_queue_on_tx_packet_sent(&queue);
    assert_true(queue.tx_packet_count == 1, "Queue should have one packet");
    assert_true(tx_queue_first_tx_packet(&queue) == last_packet, "the new first packet should now be the old last packet");

    // Mark second packet as sent
    tx_queue_on_tx_packet_sent(&queue);
    assert_true(queue.tx_packet_count == 1 && !queue.has_tx_data, "Queue should have one packet and no data");
    
}

static void test_full_fifo() {
    // Create and initialize the queue
    int fifo_capacity = 7;
    struct tx_queue queue;
    init_test_queue(&queue, 64, fifo_capacity);

    assert_eq(add_note_on(&queue), TX_QUEUE_SUCCESS, "message should fit in FIFO");
    assert_eq(add_note_on(&queue), TX_QUEUE_SUCCESS, "message should fit in FIFO");
    int fifo_size_before_failed_add = fifo.num_bytes;
    assert_eq(add_note_on(&queue), TX_QUEUE_FIFO_FULL, "message should not fit in FIFO");
    assert_eq(fifo_size_before_failed_add, fifo.num_bytes, "Failed note on add should not grow FIFO");

    tx_queue_read_from_fifo(&queue);
    assert_eq(0, fifo.num_bytes, "FIFO should be empty after reading messages");
}

static void test_full_packet_queue() {
    // Choose a packet size so that two note on messages fit in each packet
    int tx_packet_size = 10; 
    int fifo_capacity = 128;
    int max_num_tx_packets = BLE_MIDI_TX_QUEUE_PACKET_COUNT;
    struct tx_queue queue;
    init_test_queue(&queue, tx_packet_size, fifo_capacity);

    // Add enough note on messages to the FIFO to fill the tx packets with two messages left over
    int num_msgs_to_add = max_num_tx_packets * 2 + 2;
    for (int i = 0; i < num_msgs_to_add; i++) {
        add_note_on(&queue);
    }
    // Read messages from FIFO
    tx_queue_read_from_fifo(&queue);

    assert_true(queue.has_tx_data, "Full queue should have data");
    assert_eq(queue.tx_packet_count, 4, "Full queue should have 4 packets");
    assert_eq(fifo.num_bytes, 6, "Two messages should be left to add");
}

static void test_multi_packet_sysex() {
    int tx_packet_size = 10; 
    int fifo_capacity = 128;
    struct tx_queue queue;
    init_test_queue(&queue, tx_packet_size, fifo_capacity);

    // Add a sysex message spanning multiple tx packets
    uint8_t sysex_data_bytes[23];
    int sysex_data_byte_count = sizeof(sysex_data_bytes);
    for (int i = 0; i < sysex_data_byte_count; i++) {
        sysex_data_bytes[i] = i % 16;
    }

    tx_queue_fifo_add_sysex_start(&queue);
    tx_queue_fifo_add_sysex_data(&queue, sysex_data_bytes, sysex_data_byte_count);
    tx_queue_fifo_add_sysex_end(&queue);

    tx_queue_read_from_fifo(&queue);

    assert_eq(queue.tx_packets[0].tx_buf_size, queue.tx_packets[0].tx_buf_max_size, "First sysex packet should be full");
    assert_eq(queue.tx_packets[1].tx_buf_size, queue.tx_packets[1].tx_buf_max_size, "Second sysex packet should be full");
    assert_eq(queue.tx_packet_count, 3, "Sysex message should span 3 tx packets");
}

int main(int argc, char *argv[])
{
    test_non_sysex_msgs();
    test_full_fifo();
    test_full_packet_queue();
    test_multi_packet_sysex();

    return 0;
}