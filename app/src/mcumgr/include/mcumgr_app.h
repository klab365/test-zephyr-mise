#pragma once

#include <stddef.h>
#include <stdint.h>

#include <zephyr/mgmt/mcumgr/smp/smp.h>

#include "app_events.h"

typedef int (*app_mcumgr_write_handler_t)(struct smp_streamer *ctxt);
typedef int (*app_mcumgr_encoder_t)(uint8_t *payload, size_t payload_len,
                                    const void *input, size_t *encoded_len);

int app_mcumgr_register_write_command(uint8_t command_id,
                                      app_mcumgr_write_handler_t handler);

int app_mcumgr_dispatch_request(enum app_mgmt_command command, uint32_t value,
                                const uint8_t *data, uint16_t data_len,
                                struct app_mgmt_transaction *transaction);

int app_mcumgr_encode_response(struct smp_streamer *ctxt,
                               app_mcumgr_encoder_t encoder, const void *response);
