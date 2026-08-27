#ifndef NITEPR5_OVERLAY_H
#define NITEPR5_OVERLAY_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

/* B3 in-game ELF. Memory R/W is HTTP to NTPR50001 :1745 only — never :744. */

#define OVERLAY_NAME     "NitePR5"
#define OVERLAY_VERSION  "0.57"
#define PLUGIN_HOST      "127.0.0.1"
#define PLUGIN_PORT      1745

#define HEX_PEEPHOLE     512u
#define READ_MAX         4096u
#define WRITE_MAX        4096u
#define WATCH_MAX        64
#define FREEZE_MAX       32
#define FREEZE_DATA_MAX  8
#define WATCH_N_MAX      8
#define MAPS_MAX         48
#define CHEAT_MODS_MAX   32
#define LABEL_LEN        64
#define NAME_LEN         128
#define ERR_LEN          160

#define HEX_HZ           4
#define WATCH_HZ         10
#define HEX_PERIOD_MS    250
#define WATCH_PERIOD_MS  100
#define LIST_PERIOD_MS   1000
#define HTTP_TIMEOUT_MS  400

#define HUD_W            704
#define HUD_H            768
#define HUD_X            32
#define HUD_Y            48
#define FONT_W           8
#define FONT_H           8

#define HEX_COLS         16
#define HEX_ROWS         32

/* DualSense / Orbis pad bits. Do not use Share / Toolbox slots. */
#define PAD_UP           0x00000010u
#define PAD_RIGHT        0x00000020u
#define PAD_DOWN         0x00000040u
#define PAD_LEFT         0x00000080u
#define PAD_L1           0x00000400u
#define PAD_R1           0x00000800u
#define PAD_TRIANGLE     0x00001000u
#define PAD_CIRCLE       0x00002000u
#define PAD_CROSS        0x00004000u
#define PAD_SQUARE       0x00008000u
#define PAD_L3           0x00000002u
#define PAD_TOUCH        0x00100000u
#define PAD_COMBO        (PAD_L1 | PAD_R1 | PAD_TOUCH)
#define PAD_NAV          (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT | PAD_L1 | PAD_R1 | \
                          PAD_CROSS | PAD_CIRCLE | PAD_SQUARE | PAD_TRIANGLE | PAD_L3 | PAD_TOUCH)

typedef enum {
    VIEW_LIVE = 0,
    VIEW_WATCH,
    VIEW_FREEZE,
    VIEW_CHEATS,
    VIEW_COUNT
} overlay_view_t;

typedef enum {
    MODE_BROWSE = 0,
    MODE_GOTO,
    MODE_POKE,
    MODE_CONFIRM,
    MODE_MAPS
} overlay_mode_t;

typedef struct {
    uint32_t id;
    uint64_t addr;
    uint8_t n;
    char label[LABEL_LEN];
    uint8_t data[WATCH_N_MAX];
    int has_data;
} watch_row_t;

typedef struct {
    uint32_t id;
    uint64_t addr;
    uint8_t n;
    uint8_t data[FREEZE_DATA_MAX];
} freeze_row_t;

typedef struct {
    char name[NAME_LEN];
    int enabled;
} cheat_mod_row_t;

typedef struct {
    char name[33];
    uint64_t start;
    uint64_t end;
    uint16_t prot;
} map_row_t;

typedef struct {
    int open;
    int plugin_ok;
    overlay_view_t view;
    overlay_mode_t mode;
    uint32_t pid;

    uint64_t peephole;
    uint8_t hex[HEX_PEEPHOLE];
    int hex_ok;
    int hex_n;
    int cursor; /* 0 .. HEX_PEEPHOLE-1 */

    uint64_t goto_addr;
    int goto_nibble; /* 0..15, high nibble of 64-bit VA */

    int poke_width; /* 1,2,4,8 */
    uint64_t poke_addr;
    uint8_t poke_data[8];
    int poke_nibble;

    int sel; /* row in list views */

    int watch_n;
    watch_row_t watches[WATCH_MAX];
    int freeze_n;
    freeze_row_t freezes[FREEZE_MAX];

    int cheat_loaded;
    char cheat_name[NAME_LEN];
    int cheat_mod_n;
    cheat_mod_row_t mods[CHEAT_MODS_MAX];

    int map_n;
    map_row_t maps[MAPS_MAX];
    int map_sel;

    char error[ERR_LEN];
    int armed;
} overlay_state_t;

overlay_state_t *overlay_state(void);
void overlay_lock(void);
void overlay_unlock(void);

uint64_t overlay_now_ms(void);
void overlay_set_error(const char *msg);
void overlay_clear_error(void);

int overlay_hex_nibble(char c);
int overlay_parse_hex(const char *s, uint8_t *out, size_t max, size_t *n);
void overlay_hex_encode(const uint8_t *in, size_t n, char *out, size_t cap);
int overlay_parse_u64(const char *s, uint64_t *out);
void overlay_copy_str(char *dst, size_t cap, const char *src);
uint64_t overlay_align16(uint64_t addr);

void overlay_notify(const char *message);
void overlay_request_open(void);
void overlay_request_close(void);
void overlay_request_write(uint64_t addr, const uint8_t *data, uint32_t n);
void overlay_request_watch_add(uint64_t addr, uint32_t n);
void overlay_request_watch_del(uint32_t id);
void overlay_request_freeze_add(uint64_t addr, const uint8_t *data, uint32_t n);
void overlay_request_freeze_del(uint32_t id);
void overlay_request_arm(void);
void overlay_request_cheat_toggle(const char *name, int enabled);
void overlay_request_maps(void);
void overlay_on_input(uint32_t pressed);

int overlay_hooks_install(void);
void overlay_pad_poll(void);
void overlay_draw_tick(uint32_t vo_handle, uint32_t buf_idx);
int overlay_worker_start(void);

void *overlay_dlsym(const char *const *mods, const char *sym);

int detour_install(void *target, void *hook, void **orig_out);

void draw_capture_buffers(int handle, int start_index, void *const *addresses, int count,
                          const void *attr);
void draw_panel(uint32_t vo_handle, uint32_t buf_idx);

void font_glyph(unsigned char c, uint8_t rows[8]);
void hud_render(uint8_t *rgba, int w, int h);

int http_request(const char *method, const char *path, const char *body, int *status, char **out,
                 size_t *out_n);

#endif
