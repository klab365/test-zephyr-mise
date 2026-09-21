#include "app_events.h"

IPC_ACTOR_DEFINE(ping_actor, "ping", 512, K_PRIO_PREEMPT(7), 2,
                 IPC_MESSAGE_MAX(AppMgmtRequestEvent));

IPC_ACTOR_HANDLE(ping_actor, AppMgmtRequestEvent, on_app_mgmt_request)
{
    ARG_UNUSED(self);
    ARG_UNUSED(raw_msg);

    if (msg->command != APP_MGMT_CMD_PING) {
        return;
    }

    app_mgmt_respond(msg->transaction, 0, msg->value, NULL, 0U);
}
