#include "ble_advertising.h"

#include <errno.h>
#include <stdint.h>

#include <pb_encode.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/printk.h>

#include "assets/proto/ble_transport.pb.h"

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1U)
#define ADVERTISING_TIMEOUT_10MS 3000U
#define ENDPOINT_SVC_UUID_VAL \
    BT_UUID_128_ENCODE(0x7b5a0001, 0x4f1d, 0x4c8b, 0x8d4a, 0x5f4d7a123000)

static const uint8_t endpoint_svc_uuid_data[] = { ENDPOINT_SVC_UUID_VAL };
static const uint8_t device_name[] = DEVICE_NAME;
static const BleManufacturerData manufacturer_data = {
    .fw_version = 1U,
    .hw_version = 1U,
};
static struct bt_le_ext_adv *adv;
static bool advertising;
static uint8_t advertising_appearance[2];
static uint8_t encoded_manufacturer_data[2U + BleManufacturerData_size] = {
    0xff, 0xff, /* Company identifier. Replace with assigned company ID. */
};
static struct bt_data ad[4];

static void advertising_sent(struct bt_le_ext_adv *adv_set,
                             struct bt_le_ext_adv_sent_info *info)
{
    ARG_UNUSED(adv_set);
    ARG_UNUSED(info);
    advertising = false;
    printk("ble advertising: timed out\n");
}

static const struct bt_le_ext_adv_cb adv_cb = { .sent = advertising_sent };

static int build_data(void)
{
    pb_ostream_t stream = pb_ostream_from_buffer(
        &encoded_manufacturer_data[2], sizeof(encoded_manufacturer_data) - 2U);

    if (!pb_encode(&stream, BleManufacturerData_fields, &manufacturer_data)) {
        printk("ble advertising: manufacturer encode failed: %s\n", PB_GET_ERROR(&stream));
        return -EINVAL;
    }

    advertising_appearance[0] = CONFIG_BT_DEVICE_APPEARANCE & 0xffU;
    advertising_appearance[1] = (CONFIG_BT_DEVICE_APPEARANCE >> 8) & 0xffU;
    ad[0] = (struct bt_data)BT_DATA(BT_DATA_GAP_APPEARANCE, advertising_appearance,
                                    sizeof(advertising_appearance));
    ad[1] = (struct bt_data)BT_DATA(BT_DATA_UUID128_ALL, endpoint_svc_uuid_data,
                                    sizeof(endpoint_svc_uuid_data));
    ad[2] = (struct bt_data)BT_DATA(BT_DATA_MANUFACTURER_DATA, encoded_manufacturer_data,
                                    2U + stream.bytes_written);
    ad[3] = (struct bt_data)BT_DATA(BT_DATA_NAME_COMPLETE, device_name, DEVICE_NAME_LEN);
    return 0;
}

int ble_advertising_start(void)
{
    int rc;

    if (advertising) {
        return 0;
    }
    if (adv == NULL) {
        rc = bt_le_ext_adv_create(BT_LE_EXT_ADV_CONN, &adv_cb, &adv);
        if (rc != 0) {
            return rc;
        }
        rc = build_data();
        if (rc != 0) {
            return rc;
        }
        rc = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0);
        if (rc != 0) {
            return rc;
        }
    }
    rc = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_PARAM(ADVERTISING_TIMEOUT_10MS, 0));
    if (rc == 0) {
        advertising = true;
        printk("ble advertising: started as %s\n", DEVICE_NAME);
    }
    return rc;
}
