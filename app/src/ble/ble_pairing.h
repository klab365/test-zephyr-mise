#pragma once

#include <zephyr/bluetooth/conn.h>

int ble_pairing_init(void);
int ble_pairing_request_security(struct bt_conn *conn);
