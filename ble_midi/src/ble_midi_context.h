#ifndef _BLE_MIDI_CONTEXT_H_
#define _BLE_MIDI_CONTEXT_H_

#include <zephyr/kernel.h>
#include <ble_midi/ble_midi.h>
#include "ble_midi_packet.h"

#ifndef CONFIG_BLE_MIDI_TX_MODE_SINGLE_MSG
#include "tx_queue.h"
#endif

struct ble_midi_context {
    ble_midi_ready_state_t ready_state;
    int is_initialized;
    struct ble_midi_callbacks user_callbacks;
#ifdef CONFIG_BLE_MIDI_TX_MODE_SINGLE_MSG
    struct ble_midi_writer_t tx_writer;
#else
    struct tx_queue tx_queue;
    atomic_t pending_tx_queue_fifo_work_count;
#endif
};

void ble_midi_context_init(struct ble_midi_context* context);

void ble_midi_context_reset(struct ble_midi_context* context, int tx_running_status, int tx_note_off_as_note_on);

#endif // _BLE_MIDI_CONTEXT_H_