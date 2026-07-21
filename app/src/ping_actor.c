#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "app_events.h"

IPC_ACTOR_DEFINE(ping_actor, "ping", 512, K_PRIO_PREEMPT(7), 2,
                 IPC_MESSAGE_MAX(AppRequestEvent));

IPC_ACTOR_HANDLE(ping_actor, AppRequestEvent, on_app_request_event)
{
    ARG_UNUSED(self);
    ARG_UNUSED(raw_msg);

    const RequestEnvelope *request = &msg->envelope;
    if (request->which_payload != RequestEnvelope_ping_tag) {
        return;
    }

    AppResponseEvent_payload_t response = {};
    response.envelope.request_id = request->request_id;
    response.envelope.source = request->source;
    response.envelope.which_payload = ResponseEnvelope_ping_tag;
    response.envelope.payload.ping.sequence = request->payload.ping.sequence;

    int rc = ipc_publish(AppResponseEvent, response);
    if (rc != 0) {
        printk("ping actor: failed to publish response: %d\n", rc);
    }
}
