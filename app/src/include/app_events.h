#pragma once

#include <stdint.h>

#include <ipc.h>
#include <zephyr/kernel.h>

#define APP_MGMT_MAX_DATA_SIZE 256U

enum app_mgmt_command {
    APP_MGMT_CMD_ECHO,
    APP_MGMT_CMD_PING,
    APP_MGMT_CMD_SET_MATRIX_SYMBOL,
};

struct app_mgmt_transaction {
    struct k_sem completed;
    int status;
    uint32_t value;
    uint16_t data_len;
    uint8_t data[APP_MGMT_MAX_DATA_SIZE];
};

IPC_EVENT_DECLARE(AppMgmtRequestEvent, {
    enum app_mgmt_command command;
    uint32_t value;
    uint16_t data_len;
    uint8_t data[APP_MGMT_MAX_DATA_SIZE];
    struct app_mgmt_transaction *transaction;
});

void app_mgmt_respond(struct app_mgmt_transaction *transaction, int status,
                      uint32_t value, const uint8_t *data, uint16_t data_len);
