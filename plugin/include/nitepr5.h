#ifndef NITEPR5_H
#define NITEPR5_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

#define NITEPR5_TITLE_ID "NTPR50001"
#define NITEPR5_VERSION  "0.574"
#define NITEPR5_BASENAME "nitepr5"

#define NITEPR5_HTTP_PORT 1745
#define NITEPR5_DBG_PORT  744
#define NITEPR5_DBG_HOST  "127.0.0.1"

#define FREEZE_MAX      32
#define FREEZE_DATA_MAX 8
#define POLL_TIMEOUT_MS 67 /* ~15 Hz freeze cadence */

#define WATCH_MAX            64
#define WATCH_LABEL_LEN      64
#define READ_MAX             4096u
#define WRITE_MAX            4096u
#define HEX_PEEPHOLE_DEFAULT 512u

#define ENABLED_MAX     64
#define NAME_LEN        128
#define CHEAT_ID_LEN    32
#define CHEAT_VER_LEN   24
#define PROCESS_LEN     64
#define CHEAT_NAME_LEN  128
#define PATCH_MAX       16
#define MOD_MAX         64
#define PATCH_BYTES_MAX 256

#define DATA_DIR         "/data/nitepr5"
#define CHEATS_DIR       "/data/nitepr5/cheats"
#define STATE_PATH       "/data/nitepr5/state.json"
#define OVERLAY_ELF_PATH "/data/nitepr5/overlay.elf"
#define OVERLAY_ELF_ALT  "/data/etaHEN/plugins/overlay.elf"
#define OVERLAY_ALIVE_PATH "/data/nitepr5/overlay.alive"
#define OVERLAY_ALIVE_PATH_TMP "/tmp/nitepr5.overlay.alive"

#define EBOOT_NAME          "eboot.bin"
#define EXECUTABLE_MAP_NAME "executable"

typedef struct {
    uint32_t id; /* RAM only; not persisted in state.json */
    uint64_t addr;
    uint8_t data[FREEZE_DATA_MAX];
    uint8_t n;
} freeze_entry_t;

typedef struct {
    uint32_t id;
    uint64_t addr;
    uint8_t n;
    char label[WATCH_LABEL_LEN];
} watch_entry_t;

typedef struct {
    uint64_t offset;
    uint8_t on[PATCH_BYTES_MAX];
    uint8_t off[PATCH_BYTES_MAX];
    uint16_t on_n;
    uint16_t off_n;
} cheat_patch_t;

typedef struct {
    char name[NAME_LEN];
    int patch_count;
    cheat_patch_t patches[PATCH_MAX];
} cheat_mod_t;

typedef struct {
    int loaded;
    char name[CHEAT_NAME_LEN];
    char id[CHEAT_ID_LEN];
    char version[CHEAT_VER_LEN];
    char process[PROCESS_LEN];
    int mod_count;
    cheat_mod_t mods[MOD_MAX];
    char *raw_json;
} cheat_file_t;

typedef struct {
    int armed;
    int overlay_open; /* RAM only; never state.json; not an armed flag */
    int dbg;
    uint32_t pid;
    int freeze_count;
    uint32_t next_freeze_id;
    freeze_entry_t freezes[FREEZE_MAX];
    int watch_count;
    uint32_t next_watch_id;
    watch_entry_t watches[WATCH_MAX];
    int enabled_count;
    char enabled[ENABLED_MAX][NAME_LEN];
    cheat_file_t cheat;
} nitepr5_state_t;

nitepr5_state_t *nitepr5_state(void);

static inline int nitepr5_hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* Decode a hex string into out[0..*n). Returns 0 on success. */
static inline int nitepr5_parse_hex(const char *s, uint8_t *out, size_t max, size_t *n)
{
    const char *p;
    size_t hex_n = 0;
    size_t i;

    if (s == NULL || out == NULL || n == NULL) {
        return -1;
    }
    p = s;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
        p++;
    }
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }
    hex_n = strlen(p);
    while (hex_n > 0 && (p[hex_n - 1] == ' ' || p[hex_n - 1] == '\t' ||
                         p[hex_n - 1] == '\n' || p[hex_n - 1] == '\r')) {
        hex_n--;
    }
    if (hex_n == 0 || (hex_n % 2) != 0) {
        return -1;
    }
    if ((hex_n / 2) > max) {
        return -1;
    }
    for (i = 0; i < hex_n; i += 2) {
        int hi = nitepr5_hex_nibble(p[i]);
        int lo = nitepr5_hex_nibble(p[i + 1]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        out[i / 2] = (uint8_t)((hi << 4) | lo);
    }
    *n = hex_n / 2;
    return 0;
}

static inline void nitepr5_hex_encode(const uint8_t *in, size_t n, char *out, size_t out_cap)
{
    static const char *digits = "0123456789abcdef";
    size_t i;

    if (out == NULL || out_cap == 0) {
        return;
    }
    if (in == NULL || (n * 2 + 1) > out_cap) {
        out[0] = 0;
        return;
    }
    for (i = 0; i < n; i++) {
        out[i * 2] = digits[(in[i] >> 4) & 0xf];
        out[i * 2 + 1] = digits[in[i] & 0xf];
    }
    out[n * 2] = 0;
}

/* Basename only, ^[A-Za-z0-9._-]+\.json$ */
static inline int nitepr5_valid_cheat_filename(const char *s)
{
    size_t n;
    size_t i;

    if (s == NULL) {
        return 0;
    }
    n = strlen(s);
    if (n < 6 || n > 120) {
        return 0;
    }
    if (strcmp(s + n - 5, ".json") != 0) {
        return 0;
    }
    for (i = 0; i < n - 5; i++) {
        char c = s[i];
        if (!(isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-')) {
            return 0;
        }
    }
    return 1;
}

static inline int nitepr5_ascii_ieq(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return 0;
    }
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (tolower(ca) != tolower(cb)) {
            return 0;
        }
    }
    return *a == 0 && *b == 0;
}

/* Decimal u64 (strtoull). Query addr and JSON integer text. */
static inline int nitepr5_parse_u64(const char *s, uint64_t *out)
{
    char *end = NULL;
    unsigned long long v;

    if (s == NULL || out == NULL) {
        return -1;
    }
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    if (*s == 0 || *s == '-') {
        return -1;
    }
    errno = 0;
    v = strtoull(s, &end, 10);
    if (errno == ERANGE || end == s) {
        return -1;
    }
    while (*end == ' ' || *end == '\t') {
        end++;
    }
    if (*end != 0) {
        return -1;
    }
    *out = (uint64_t)v;
    return 0;
}

static inline void nitepr5_copy_str(char *dst, size_t cap, const char *src)
{
    size_t i;

    if (dst == NULL || cap == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = 0;
        return;
    }
    for (i = 0; i + 1 < cap && src[i]; i++) {
        dst[i] = src[i];
    }
    dst[i] = 0;
}

static inline const char *nitepr5_basename(const char *name)
{
    const char *s = name ? name : "";
    const char *p;

    for (p = s; *p; p++) {
        if (*p == '/' || *p == '\\') {
            s = p + 1;
        }
    }
    return s;
}

#endif /* NITEPR5_H */
