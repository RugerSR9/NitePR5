#include "fs_state.h"
#include "nitepr5.h"
#include "cheats.h"
#include "cJSON.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <inttypes.h>

#ifndef S_IRWXU
#define S_IRWXU 0700
#endif

int fs_mkdirs(void)
{
    if (mkdir(DATA_DIR, 0777) != 0 && errno != EEXIST) {
        /* still try cheats dir */
    }
    if (mkdir(CHEATS_DIR, 0777) != 0 && errno != EEXIST) {
        return -1;
    }
    (void)S_IRWXU;
    return 0;
}

int fs_cheat_join(const char *filename, char *out, size_t cap)
{
    if (out == NULL || cap < 8) {
        return -1;
    }
    if (!nitepr5_valid_cheat_filename(filename)) {
        return -1;
    }
    if (strchr(filename, '/') != NULL || strchr(filename, '\\') != NULL) {
        return -1;
    }
    snprintf(out, cap, "%s/%s", CHEATS_DIR, filename);
    return 0;
}

int fs_read_file(const char *path, char **out, size_t *n)
{
    FILE *f;
    long sz;
    char *buf;

    if (path == NULL || out == NULL || n == NULL) {
        return -1;
    }
    *out = NULL;
    *n = 0;
    f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    sz = ftell(f);
    if (sz < 0 || sz > 1024 * 1024) {
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    buf = (char *)malloc((size_t)sz + 1);
    if (buf == NULL) {
        fclose(f);
        return -1;
    }
    if (sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return -1;
    }
    buf[sz] = 0;
    fclose(f);
    *out = buf;
    *n = (size_t)sz;
    return 0;
}

int fs_write_file(const char *path, const void *data, size_t n)
{
    FILE *f;
    size_t w;

    if (path == NULL || (n > 0 && data == NULL)) {
        return -1;
    }
    f = fopen(path, "wb");
    if (f == NULL) {
        return -1;
    }
    w = n ? fwrite(data, 1, n, f) : 0;
    if (fclose(f) != 0) {
        return -1;
    }
    return w == n ? 0 : -1;
}

static void add_u64(cJSON *obj, const char *key, uint64_t v)
{
    char num[32];

    snprintf(num, sizeof num, "%" PRIu64, v);
    cJSON_AddRawToObject(obj, key, num);
}

int fs_state_save(void)
{
    nitepr5_state_t *st = nitepr5_state();
    cJSON *root;
    cJSON *frs;
    cJSON *ens;
    char *printed;
    int i;
    int rc;

    root = cJSON_CreateObject();
    if (root == NULL) {
        return -1;
    }
    cJSON_AddBoolToObject(root, "armed", st->armed ? 1 : 0);
    cJSON_AddNumberToObject(root, "pid", (double)st->pid);
    cJSON_AddNumberToObject(root, "freeze_count", st->freeze_count);

    frs = cJSON_AddArrayToObject(root, "freezes");
    for (i = 0; i < st->freeze_count; i++) {
        cJSON *row = cJSON_CreateObject();
        char hex[FREEZE_DATA_MAX * 2 + 1];

        add_u64(row, "addr", st->freezes[i].addr);
        nitepr5_hex_encode(st->freezes[i].data, st->freezes[i].n, hex, sizeof hex);
        cJSON_AddStringToObject(row, "data", hex);
        cJSON_AddItemToArray(frs, row);
    }

    ens = cJSON_AddArrayToObject(root, "enabled");
    for (i = 0; i < st->enabled_count; i++) {
        cJSON_AddItemToArray(ens, cJSON_CreateString(st->enabled[i]));
    }

    if (st->cheat.loaded && st->cheat.raw_json != NULL && st->cheat.raw_json[0]) {
        cJSON *ch = cJSON_Parse(st->cheat.raw_json);
        if (ch != NULL) {
            cJSON_AddItemToObject(root, "cheat", ch);
        } else {
            cJSON_AddNullToObject(root, "cheat");
        }
    } else {
        cJSON_AddNullToObject(root, "cheat");
    }

    printed = cJSON_Print(root);
    cJSON_Delete(root);
    if (printed == NULL) {
        return -1;
    }
    rc = fs_write_file(STATE_PATH, printed, strlen(printed));
    cJSON_free(printed);
    return rc;
}

int fs_state_load(void)
{
    nitepr5_state_t *st = nitepr5_state();
    char *text = NULL;
    size_t n = 0;
    cJSON *root;
    cJSON *frs;
    cJSON *ens;
    cJSON *ch;
    cJSON *it;
    int i;

    if (fs_read_file(STATE_PATH, &text, &n) != 0) {
        return -1;
    }
    root = cJSON_Parse(text);
    free(text);
    if (root == NULL) {
        return -1;
    }

    it = cJSON_GetObjectItemCaseSensitive(root, "pid");
    if (cJSON_IsNumber(it) && it->valuedouble >= 0) {
        st->pid = (uint32_t)it->valuedouble;
    }

    frs = cJSON_GetObjectItemCaseSensitive(root, "freezes");
    st->freeze_count = 0;
    if (cJSON_IsArray(frs)) {
        int sz = cJSON_GetArraySize(frs);
        if (sz > FREEZE_MAX) {
            sz = FREEZE_MAX;
        }
        for (i = 0; i < sz; i++) {
            cJSON *row = cJSON_GetArrayItem(frs, i);
            cJSON *addr;
            cJSON *data;
            size_t dn = 0;

            if (!cJSON_IsObject(row)) {
                continue;
            }
            addr = cJSON_GetObjectItemCaseSensitive(row, "addr");
            data = cJSON_GetObjectItemCaseSensitive(row, "data");
            if (!cJSON_IsNumber(addr) || !cJSON_IsString(data)) {
                continue;
            }
            if (addr->valuedouble < 0) {
                continue;
            }
            if (nitepr5_parse_hex(data->valuestring, st->freezes[st->freeze_count].data,
                                  FREEZE_DATA_MAX, &dn) != 0 ||
                dn < 1 || dn > FREEZE_DATA_MAX) {
                continue;
            }
            st->freezes[st->freeze_count].addr = (uint64_t)addr->valuedouble;
            st->freezes[st->freeze_count].n = (uint8_t)dn;
            st->freeze_count++;
        }
    }

    cheats_clear();
    ch = cJSON_GetObjectItemCaseSensitive(root, "cheat");
    if (cJSON_IsObject(ch)) {
        char *js = cJSON_PrintUnformatted(ch);
        if (js != NULL) {
            (void)cheats_load_json(js, strlen(js));
            cJSON_free(js);
        }
    }

    ens = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    st->enabled_count = 0;
    if (cJSON_IsArray(ens)) {
        int sz = cJSON_GetArraySize(ens);
        const char *names[ENABLED_MAX];
        int nc = 0;
        for (i = 0; i < sz && nc < ENABLED_MAX; i++) {
            cJSON *s = cJSON_GetArrayItem(ens, i);
            if (cJSON_IsString(s) && s->valuestring) {
                names[nc++] = s->valuestring;
            }
        }
        cheats_replace_enabled(names, nc);
    }

    it = cJSON_GetObjectItemCaseSensitive(root, "armed");
    st->armed = cJSON_IsTrue(it) ? 1 : 0;

    cJSON_Delete(root);
    return 0;
}
