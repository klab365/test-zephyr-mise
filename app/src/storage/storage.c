#include <errno.h>

#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/printk.h>

#include "storage.h"

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(app_storage);
static struct fs_mount_t mount = {
    .type = FS_LITTLEFS, .fs_data = &app_storage,
    .storage_dev = (void *)FIXED_PARTITION_ID(littlefs_partition), .mnt_point = "/lfs",
};
K_MUTEX_DEFINE(storage_mutex);
static bool initialized;

int storage_init(void)
{
    k_mutex_lock(&storage_mutex, K_FOREVER);
    if (initialized) {
        k_mutex_unlock(&storage_mutex);
        return 0;
    }
    int rc = fs_mount(&mount);
    if (rc != 0) {
        rc = fs_mkfs(FS_LITTLEFS, (uintptr_t)mount.storage_dev, NULL, 0);
        if (rc == 0) rc = fs_mount(&mount);
    }
    initialized = rc == 0;
    printk("storage: initialization %s (%d)\n", initialized ? "complete" : "failed", rc);
    k_mutex_unlock(&storage_mutex);
    return rc;
}

int storage_reset(const char *path)
{
    k_mutex_lock(&storage_mutex, K_FOREVER);
    int rc = fs_unlink(path);
    if (rc == -ENOENT) rc = 0;
    if (rc == 0) {
        struct fs_file_t file;
        fs_file_t_init(&file);
        rc = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE);
        if (rc == 0) fs_close(&file);
    }
    if (rc != 0) printk("storage: reset %s failed: %d\n", path, rc);
    k_mutex_unlock(&storage_mutex);
    return rc;
}

int storage_write(const char *path, const uint8_t *data, size_t size)
{
    k_mutex_lock(&storage_mutex, K_FOREVER);
    struct fs_file_t file;
    fs_file_t_init(&file);
    int rc = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE);
    if (rc == 0) {
        if (fs_write(&file, data, size) != size) rc = -EIO;
        fs_close(&file);
    }
    if (rc != 0) printk("storage: write %s failed: %d\n", path, rc);
    k_mutex_unlock(&storage_mutex);
    return rc;
}

int storage_append(const char *path, const uint8_t *data, size_t size)
{
    k_mutex_lock(&storage_mutex, K_FOREVER);
    struct fs_file_t file;
    fs_file_t_init(&file);
    int rc = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE | FS_O_APPEND);
    if (rc == 0) {
        if (fs_write(&file, data, size) != size) rc = -EIO;
        fs_close(&file);
    }
    k_mutex_unlock(&storage_mutex);
    return rc;
}

int storage_read(const char *path, uint32_t offset, uint8_t *data, size_t size, size_t *read)
{
    k_mutex_lock(&storage_mutex, K_FOREVER);
    struct fs_file_t file;
    fs_file_t_init(&file);
    int rc = fs_open(&file, path, FS_O_READ);
    if (rc == 0) {
        rc = fs_seek(&file, offset, FS_SEEK_SET);
        if (rc == 0) {
            ssize_t result = fs_read(&file, data, size);
            if (result < 0) rc = result;
            else *read = result;
        }
        fs_close(&file);
    }
    k_mutex_unlock(&storage_mutex);
    return rc;
}
