#ifndef BLE_MIDI_TX_QUEUE_H
#define BLE_MIDI_TX_QUEUE_H

#include <stdint.h>
#include "ble_midi_packet.h"

#ifdef CONFIG_BLE_MIDI_TX_QUEUE_PACKET_COUNT
#define BLE_MIDI_TX_QUEUE_PACKET_COUNT CONFIG_BLE_MIDI_TX_QUEUE_PACKET_COUNT
#else
#define BLE_MIDI_TX_QUEUE_PACKET_COUNT 4
#endif

enum tx_queue_error {
	TX_QUEUE_SUCCESS = 0,
	TX_QUEUE_FIFO_FULL = -1
};

struct tx_queue_callbacks {
	int (*fifo_peek)(uint8_t* bytes, int num_bytes);
	int (*fifo_read)(int num_bytes);
	int (*fifo_get_free_space)();
	int (*fifo_is_empty)();
	int (*fifo_clear)();
	int (*fifo_write)(const uint8_t* bytes, int num_bytes);
	uint16_t (*ble_timestamp)();
};

struct tx_queue {
	struct tx_queue_callbacks callbacks;
	struct ble_midi_writer_t tx_packets[BLE_MIDI_TX_QUEUE_PACKET_COUNT];
	int num_remaining_data_bytes;
	int first_tx_packet_idx;
	int tx_packet_count;
	int has_tx_data;
};

// INIT / CLEAR API. 
void tx_queue_init(struct tx_queue* queue, struct tx_queue_callbacks* callbacks, int running_status_enabled, int note_off_as_note_on);
void tx_queue_set_callbacks(struct tx_queue* queue, struct tx_queue_callbacks* callbacks);
void tx_queue_reset(struct tx_queue* queue);

// Producer API
enum tx_queue_error tx_queue_set_max_tx_packet_size(struct tx_queue* queue, uint16_t size);
enum tx_queue_error tx_queue_push_msg(struct tx_queue* queue, const uint8_t* bytes);
enum tx_queue_error tx_queue_push_sysex_start(struct tx_queue* queue);
enum tx_queue_error tx_queue_push_sysex_end(struct tx_queue* queue);
int tx_queue_push_sysex_data(struct tx_queue* queue, const uint8_t* bytes, int num_bytes);

// Consumer API

/**
 * Pop pending FIFO messages one by one and append them to a pending BLE MIDI tx packet.
 * If one packet is full, start filling up the next, if there is one available.
 * Only remove data from FIFO that has been written to a packet.
 */
int tx_queue_pop_pending(struct tx_queue* queue);

/**
 * Move on to fill the next tx packet in the queue. 
 * Call this when a tx packet has been filled. 
 * Returns 0 on success or non-zero if all tx packets have been filled.
 */
int tx_queue_push_tx_packet(struct tx_queue* queue);

/**
 * Remove the first tx packet in the queue.
 * Call this when a tx packet has been sent.
 * Returns 0 on success or non-zero if there is no packet to pop.
 */
int tx_queue_pop_tx_packet(struct tx_queue* queue);

struct ble_midi_writer_t* tx_queue_first_tx_packet(struct tx_queue* queue);

struct ble_midi_writer_t* tx_queue_last_tx_packet(struct tx_queue* queue);

#endif // BLE_MIDI_TX_QUEUE_H
