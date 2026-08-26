#include "notify.h"

#include <stddef.h>
#include <string.h>

typedef struct notify_request {
    char useless1[45];
    char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int device, notify_request_t *req, size_t size, int unk);

static int g_dbg_missing_told;

void notify_toast(const char *message)
{
    notify_request_t req;

    memset(&req, 0, sizeof req);
    if (message != NULL) {
        size_t i;
        for (i = 0; i < sizeof req.message - 1 && message[i]; i++) {
            req.message[i] = message[i];
        }
        req.message[i] = 0;
    }
    sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}

void notify_start(void)
{
    notify_toast("NitePR5 started (:1745)");
}

void notify_armed(void)
{
    notify_toast("NitePR5 freeze armed");
}

void notify_dbg_missing(void)
{
    if (g_dbg_missing_told) {
        return;
    }
    g_dbg_missing_told = 1;
    notify_toast("NitePR5: PS5Debug :744 missing");
}

void notify_dbg_recovered(void)
{
    g_dbg_missing_told = 0;
}
