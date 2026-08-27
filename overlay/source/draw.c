#include "overlay.h"

#include <stdint.h>
#include <string.h>

#define FB_MAX 16

typedef struct {
    int used;
    int handle;
    int index;
    uint8_t *addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    int32_t format;
    int32_t tiling; /* 0 tile, 1 linear (VideoOut) */
} fb_slot_t;

/* VideoOut buffer attribute — OpenOrbis / public layout. */
typedef struct {
    int32_t pixelFormat;
    int32_t tilingMode;
    int32_t aspectRatio;
    uint32_t width;
    uint32_t height;
    uint32_t pitchInPixel;
    uint32_t option;
    uint32_t reserved0;
    uint64_t reserved1;
} vo_attr_t;

static fb_slot_t g_fb[FB_MAX];
static uint8_t g_hud[HUD_W * HUD_H * 4];

static size_t tiled32_off(uint32_t x, uint32_t y, uint32_t pitch_px)
{
    uint32_t mx = x / 32u;
    uint32_t my = y / 32u;
    uint32_t lx = x % 32u;
    uint32_t ly = y % 32u;
    uint32_t macros = pitch_px / 32u;
    if (macros == 0) {
        macros = 1;
    }
    return ((size_t)my * macros + mx) * 32u * 32u + (size_t)ly * 32u + lx;
}

static void blend_px(uint8_t *dst, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa, int abgr)
{
    unsigned ia = 255u - sa;
    unsigned dr, dg, db;

    if (sa == 0) {
        return;
    }
    if (abgr) {
        db = dst[0];
        dg = dst[1];
        dr = dst[2];
        dst[0] = (uint8_t)((db * ia + sb * sa) / 255u);
        dst[1] = (uint8_t)((dg * ia + sg * sa) / 255u);
        dst[2] = (uint8_t)((dr * ia + sr * sa) / 255u);
        dst[3] = 255;
    } else {
        dr = dst[0];
        dg = dst[1];
        db = dst[2];
        dst[0] = (uint8_t)((dr * ia + sr * sa) / 255u);
        dst[1] = (uint8_t)((dg * ia + sg * sa) / 255u);
        dst[2] = (uint8_t)((db * ia + sb * sa) / 255u);
        dst[3] = 255;
    }
}

void draw_capture_buffers(int handle, int start_index, void *const *addresses, int count,
                          const void *attr)
{
    const vo_attr_t *a = (const vo_attr_t *)attr;
    int i;
    uint32_t w = 1920, h = 1080, pitch = 1920;
    int32_t fmt = 0, tile = 1;

    if (addresses == NULL || count <= 0) {
        return;
    }
    if (a) {
        if (a->width) {
            w = a->width;
        }
        if (a->height) {
            h = a->height;
        }
        pitch = a->pitchInPixel ? a->pitchInPixel : w;
        fmt = a->pixelFormat;
        tile = a->tilingMode;
    }
    for (i = 0; i < count; i++) {
        int slot = -1;
        int j;
        int idx = start_index + i;
        if (addresses[i] == NULL) {
            continue;
        }
        for (j = 0; j < FB_MAX; j++) {
            if (g_fb[j].used && g_fb[j].handle == handle && g_fb[j].index == idx) {
                slot = j;
                break;
            }
        }
        if (slot < 0) {
            for (j = 0; j < FB_MAX; j++) {
                if (!g_fb[j].used) {
                    slot = j;
                    break;
                }
            }
        }
        if (slot < 0) {
            slot = i % FB_MAX;
        }
        g_fb[slot].used = 1;
        g_fb[slot].handle = handle;
        g_fb[slot].index = idx;
        g_fb[slot].addr = (uint8_t *)addresses[i];
        g_fb[slot].width = w;
        g_fb[slot].height = h;
        g_fb[slot].pitch = pitch;
        g_fb[slot].format = fmt;
        g_fb[slot].tiling = tile;
    }
}

static fb_slot_t *find_fb(uint32_t vo_handle, uint32_t buf_idx)
{
    int i;
    fb_slot_t *any = NULL;
    for (i = 0; i < FB_MAX; i++) {
        if (!g_fb[i].used) {
            continue;
        }
        any = &g_fb[i];
        if ((uint32_t)g_fb[i].handle == vo_handle && (uint32_t)g_fb[i].index == buf_idx) {
            return &g_fb[i];
        }
    }
    for (i = 0; i < FB_MAX; i++) {
        if (g_fb[i].used && (uint32_t)g_fb[i].index == buf_idx) {
            return &g_fb[i];
        }
    }
    return any;
}

void draw_panel(uint32_t vo_handle, uint32_t buf_idx)
{
    fb_slot_t *fb;
    int x, y;
    int abgr = 0;

    overlay_lock();
    hud_render(g_hud, HUD_W, HUD_H);
    overlay_unlock();

    fb = find_fb(vo_handle, buf_idx);
    if (fb == NULL || fb->addr == NULL) {
        return;
    }
    /* A8B8G8R8 family uses bit 0x200 in public pixel-format enums. */
    if ((fb->format & 0x200) != 0) {
        abgr = 1;
    }
    for (y = 0; y < HUD_H; y++) {
        int gy = HUD_Y + y;
        if ((uint32_t)gy >= fb->height) {
            break;
        }
        for (x = 0; x < HUD_W; x++) {
            int gx = HUD_X + x;
            const uint8_t *s;
            uint8_t *d;
            size_t off;
            if ((uint32_t)gx >= fb->width) {
                break;
            }
            s = g_hud + ((size_t)y * HUD_W + (size_t)x) * 4;
            if (s[3] == 0) {
                continue;
            }
            if (fb->tiling == 1) {
                off = ((size_t)gy * fb->pitch + (size_t)gx) * 4u;
            } else {
                off = tiled32_off((uint32_t)gx, (uint32_t)gy, fb->pitch) * 4u;
            }
            d = fb->addr + off;
            blend_px(d, s[0], s[1], s[2], s[3], abgr);
        }
    }
}

void overlay_draw_tick(uint32_t vo_handle, uint32_t buf_idx)
{
    overlay_state_t *st = overlay_state();
    int open;

    overlay_lock();
    open = st->open;
    overlay_unlock();
    if (!open) {
        return;
    }
    draw_panel(vo_handle, buf_idx);
}
