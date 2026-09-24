#pragma once

#include <stddef.h>
#include <stdint.h>

int storage_init(void);
int storage_reset(const char *path);
int storage_write(const char *path, const uint8_t *data, size_t size);
int storage_append(const char *path, const uint8_t *data, size_t size);
int storage_read(const char *path, uint32_t offset, uint8_t *data, size_t size, size_t *read);
