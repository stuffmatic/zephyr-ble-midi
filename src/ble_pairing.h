#ifndef __BLE_BONDING_H__
#define __BLE_BONDING_H__

#include <zephyr/bluetooth/bluetooth.h>

// called before bt_enable
int ble_pairing_init();

// called after bt_enable
int ble_pairing_load();

int ble_pairing_clear();

void advertise_with_accept_list(const struct bt_data *ad, int ad_size, const struct bt_data *sd, int sd_size);

int ble_disconnect_if_connected();

#endif 