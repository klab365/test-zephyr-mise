#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "app_events.h"

#define DEMO_LOG_ENTRY_COUNT 100U
#define DEMO_LOG_PAGE_SIZE 4U
#define DEMO_LOG_START_TIMESTAMP_MS 1710000000000ULL
#define DEMO_LOG_INTERVAL_MS 1000U

IPC_ACTOR_DEFINE(log_actor, "log", 1024, K_PRIO_PREEMPT(7), 2,
                 IPC_MESSAGE_MAX(AppRequestEvent));

IPC_ACTOR_HANDLE(log_actor, AppRequestEvent, on_app_request_event)
{
    ARG_UNUSED(self);
    ARG_UNUSED(raw_msg);

    const RequestEnvelope *request = &msg->envelope;
    if (request->which_payload != RequestEnvelope_get_logs_tag) {
        return;
    }

    const GetLogsRequest *log_request = &request->payload.get_logs;
    uint32_t offset = MIN(log_request->offset, DEMO_LOG_ENTRY_COUNT);
    uint32_t requested = log_request->max_entries;
    uint32_t count = MIN(requested == 0U ? DEMO_LOG_PAGE_SIZE : requested,
                         DEMO_LOG_PAGE_SIZE);
    count = MIN(count, DEMO_LOG_ENTRY_COUNT - offset);

    AppResponseEvent_payload_t response = {};
    GetLogsResponse *logs = &response.envelope.payload.get_logs;

    response.envelope.request_id = request->request_id;
    response.envelope.source = request->source;
    response.envelope.which_payload = ResponseEnvelope_get_logs_tag;
    logs->entries_count = count;
    logs->next_offset = offset + count;
    logs->final = logs->next_offset == DEMO_LOG_ENTRY_COUNT;

    for (uint32_t index = 0U; index < count; ++index) {
        LogEntry *entry = &logs->entries[index];
        uint32_t sequence = offset + index;

        entry->sequence = sequence;
        entry->timestamp_ms = DEMO_LOG_START_TIMESTAMP_MS +
                              ((uint64_t) sequence * DEMO_LOG_INTERVAL_MS);
        snprintk(entry->level, sizeof(entry->level), "%s",
                 (sequence % 10U) == 0U ? "WARN" : "INFO");
        snprintk(entry->message, sizeof(entry->message), "Dummy log entry %u", sequence);
    }

    int rc = ipc_publish(AppResponseEvent, response);
    if (rc != 0) {
        printk("log actor: failed to publish response: %d\n", rc);
    }
}
