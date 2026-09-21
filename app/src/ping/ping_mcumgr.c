#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/sys/util.h>

#include "app_mcumgr_decode.h"
#include "app_mcumgr_encode.h"
#include "mcumgr_app.h"

#define PING_MGMT_COMMAND_ID 1U

static int ping_mcumgr_handler(struct smp_streamer *ctxt)
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

    rc = app_mcumgr_dispatch_request(APP_MGMT_CMD_PING, request.uint1uint, NULL, 0U,
                                     &transaction);
    if (rc != 0) {
        return MGMT_ERR_EUNKNOWN;
    }

    response.uint1uint = transaction.value;
    return app_mcumgr_encode_response(
        ctxt, (app_mcumgr_encoder_t)cbor_encode_ping_response, &response);
}

static int ping_mcumgr_register(void)
{
    return app_mcumgr_register_write_command(PING_MGMT_COMMAND_ID, ping_mcumgr_handler);
}

SYS_INIT(ping_mcumgr_register, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
