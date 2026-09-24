#pragma once

#include <ipc.h>

#include "assets/proto/app_protocol.pb.h"

IPC_EVENT_DECLARE(AppRequestEvent, {
    RequestEnvelope envelope;
});

IPC_EVENT_DECLARE(AppResponseEvent, {
    ResponseEnvelope envelope;
});

/* Log producers publish domain records; log_actor serializes and persists them. */
IPC_EVENT_DECLARE(LogAppendEvent, {
    LogEntry entry;
});
