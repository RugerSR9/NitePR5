#include "overlay.h"

#include <pthread.h>
#include <stdio.h>
#include <time.h>

static overlay_state_t g_st;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

overlay_state_t *overlay_state(void)
{
    return &g_st;
}

void overlay_lock(void)
{
    pthread_mutex_lock(&g_mu);
}

void overlay_unlock(void)
{
    pthread_mutex_unlock(&g_mu);
}

uint64_t overlay_now_ms(void)
{
    struct timespec ts;

    ts.tv_sec = 0;
    ts.tv_nsec = 0;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

void overlay_set_error(const char *msg)
{
    overlay_copy_str(g_st.error, ERR_LEN, msg ? msg : "error");
}

void overlay_clear_error(void)
{
    g_st.error[0] = 0;
}

int overlay_hex_nibble(char c)
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

int overlay_parse_hex(const char *s, uint8_t *out, size_t max, size_t *n)
{
    const char *p;
    size_t hex_n;
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
    while (hex_n > 0 && (p[hex_n - 1] == ' ' || p[hex_n - 1] == '\t' || p[hex_n - 1] == '\n' ||
                         p[hex_n - 1] == '\r')) {
        hex_n--;
    }
    if (hex_n == 0 || (hex_n % 2) != 0 || (hex_n / 2) > max) {
        return -1;
    }
    for (i = 0; i < hex_n; i += 2) {
        int hi = overlay_hex_nibble(p[i]);
        int lo = overlay_hex_nibble(p[i + 1]);
        if (hi < 0 || lo < 0) {
            return -1;
        }
        out[i / 2] = (uint8_t)((hi << 4) | lo);
    }
    *n = hex_n / 2;
    return 0;
}

void overlay_hex_encode(const uint8_t *in, size_t n, char *out, size_t cap)
{
    static const char *digits = "0123456789abcdef";
    size_t i;

    if (out == NULL || cap == 0) {
        return;
    }
    if (in == NULL || (n * 2 + 1) > cap) {
        out[0] = 0;
        return;
    }
    for (i = 0; i < n; i++) {
        out[i * 2] = digits[(in[i] >> 4) & 0xf];
        out[i * 2 + 1] = digits[in[i] & 0xf];
    }
    out[n * 2] = 0;
}

int overlay_parse_u64(const char *s, uint64_t *out)
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

void overlay_copy_str(char *dst, size_t cap, const char *src)
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

uint64_t overlay_align16(uint64_t addr)
{
    return addr - (addr % 16ull);
}
