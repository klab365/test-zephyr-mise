#include <errno.h>
#include <string.h>

#include "app_events.h"

IPC_EVENT_DEFINE(AppMgmtRequestEvent);

void app_mgmt_respond(struct app_mgmt_transaction *transaction, int status,
                      uint32_t value, const uint8_t *data, uint16_t data_len)
{
    if (transaction == NULL) {
        return;
    }

    transaction->status = status;
    transaction->value = value;
    transaction->data_len = 0U;

    if (status == 0 && data != NULL) {
        if (data_len > sizeof(transaction->data)) {
            transaction->status = -EMSGSIZE;
        } else {
            memcpy(transaction->data, data, data_len);
            transaction->data_len = data_len;
        }
    }

    k_sem_give(&transaction->completed);
}
