#include "ble_pairing.h"

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/bluetooth.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ble_pairing);

#define APP_BT_IDENTITY 1

#define ADV_PARAMS(identity, use_whitelist) ((struct bt_le_adv_param[]) { \
	{ \
	.id = identity, \
	.sid = 0, \
	.secondary_max_skip = 0, \
	.options = BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_ONE_TIME | (use_whitelist ? BT_LE_ADV_OPT_FILTER_CONN : 0), \
	.interval_min = BT_GAP_ADV_FAST_INT_MIN_2, \
	.interval_max = BT_GAP_ADV_FAST_INT_MAX_2, \
	.peer = NULL, \
}})

static void setup_accept_list_cb(const struct bt_bond_info *info, void *user_data)
{
	int *bond_cnt = user_data;
	if ((*bond_cnt) < 0) {
		return;
	}
	int err = bt_le_filter_accept_list_add(&info->addr);
	LOG_INF("Added following peer to whitelist: %x %x \n", info->addr.a.val[0],
		info->addr.a.val[1]);
	if (err) {
		LOG_INF("Cannot add peer to Filter Accept List (err: %d)\n", err);
		(*bond_cnt) = -EIO;
	} else {
		(*bond_cnt)++;
	}
}

static int setup_accept_list(uint8_t local_id)
{
	int err = bt_le_filter_accept_list_clear();
	if (err) {
		LOG_INF("Cannot clear Filter Accept List (err: %d)\n", err);
		return err;
	}
	int bond_cnt = 0;
	bt_foreach_bond(local_id, setup_accept_list_cb, &bond_cnt);
	return bond_cnt;
}

void advertise_with_accept_list(const struct bt_data *ad, int ad_size, const struct bt_data *sd, int sd_size)
{
    // TODO: this should use the system work queue according to nordic online course
    int err = 0;
	int allowed_cnt = setup_accept_list(APP_BT_IDENTITY);
	if (allowed_cnt < 0) {
		LOG_INF("Acceptlist setup failed (err:%d)\n", allowed_cnt);
	} else {
		err = bt_le_adv_start(ADV_PARAMS(APP_BT_IDENTITY, allowed_cnt > 0), ad, ad_size, sd, sd_size);
		if (err) {
			LOG_INF("Advertising failed to start (err %d)", err);
			return;
		}
		LOG_INF("Advertising successfully started, whitelist size %d", allowed_cnt);
	}
}

static void on_auth_cancel(struct bt_conn *conn)
{
	LOG_INF("on_auth_cancel");
}

static void on_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	LOG_INF("on_passkey_display, passkey %06d", passkey);
}

static void on_pairing_confirm(struct bt_conn *conn)
{
	LOG_INF("on_pairing_confirm");
	bt_conn_auth_pairing_confirm(conn);
}

// Set all callbacks to NULL for "Just works" pairing
static struct bt_conn_auth_cb conn_auth_callbacks = {
	.passkey_display = NULL, 
	.passkey_confirm = NULL,
	.cancel = NULL,
	.pairing_confirm = NULL
};

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	LOG_INF("Connected cb, err %d", err);
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	LOG_INF("Disconnected, reason %d", reason);
}

void identity_resolved_cb(struct bt_conn *conn, const bt_addr_le_t *rpa, const bt_addr_le_t *identity) 
{
	LOG_INF("identity_resolved_cb");
}

bool le_param_req_cb(struct bt_conn *conn, struct bt_le_conn_param *param)
{
	LOG_INF("le_param_req_cb");
}

void le_param_updated_cb(struct bt_conn *conn, uint16_t interval, uint16_t latency, uint16_t timeout)
{
	LOG_INF("le_param_updated_cb, interval %d, latency %d, timeout %d", interval, latency, timeout);
}

void security_changed_cb(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	LOG_INF("security_changed_cb, level %d, err %d", level, err);
}

static struct bt_conn_cb conn_callbacks = {
	.connected = connected_cb,
	.disconnected = disconnected_cb,
	.identity_resolved = identity_resolved_cb,
	.le_param_req = le_param_req_cb,
	.le_param_updated = le_param_updated_cb,
	.security_changed = security_changed_cb,
};

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Pairing cancelled: %s", addr);
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Pairing completed: %s, bonded: %d", addr, bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	LOG_INF("Pairing failed conn: %s, reason %d", addr, reason);
}

static void bond_deleted(uint8_t id, const bt_addr_le_t *peer)
{
	char addr[BT_ADDR_LE_STR_LEN];
	bt_addr_le_to_str(peer, addr, sizeof(addr));

	LOG_INF("Bond deleted, id %d, peer %s", id, addr);
}

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {.pairing_complete = pairing_complete,
							       .pairing_failed = pairing_failed,
							       .bond_deleted = bond_deleted};

int ble_pairing_init() 
{
	bt_conn_cb_register(&conn_callbacks);

    int auth_cb_err = bt_conn_auth_cb_register(&conn_auth_callbacks);
	if (auth_cb_err) {
		LOG_ERR("Failed to register authorization callbacks.");
		__ASSERT(auth_cb_err == 0, "bt_conn_auth_cb_register failed");
	}

	int auth_info_cb_err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
	if (auth_info_cb_err) {
		LOG_ERR("Failed to register authorization info callbacks.");
		__ASSERT(auth_info_cb_err == 0, "bt_conn_auth_info_cb_register failed");
	}
}

int ble_pairing_load()
{
    // load ble bonds. must be called after bt_enable.
	int settings_load_err = settings_load();
	__ASSERT(settings_load_err == 0, "bt_enable failed");

	int id_count = 0;
    bt_id_get(NULL, &id_count);
    LOG_INF("bt_id_get returned %d ids", id_count);
    __ASSERT(id_count == 1 || id_count == 2,
             "there should be 1 or 2 ble identities");
	if (id_count < 2) {
		int new_id = bt_id_create(NULL, NULL);
        LOG_INF("ble identity count was %d, created one more (%d)", id_count,
                new_id);
		__ASSERT_NO_MSG(new_id == APP_BT_IDENTITY);
	}
}

int ble_pairing_clear()
{
	int reset_err = bt_id_reset(APP_BT_IDENTITY, NULL, NULL);
	__ASSERT_NO_MSG(reset_err == APP_BT_IDENTITY);
}