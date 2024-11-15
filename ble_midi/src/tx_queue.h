#ifndef BLE_MIDI_TX_QUEUE_H
#define BLE_MIDI_TX_QUEUE_H

#include <stdint.h>
#include "ble_midi_packet.h"

#ifdef CONFIG_TX_QUEUE_PACKET_COUNT
#define TX_QUEUE_PACKET_COUNT CONFIG_TX_QUEUE_PACKET_COUNT
#else
#define TX_QUEUE_PACKET_COUNT 4
#endif

enum tx_queue_error {
	TX_QUEUE_SUCCESS = 0,
	TX_QUEUE_FIFO_FULL = -1,
	TX_QUEUE_NO_TX_PACKETS = -2,
	// Error writing to FIFO
	TX_QUEUE_FIFO_WRITE_ERROR = -3,
	// Invalid data read from the FIFO, e.g a status byte in a sysex data chunk
	TX_QUEUE_INVALID_DATA = -4
};

struct tx_queue_callbacks {
	// Returns the number of bytes peeked
	int (*fifo_peek)(uint8_t* bytes, int num_bytes);
	// Returns the number of bytes read
	int (*fifo_read)(int num_bytes);
	int (*fifo_get_free_space)();
	int (*fifo_is_empty)();
	int (*fifo_clear)();
	// Returns the number of bytes written
	int (*fifo_write)(const uint8_t* bytes, int num_bytes);

	void (*notify_has_data)(int has_data);
	uint16_t (*ble_timestamp)();
};

struct tx_queue {
	struct tx_queue_callbacks callbacks;
	struct ble_midi_writer_t tx_packets[TX_QUEUE_PACKET_COUNT];
	int first_tx_packet_idx;
	int tx_packet_count;
	int has_tx_data;
	// Used to keep track of how many additional sysex data bytes to read from the
	// FIFO in case the packet queue got filled up with a partial sysex message
	int num_remaining_data_bytes;
	int curr_sysex_data_chunk_size;
};

// INIT / CLEAR API. 
void tx_queue_init(struct tx_queue* queue, struct tx_queue_callbacks* callbacks, int running_status_enabled, int note_off_as_note_on);
void tx_queue_set_callbacks(struct tx_queue* queue, struct tx_queue_callbacks* callbacks);
void tx_queue_reset(struct tx_queue* queue);

// Producer API (writes to the FIFO)
enum tx_queue_error tx_queue_fifo_add_tx_packet_size(struct tx_queue* queue, uint16_t size);
enum tx_queue_error tx_queue_fifo_add_msg(struct tx_queue* queue, const uint8_t* bytes);
enum tx_queue_error tx_queue_fifo_add_sysex_start(struct tx_queue* queue);
enum tx_queue_error tx_queue_fifo_add_sysex_end(struct tx_queue* queue);
int tx_queue_fifo_add_sysex_data(struct tx_queue* queue, const uint8_t* bytes, int num_bytes);

// Consumer API

/**
 * Read pending FIFO messages one by one and append them to a pending BLE MIDI tx packet.
 * If one packet is full, start filling up the next, if there is one available.
 * Only remove data from FIFO that has been written to a packet.
 */
int tx_queue_read_from_fifo(struct tx_queue* queue);

/**
 * Move on to fill the next tx packet in the queue. 
 * Call this when a tx packet has been filled. 
 * Returns 0 on success or non-zero if all tx packets have been filled.
 */
enum tx_queue_error tx_queue_tx_packet_add(struct tx_queue* queue);

/**
 * Remove the first tx packet in the queue.
 * Call this when a tx packet has been sent.
 * Returns 0 on success or non-zero if there is no packet to pop.
 */
enum tx_queue_error tx_queue_on_tx_packet_sent(struct tx_queue* queue);

/* The first BLE MIDI tx packet in the queue. Returns null if there is no data. */
struct ble_midi_writer_t* tx_queue_first_tx_packet(struct tx_queue* queue);

/* The last BLE MIDI tx packet in the queue. */
struct ble_midi_writer_t* tx_queue_last_tx_packet(struct tx_queue* queue);

#endif // BLE_MIDI_TX_QUEUE_H
