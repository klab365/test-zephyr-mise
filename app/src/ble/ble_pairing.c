#include "ble_pairing.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>

static void pairing_confirm(struct bt_conn *conn)
{
    int rc = bt_conn_auth_pairing_confirm(conn);

    if (rc != 0) {
        printk("ble pairing: confirmation failed: %d\n", rc);
    }
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
    ARG_UNUSED(conn);
    printk("ble pairing: complete%s\n", bonded ? " (bonded)" : "");
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
    ARG_UNUSED(conn);
    printk("ble pairing: failed: %d\n", reason);
}

static const struct bt_conn_auth_cb auth_callbacks = {
    .pairing_confirm = pairing_confirm,
};

static struct bt_conn_auth_info_cb auth_info_callbacks = {
    .pairing_complete = pairing_complete,
    .pairing_failed = pairing_failed,
};

int ble_pairing_init(void)
{
    int rc = bt_conn_auth_cb_register(&auth_callbacks);

    if (rc != 0) {
        return rc;
    }
    rc = bt_conn_auth_info_cb_register(&auth_info_callbacks);
    if (rc != 0) {
        return rc;
    }
    return settings_load();
}

int ble_pairing_request_security(struct bt_conn *conn)
{
    return bt_conn_set_security(conn, BT_SECURITY_L2);
}
