#include "overlay.h"

#include <stdio.h>

/* Chrome: dark translucent so the running game stays visible through the panel. */
#define COL_BG_R 12
#define COL_BG_G 14
#define COL_BG_B 22
#define COL_BG_A 168
#define COL_FG_R 230
#define COL_FG_G 232
#define COL_FG_B 236
#define COL_DIM_R 140
#define COL_DIM_G 148
#define COL_DIM_B 160
#define COL_ACC_R 80
#define COL_ACC_G 200
#define COL_ACC_B 255
#define COL_ERR_R 255
#define COL_ERR_G 96
#define COL_ERR_B 96
#define COL_SEL_R 32
#define COL_SEL_G 64
#define COL_SEL_B 96
#define COL_SEL_A 200

static void pix(uint8_t *rgba, int w, int h, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    uint8_t *p;
    if ((unsigned)x >= (unsigned)w || (unsigned)y >= (unsigned)h) {
        return;
    }
    p = rgba + ((size_t)y * (size_t)w + (size_t)x) * 4;
    p[0] = r;
    p[1] = g;
    p[2] = b;
    p[3] = a;
}

static void fill(uint8_t *rgba, int w, int h, int x, int y, int bw, int bh, uint8_t r, uint8_t g,
                 uint8_t b, uint8_t a)
{
    int iy, ix;
    for (iy = 0; iy < bh; iy++) {
        for (ix = 0; ix < bw; ix++) {
            pix(rgba, w, h, x + ix, y + iy, r, g, b, a);
        }
    }
}

static void glyph_at(uint8_t *rgba, int w, int h, int x, int y, unsigned char c, int scale, uint8_t r,
                     uint8_t g, uint8_t b)
{
    uint8_t rows[8];
    int gy, gx, sy, sx;
    font_glyph(c, rows);
    for (gy = 0; gy < 8; gy++) {
        for (gx = 0; gx < 8; gx++) {
            if (rows[gy] & (1u << gx)) {
                for (sy = 0; sy < scale; sy++) {
                    for (sx = 0; sx < scale; sx++) {
                        pix(rgba, w, h, x + gx * scale + sx, y + gy * scale + sy, r, g, b, 255);
                    }
                }
            }
        }
    }
}

static void text_at(uint8_t *rgba, int w, int h, int x, int y, const char *s, int scale, uint8_t r,
                    uint8_t g, uint8_t b)
{
    int cx = x;
    if (s == NULL) {
        return;
    }
    while (*s) {
        glyph_at(rgba, w, h, cx, y, (unsigned char)*s, scale, r, g, b);
        cx += 8 * scale;
        s++;
    }
}

static int printable(uint8_t b)
{
    return b >= 32 && b < 127;
}

static void fmt_u64(char *buf, size_t cap, uint64_t v)
{
    snprintf(buf, cap, "%016llx", (unsigned long long)v);
}

static const char *view_name(overlay_view_t v)
{
    switch (v) {
    case VIEW_LIVE:
        return "LIVE";
    case VIEW_WATCH:
        return "WATCH";
    case VIEW_FREEZE:
        return "FREEZE";
    case VIEW_CHEATS:
        return "CHEATS";
    default:
        return "?";
    }
}

void hud_render(uint8_t *rgba, int w, int h)
{
    overlay_state_t *st = overlay_state();
    char line[128];
    int y;
    int i;
    const char *tabs = "L1/R1  LIVE | WATCH | FREEZE | CHEATS";

    if (rgba == NULL) {
        return;
    }
    fill(rgba, w, h, 0, 0, w, h, COL_BG_R, COL_BG_G, COL_BG_B, COL_BG_A);
    fill(rgba, w, h, 0, 0, w, 36, 8, 10, 16, 210);
    fill(rgba, w, h, 0, 0, 4, h, COL_ACC_R, COL_ACC_G, COL_ACC_B, 220);
    text_at(rgba, w, h, 16, 8, "NitePR5", 2, COL_ACC_R, COL_ACC_G, COL_ACC_B);
    snprintf(line, sizeof line, "%s", view_name(st->view));
    text_at(rgba, w, h, 160, 12, line, 1, COL_FG_R, COL_FG_G, COL_FG_B);
    if (st->plugin_ok) {
        text_at(rgba, w, h, 240, 12, "1745 ok", 1, 96, 220, 120);
    } else {
        text_at(rgba, w, h, 240, 12, "1745 FAIL", 1, COL_ERR_R, COL_ERR_G, COL_ERR_B);
    }
    snprintf(line, sizeof line, "pid %u", st->pid);
    text_at(rgba, w, h, 360, 12, line, 1, COL_DIM_R, COL_DIM_G, COL_DIM_B);
    text_at(rgba, w, h, 16, 40, tabs, 1, COL_DIM_R, COL_DIM_G, COL_DIM_B);
    y = 56;
    if (st->error[0]) {
        text_at(rgba, w, h, 16, y, st->error, 1, COL_ERR_R, COL_ERR_G, COL_ERR_B);
        y += 12;
    }

    if (st->mode == MODE_GOTO) {
        char hex[17];
        fmt_u64(hex, sizeof hex, st->goto_addr);
        text_at(rgba, w, h, 16, y, "GOTO  (D-pad nibbles, Cross apply, Circle cancel)", 1, COL_ACC_R,
                COL_ACC_G, COL_ACC_B);
        y += 14;
        text_at(rgba, w, h, 16, y, hex, 2, COL_FG_R, COL_FG_G, COL_FG_B);
        fill(rgba, w, h, 16 + st->goto_nibble * 16, y + 18, 14, 3, COL_ACC_R, COL_ACC_G, COL_ACC_B,
             255);
        return;
    }
    if (st->mode == MODE_POKE || st->mode == MODE_CONFIRM) {
        char hex[17];
        overlay_hex_encode(st->poke_data, (size_t)st->poke_width, hex, sizeof hex);
        snprintf(line, sizeof line, "POKE  %u B @ %016llx", st->poke_width,
                 (unsigned long long)st->poke_addr);
        text_at(rgba, w, h, 16, y, line, 1, COL_ACC_R, COL_ACC_G, COL_ACC_B);
        y += 14;
        text_at(rgba, w, h, 16, y, hex, 2, COL_FG_R, COL_FG_G, COL_FG_B);
        y += 24;
        text_at(rgba, w, h, 16, y, "Up/Dn value  L/R nibble  L3 width", 1, COL_DIM_R, COL_DIM_G,
                COL_DIM_B);
        y += 14;
        text_at(rgba, w, h, 16, y, "Cross = commit write    Circle = cancel", 1, COL_FG_R, COL_FG_G,
                COL_FG_B);
        return;
    }
    if (st->mode == MODE_MAPS) {
        text_at(rgba, w, h, 16, y, "MAPS  Cross jump  Circle back", 1, COL_ACC_R, COL_ACC_G,
                COL_ACC_B);
        y += 14;
        for (i = 0; i < st->map_n && i < 40; i++) {
            uint8_t r = COL_FG_R, g = COL_FG_G, b = COL_FG_B;
            if (i == st->map_sel) {
                fill(rgba, w, h, 12, y - 1, w - 24, 10, COL_SEL_R, COL_SEL_G, COL_SEL_B, COL_SEL_A);
                r = COL_ACC_R;
                g = COL_ACC_G;
                b = COL_ACC_B;
            }
            snprintf(line, sizeof line, "%016llx  %s", (unsigned long long)st->maps[i].start,
                     st->maps[i].name);
            text_at(rgba, w, h, 16, y, line, 1, r, g, b);
            y += 10;
        }
        if (st->map_n == 0) {
            text_at(rgba, w, h, 16, y, "(no maps yet)", 1, COL_DIM_R, COL_DIM_G, COL_DIM_B);
        }
        return;
    }

    if (st->view == VIEW_LIVE) {
        int row;
        int col;
        snprintf(line, sizeof line, "addr %016llx  n=%d  X poke  Sq goto  Tri maps  L3 watch",
                 (unsigned long long)st->peephole, st->hex_ok ? st->hex_n : 0);
        text_at(rgba, w, h, 16, y, line, 1, COL_FG_R, COL_FG_G, COL_FG_B);
        y += 14;
        if (!st->hex_ok) {
            text_at(rgba, w, h, 16, y, "waiting for /read ...", 1, COL_DIM_R, COL_DIM_G, COL_DIM_B);
            return;
        }
        for (row = 0; row < HEX_ROWS; row++) {
            char addr[20];
            int x = 16;
            fmt_u64(addr, sizeof addr, st->peephole + (uint64_t)row * HEX_COLS);
            text_at(rgba, w, h, x, y, addr, 1, COL_DIM_R, COL_DIM_G, COL_DIM_B);
            x += 17 * 8;
            for (col = 0; col < HEX_COLS; col++) {
                int off = row * HEX_COLS + col;
                char hb[3];
                uint8_t r = COL_FG_R, g = COL_FG_G, b = COL_FG_B;
                if (off == st->cursor) {
                    fill(rgba, w, h, x - 1, y - 1, 18, 10, COL_SEL_R, COL_SEL_G, COL_SEL_B,
                         COL_SEL_A);
                    r = COL_ACC_R;
                    g = COL_ACC_G;
                    b = COL_ACC_B;
                }
                snprintf(hb, sizeof hb, "%02x", st->hex[off]);
                text_at(rgba, w, h, x, y, hb, 1, r, g, b);
                x += 3 * 8;
            }
            x += 8;
            for (col = 0; col < HEX_COLS; col++) {
                uint8_t by = st->hex[row * HEX_COLS + col];
                char ch[2] = {printable(by) ? (char)by : '.', 0};
                text_at(rgba, w, h, x, y, ch, 1, COL_DIM_R, COL_DIM_G, COL_DIM_B);
                x += 8;
            }
            y += 10;
        }
        text_at(rgba, w, h, 16, h - 18, "L1+R1+Touch close   Cross confirms poke", 1, COL_DIM_R,
                COL_DIM_G, COL_DIM_B);
        return;
    }

    if (st->view == VIEW_WATCH) {
        text_at(rgba, w, h, 16, y, "WATCH  <=64  ~10Hz   Square del   Cross freeze", 1, COL_FG_R,
                COL_FG_G, COL_FG_B);
        y += 14;
        if (st->watch_n == 0) {
            text_at(rgba, w, h, 16, y, "empty — add from Live with L3", 1, COL_DIM_R, COL_DIM_G,
                    COL_DIM_B);
            return;
        }
        for (i = 0; i < st->watch_n; i++) {
            char hex[17];
            uint8_t r = COL_FG_R, g = COL_FG_G, b = COL_FG_B;
            if (i == st->sel) {
                fill(rgba, w, h, 12, y - 1, w - 24, 10, COL_SEL_R, COL_SEL_G, COL_SEL_B, COL_SEL_A);
                r = COL_ACC_R;
                g = COL_ACC_G;
                b = COL_ACC_B;
            }
            if (st->watches[i].has_data) {
                overlay_hex_encode(st->watches[i].data, st->watches[i].n, hex, sizeof hex);
            } else {
                overlay_copy_str(hex, sizeof hex, "--------");
            }
            snprintf(line, sizeof line, "#%u  %016llx  %s  %s", st->watches[i].id,
                     (unsigned long long)st->watches[i].addr, hex, st->watches[i].label);
            text_at(rgba, w, h, 16, y, line, 1, r, g, b);
            y += 10;
        }
        return;
    }

    if (st->view == VIEW_FREEZE) {
        snprintf(line, sizeof line, "FREEZE  <=32  plugin ticks 15Hz  armed=%s  Cross arm",
                 st->armed ? "yes" : "no");
        text_at(rgba, w, h, 16, y, line, 1, COL_FG_R, COL_FG_G, COL_FG_B);
        y += 14;
        text_at(rgba, w, h, 16, y, "Square del   overlay does not write ticks", 1, COL_DIM_R,
                COL_DIM_G, COL_DIM_B);
        y += 14;
        if (st->freeze_n == 0) {
            text_at(rgba, w, h, 16, y, "empty — add from Watch Cross or Live poke freeze", 1,
                    COL_DIM_R, COL_DIM_G, COL_DIM_B);
            return;
        }
        for (i = 0; i < st->freeze_n; i++) {
            char hex[17];
            uint8_t r = COL_FG_R, g = COL_FG_G, b = COL_FG_B;
            if (i == st->sel) {
                fill(rgba, w, h, 12, y - 1, w - 24, 10, COL_SEL_R, COL_SEL_G, COL_SEL_B, COL_SEL_A);
                r = COL_ACC_R;
                g = COL_ACC_G;
                b = COL_ACC_B;
            }
            overlay_hex_encode(st->freezes[i].data, st->freezes[i].n, hex, sizeof hex);
            snprintf(line, sizeof line, "#%u  %016llx  %s", st->freezes[i].id,
                     (unsigned long long)st->freezes[i].addr, hex);
            text_at(rgba, w, h, 16, y, line, 1, r, g, b);
            y += 10;
        }
        return;
    }

    text_at(rgba, w, h, 16, y, "CHEATS  Cross toggle   (GoldHEN via plugin)", 1, COL_FG_R, COL_FG_G,
            COL_FG_B);
    y += 14;
    if (!st->cheat_loaded) {
        text_at(rgba, w, h, 16, y, "no cheat loaded on plugin", 1, COL_DIM_R, COL_DIM_G, COL_DIM_B);
        return;
    }
    snprintf(line, sizeof line, "%s", st->cheat_name);
    text_at(rgba, w, h, 16, y, line, 1, COL_ACC_R, COL_ACC_G, COL_ACC_B);
    y += 14;
    for (i = 0; i < st->cheat_mod_n; i++) {
        uint8_t r = COL_FG_R, g = COL_FG_G, b = COL_FG_B;
        if (i == st->sel) {
            fill(rgba, w, h, 12, y - 1, w - 24, 10, COL_SEL_R, COL_SEL_G, COL_SEL_B, COL_SEL_A);
            r = COL_ACC_R;
            g = COL_ACC_G;
            b = COL_ACC_B;
        }
        snprintf(line, sizeof line, "[%s]  %s", st->mods[i].enabled ? "ON " : "off",
                 st->mods[i].name);
        text_at(rgba, w, h, 16, y, line, 1, r, g, b);
        y += 10;
    }
}
