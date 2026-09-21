#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/sys/util.h>

#include "app_mcumgr_decode.h"
#include "app_mcumgr_encode.h"
#include "mcumgr_app.h"

#define ECHO_MGMT_COMMAND_ID 0U

static int echo_mcumgr_handler(struct smp_streamer *ctxt)
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

    rc = app_mcumgr_dispatch_request(APP_MGMT_CMD_ECHO, 0U, request.app_data_m.value,
                                     request.app_data_m.len, &transaction);
    if (rc != 0) {
        return MGMT_ERR_EUNKNOWN;
    }

    response.app_data_m.value = transaction.data;
    response.app_data_m.len = transaction.data_len;
    return app_mcumgr_encode_response(
        ctxt, (app_mcumgr_encoder_t)cbor_encode_echo_response, &response);
}

static int echo_mcumgr_register(void)
{
    return app_mcumgr_register_write_command(ECHO_MGMT_COMMAND_ID, echo_mcumgr_handler);
}

SYS_INIT(echo_mcumgr_register, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
