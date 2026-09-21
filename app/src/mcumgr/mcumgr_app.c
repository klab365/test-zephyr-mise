#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/util.h>

#include "mcumgr_app.h"

#define APP_MGMT_GROUP_ID 64U
#define APP_MGMT_MAX_COMMANDS 256U

static K_MUTEX_DEFINE(app_mgmt_request_lock);
static struct mgmt_handler app_mgmt_handlers[APP_MGMT_MAX_COMMANDS];

int app_mcumgr_register_write_command(uint8_t command_id,
                                      app_mcumgr_write_handler_t handler)
{
    if (handler == NULL || app_mgmt_handlers[command_id].mh_write != NULL) {
        return -EINVAL;
    }

    app_mgmt_handlers[command_id].mh_write = handler;
    return 0;
}

int app_mcumgr_dispatch_request(enum app_mgmt_command command, uint32_t value,
                                const uint8_t *data, uint16_t data_len,
                                struct app_mgmt_transaction *transaction)
{
    AppMgmtRequestEvent_payload_t request = {
        .command = command,
        .value = value,
        .data_len = data_len,
        .transaction = transaction,
    };
    int rc;

    if (data_len > sizeof(request.data)) {
        return -EMSGSIZE;
    }
    if (data != NULL) {
        memcpy(request.data, data, data_len);
    }

    k_sem_init(&transaction->completed, 0, 1);
    transaction->status = -EIO;
    transaction->data_len = 0U;

    /* A transaction lives on this handler's stack, so serialize SMP requests. */
    k_mutex_lock(&app_mgmt_request_lock, K_FOREVER);
    rc = ipc_publish(AppMgmtRequestEvent, request);
    if (rc == 0) {
        k_sem_take(&transaction->completed, K_FOREVER);
        rc = transaction->status;
    }
    k_mutex_unlock(&app_mgmt_request_lock);

    return rc;
}

int app_mcumgr_encode_response(struct smp_streamer *ctxt,
                               app_mcumgr_encoder_t encoder, const void *response)
{
    struct net_buf *buffer = ctxt->writer->nb;
    size_t encoded_len;
    int rc;

    rc = encoder(buffer->data + buffer->len, net_buf_tailroom(buffer), response, &encoded_len);
    if (rc != ZCBOR_SUCCESS) {
        return MGMT_ERR_EMSGSIZE;
    }

    (void)net_buf_add(buffer, encoded_len);
    return MGMT_ERR_EOK;
}

static struct mgmt_group app_mgmt_group = {
    .mg_handlers = app_mgmt_handlers,
    .mg_handlers_count = ARRAY_SIZE(app_mgmt_handlers),
    .mg_group_id = APP_MGMT_GROUP_ID,
    .custom_payload = true,
};

static int app_mgmt_register(void)
{
    mgmt_register_group(&app_mgmt_group);
    return 0;
}

SYS_INIT(app_mgmt_register, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
