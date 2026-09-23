#include <errno.h>
#include <string.h>

#include <pb_decode.h>
#include <pb_encode.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/printk.h>

#include "app_events.h"

#define DEMO_LOG_ENTRY_COUNT 1000U
#define DEMO_LOG_PAGE_SIZE 8U
#define LOG_PATH "/lfs/logs.bin"

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(log_storage);
static struct fs_mount_t log_mount = {
    .type = FS_LITTLEFS,
    .fs_data = &log_storage,
    .storage_dev = (void *)FIXED_PARTITION_ID(littlefs_partition),
    .mnt_point = "/lfs",
};
static bool storage_ready;

static int write_dummy_logs(void)
{
    struct fs_file_t file;
    uint8_t encoded[LogEntry_size];

    fs_file_t_init(&file);
    int rc = fs_open(&file, LOG_PATH, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
    if (rc != 0) return rc;
    for (uint32_t sequence = 0U; sequence < DEMO_LOG_ENTRY_COUNT; ++sequence) {
        LogEntry entry = LogEntry_init_zero;
        pb_ostream_t stream = pb_ostream_from_buffer(encoded, sizeof(encoded));
        entry.sequence = sequence;
        entry.timestamp_ms = 1710000000000ULL + ((uint64_t)sequence * 1000U);
        snprintk(entry.level, sizeof(entry.level), "%s", sequence % 10U ? "INFO" : "WARN");
        snprintk(entry.message, sizeof(entry.message), "Dummy log entry %u", sequence);
        if (!pb_encode(&stream, LogEntry_fields, &entry) || stream.bytes_written > UINT16_MAX) {
            rc = -EINVAL; break;
        }
        uint16_t length = stream.bytes_written;
        if (fs_write(&file, &length, sizeof(length)) != sizeof(length) ||
            fs_write(&file, encoded, length) != length) { rc = -EIO; break; }
    }
    fs_close(&file);
    return rc;
}

static int init_storage(void)
{
    int rc = fs_mount(&log_mount);
    if (rc != 0) {
        rc = fs_mkfs(FS_LITTLEFS, (uintptr_t)log_mount.storage_dev, NULL, 0);
        if (rc != 0) return rc;
        rc = fs_mount(&log_mount);
        if (rc != 0) return rc;
    }
    struct fs_dirent entry;
    rc = fs_stat(LOG_PATH, &entry);
    if (rc == -ENOENT) rc = write_dummy_logs();
    return rc;
}

static int read_entry(struct fs_file_t *file, LogEntry *entry)
{
    uint16_t length;
    uint8_t encoded[LogEntry_size];
    if (fs_read(file, &length, sizeof(length)) != sizeof(length) || length > sizeof(encoded) ||
        fs_read(file, encoded, length) != length) return -EIO;
    pb_istream_t stream = pb_istream_from_buffer(encoded, length);
    return pb_decode(&stream, LogEntry_fields, entry) ? 0 : -EINVAL;
}

IPC_ACTOR_DEFINE(log_actor, "log", 1536, K_PRIO_PREEMPT(7), 2, IPC_MESSAGE_MAX(AppRequestEvent));

IPC_START_HOOK(log_actor, on_log_start)
{
    ARG_UNUSED(self);
    int rc = init_storage();
    storage_ready = rc == 0;
    printk("log actor: storage %s (%d)\n", storage_ready ? "ready" : "failed", rc);
}

IPC_ACTOR_HANDLE(log_actor, AppRequestEvent, on_app_request_event)
{
    ARG_UNUSED(self); ARG_UNUSED(raw_msg);
    const RequestEnvelope *request = &msg->envelope;
    if (!storage_ready || request->which_payload != RequestEnvelope_get_logs_tag) return;
    uint32_t offset = MIN(request->payload.get_logs.offset, DEMO_LOG_ENTRY_COUNT);
    uint32_t count = MIN(DEMO_LOG_PAGE_SIZE, DEMO_LOG_ENTRY_COUNT - offset);
    struct fs_file_t file; fs_file_t_init(&file);
    if (fs_open(&file, LOG_PATH, FS_O_READ) != 0) return;
    LogEntry discard;
    for (uint32_t i = 0; i < offset && read_entry(&file, &discard) == 0; ++i) {}
    AppResponseEvent_payload_t response = {};
    GetLogsResponse *logs = &response.envelope.payload.get_logs;
    response.envelope.request_id = request->request_id; response.envelope.source = request->source;
    response.envelope.which_payload = ResponseEnvelope_get_logs_tag;
    logs->page_size = DEMO_LOG_PAGE_SIZE; logs->total_entries = DEMO_LOG_ENTRY_COUNT;
    for (; logs->entries_count < count; ++logs->entries_count) {
        if (read_entry(&file, &logs->entries[logs->entries_count]) != 0) break;
    }
    fs_close(&file);
    logs->next_offset = offset + logs->entries_count;
    logs->final = logs->next_offset == DEMO_LOG_ENTRY_COUNT;
    (void)ipc_publish(AppResponseEvent, response);
}
