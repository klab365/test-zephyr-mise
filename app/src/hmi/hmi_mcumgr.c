#include <zephyr/mgmt/mcumgr/mgmt/mgmt.h>
#include <zephyr/sys/util.h>

#include "app_mcumgr_decode.h"
#include "app_mcumgr_encode.h"
#include "mcumgr_app.h"

#define HMI_MGMT_COMMAND_ID 2U

static int hmi_mcumgr_handler(struct smp_streamer *ctxt)
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

    rc = app_mcumgr_dispatch_request(APP_MGMT_CMD_SET_MATRIX_SYMBOL,
                                     request.matrix_symbol_m, NULL, 0U, &transaction);
    if (rc != 0) {
        return MGMT_ERR_EINVAL;
    }

    response.matrix_symbol_m = transaction.value;
    return app_mcumgr_encode_response(
        ctxt, (app_mcumgr_encoder_t)cbor_encode_set_matrix_symbol_response, &response);
}

static int hmi_mcumgr_register(void)
{
    return app_mcumgr_register_write_command(HMI_MGMT_COMMAND_ID, hmi_mcumgr_handler);
}

SYS_INIT(hmi_mcumgr_register, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
