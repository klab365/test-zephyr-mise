#include "ble_actor_internal.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <pb_decode.h>
#include <pb_encode.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "app_events.h"
#include "assets/proto/ble_transport.pb.h"
#include "assets/proto/app_protocol.pb.h"
#include "hmi.h"

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1U)
#define ADVERTISING_TIMEOUT_10MS 3000U

#define ENDPOINT_SVC_UUID_VAL \
    BT_UUID_128_ENCODE(0x7b5a0001, 0x4f1d, 0x4c8b, 0x8d4a, 0x5f4d7a123000)

static struct bt_conn *current_conn;
static bool notify_enabled;
static bool bt_started;
static bool advertising;
static struct bt_le_ext_adv *adv;
static uint32_t rx_message_id;
static uint32_t rx_total_size;
static uint16_t rx_len;
static bool rx_active;
static uint8_t rx_buf[BLE_ENDPOINT_MAX_PROTOBUF_SIZE];

static struct bt_uuid_128 endpoint_svc_uuid = BT_UUID_INIT_128(ENDPOINT_SVC_UUID_VAL);
static struct bt_uuid_128 endpoint_rx_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x7b5a0002, 0x4f1d, 0x4c8b, 0x8d4a, 0x5f4d7a123000));
static struct bt_uuid_128 endpoint_tx_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x7b5a0003, 0x4f1d, 0x4c8b, 0x8d4a, 0x5f4d7a123000));

static const BleAdvertisingData advertising_data = {
    .appearance = CONFIG_BT_DEVICE_APPEARANCE,
    .service_uuid128 = {
        .size = 16U,
        .bytes = { ENDPOINT_SVC_UUID_VAL },
    },
    .manufacturer_data = {
        .size = 6U,
        .bytes = {
            0xff, 0xff, /* Company identifier. Replace with assigned company ID. */
            0x01, 0x00, /* Firmware version: major, minor. */
            0x01, 0x00, /* Hardware version: major, minor. */
        },
    },
    .device_name = {
        .size = DEVICE_NAME_LEN,
        .bytes = DEVICE_NAME,
    },
};

BUILD_ASSERT(DEVICE_NAME_LEN <= sizeof(((BleAdvertisingData *) 0)->device_name.bytes));

static uint8_t advertising_appearance[2];
static struct bt_data ad[4];

IPC_CMD_DEFINE_LOCAL(BleRestartAdvertising, { });

static void reset_reassembly(void);

static void advertising_sent(struct bt_le_ext_adv *adv_set,
                             struct bt_le_ext_adv_sent_info *info)
{
    ARG_UNUSED(adv_set);
    ARG_UNUSED(info);

    advertising = false;
    printk("ble actor: advertising timed out\n");
}

static const struct bt_le_ext_adv_cb adv_cb = {
    .sent = advertising_sent,
};

static void build_advertising_data(void)
{
    advertising_appearance[0] = advertising_data.appearance & 0xffU;
    advertising_appearance[1] = (advertising_data.appearance >> 8) & 0xffU;

    ad[0].type = BT_DATA_GAP_APPEARANCE;
    ad[0].data_len = sizeof(advertising_appearance);
    ad[0].data = advertising_appearance;

    ad[1].type = BT_DATA_UUID128_ALL;
    ad[1].data_len = advertising_data.service_uuid128.size;
    ad[1].data = advertising_data.service_uuid128.bytes;

    ad[2].type = BT_DATA_MANUFACTURER_DATA;
    ad[2].data_len = advertising_data.manufacturer_data.size;
    ad[2].data = advertising_data.manufacturer_data.bytes;

    ad[3].type = BT_DATA_NAME_COMPLETE;
    ad[3].data_len = advertising_data.device_name.size;
    ad[3].data = advertising_data.device_name.bytes;
}

static int create_advertising_set(void)
{
    if (adv != NULL) {
        return 0;
    }

    int rc = bt_le_ext_adv_create(BT_LE_EXT_ADV_CONN, &adv_cb, &adv);
    if (rc != 0) {
        printk("ble actor: failed to create advertising set: %d\n", rc);
        return rc;
    }

    build_advertising_data();

    rc = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0);
    if (rc != 0) {
        printk("ble actor: failed to set advertising data: %d\n", rc);
        return rc;
    }

    return 0;
}

static int start_advertising(void)
{
    if (advertising) {
        return 0;
    }

    int rc = create_advertising_set();
    if (rc != 0) {
        return rc;
    }

    rc = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_PARAM(ADVERTISING_TIMEOUT_10MS, 0));
    if (rc != 0) {
        printk("ble actor: advertising failed: %d\n", rc);
        return rc;
    }

    advertising = true;

    printk("ble actor: advertising as %s\n", DEVICE_NAME);
    return 0;
}

static void start_ble(void)
{
    if (!bt_started) {
        int rc = bt_enable(NULL);
        if (rc != 0) {
            printk("ble actor: bt_enable failed: %d\n", rc);
            return;
        }

        bt_started = true;
    }

    int rc = start_advertising();
    if (rc != 0) {
        return;
    }
}

static ssize_t endpoint_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                              const void *buf, uint16_t len, uint16_t offset,
                              uint8_t flags)
{
    (void) conn;
    (void) attr;
    (void) flags;

    if (offset != 0U) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    if (len == 0U || len > sizeof(((BleEndpointFrame_payload_t *) 0)->protobuf)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    BleEndpointFrame_payload_t payload = {
        .len = len,
    };

    memcpy(payload.protobuf, buf, payload.len);

    int rc = ipc_send(BleEndpointFrame, payload);
    if (rc != 0) {
        printk("ble actor: failed to enqueue endpoint frame: %d\n", rc);
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    return len;
}

static void endpoint_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    (void) attr;

    notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    printk("ble actor: notifications %s\n", notify_enabled ? "enabled" : "disabled");
}

BT_GATT_SERVICE_DEFINE(endpoint_svc,
    BT_GATT_PRIMARY_SERVICE(&endpoint_svc_uuid),
    BT_GATT_CHARACTERISTIC(&endpoint_rx_uuid.uuid,
        BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
        BT_GATT_PERM_WRITE, NULL, endpoint_write, NULL),
    BT_GATT_CHARACTERISTIC(&endpoint_tx_uuid.uuid,
        BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE, NULL, NULL, NULL),
    BT_GATT_CCC(endpoint_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err != 0U) {
        printk("ble actor: connection failed: 0x%02x\n", err);
        return;
    }

    current_conn = bt_conn_ref(conn);
    advertising = false;
    printk("ble actor: connected\n");
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    (void) conn;

    if (current_conn != NULL) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
    notify_enabled = false;
    reset_reassembly();
    printk("ble actor: disconnected: 0x%02x\n", reason);

    BleRestartAdvertising_payload_t restart = {};
    int rc = ipc_send(BleRestartAdvertising, restart);
    if (rc != 0) {
        printk("ble actor: failed to enqueue advertising restart: %d\n", rc);
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

static int notify_chunk(uint32_t message_id, uint32_t offset, const uint8_t *data,
                        uint16_t len, uint32_t total_size, bool final)
{
    uint8_t tx_buf[sizeof(BleChunk)];
    uint16_t mtu_payload;

    if (current_conn == NULL || !notify_enabled) {
        return -ENOTCONN;
    }

    BleChunk chunk = BleChunk_init_zero;
    if (len > sizeof(chunk.data.bytes)) {
        return -EMSGSIZE;
    }

    chunk.message_id = message_id;
    chunk.offset = offset;
    chunk.total_size = total_size;
    chunk.data.size = len;
    chunk.final = final;
    memcpy(chunk.data.bytes, data, len);

    pb_ostream_t ostream = pb_ostream_from_buffer(tx_buf, sizeof(tx_buf));
    if (!pb_encode(&ostream, BleChunk_fields, &chunk)) {
        printk("ble actor: chunk encode failed: %s\n", PB_GET_ERROR(&ostream));
        return -EMSGSIZE;
    }

    mtu_payload = bt_gatt_get_mtu(current_conn) - 3U;
    if (ostream.bytes_written > mtu_payload) {
        printk("ble actor: chunk too large for negotiated MTU payload=%u\n", mtu_payload);
        return -EMSGSIZE;
    }

    return bt_gatt_notify(current_conn, &endpoint_svc.attrs[4], tx_buf, ostream.bytes_written);
}

static int notify_protobuf_response(const uint8_t *protobuf, uint16_t len)
{
    static uint32_t tx_message_id;
    uint32_t message_id = ++tx_message_id;
    uint16_t offset = 0U;

    while (offset < len) {
        BleChunk chunk_shape = BleChunk_init_zero;
        uint16_t chunk_len = MIN((uint16_t) sizeof(chunk_shape.data.bytes),
                                 (uint16_t) (len - offset));
        int rc;

        do {
            rc = notify_chunk(message_id, offset, &protobuf[offset], chunk_len, len,
                              (offset + chunk_len) == len);
            if (rc != -EMSGSIZE || chunk_len == 1U) {
                break;
            }
            chunk_len--;
        } while (true);

        if (rc != 0) {
            return rc;
        }

        offset += chunk_len;
    }

    return 0;
}

static void handle_protobuf_request(const uint8_t *protobuf, uint16_t len)
{
    AppRequestEvent_payload_t request = {};
    pb_istream_t istream = pb_istream_from_buffer(protobuf, len);

    if (!pb_decode(&istream, RequestEnvelope_fields, &request.envelope)) {
        printk("ble actor: request decode failed: %s\n", PB_GET_ERROR(&istream));
        return;
    }

    request.envelope.source = MessageSource_SOURCE_BLE;

    int rc = ipc_publish(AppRequestEvent, request);
    if (rc != 0) {
        printk("ble actor: failed to publish app request: %d\n", rc);
        return;
    }
}

static void reset_reassembly(void)
{
    rx_active = false;
    rx_message_id = 0U;
    rx_total_size = 0U;
    rx_len = 0U;
}

static void handle_chunk_frame(const BleEndpointFrame_payload_t *frame)
{
    BleChunk chunk = BleChunk_init_zero;
    pb_istream_t istream = pb_istream_from_buffer(frame->protobuf, frame->len);

    if (!pb_decode(&istream, BleChunk_fields, &chunk)) {
        printk("ble actor: chunk decode failed: %s\n", PB_GET_ERROR(&istream));
        reset_reassembly();
        return;
    }

    if (chunk.total_size == 0U || chunk.total_size > sizeof(rx_buf) ||
        chunk.data.size > sizeof(chunk.data.bytes) ||
        (chunk.offset + chunk.data.size) > chunk.total_size) {
        printk("ble actor: invalid chunk message_id=%u offset=%u total=%u data=%u\n",
               chunk.message_id, chunk.offset, chunk.total_size, chunk.data.size);
        reset_reassembly();
        return;
    }

    if (!rx_active || rx_message_id != chunk.message_id) {
        if (chunk.offset != 0U) {
            printk("ble actor: chunk missing start message_id=%u offset=%u\n",
                   chunk.message_id, chunk.offset);
            reset_reassembly();
            return;
        }

        rx_active = true;
        rx_message_id = chunk.message_id;
        rx_total_size = chunk.total_size;
        rx_len = 0U;
    }

    if (chunk.total_size != rx_total_size || chunk.offset != rx_len) {
        printk("ble actor: unexpected chunk message_id=%u offset=%u expected=%u\n",
               chunk.message_id, chunk.offset, rx_len);
        reset_reassembly();
        return;
    }

    memcpy(&rx_buf[rx_len], chunk.data.bytes, chunk.data.size);
    rx_len += chunk.data.size;

    if (!chunk.final) {
        return;
    }

    if (rx_len != rx_total_size) {
        printk("ble actor: final chunk before complete len=%u total=%u\n", rx_len, rx_total_size);
        reset_reassembly();
        return;
    }

    printk("ble actor: reassembled protobuf_len=%u\n", rx_len);
    handle_protobuf_request(rx_buf, rx_len);
    reset_reassembly();
}

IPC_ACTOR_DEFINE(ble_actor, "ble", 2048, K_PRIO_PREEMPT(7), 8,
                 IPC_MESSAGE_MAX(BleEndpointFrame, AppResponseEvent, LongPressEvent,
                                 BleRestartAdvertising));

IPC_START_HOOK(ble_actor, on_ble_start)
{
    ARG_UNUSED(self);

    start_ble();
}

IPC_ACTOR_HANDLE(ble_actor, LongPressEvent, on_long_press_event)
{
    ARG_UNUSED(self);
    ARG_UNUSED(msg);
    ARG_UNUSED(raw_msg);

    printk("ble actor: long press event received, restarting advertising\n");
    start_ble();
}

IPC_ACTOR_HANDLE(ble_actor, BleEndpointFrame, on_endpoint_frame)
{
    ARG_UNUSED(self);
    ARG_UNUSED(raw_msg);

    printk("ble actor: chunk_len=%u\n", msg->len);
    handle_chunk_frame(msg);
}

IPC_ACTOR_HANDLE(ble_actor, AppResponseEvent, on_app_response_event)
{
    ARG_UNUSED(self);
    ARG_UNUSED(raw_msg);

    if (msg->envelope.source != MessageSource_SOURCE_BLE) {
        return;
    }

    uint8_t reply[BLE_ENDPOINT_MAX_PROTOBUF_SIZE];
    pb_ostream_t ostream = pb_ostream_from_buffer(reply, sizeof(reply));

    if (!pb_encode(&ostream, ResponseEnvelope_fields, &msg->envelope)) {
        printk("ble actor: response encode failed: %s\n", PB_GET_ERROR(&ostream));
        return;
    }

    int rc = notify_protobuf_response(reply, ostream.bytes_written);
    if (rc != 0 && rc != -ENOTCONN) {
        printk("ble actor: response notify failed: %d\n", rc);
    }
}

IPC_ACTOR_HANDLE(ble_actor, BleRestartAdvertising, on_restart_advertising)
{
    ARG_UNUSED(self);
    (void) msg;
    ARG_UNUSED(raw_msg);

    int rc = start_advertising();
    if (rc != 0) {
        printk("ble actor: advertising restart failed: %d\n", rc);
    }
}
