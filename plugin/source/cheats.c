#include "cheats.h"
#include "dbg_client.h"
#include "fs_state.h"
#include "nitepr5.h"
#include "notify.h"
#include "cJSON.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copy_str(char *dst, size_t cap, const char *src)
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

void cheats_clear(void)
{
    nitepr5_state_t *st = nitepr5_state();

    free(st->cheat.raw_json);
    memset(&st->cheat, 0, sizeof st->cheat);
}

int cheats_find_mod(const char *name)
{
    nitepr5_state_t *st = nitepr5_state();
    int i;

    if (name == NULL || !st->cheat.loaded) {
        return -1;
    }
    for (i = 0; i < st->cheat.mod_count; i++) {
        if (strcmp(st->cheat.mods[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int cheats_replace_enabled(const char *const *names, int count)
{
    nitepr5_state_t *st = nitepr5_state();
    int i;

    st->enabled_count = 0;
    if (names == NULL || count <= 0) {
        return 0;
    }
    for (i = 0; i < count && st->enabled_count < ENABLED_MAX; i++) {
        if (names[i] == NULL || names[i][0] == 0) {
            continue;
        }
        copy_str(st->enabled[st->enabled_count], NAME_LEN, names[i]);
        st->enabled_count++;
    }
    return 0;
}

static int enabled_contains(const nitepr5_state_t *st, const char *name)
{
    int i;

    for (i = 0; i < st->enabled_count; i++) {
        if (strcmp(st->enabled[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void enabled_set(nitepr5_state_t *st, const char *name, int on)
{
    int i;

    if (on) {
        if (enabled_contains(st, name)) {
            return;
        }
        if (st->enabled_count >= ENABLED_MAX) {
            return;
        }
        copy_str(st->enabled[st->enabled_count], NAME_LEN, name);
        st->enabled_count++;
        return;
    }
    for (i = 0; i < st->enabled_count; i++) {
        if (strcmp(st->enabled[i], name) == 0) {
            int j;
            for (j = i; j + 1 < st->enabled_count; j++) {
                memcpy(st->enabled[j], st->enabled[j + 1], NAME_LEN);
            }
            st->enabled_count--;
            st->enabled[st->enabled_count][0] = 0;
            return;
        }
    }
}

static int parse_patch(cJSON *raw, cheat_patch_t *out)
{
    cJSON *offset;
    cJSON *on;
    cJSON *off;
    size_t on_n = 0;
    size_t off_n = 0;
    char *end = NULL;

    if (!cJSON_IsObject(raw) || out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof *out);
    offset = cJSON_GetObjectItemCaseSensitive(raw, "offset");
    on = cJSON_GetObjectItemCaseSensitive(raw, "on");
    off = cJSON_GetObjectItemCaseSensitive(raw, "off");
    if (!cJSON_IsString(offset) || offset->valuestring == NULL || offset->valuestring[0] == 0) {
        return -1;
    }
    if (!cJSON_IsString(on) || !cJSON_IsString(off)) {
        return -1;
    }
    out->offset = strtoull(offset->valuestring, &end, 16);
    if (end == offset->valuestring) {
        return -1;
    }
    if (nitepr5_parse_hex(on->valuestring, out->on, PATCH_BYTES_MAX, &on_n) != 0 || on_n < 1) {
        return -1;
    }
    if (nitepr5_parse_hex(off->valuestring, out->off, PATCH_BYTES_MAX, &off_n) != 0 || off_n < 1) {
        return -1;
    }
    out->on_n = (uint16_t)on_n;
    out->off_n = (uint16_t)off_n;
    return 0;
}

static int parse_mod(cJSON *raw, cheat_mod_t *out)
{
    cJSON *name;
    cJSON *memory;
    int i;
    int n;

    if (!cJSON_IsObject(raw) || out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof *out);
    name = cJSON_GetObjectItemCaseSensitive(raw, "name");
    memory = cJSON_GetObjectItemCaseSensitive(raw, "memory");
    if (!cJSON_IsString(name) || name->valuestring == NULL || name->valuestring[0] == 0) {
        return -1;
    }
    if (!cJSON_IsArray(memory)) {
        return -1;
    }
    copy_str(out->name, sizeof out->name, name->valuestring);
    n = cJSON_GetArraySize(memory);
    if (n > PATCH_MAX) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        if (parse_patch(cJSON_GetArrayItem(memory, i), &out->patches[out->patch_count]) != 0) {
            return -1;
        }
        out->patch_count++;
    }
    return 0;
}

int cheats_load_json(const char *json, size_t n)
{
    nitepr5_state_t *st = nitepr5_state();
    cJSON *root;
    cJSON *mods;
    cJSON *item;
    cheat_file_t tmp;
    int i;
    int mc;

    (void)n;
    if (json == NULL) {
        return -1;
    }
    root = cJSON_Parse(json);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return -1;
    }

    memset(&tmp, 0, sizeof tmp);
    item = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        cJSON_Delete(root);
        return -1;
    }
    copy_str(tmp.name, sizeof tmp.name, item->valuestring);

    item = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        cJSON_Delete(root);
        return -1;
    }
    copy_str(tmp.id, sizeof tmp.id, item->valuestring);

    item = cJSON_GetObjectItemCaseSensitive(root, "version");
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        cJSON_Delete(root);
        return -1;
    }
    copy_str(tmp.version, sizeof tmp.version, item->valuestring);

    item = cJSON_GetObjectItemCaseSensitive(root, "process");
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        cJSON_Delete(root);
        return -1;
    }
    copy_str(tmp.process, sizeof tmp.process, item->valuestring[0] ? item->valuestring : EBOOT_NAME);

    mods = cJSON_GetObjectItemCaseSensitive(root, "mods");
    if (!cJSON_IsArray(mods)) {
        cJSON_Delete(root);
        return -1;
    }
    mc = cJSON_GetArraySize(mods);
    if (mc > MOD_MAX) {
        cJSON_Delete(root);
        return -1;
    }
    for (i = 0; i < mc; i++) {
        if (parse_mod(cJSON_GetArrayItem(mods, i), &tmp.mods[tmp.mod_count]) != 0) {
            cJSON_Delete(root);
            return -1;
        }
        tmp.mod_count++;
    }

    tmp.raw_json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (tmp.raw_json == NULL) {
        return -1;
    }
    tmp.loaded = 1;

    cheats_clear();
    st->cheat = tmp;
    return 0;
}

int cheats_load_filename(const char *filename)
{
    char path[512];
    char *text = NULL;
    size_t n = 0;
    int rc;

    if (fs_cheat_join(filename, path, sizeof path) != 0) {
        return -1;
    }
    if (fs_read_file(path, &text, &n) != 0) {
        return -1;
    }
    rc = cheats_load_json(text, n);
    free(text);
    return rc;
}

static void sanitize_component(char *dst, size_t cap, const char *src)
{
    size_t i = 0;

    if (cap == 0) {
        return;
    }
    while (src && *src && i + 1 < cap) {
        char c = *src++;
        if (!(isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-')) {
            c = '_';
        }
        dst[i++] = c;
    }
    dst[i] = 0;
}

void cheats_save_goldhen_file(void)
{
    nitepr5_state_t *st = nitepr5_state();
    char id[CHEAT_ID_LEN];
    char ver[CHEAT_VER_LEN];
    char fname[160];
    char path[512];

    if (!st->cheat.loaded || st->cheat.raw_json == NULL) {
        return;
    }
    sanitize_component(id, sizeof id, st->cheat.id);
    sanitize_component(ver, sizeof ver, st->cheat.version);
    if (id[0] == 0) {
        return;
    }
    snprintf(fname, sizeof fname, "%s_%s.json", id, ver[0] ? ver : "00.00");
    if (fs_cheat_join(fname, path, sizeof path) != 0) {
        return;
    }
    (void)fs_write_file(path, st->cheat.raw_json, strlen(st->cheat.raw_json));
}

static int write_mod(uint32_t pid, const cheat_mod_t *mod, int enabled)
{
    nitepr5_state_t *st = nitepr5_state();
    uint64_t base = 0;
    int i;

    if (!dbg_connected()) {
        if (dbg_connect() != 0) {
            st->dbg = 0;
            notify_dbg_missing();
            return -1;
        }
        st->dbg = 1;
        notify_dbg_recovered();
    }
    if (dbg_module_base(pid, st->cheat.process, &base) != 0) {
        return -1;
    }
    for (i = 0; i < mod->patch_count; i++) {
        const uint8_t *data = enabled ? mod->patches[i].on : mod->patches[i].off;
        uint32_t n = enabled ? mod->patches[i].on_n : mod->patches[i].off_n;
        if (dbg_proc_write(pid, base + mod->patches[i].offset, data, n) != 0) {
            st->dbg = 0;
            notify_dbg_missing();
            return -1;
        }
    }
    st->dbg = 1;
    return 0;
}

int cheats_toggle(const char *name, int enabled)
{
    nitepr5_state_t *st = nitepr5_state();
    int idx;

    idx = cheats_find_mod(name);
    if (idx < 0) {
        return -1;
    }
    enabled_set(st, name, enabled ? 1 : 0);
    (void)fs_state_save();
    if (st->pid != 0) {
        (void)write_mod(st->pid, &st->cheat.mods[idx], enabled ? 1 : 0);
    }
    return 0;
}

int cheats_apply_enabled(void)
{
    nitepr5_state_t *st = nitepr5_state();
    int i;

    if (!st->cheat.loaded || st->pid == 0) {
        return 0;
    }
    for (i = 0; i < st->cheat.mod_count; i++) {
        if (enabled_contains(st, st->cheat.mods[i].name)) {
            if (write_mod(st->pid, &st->cheat.mods[i], 1) != 0) {
                return -1;
            }
        }
    }
    return 0;
}
