#include "app_events.h"

IPC_ACTOR_DEFINE(echo_actor, "echo", 512, K_PRIO_PREEMPT(7), 2,
                 IPC_MESSAGE_MAX(AppMgmtRequestEvent));

IPC_ACTOR_HANDLE(echo_actor, AppMgmtRequestEvent, on_app_mgmt_request)
{
    ARG_UNUSED(self);
    ARG_UNUSED(raw_msg);

    if (msg->command != APP_MGMT_CMD_ECHO) {
        return;
    }

    app_mgmt_respond(msg->transaction, 0, 0U, msg->data, msg->data_len);
}
