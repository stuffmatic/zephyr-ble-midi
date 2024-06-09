#ifndef _BLE_MIDI_CONN_EVENT_NOTIFICATIONS_H_
#define _BLE_MIDI_CONN_EVENT_NOTIFICATIONS_H_

#include <zephyr/bluetooth/conn.h>

typedef void (*conn_event_notification_cb_t)();

/**
 * Configures an interrupt that is triggered just before each BLE connection event.
 * Used to trigger transmission of BLE MIDI data accumulated between connection events.
 */
int conn_event_notifications_init(conn_event_notification_cb_t callback);
void conn_event_notifications_refresh_conn_interval(struct bt_conn *conn);
void conn_event_notifications_set_enabled(struct bt_conn *conn, int enabled);

#endif // _BLE_MIDI_CONN_EVENT_NOTIFICATIONS_H_