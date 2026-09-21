#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/mgmt/mcumgr/transport/smp_bt.h>
#include <zephyr/sys/printk.h>

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, SMP_BT_SVC_UUID_VAL),
};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
            sizeof(CONFIG_BT_DEVICE_NAME) - 1U),
};

static void advertise(void)
{
    int rc = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

    if (rc != 0) {
        printk("smp: advertising failed: %d\n", rc);
        return;
    }

    printk("smp: advertising as %s\n", CONFIG_BT_DEVICE_NAME);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    ARG_UNUSED(conn);
    if (err != 0U) {
        printk("smp: connection failed: 0x%02x\n", err);
        advertise();
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    ARG_UNUSED(conn);
    printk("smp: disconnected: 0x%02x\n", reason);
}

static void connection_recycled(void)
{
    /* The Bluetooth host has released connection resources; advertising is safe again. */
    advertise();
}

BT_CONN_CB_DEFINE(smp_conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .recycled = connection_recycled,
};

void smp_ble_start(void)
{
    int rc = bt_enable(NULL);

    if (rc != 0) {
        printk("smp: Bluetooth enable failed: %d\n", rc);
        return;
    }

    advertise();
}
