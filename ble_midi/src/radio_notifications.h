#ifndef _BLE_MIDI_RADIO_NOTIFICATIONS_H_
#define _BLE_MIDI_RADIO_NOTIFICATIONS_H_

typedef void (*radio_notification_cb_t)();

/**
 * Configures an interrupt that is triggered just before each BLE connection event.
 * Used to trigger transmission of BLE MIDI data accumulated between connection events.
 */
int radio_notifications_init(radio_notification_cb_t callback);

void radio_notifications_enable();
void radio_notifications_disable();

#endif // _BLE_MIDI_RADIO_NOTIFICATIONS_H_