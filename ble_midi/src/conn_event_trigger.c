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
#include <bluetooth/hci_vs_sdc.h>
#include <hal/nrf_egu.h>
#include <nrfx_timer.h>
#include <zephyr/kernel.h>

#define PPI_CH_ID      CONFIG_BLE_MIDI_EVENT_TRIGGER_PPI_CHANNEL
#if defined(DPPIC_PRESENT)
#define SWI_IRQn EGU0_IRQn
#else
#define SWI_IRQn SWI0_IRQn
#endif

atomic_t conn_interval_us = ATOMIC_INIT(0);

#define NRFX_TIMER_IDX 1
#define NRFX_TIMER_IRQ TIMER1_IRQn
#define EGU_INSTANCE NRF_EGU0
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
    } else {
		LOG_INF("Initialized event trigger timer");
	}
	
    IRQ_DIRECT_CONNECT(NRFX_TIMER_IRQ, 0, nrfx_timer_1_irq_handler, 0);
	irq_enable(NRFX_TIMER_IRQ);
}

void timer_deinit() {
	irq_disable(NRFX_TIMER_IRQ);
	if (nrfx_timer_init_check(&timer)) {
		nrfx_timer_uninit(&timer);
		LOG_INF("Deinitialized event trigger timer");
	}
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
	nrf_egu_event_clear(EGU_INSTANCE, NRF_EGU_EVENT_TRIGGERED0);
	// Set up a timer that fires just before the next connection event

	int delay_us = atomic_get(&conn_interval_us) - CONFIG_BLE_MIDI_CONN_EVENT_NOTIFICATION_DISTANCE_US;
	timer_trigger(delay_us < 0 ? 0 : delay_us);
}

static int setup_connection_event_trigger(struct bt_conn *conn, bool enable)
{
	int err;
	sdc_hci_cmd_vs_set_event_start_task_t cmd_params;
	uint16_t conn_handle;

	err = bt_hci_get_conn_handle(conn, &conn_handle);
	if (err) {
		LOG_ERR("Failed obtaining conn_handle (err %d)", err);
		return err;
	}

	cmd_params.handle_type = SDC_HCI_VS_SET_EVENT_START_TASK_HANDLE_TYPE_CONN;
	cmd_params.handle = conn_handle;

	/* Configure event task to trigger NRF_EGU_TASK_TRIGGER0.
	 * This will generate a software interrupt.
	 */
	if (enable) {
		cmd_params.task_address =
			nrf_egu_task_address_get(EGU_INSTANCE, NRF_EGU_TASK_TRIGGER0);

		IRQ_DIRECT_CONNECT(SWI_IRQn, 5, egu0_handler, 0);
		nrf_egu_int_enable(EGU_INSTANCE, NRF_EGU_INT_TRIGGERED0);
		NVIC_EnableIRQ(SWI_IRQn);
	} else {
		cmd_params.task_address = 0;
		nrf_egu_int_disable(EGU_INSTANCE, NRF_EGU_INT_TRIGGERED0);
		NVIC_DisableIRQ(SWI_IRQn);
	}

	err = hci_vs_sdc_set_event_start_task(&cmd_params);
	if (err) {
		LOG_ERR("Error for command hci_vs_sdc_set_event_start_task() (%d\n",
		       err);
		return err;
	}

	LOG_INF("Configured connection event trigger, enabled %d", enable);

	return 0;
}

int conn_event_trigger_init(conn_event_trigger_cb_t callback)
{
    user_callback = callback;
	return 0;
}

void conn_event_trigger_refresh_conn_interval(struct bt_conn *conn)
{
	struct bt_conn_info conn_info;
	int result = bt_conn_get_info(conn, &conn_info);
	if (result == 0) {
		atomic_set(&conn_interval_us, 1250 * conn_info.le.interval);
		LOG_INF("New conn. interval %d us", (int)atomic_get(&conn_interval_us));
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

	if (enabled) {
		setup_connection_event_trigger(conn, enabled);
	} else {
		// The connection event trigger is automatically removed 
		// on disconnect.
	}
}
