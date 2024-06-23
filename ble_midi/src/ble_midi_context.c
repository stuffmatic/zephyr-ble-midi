#include "ble_midi_context.h"

void ble_midi_context_init(struct ble_midi_context* context) {
    context->user_callbacks.ready_cb = NULL;
    context->user_callbacks.midi_message_cb = NULL;
    context->user_callbacks.sysex_data_cb = NULL;
    context->user_callbacks.sysex_end_cb = NULL;
    context->user_callbacks.sysex_start_cb = NULL;
    context->user_callbacks.tx_done_cb = NULL;
    context->ready_state = BLE_MIDI_NOT_CONNECTED;
    
    ble_midi_context_reset(context, 0, 0);
}

void ble_midi_context_reset(struct ble_midi_context* context, int tx_running_status, int tx_note_off_as_note_on) {
    atomic_set(&context->pending_midi_msg_work_count, 0);
    ble_midi_writer_init(&context->sysex_tx_writer, tx_running_status, tx_note_off_as_note_on);
	ble_midi_writer_init(&context->tx_writer, tx_running_status, tx_note_off_as_note_on);
}