#ifndef _BLE_MIDI_CONTEXT_H_
#define _BLE_MIDI_CONTEXT_H_

#include <zephyr/kernel.h>
#include <ble_midi/ble_midi.h>
#include "ble_midi_packet.h"

struct ble_midi_context {
    atomic_t pending_midi_msg_work_count;
    int is_initialized;
    struct ble_midi_writer_t tx_writer;
    struct ble_midi_writer_t sysex_tx_writer;
    struct ble_midi_callbacks user_callbacks;
};

void ble_midi_context_init(struct ble_midi_context* context);

void ble_midi_context_reset(struct ble_midi_context* context, int tx_running_status, int tx_note_off_as_note_on);

#endif // _BLE_MIDI_CONTEXT_H_