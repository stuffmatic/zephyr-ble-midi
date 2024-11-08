#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include "../ble_midi/src/tx_queue.h"

// Quick and dirty FIFO implementation for testing
#define FIFO_SIZE 1024
struct fifo {
    uint8_t bytes[FIFO_SIZE];
    int num_bytes;
};

static struct fifo fifo = {
    .num_bytes = 0
};

int fifo_peek(uint8_t *bytes, int num_bytes) {
    int num_bytes_to_peek = num_bytes > fifo.num_bytes ? fifo.num_bytes : num_bytes;
    memcpy(bytes, fifo.bytes, num_bytes_to_peek);
    return num_bytes_to_peek;
}

int fifo_read(int num_bytes) {
    int num_bytes_to_read = num_bytes > fifo.num_bytes ? fifo.num_bytes : num_bytes;
    
    for (int i = 0; i < num_bytes_to_read; i++) {
        fifo.bytes[i] = fifo.bytes[i + num_bytes_to_read];
    }

    fifo.num_bytes -= num_bytes_to_read;
    return num_bytes_to_read;
}

int fifo_get_free_space()
{
    return FIFO_SIZE - fifo.num_bytes;
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
    .ble_timestamp = ble_timestamp
};
static int running_status_enabled = 0;
static int note_off_as_note_on = 0;

void test_tx_packet_queue() {
    struct tx_queue queue;
    fifo_clear();
	tx_queue_init(&queue, &callbacks, running_status_enabled, note_off_as_note_on);
    assert(!queue.has_tx_data && "Expected new queue to not have tx data");
    assert(queue.tx_packet_count == 1 && "Expected new queue to have one active tx packet");
    tx_queue_push_tx_packet(&queue);
    assert(queue.tx_packet_count == 2);
    tx_queue_push_tx_packet(&queue);
    tx_queue_push_tx_packet(&queue);
    assert(queue.tx_packet_count == 4);
    // Pushing beyond max packet count should not increase packet count
    tx_queue_push_tx_packet(&queue);
    assert(queue.tx_packet_count == 4);

    tx_queue_pop_tx_packet(&queue);
    assert(queue.tx_packet_count == 3);
    tx_queue_pop_tx_packet(&queue);
    assert(queue.tx_packet_count == 2);
    tx_queue_pop_tx_packet(&queue);
    assert(queue.tx_packet_count == 1);
    // Popping beyond last packet should not decrease packet count.
    tx_queue_pop_tx_packet(&queue);
    assert(queue.tx_packet_count == 1);
}

int main(int argc, char *argv[])
{
    test_tx_packet_queue();
    /*
	struct tx_queue queue;
	tx_queue_init(&queue, &callbacks, running_status_enabled, note_off_as_note_on);

    uint8_t msg[3] = { 0xC0, 0x1, 0x2 };
    tx_queue_set_max_tx_packet_size(&queue, 6);
    tx_queue_push_msg(&queue, msg);
    msg[0] = 0xd0;
    tx_queue_push_msg(&queue, msg);
    tx_queue_pop_pending(&queue);
    tx_queue_pop_tx_packet(&queue);
    msg[0] = 0xe0;
    tx_queue_push_msg(&queue, msg);
    tx_queue_pop_pending(&queue);
    tx_queue_reset(&queue); */

    return 0;
}