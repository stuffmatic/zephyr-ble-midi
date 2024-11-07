#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include "../ble_midi/src/tx_queue.h"

#define FIFO_SIZE 1024

struct fifo {
    uint8_t bytes[FIFO_SIZE];
    int read_pos;
    int write_post;
};

static struct fifo fifo;

int(fifo_peek)(uint8_t *bytes, int num_bytes)
{
}

int fifo_read(int num_bytes)
{

}

int fifo_get_free_space()
{

}

int fifo_is_empty()
{

}

int fifo_clear()
{
    fifo.read_pos = 0;
    fifo.write_post = 0;
}

int fifo_write(const uint8_t *bytes, int num_bytes)
{

}

uint16_t ble_timestamp()
{
	return 123;
}

int main(int argc, char *argv[])
{
	struct tx_queue queue;
	tx_queue_init(&queue, NULL);

    tx_queue_write_sysex_start(&queue);
}