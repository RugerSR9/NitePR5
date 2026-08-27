#include "overlay.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int32_t (*gnm_flip_wl_fn)(uint32_t, uint32_t, uint32_t **, uint32_t *, uint32_t **,
                                  uint32_t *, uint32_t, uint32_t, uint32_t, uint32_t);
typedef int32_t (*gnm_flip_fn)(uint32_t, void **, uint32_t *, void **, uint32_t *, uint32_t,
                               uint32_t, uint32_t, uint64_t);
typedef int32_t (*pad_read_fn)(int32_t, void *);
typedef int32_t (*vo_reg_fn)(int32_t, int32_t, void *const *, int32_t, const void *);

static gnm_flip_wl_fn g_flip_wl_orig;
static gnm_flip_fn g_flip_orig;
static pad_read_fn g_pad_orig;
static vo_reg_fn g_vo_orig;
static uint32_t g_prev_raw;
static pthread_mutex_t g_padmu = PTHREAD_MUTEX_INITIALIZER;

extern int32_t sceGnmSubmitAndFlipCommandBuffersForWorkload(uint32_t, uint32_t, uint32_t **,
                                                            uint32_t *, uint32_t **, uint32_t *,
                                                            uint32_t, uint32_t, uint32_t, uint32_t)
    __attribute__((weak));
extern int32_t scePadReadState(int32_t, void *) __attribute__((weak));
extern int32_t scePadInit(void) __attribute__((weak));
extern int32_t scePadGetHandle(int32_t, int32_t, int32_t) __attribute__((weak));
extern int32_t scePadOpen(int32_t, int32_t, int32_t) __attribute__((weak));
extern int32_t sceUserServiceGetForegroundUser(int32_t *) __attribute__((weak));

static void combo_edge(uint32_t buttons)
{
    int combo_now = ((buttons & PAD_COMBO) == PAD_COMBO);
    int combo_prev;
    int open;
    overlay_state_t *st = overlay_state();

    pthread_mutex_lock(&g_padmu);
    combo_prev = ((g_prev_raw & PAD_COMBO) == PAD_COMBO);
    if (combo_now && !combo_prev) {
        overlay_lock();
        open = st->open;
        overlay_unlock();
        overlay_notify(open ? "NitePR5 combo (close)" : "NitePR5 combo (open)");
        if (open) {
            overlay_request_close();
        } else {
            overlay_request_open();
        }
    }
    g_prev_raw = buttons;
    pthread_mutex_unlock(&g_padmu);
}

static int32_t flip_wl_hook(uint32_t workload, uint32_t count, uint32_t **dcb, uint32_t *dcb_sz,
                            uint32_t **ccb, uint32_t *ccb_sz, uint32_t vo, uint32_t buf,
                            uint32_t mode, uint32_t arg)
{
    int32_t rc;
    if (g_flip_wl_orig) {
        rc = g_flip_wl_orig(workload, count, dcb, dcb_sz, ccb, ccb_sz, vo, buf, mode, arg);
    } else {
        rc = 0;
    }
    overlay_draw_tick(vo, buf);
    return rc;
}

static int32_t flip_hook(uint32_t count, void **dcb, uint32_t *dcb_sz, void **ccb, uint32_t *ccb_sz,
                         uint32_t vo, uint32_t buf, uint32_t mode, uint64_t arg)
{
    int32_t rc;
    if (g_flip_orig) {
        rc = g_flip_orig(count, dcb, dcb_sz, ccb, ccb_sz, vo, buf, mode, arg);
    } else {
        rc = 0;
    }
    overlay_draw_tick(vo, buf);
    return rc;
}

static int32_t vo_reg_hook(int32_t handle, int32_t start, void *const *addrs, int32_t n,
                           const void *attr)
{
    int32_t rc = 0;
    if (g_vo_orig) {
        rc = g_vo_orig(handle, start, addrs, n, attr);
    }
    draw_capture_buffers((int)handle, (int)start, addrs, (int)n, attr);
    return rc;
}

static int32_t pad_hook(int32_t handle, void *data)
{
    int32_t rc;
    uint32_t buttons = 0;
    uint32_t *btn;
    overlay_state_t *st = overlay_state();
    int open;

    if (g_pad_orig) {
        rc = g_pad_orig(handle, data);
    } else {
        rc = -1;
    }
    if (rc != 0 || data == NULL) {
        return rc;
    }
    btn = (uint32_t *)data;
    buttons = btn[0];
    pthread_mutex_lock(&g_padmu);
    {
        uint32_t prev = g_prev_raw;
        pthread_mutex_unlock(&g_padmu);
        combo_edge(buttons);
        {
            uint32_t out = buttons;
            int combo_now = ((buttons & PAD_COMBO) == PAD_COMBO);

            overlay_lock();
            open = st->open;
            overlay_unlock();
            if (combo_now) {
                out &= (uint32_t)~PAD_COMBO;
            }
            if (open) {
                if (!combo_now) {
                    uint32_t pressed = buttons & ~prev;
                    if (pressed & PAD_NAV) {
                        overlay_on_input(pressed);
                    }
                }
                out &= (uint32_t)~PAD_NAV;
            }
            btn[0] = out;
        }
    }
    return rc;
}

void overlay_pad_poll(void)
{
    static int inited;
    static int handle = -1;
    static int fail_told;
    static int ok_told;
    uint8_t data[256];
    int32_t rc;

    if (!inited) {
        int32_t uid = 0;

        inited = 1;
        if (scePadInit) {
            (void)scePadInit();
        }
        if (sceUserServiceGetForegroundUser && sceUserServiceGetForegroundUser(&uid) == 0) {
            if (scePadGetHandle) {
                handle = scePadGetHandle(uid, 0, 0);
            }
            if (handle < 0 && scePadOpen) {
                handle = scePadOpen(uid, 0, 0);
            }
        }
        if (handle < 0 && !fail_told) {
            fail_told = 1;
            overlay_notify("NitePR5 pad handle failed");
        }
    }
    if (handle < 0 || scePadReadState == NULL) {
        return;
    }
    memset(data, 0, sizeof data);
    rc = scePadReadState(handle, data);
    if (rc != 0) {
        return;
    }
    if (!ok_told) {
        ok_told = 1;
        overlay_notify("NitePR5 pad poll ok (click touchpad)");
    }
    {
        uint32_t buttons = *(uint32_t *)data;
        uint32_t prev;
        uint32_t pressed;
        int open;
        int combo_now;
        overlay_state_t *st = overlay_state();

        /* combo_edge writes g_prev_raw; capture prev first so HUD nav
         * still sees a rising edge (poll cannot swallow PAD_NAV). */
        pthread_mutex_lock(&g_padmu);
        prev = g_prev_raw;
        pthread_mutex_unlock(&g_padmu);
        combo_edge(buttons);

        overlay_lock();
        open = st->open;
        overlay_unlock();
        combo_now = ((buttons & PAD_COMBO) == PAD_COMBO);
        if (open && !combo_now) {
            pressed = buttons & ~prev;
            if (pressed & PAD_NAV) {
                overlay_on_input(pressed);
            }
        }
    }
}

int overlay_hooks_install(void)
{
    static const char *gnm_mods[] = {"libSceGnmDriverForNeoMode.sprx", "libSceGnmDriver.sprx", NULL};
    static const char *vo_mods[] = {"libSceVideoOut.sprx", "libSceVideoOutForNeoMode.sprx", NULL};
    void *flip_wl;
    void *flip;
    void *vo;
    int pad_ok = 0; /* 0.572: pad detour off; DualSense is pad poll */
    int flip_ok = 0;
    int vo_ok = 0;
    char msg[80];

    /* Keep pad_hook compiled; do not steal scePadReadState PLT. */
    (void)pad_hook;

    flip_wl = overlay_dlsym(gnm_mods, "sceGnmSubmitAndFlipCommandBuffersForWorkload");
    if (flip_wl == NULL && sceGnmSubmitAndFlipCommandBuffersForWorkload) {
        flip_wl = (void *)sceGnmSubmitAndFlipCommandBuffersForWorkload;
    }
    flip = overlay_dlsym(gnm_mods, "sceGnmSubmitAndFlipCommandBuffers");
    vo = overlay_dlsym(vo_mods, "sceVideoOutRegisterBuffers");

    if (vo && detour_install(vo, (void *)vo_reg_hook, (void **)&g_vo_orig) == 0) {
        vo_ok = 1;
    }
    if (flip_wl && detour_install(flip_wl, (void *)flip_wl_hook, (void **)&g_flip_wl_orig) == 0) {
        flip_ok = 1;
    } else if (flip && detour_install(flip, (void *)flip_hook, (void **)&g_flip_orig) == 0) {
        flip_ok = 1;
    }
    snprintf(msg, sizeof msg, "NitePR5 hooks pad=%d flip=%d vo=%d", pad_ok, flip_ok, vo_ok);
    overlay_notify(msg);
    return flip_ok ? 0 : -1;
}
