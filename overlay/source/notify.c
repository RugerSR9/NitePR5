#include "overlay.h"

#include <stddef.h>
#include <string.h>

typedef struct notify_request {
    char useless1[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int device, notify_request_t *req, size_t size, int unk);

void overlay_notify(const char *message)
{
    notify_request_t req;
    size_t i;

    memset(&req, 0, sizeof req);
    if (message != NULL) {
        for (i = 0; i < sizeof req.message - 1 && message[i]; i++) {
            req.message[i] = message[i];
        }
        req.message[i] = 0;
    }
    sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}
