#include <string.h>

#include <pb_encode.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#include "app_events.h"
#include "storage.h"

#define CURVE_FILE_ID "curve_1"
#define CURVE_PATH "/lfs/curve_1.bin"
/* BLE actor fragments this 512-byte response block into BLE notifications. */
#define FILE_BLOCK_SIZE 512U

BUILD_ASSERT(MeasurementCurve_size <= 1024U,
             "MeasurementCurve worst-case encoding must not exceed 1 KiB");

IPC_CMD_DEFINE_LOCAL(MeasurementTick, { uint32_t session_id; });

static bool acquisition_active;
static uint32_t active_session;
static uint32_t next_session;
static MeasurementCurve curve;
static uint8_t encoded_curve[MeasurementCurve_size];

static void schedule_tick(uint32_t session_id)
{
    MeasurementTick_payload_t tick = {.session_id = session_id};
    (void)ipc_send_after(MeasurementTick, 1000U, tick);
}

/* The curve CRC covers only the measurement data, not mutable curve metadata. */
static bool serialize_curve(uint32_t *crc, uint16_t *size)
{
    uint8_t encoded_measurement[Measurement_size];
    *crc = 0;
    for (pb_size_t i = 0; i < curve.measurements_count; ++i) {
        pb_ostream_t measurement_stream =
            pb_ostream_from_buffer(encoded_measurement, sizeof(encoded_measurement));
        if (!pb_encode(&measurement_stream, Measurement_fields, &curve.measurements[i])) return false;
        *crc = crc32_ieee_update(*crc, encoded_measurement, measurement_stream.bytes_written);
    }

    pb_ostream_t stream = pb_ostream_from_buffer(encoded_curve, sizeof(encoded_curve));
    if (!pb_encode(&stream, MeasurementCurve_fields, &curve)) return false;
    *size = stream.bytes_written;
    return true;
}

static void publish_curve(const RequestEnvelope *request)
{
    const GetMeasurementCurveRequest *request_curve = &request->payload.get_measurement_curve;
    uint8_t data[FILE_BLOCK_SIZE];
    size_t read = 0;
    uint16_t wanted = FILE_BLOCK_SIZE;
    printk("measurement actor: curve read offset=%u requested=%u\n", request_curve->offset,
           wanted);
    int rc = storage_read(CURVE_PATH, request_curve->offset, data, wanted, &read);
    if (rc != 0) {
        printk("measurement actor: curve read failed: %d\n", rc);
        return;
    }
    printk("measurement actor: curve read %u bytes\n", (unsigned int)read);

    AppResponseEvent_payload_t response = {};
    GetMeasurementCurveResponse *curve_response =
        &response.envelope.payload.get_measurement_curve;
    response.envelope.request_id = request->request_id;
    response.envelope.source = request->source;
    response.envelope.which_payload = ResponseEnvelope_get_measurement_curve_tag;
    curve_response->transfer_id = request_curve->transfer_id;
    curve_response->offset = request_curve->offset;
    curve_response->data.size = read;
    memcpy(curve_response->data.bytes, data, read);
    curve_response->final = read < wanted;
    rc = ipc_publish(AppResponseEvent, response);
    printk("measurement actor: curve response publish: %d\n", rc);
}

IPC_ACTOR_DEFINE(measurement_actor, "measurement", 3072, K_PRIO_PREEMPT(7), 4,
                 IPC_MESSAGE_MAX(AppRequestEvent, MeasurementTick));

IPC_ACTOR_HANDLE(measurement_actor, AppRequestEvent, on_measurement_request)
{
    ARG_UNUSED(self); ARG_UNUSED(raw_msg);
    const RequestEnvelope *request = &msg->envelope;
    printk("measurement actor: request id=%u payload=%d\n", request->request_id,
           request->which_payload);
    if (request->which_payload == RequestEnvelope_start_measurement_tag) {
        active_session = ++next_session;
        acquisition_active = false;
        curve = (MeasurementCurve){};
        strcpy(curve.id, CURVE_FILE_ID);
        curve.started_timestamp_ms = k_uptime_get();
        int rc = storage_reset(CURVE_PATH);
        if (rc != 0) {
            printk("measurement actor: curve reset failed: %d\n", rc);
            return;
        }
        acquisition_active = true;
        schedule_tick(active_session);

        AppResponseEvent_payload_t response = {};
        response.envelope.request_id = request->request_id;
        response.envelope.source = request->source;
        response.envelope.which_payload = ResponseEnvelope_start_measurement_tag;
        strcpy(response.envelope.payload.start_measurement.file_id, CURVE_FILE_ID);
        (void)ipc_publish(AppResponseEvent, response);
    } else if (request->which_payload == RequestEnvelope_stop_measurement_tag) {
        acquisition_active = false;
        curve.count = curve.measurements_count;
        uint32_t crc;
        uint16_t size;
        if (!serialize_curve(&crc, &size)) {
            printk("measurement actor: curve serialization failed\n");
            return;
        }
        printk("measurement actor: writing curve size=%u count=%u crc=0x%08x\n", size,
               curve.count, crc);
        uint8_t crc_trailer[sizeof(crc)];
        sys_put_le32(crc, crc_trailer);
        int rc = storage_write(CURVE_PATH, encoded_curve, size);
        if (rc == 0) rc = storage_append(CURVE_PATH, crc_trailer, sizeof(crc_trailer));
        if (rc != 0) {
            printk("measurement actor: curve write failed: %d\n", rc);
            return;
        }

        AppResponseEvent_payload_t response = {};
        response.envelope.request_id = request->request_id;
        response.envelope.source = request->source;
        response.envelope.which_payload = ResponseEnvelope_stop_measurement_tag;
        strcpy(response.envelope.payload.stop_measurement.file_id, CURVE_FILE_ID);
        (void)ipc_publish(AppResponseEvent, response);
    } else if (request->which_payload == RequestEnvelope_get_measurement_curve_tag) {
        publish_curve(request);
    }
}

IPC_ACTOR_HANDLE(measurement_actor, MeasurementTick, on_measurement_tick)
{
    ARG_UNUSED(self); ARG_UNUSED(raw_msg);
    if (!acquisition_active || msg->session_id != active_session) return;
    if (curve.measurements_count == ARRAY_SIZE(curve.measurements)) {
        acquisition_active = false;
        printk("measurement actor: curve reached its 1 KiB capacity\n");
        return;
    }
    Measurement *measurement = &curve.measurements[curve.measurements_count++];
    measurement->value = curve.measurements_count - 1U;
    curve.count = curve.measurements_count;
    schedule_tick(active_session);
}
