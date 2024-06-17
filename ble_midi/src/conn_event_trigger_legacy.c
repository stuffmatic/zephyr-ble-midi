// Pre v2.6.0, use MPSL radio notifications (removed in v2.6.0)

#include <zephyr/kernel.h>
#include <ncs_version.h>
#include <mpsl_radio_notification.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(ble_midi, CONFIG_BLE_MIDI_LOG_LEVEL);

#include "conn_event_trigger.h"

static conn_event_trigger_cb_t user_callback = NULL;

#define RADIO_NOTIF_PRIORITY 1

/* Called just before each BLE connection event. */
static void radio_notif_handler(void)
{
    if (user_callback)
    {
        user_callback();
    }
}

int conn_event_trigger_init(conn_event_trigger_cb_t callback) {    
    user_callback = callback;
	int rc = mpsl_radio_notification_cfg_set(MPSL_RADIO_NOTIFICATION_TYPE_INT_ON_ACTIVE,
						 MPSL_RADIO_NOTIFICATION_DISTANCE_420US, TEMP_IRQn);
	if (rc == 0) {
		IRQ_CONNECT(TEMP_IRQn, RADIO_NOTIF_PRIORITY, radio_notif_handler, NULL, 0);
		LOG_INF("Finished setting up mpsl connection event interrupt");
	} else {
		LOG_ERR("mpsl_radio_notification_cfg_set failed with error %d", rc);
	}

	__ASSERT(rc == 0, "mpsl_radio_notification_cfg_set failed");
}

void conn_event_trigger_refresh_conn_interval(struct bt_conn *conn)
{
	// Nothing when using MPSL radio notifications
}

void conn_event_trigger_set_enabled(struct bt_conn *conn, int enabled) {
    if (enabled) {
        irq_enable(TEMP_IRQn);
    } else {
        irq_disable(TEMP_IRQn);
    }
}