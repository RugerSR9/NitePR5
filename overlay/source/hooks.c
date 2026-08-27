#include "overlay.h"

#include <stdint.h>
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

extern int32_t sceGnmSubmitAndFlipCommandBuffersForWorkload(uint32_t, uint32_t, uint32_t **,
                                                            uint32_t *, uint32_t **, uint32_t *,
                                                            uint32_t, uint32_t, uint32_t, uint32_t)
    __attribute__((weak));
extern int32_t scePadReadState(int32_t, void *) __attribute__((weak));

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

    {
        int combo_now = ((buttons & PAD_COMBO) == PAD_COMBO);
        int combo_prev = ((g_prev_raw & PAD_COMBO) == PAD_COMBO);
        uint32_t out = buttons;

        overlay_lock();
        open = st->open;
        overlay_unlock();
        if (combo_now && !combo_prev) {
            if (open) {
                overlay_request_close();
            } else {
                overlay_request_open();
            }
        }
        overlay_lock();
        open = st->open;
        overlay_unlock();
        if (combo_now) {
            out &= (uint32_t)~PAD_COMBO;
        }
        if (open) {
            if (!combo_now) {
                uint32_t pressed = buttons & ~g_prev_raw;
                if (pressed & PAD_NAV) {
                    overlay_on_input(pressed);
                }
            }
            out &= (uint32_t)~PAD_NAV;
        }
        btn[0] = out;
        g_prev_raw = buttons;
    }
    return rc;
}

int overlay_hooks_install(void)
{
    static const char *gnm_mods[] = {"libSceGnmDriverForNeoMode.sprx", "libSceGnmDriver.sprx", NULL};
    static const char *pad_mods[] = {"libScePad.sprx", NULL};
    static const char *vo_mods[] = {"libSceVideoOut.sprx", "libSceVideoOutForNeoMode.sprx", NULL};
    void *flip_wl;
    void *flip;
    void *pad;
    void *vo;
    int ok = 0;

    flip_wl = overlay_dlsym(gnm_mods, "sceGnmSubmitAndFlipCommandBuffersForWorkload");
    if (flip_wl == NULL && sceGnmSubmitAndFlipCommandBuffersForWorkload) {
        flip_wl = (void *)sceGnmSubmitAndFlipCommandBuffersForWorkload;
    }
    flip = overlay_dlsym(gnm_mods, "sceGnmSubmitAndFlipCommandBuffers");
    pad = overlay_dlsym(pad_mods, "scePadReadState");
    if (pad == NULL && scePadReadState) {
        pad = (void *)scePadReadState;
    }
    vo = overlay_dlsym(vo_mods, "sceVideoOutRegisterBuffers");

    if (vo && detour_install(vo, (void *)vo_reg_hook, (void **)&g_vo_orig) == 0) {
        ok++;
    }
    if (flip_wl && detour_install(flip_wl, (void *)flip_wl_hook, (void **)&g_flip_wl_orig) == 0) {
        ok++;
    } else if (flip && detour_install(flip, (void *)flip_hook, (void **)&g_flip_orig) == 0) {
        ok++;
    }
    if (pad && detour_install(pad, (void *)pad_hook, (void **)&g_pad_orig) == 0) {
        ok++;
    }
    return ok >= 2 ? 0 : -1; /* need flip tick + pad; VideoOut optional until buffers exist */
}
