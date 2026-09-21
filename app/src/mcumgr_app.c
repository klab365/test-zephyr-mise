#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/mgmt/mcumgr/smp/smp.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/util.h>

#include "app_events.h"
#include "app_mcumgr_decode.h"
#include "app_mcumgr_encode.h"

/* User-defined mcumgr group: 64, commands: echo=0, ping=1, set-symbol=2. */
#define APP_MGMT_GROUP_ID 64U

static K_MUTEX_DEFINE(app_mgmt_request_lock);

static int dispatch_request(enum app_mgmt_command command, uint32_t value,
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

static int encode_response(struct smp_streamer *ctxt,
                           int (*encode)(uint8_t *, size_t, const void *, size_t *),
                           const void *response)
{
    struct net_buf *buffer = ctxt->writer->nb;
    size_t encoded_len;
    int rc;

    rc = encode(buffer->data + buffer->len, net_buf_tailroom(buffer), response, &encoded_len);
    if (rc != ZCBOR_SUCCESS) {
        return MGMT_ERR_EMSGSIZE;
    }

    (void)net_buf_add(buffer, encoded_len);
    return MGMT_ERR_EOK;
}

static int encode_echo_response(struct smp_streamer *ctxt,
                                const struct echo_response *response)
{
    return encode_response(ctxt, (int (*)(uint8_t *, size_t, const void *, size_t *))
                                      cbor_encode_echo_response,
                           response);
}

static int encode_ping_response(struct smp_streamer *ctxt,
                                const struct ping_response *response)
{
    return encode_response(ctxt, (int (*)(uint8_t *, size_t, const void *, size_t *))
                                      cbor_encode_ping_response,
                           response);
}

static int encode_set_matrix_symbol_response(
    struct smp_streamer *ctxt, const struct set_matrix_symbol_response *response)
{
    return encode_response(ctxt, (int (*)(uint8_t *, size_t, const void *, size_t *))
                                      cbor_encode_set_matrix_symbol_response,
                           response);
}

static int app_mgmt_echo(struct smp_streamer *ctxt)
{
    struct echo_request request;
    struct echo_response response;
    struct app_mgmt_transaction transaction;
    int rc;

    rc = cbor_decode_echo_request(ctxt->reader->nb->data, ctxt->reader->nb->len, &request,
                                  NULL);
    if (rc != ZCBOR_SUCCESS) {
        return MGMT_ERR_EINVAL;
    }

    rc = dispatch_request(APP_MGMT_CMD_ECHO, 0U, request.app_data_m.value,
                          request.app_data_m.len, &transaction);
    if (rc != 0) {
        return MGMT_ERR_EUNKNOWN;
    }

    response.app_data_m.value = transaction.data;
    response.app_data_m.len = transaction.data_len;
    return encode_echo_response(ctxt, &response);
}

static int app_mgmt_ping(struct smp_streamer *ctxt)
{
    struct ping_request request;
    struct ping_response response;
    struct app_mgmt_transaction transaction;
    int rc;

    rc = cbor_decode_ping_request(ctxt->reader->nb->data, ctxt->reader->nb->len, &request,
                                  NULL);
    if (rc != ZCBOR_SUCCESS) {
        return MGMT_ERR_EINVAL;
    }

    rc = dispatch_request(APP_MGMT_CMD_PING, request.uint1uint, NULL, 0U, &transaction);
    if (rc != 0) {
        return MGMT_ERR_EUNKNOWN;
    }

    response.uint1uint = transaction.value;
    return encode_ping_response(ctxt, &response);
}

static int app_mgmt_set_matrix_symbol(struct smp_streamer *ctxt)
{
    struct set_matrix_symbol_request request;
    struct set_matrix_symbol_response response;
    struct app_mgmt_transaction transaction;
    int rc;

    rc = cbor_decode_set_matrix_symbol_request(ctxt->reader->nb->data,
                                               ctxt->reader->nb->len, &request, NULL);
    if (rc != ZCBOR_SUCCESS) {
        return MGMT_ERR_EINVAL;
    }

    rc = dispatch_request(APP_MGMT_CMD_SET_MATRIX_SYMBOL, request.matrix_symbol_m, NULL, 0U,
                          &transaction);
    if (rc != 0) {
        return MGMT_ERR_EINVAL;
    }

    response.matrix_symbol_m = transaction.value;
    return encode_set_matrix_symbol_response(ctxt, &response);
}

static const struct mgmt_handler app_mgmt_handlers[] = {
    { .mh_write = app_mgmt_echo },
    { .mh_write = app_mgmt_ping },
    { .mh_write = app_mgmt_set_matrix_symbol },
};

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
