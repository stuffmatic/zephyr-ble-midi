#include "tx_queue.h"

// Indicates the start of a chunk of sysex bytes in the FIFO. This ID must be < 128
// since message chunks in the FIFO start with status bytes that are >= 128.
#define SYSEX_DATA_CHUNK_ID 0x0d

// [0] - sysex data chunk ID 
// [1] - data byte count, LSB
// [2] - data byte count, MSB
// ... - data bytes
#define SYSEX_DATA_CHUNK_HEADER_SIZE 3

// The maximum number of data bytes in a sysex data chunk in the FIFO. Must fit into 2 bytes
#define SYSEX_DATA_CHUNK_MAX_BYTE_COUNT ((1 << 8) - 1)

// The maximum size of a chunk of sysex data bytes in the FIFO. 
#define SYSEX_DATA_CHUNK_MAX_SIZE (SYSEX_DATA_CHUNK_HEADER_SIZE + SYSEX_DATA_CHUNK_MAX_BYTE_COUNT)

#define SYSEX_START 0xf0
#define SYSEX_END 0xf7

static uint8_t sysex_chunk_scratch_buf[SYSEX_DATA_CHUNK_MAX_SIZE];

static enum tx_queue_error write_3_byte_chunk(struct tx_queue* queue, const uint8_t* bytes) {
	if (queue->callbacks.fifo_get_free_space() < 3) {
		return TX_QUEUE_FIFO_FULL;
	}
	int write_result = queue->callbacks.fifo_write(bytes, 3);
	return write_result == 0 ? TX_QUEUE_SUCCESS : TX_QUEUE_FIFO_FULL;
}

// TODO: return success - 0, invalid data -1,  or no room 1
static int add_3_byte_chunk_to_packet(struct tx_queue* queue, uint8_t* bytes) {	
	struct ble_midi_writer_t* tx_packet = tx_queue_last_tx_packet(queue);
	int first_byte = bytes[0];
	int timestamp = queue->callbacks.ble_timestamp();
	enum ble_midi_packet_error_t add_result = BLE_MIDI_PACKET_SUCCESS;

    // Make two attempts to add 
	for (int attempt = 0; attempt < 2; attempt++) {
		if (first_byte == SYSEX_START) {
			add_result = ble_midi_writer_start_sysex_msg(tx_packet, timestamp);
		} else if (first_byte == SYSEX_END) {
			add_result = ble_midi_writer_end_sysex_msg(tx_packet, timestamp);
		} else {
			add_result = ble_midi_writer_add_msg(tx_packet, bytes, timestamp);
		}

		if (add_result == BLE_MIDI_PACKET_ERROR_PACKET_FULL) {
			int push_result = tx_queue_push_tx_packet(queue);
            if (push_result) {
                // No free tx packets.
                break;
            }
		} else {
			// chunk was either added or invalid.
			break;
		}
	}

	if (add_result == BLE_MIDI_PACKET_SUCCESS) {
		// chunk was added, we're done
		return 0;
	} else if (add_result == BLE_MIDI_PACKET_ERROR_PACKET_FULL) {
        return 1;
    } 

    // Error adding chunk, shouldn't happen. 
    // Signal to the caller that it should be removed from the FIFO.
    return -1;
}

static int add_data_bytes_to_packet(struct tx_queue* queue, const uint8_t* bytes, int byte_count) {
	return 0;
}

// INIT / CLEAR API. 
void tx_queue_reset(struct tx_queue* queue) {
	queue->num_remaining_data_bytes = 0;
	queue->first_tx_packet_idx = 0;
	queue->tx_packet_count = 0;
	queue->callbacks.fifo_clear();
    
    for (int i = 0; i < BLE_MIDI_TX_QUEUE_PACKET_COUNT; i++) {
        ble_midi_writer_reset(&queue->tx_packets[i]);
    }
}

void tx_queue_init(struct tx_queue* queue, struct tx_queue_callbacks* callbacks) {
	// Assign callbacks
	queue->callbacks.ble_timestamp = callbacks->ble_timestamp;
	queue->callbacks.fifo_get_free_space = callbacks->fifo_get_free_space;
	queue->callbacks.fifo_is_empty = callbacks->fifo_is_empty;
	queue->callbacks.fifo_peek = callbacks->fifo_peek;
	queue->callbacks.fifo_read = callbacks->fifo_read;
	queue->callbacks.fifo_write = callbacks->fifo_write;
	queue->callbacks.fifo_clear = callbacks->fifo_clear;

	// reset state
	tx_queue_reset(queue);
}

// WRITE API. 

enum tx_queue_error tx_queue_push_msg(struct tx_queue* queue, const uint8_t* bytes) {
	return write_3_byte_chunk(queue, bytes);
}

enum tx_queue_error tx_queue_push_sysex_start(struct tx_queue* queue) {
	// Store sysex start as three zero padded bytes for simplicity 
	uint8_t bytes[3] = { SYSEX_START, 0, 0 };
	return write_3_byte_chunk(queue, bytes);
}

enum tx_queue_error tx_queue_push_sysex_end(struct tx_queue* queue) {
	// Store sysex end as three zero padded bytes for simplicity
	uint8_t bytes[3] = { SYSEX_END, 0, 0 };
	return write_3_byte_chunk(queue, bytes);
}

int tx_queue_push_sysex_data(struct tx_queue* queue, const uint8_t* bytes, int num_bytes) {
	int fifo_space_left = queue->callbacks.fifo_get_free_space();
	if (fifo_space_left <= SYSEX_DATA_CHUNK_HEADER_SIZE) {
		// Not enough room in the FIFO to send at least one data byte. 
		// Tell the caller 0 bytes were added.
		return 0;
	}

	// If we made it here, there's room for at least one data byte.
	int num_bytes_to_send = num_bytes;

	if (num_bytes_to_send > SYSEX_DATA_CHUNK_MAX_BYTE_COUNT) {
		// Don't exceed the maximum chunk size
		num_bytes_to_send = SYSEX_DATA_CHUNK_MAX_BYTE_COUNT; 
	}

	if (num_bytes_to_send > fifo_space_left - SYSEX_DATA_CHUNK_HEADER_SIZE) {
		// Only send as many bytes as there's room for in the FIFO
		num_bytes_to_send = fifo_space_left - SYSEX_DATA_CHUNK_HEADER_SIZE;
	}

	uint8_t chunk_header[SYSEX_DATA_CHUNK_HEADER_SIZE] = { 
		SYSEX_DATA_CHUNK_ID, 
		num_bytes_to_send & 0xff, 
		(num_bytes_to_send >> 8) & 0xff, 
	};

	int header_write_result = queue->callbacks.fifo_write(chunk_header, SYSEX_DATA_CHUNK_HEADER_SIZE);
	int data_write_result = queue->callbacks.fifo_write(bytes, num_bytes_to_send);

	// TODO: error handling?

	return num_bytes_to_send;
}

int tx_queue_pop_pending(struct tx_queue* queue) {
	uint8_t msg_bytes[3] = { 0, 0, 0};

    if (queue->tx_packet_count == 0) {
        tx_queue_push_tx_packet(queue);
    }

	while (!queue->callbacks.fifo_is_empty()) {
		int peek_result = queue->callbacks.fifo_peek(msg_bytes, 3);
		int first_byte = msg_bytes[0];
		int add_result = 0;
		if (first_byte >= 128) {
			// sysex start, sysex end or non-sysex msg
			add_result = add_3_byte_chunk_to_packet(queue, msg_bytes);
		} else if (first_byte == SYSEX_DATA_CHUNK_ID) {
			// sysex data chunk
			int byte_count = msg_bytes[1] || (msg_bytes[2] << 8);
			int peek_result = queue->callbacks.fifo_peek(sysex_chunk_scratch_buf, byte_count + SYSEX_DATA_CHUNK_HEADER_SIZE);
			add_result = add_data_bytes_to_packet(queue, &sysex_chunk_scratch_buf[SYSEX_DATA_CHUNK_HEADER_SIZE], byte_count);
			// TODO: handle the case where some but not all bytes were added
		}
	}
}

int tx_queue_push_tx_packet(struct tx_queue* queue) {
    if (queue->tx_packet_count >= BLE_MIDI_TX_QUEUE_PACKET_COUNT) {
        return 1;
    }

    queue->tx_packet_count++;

    struct ble_midi_writer_t* packet = tx_queue_last_tx_packet(queue);
    ble_midi_writer_reset(packet);

    return 0;
}

int tx_queue_pop_tx_packet(struct tx_queue* queue) {
    if (queue->tx_packet_count <= 0) {
        return 1;
    }
    queue->tx_packet_count--;
    queue->first_tx_packet_idx = (queue->first_tx_packet_idx + 1) % BLE_MIDI_TX_QUEUE_PACKET_COUNT;

    return 0;
}

struct ble_midi_writer_t* tx_queue_last_tx_packet(struct tx_queue* queue) {
    if (queue->tx_packet_count == 0) {
        return 0;
    }

    int tx_packet_idx = (queue->first_tx_packet_idx + queue->tx_packet_count) % BLE_MIDI_TX_QUEUE_PACKET_COUNT;
    return &queue->tx_packets[tx_packet_idx];
}

struct ble_midi_writer_t* tx_queue_last_first_packet(struct tx_queue* queue) {
    if (queue->tx_packet_count == 0) {
        return 0;
    }
    int tx_packet_idx = queue->first_tx_packet_idx;
    return &queue->tx_packets[tx_packet_idx];
}
