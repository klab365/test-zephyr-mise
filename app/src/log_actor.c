#include <string.h>

#include <pb_encode.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "app_events.h"
#include "storage.h"

#define LOG_FILE_ID "logs"
#define LOG_PATH "/lfs/logs.bin"
#define FILE_BLOCK_SIZE 256U

static size_t encode_varint(uint8_t *encoded, size_t value)
{
    size_t len = 0;
    do {
        encoded[len] = value & 0x7fU;
        value >>= 7U;
        if (value != 0U) encoded[len] |= 0x80U;
        ++len;
    } while (value != 0U);
    return len;
}

static void publish_file(const RequestEnvelope *request)
{
    uint8_t data[FILE_BLOCK_SIZE];
    size_t read = 0;
    uint16_t wanted = MIN(request->payload.file_read.max_bytes ?
                          request->payload.file_read.max_bytes : FILE_BLOCK_SIZE, FILE_BLOCK_SIZE);
    if (storage_read(LOG_PATH, request->payload.file_read.offset, data, wanted, &read) != 0) return;

    AppResponseEvent_payload_t response = {};
    FileDataResponse *file = &response.envelope.payload.file_data;
    response.envelope.request_id = request->request_id;
    response.envelope.source = request->source;
    response.envelope.which_payload = ResponseEnvelope_file_data_tag;
    strcpy(file->file_id, LOG_FILE_ID);
    file->transfer_id = request->payload.file_read.transfer_id;
    file->offset = request->payload.file_read.offset;
    file->data.size = read;
    memcpy(file->data.bytes, data, read);
    file->final = read < wanted;
    (void)ipc_publish(AppResponseEvent, response);
}

IPC_ACTOR_DEFINE(log_actor, "log", 1024, K_PRIO_PREEMPT(7), 4,
                 IPC_MESSAGE_MAX(AppRequestEvent, LogAppendEvent));

IPC_ACTOR_HANDLE(log_actor, LogAppendEvent, on_log_append)
{
    ARG_UNUSED(self); ARG_UNUSED(raw_msg);
    uint8_t encoded[LogEntry_size];
    uint8_t record[LogEntry_size + (sizeof(size_t) * 8U + 6U) / 7U];
    pb_ostream_t stream = pb_ostream_from_buffer(encoded, sizeof(encoded));
    if (!pb_encode(&stream, LogEntry_fields, &msg->entry)) return;
    size_t length_size = encode_varint(record, stream.bytes_written);
    memcpy(record + length_size, encoded, stream.bytes_written);
    if (storage_append(LOG_PATH, record, length_size + stream.bytes_written) != 0) {
        printk("log actor: failed to append log entry\n");
    }
}

IPC_ACTOR_HANDLE(log_actor, AppRequestEvent, on_log_request)
{
    ARG_UNUSED(self); ARG_UNUSED(raw_msg);
    const RequestEnvelope *request = &msg->envelope;
    if (request->which_payload == RequestEnvelope_new_log_file_tag) {
        if (storage_reset(LOG_PATH) != 0) return;
        AppResponseEvent_payload_t response = {};
        response.envelope.request_id = request->request_id;
        response.envelope.source = request->source;
        response.envelope.which_payload = ResponseEnvelope_new_log_file_tag;
        strcpy(response.envelope.payload.new_log_file.file_id, LOG_FILE_ID);
        (void)ipc_publish(AppResponseEvent, response);
    } else if (request->which_payload == RequestEnvelope_file_read_tag &&
               strcmp(request->payload.file_read.file_id, LOG_FILE_ID) == 0) {
        publish_file(request);
    }
}
