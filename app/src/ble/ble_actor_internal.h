#pragma once

#include <stdint.h>

#include <ipc.h>

#include "assets/proto/ble_transport.pb.h"

#define BLE_ENDPOINT_MAX_PROTOBUF_SIZE 512U

/* GATT write/notify payload: protobuf BleChunk bytes. */
IPC_CMD_DEFINE_LOCAL(BleEndpointFrame, {
    uint16_t len;
    uint8_t protobuf[sizeof(BleChunk)];
});
