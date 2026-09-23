#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "app_events.h"

#define DEMO_FILE_SIZE 2048U
#define DEMO_FILE_BLOCK_SIZE 256U

IPC_ACTOR_DEFINE(file_actor, "file", 768, K_PRIO_PREEMPT(7), 2,
                 IPC_MESSAGE_MAX(AppRequestEvent));

IPC_ACTOR_HANDLE(file_actor, AppRequestEvent, on_app_request_event)
{
    ARG_UNUSED(self);
    ARG_UNUSED(raw_msg);

    const RequestEnvelope *request = &msg->envelope;
    if (request->which_payload != RequestEnvelope_file_read_tag) {
        return;
    }

    const FileReadRequest *file_request = &request->payload.file_read;
    if (file_request->offset >= DEMO_FILE_SIZE) {
        printk("file actor: invalid offset=%u\n", file_request->offset);
        return;
    }

    uint32_t remaining = DEMO_FILE_SIZE - file_request->offset;
    uint32_t requested = file_request->max_bytes;
    uint16_t len = MIN((uint32_t) DEMO_FILE_BLOCK_SIZE,
                       MIN(requested == 0U ? DEMO_FILE_BLOCK_SIZE : requested, remaining));

    AppResponseEvent_payload_t response = {};
    FileDataResponse *file_data = &response.envelope.payload.file_data;

    response.envelope.request_id = request->request_id;
    response.envelope.source = request->source;
    response.envelope.which_payload = ResponseEnvelope_file_data_tag;
    file_data->transfer_id = file_request->transfer_id;
    file_data->offset = file_request->offset;
    file_data->data.size = len;
    file_data->final = (file_request->offset + len) == DEMO_FILE_SIZE;

    for (uint16_t i = 0U; i < len; ++i) {
        file_data->data.bytes[i] = (uint8_t) (file_request->offset + i);
    }

    int rc = ipc_publish(AppResponseEvent, response);
    if (rc != 0) {
        printk("file actor: failed to publish response: %d\n", rc);
    }
}
