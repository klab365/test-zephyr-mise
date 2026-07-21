#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "app_events.h"

IPC_ACTOR_DEFINE(echo_actor, "echo", 512, K_PRIO_PREEMPT(7), 2,
                 IPC_MESSAGE_MAX(AppRequestEvent));

IPC_ACTOR_HANDLE(echo_actor, AppRequestEvent, on_app_request_event)
{
    ARG_UNUSED(self);
    ARG_UNUSED(raw_msg);

    const RequestEnvelope *request = &msg->envelope;
    if (request->which_payload != RequestEnvelope_echo_tag) {
        return;
    }

    AppResponseEvent_payload_t response = {};
    response.envelope.request_id = request->request_id;
    response.envelope.source = request->source;
    response.envelope.which_payload = ResponseEnvelope_echo_tag;
    response.envelope.payload.echo.payload.size = request->payload.echo.payload.size;
    memcpy(response.envelope.payload.echo.payload.bytes,
           request->payload.echo.payload.bytes,
           request->payload.echo.payload.size);

    int rc = ipc_publish(AppResponseEvent, response);
    if (rc != 0) {
        printk("echo actor: failed to publish response: %d\n", rc);
    }
}
