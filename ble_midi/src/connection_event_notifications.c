#include <zephyr/kernel.h>
#include <ncs_version.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(ble_midi, CONFIG_BLE_MIDI_LOG_LEVEL);

#include "connection_event_notifications.h"

static conn_event_notification_cb_t user_callback = NULL;

#if NCS_VERSION_NUMBER < 0x20600
// Pre v2.6.0, use MPSL radio notifications (removed in v2.6.0)

#include <mpsl_radio_notification.h>

#define RADIO_NOTIF_PRIORITY 1

/* Called just before each BLE connection event. */
static void radio_notif_handler(void)
{
    if (user_callback)
    {
        user_callback();
    }
}

int conn_event_notifications_init(conn_event_notification_cb_t callback) {    
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

void conn_event_notifications_refresh_conn_interval(struct bt_conn *conn)
{
	// Nothing when using MPSL radio notifications
}

void conn_event_notifications_set_enabled(struct bt_conn *conn, int enabled) {
    if (enabled) {
        irq_enable(TEMP_IRQn);
    } else {
        irq_disable(TEMP_IRQn);
    }
}

#else

// v2.6.0 or newer. MPSL radio notifications API is no longer available.
// https://developer.nordicsemi.com/nRF_Connect_SDK/doc/2.6.0/nrfxlib/mpsl/CHANGELOG.html
// Use event trigger API to get notifications just before connection events.

#include <sdc_hci_vs.h>
#include <zephyr/bluetooth/hci.h>
#include <hal/nrf_egu.h>
#include <zephyr/kernel.h>

#define PPI_CH_ID      15
#if defined(DPPIC_PRESENT)
#define SWI_IRQn EGU0_IRQn
#else
#define SWI_IRQn SWI0_IRQn
#endif

atomic_t conn_interval_us = ATOMIC_INIT(0);

static void timer_handler(struct k_timer *t)
{
	if (user_callback) {
		user_callback();
	}
}

K_TIMER_DEFINE(timer, timer_handler, NULL);

static void egu0_handler(const void *context)
{
	nrf_egu_event_clear(NRF_EGU0, NRF_EGU_EVENT_TRIGGERED0);
	// Set up a timer that fires just before the next connection event

	int dur_us = atomic_get(&conn_interval_us) - CONFIG_BLE_MIDI_CONN_EVENT_NOTIFICATION_DISTANCE_US;
	k_timer_start(&timer, K_USEC(dur_us > 0 ? dur_us : 0), K_NO_WAIT);
}

static int setup_connection_event_trigger(struct bt_conn *conn, bool enable)
{
	// Adapted from https://github.com/nrfconnect/sdk-nrf/blob/main/samples/bluetooth/connection_event_trigger/src/main.c

	int err;
	struct net_buf *buf;
	struct net_buf *rsp = NULL;
	// sdc_hci_cmd_vs_get_next_conn_event_counter_t *cmd_get_conn_event_counter;
	// sdc_hci_cmd_vs_get_next_conn_event_counter_return_t *cmd_event_counter_return;
	sdc_hci_cmd_vs_set_conn_event_trigger_t *cmd_set_trigger;
	uint16_t conn_handle;

	err = bt_hci_get_conn_handle(conn, &conn_handle);
	if (err) {
		LOG_ERR("Failed obtaining conn_handle (err %d)\n", err);
		return err;
	}

	/*buf = bt_hci_cmd_create(SDC_HCI_OPCODE_CMD_VS_GET_NEXT_CONN_EVENT_COUNTER,
				sizeof(*cmd_get_conn_event_counter));

	if (!buf) {
		LOG_ERR("Could not allocate command buffer\n");
		return -ENOMEM;
	}

	cmd_get_conn_event_counter = net_buf_add(buf, sizeof(*cmd_get_conn_event_counter));
	cmd_get_conn_event_counter->conn_handle = conn_handle;

	err = bt_hci_cmd_send_sync(SDC_HCI_OPCODE_CMD_VS_GET_NEXT_CONN_EVENT_COUNTER, buf, &rsp);

	if (err) {
		LOG_ERR("Error for command SDC_HCI_OPCODE_CMD_VS_GET_NEXT_CONN_EVENT_COUNTER (%d)",
		       err);
		return err;
	}

	cmd_event_counter_return =
		(struct hci_cmd_vs_get_next_conn_event_counter_return *)rsp->data; */

	buf = bt_hci_cmd_create(SDC_HCI_OPCODE_CMD_VS_SET_CONN_EVENT_TRIGGER,
				sizeof(*cmd_set_trigger));

	if (!buf) {
		LOG_ERR("Could not allocate command buffer");
		return -ENOMEM;
	}

	/* Configure event trigger to trigger NRF_EGU_TASK_TRIGGER0
	 * through (D)PPI channel PPI_CH_ID.
	 * This will generate a software interrupt: SWI_IRQn.
	 */

	cmd_set_trigger = net_buf_add(buf, sizeof(*cmd_set_trigger));
	cmd_set_trigger->conn_handle = conn_handle;
	cmd_set_trigger->role = SDC_HCI_VS_CONN_EVENT_TRIGGER_ROLE_CONN;
	cmd_set_trigger->ppi_ch_id = PPI_CH_ID;
	cmd_set_trigger->period_in_events = 1;
	cmd_set_trigger->conn_evt_counter_start = 100 + 20; 
		// cmd_event_counter_return->next_conn_event_counter + 20;

	if (enable) {
		cmd_set_trigger->task_endpoint =
			nrf_egu_task_address_get(NRF_EGU0, NRF_EGU_TASK_TRIGGER0);
		IRQ_DIRECT_CONNECT(SWI_IRQn, 5, egu0_handler, 0);
		nrf_egu_int_enable(NRF_EGU0, NRF_EGU_INT_TRIGGERED0);
		NVIC_EnableIRQ(SWI_IRQn);
	} else {
		cmd_set_trigger->task_endpoint = 0;
		nrf_egu_int_disable(NRF_EGU0, NRF_EGU_INT_TRIGGERED0);
		NVIC_DisableIRQ(SWI_IRQn);
	}

	err = bt_hci_cmd_send_sync(SDC_HCI_OPCODE_CMD_VS_SET_CONN_EVENT_TRIGGER, buf, NULL);
	if (err) {
		LOG_ERR("Error for command SDC_HCI_OPCODE_CMD_VS_SET_CONN_EVENT_TRIGGER (%d)\n",
		       err);
		return err;
	}

	LOG_INF("Configured connection event trigger, enabled %d\n", enable);

	net_buf_unref(rsp);
	return 0;
}

int conn_event_notifications_init(conn_event_notification_cb_t callback)
{
    user_callback = callback;
}

void conn_event_notifications_refresh_conn_interval(struct bt_conn *conn)
{
	struct bt_conn_info conn_info;
	int result = bt_conn_get_info(conn, &conn_info);
	if (result == 0) {
		atomic_set(&conn_interval_us, 1250 * conn_info.le.interval);
		LOG_INF("New conn. interval %d us", atomic_get(&conn_interval_us));
	} else {
		LOG_ERR("bt_conn_get_info failed with error %d", result);
	}
}

void conn_event_notifications_set_enabled(struct bt_conn *conn, int enabled)
{	
	setup_connection_event_trigger(conn, enabled);
}

#endif