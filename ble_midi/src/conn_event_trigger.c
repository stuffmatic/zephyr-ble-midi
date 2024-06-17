#include <zephyr/kernel.h>
#include <ncs_version.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(ble_midi, CONFIG_BLE_MIDI_LOG_LEVEL);

#include "conn_event_trigger.h"

static conn_event_trigger_cb_t user_callback = NULL;

// v2.6.0 or newer. MPSL radio notifications API is no longer available.
// https://developer.nordicsemi.com/nRF_Connect_SDK/doc/2.6.0/nrfxlib/mpsl/CHANGELOG.html
// Use event trigger API to get notifications just before connection events.

#include <sdc_hci_vs.h>
#include <zephyr/bluetooth/hci.h>
#include <hal/nrf_egu.h>
#include <nrfx_timer.h>
#include <zephyr/kernel.h>

#define PPI_CH_ID      15
#if defined(DPPIC_PRESENT)
#define SWI_IRQn EGU0_IRQn
#else
#define SWI_IRQn SWI0_IRQn
#endif

atomic_t conn_interval_us = ATOMIC_INIT(0);

#define NRFX_TIMER_IDX 1
#define NRFX_TIMER_IRQ TIMER1_IRQn
static const nrfx_timer_t timer = NRFX_TIMER_INSTANCE(NRFX_TIMER_IDX);

static void timer_handler(nrf_timer_event_t event_type, void * p_context) {
	if (user_callback) {
		user_callback();
	}
}

void timer_init() {
	uint32_t base_frequency = NRF_TIMER_BASE_FREQUENCY_GET(timer.p_reg);
	nrfx_timer_config_t timer_cfg = NRFX_TIMER_DEFAULT_CONFIG(base_frequency);
    // TODO: set lower interrupt priority?
    // timer_cfg.interrupt_priority = 3;
	timer_cfg.bit_width = NRF_TIMER_BIT_WIDTH_32;
	
    int init_result = nrfx_timer_init(&timer, &timer_cfg, timer_handler);
    if (init_result != NRFX_SUCCESS) {
        LOG_ERR("nrfx_timer_init result %d", init_result);
	    __ASSERT_NO_MSG(init_result == NRFX_SUCCESS);
    }

    IRQ_DIRECT_CONNECT(NRFX_TIMER_IRQ, 0, nrfx_timer_1_irq_handler, 0);
	irq_enable(NRFX_TIMER_IRQ);
}

void timer_deinit() {
	irq_disable(NRFX_TIMER_IRQ);
	nrfx_timer_uninit(&timer);
}

void timer_trigger(uint32_t delay_us) {
	// https://devzone.nordicsemi.com/f/nordic-q-a/111922/nrfx_timer_extended_compare-fails-assertion-when-timer-is-paused
	nrfx_timer_disable(&timer);
	nrfx_timer_clear(&timer);
	uint32_t tick_count = nrfx_timer_us_to_ticks(&timer, delay_us);
	nrfx_timer_extended_compare(&timer,
                                NRF_TIMER_CC_CHANNEL0,
                                tick_count,
                                NRF_TIMER_SHORT_COMPARE0_STOP_MASK,
                                1);
	nrfx_timer_enable(&timer);
}

static void egu0_handler(const void *context)
{
	nrf_egu_event_clear(NRF_EGU0, NRF_EGU_EVENT_TRIGGERED0);
	// Set up a timer that fires just before the next connection event

	int delay_us = atomic_get(&conn_interval_us) - CONFIG_BLE_MIDI_CONN_EVENT_NOTIFICATION_DISTANCE_US;
	timer_trigger(delay_us < 0 ? 0 : delay_us);
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
	cmd_set_trigger->conn_evt_counter_start = 0; 
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

	// net_buf_unref(rsp);
	return 0;
}

int conn_event_trigger_init(conn_event_trigger_cb_t callback)
{
    user_callback = callback;
}

void conn_event_trigger_refresh_conn_interval(struct bt_conn *conn)
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

void conn_event_trigger_set_enabled(struct bt_conn *conn, int enabled)
{	
	if (enabled) {
		timer_init();
	} else {
		timer_deinit();
	}
	setup_connection_event_trigger(conn, enabled);
}
