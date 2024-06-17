#ifndef _BLE_MIDI_CONN_EVENT_TRIGGER_H_
#define _BLE_MIDI_CONN_EVENT_TRIGGER_H_

#include <zephyr/bluetooth/conn.h>

typedef void (*conn_event_trigger_cb_t)();

/**
 * Configures an interrupt that is triggered just before each BLE connection event.
 * Used to trigger transmission of BLE MIDI data accumulated between connection events.
 */
int conn_event_trigger_init(conn_event_trigger_cb_t callback);
void conn_event_trigger_refresh_conn_interval(struct bt_conn *conn);
void conn_event_trigger_set_enabled(struct bt_conn *conn, int enabled);

#endif // _BLE_MIDI_CONN_EVENT_TRIGGER_H_