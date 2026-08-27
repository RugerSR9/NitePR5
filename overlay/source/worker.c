#include "overlay.h"
#include "cJSON.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

enum {
    CMD_NONE = 0,
    CMD_OPEN,
    CMD_CLOSE,
    CMD_WRITE,
    CMD_WATCH_ADD,
    CMD_WATCH_DEL,
    CMD_FREEZE_ADD,
    CMD_FREEZE_DEL,
    CMD_ARM,
    CMD_CHEAT_TOGGLE,
    CMD_MAPS
};

typedef struct {
    int kind;
    uint64_t addr;
    uint32_t id;
    uint32_t n;
    uint8_t data[8];
    char name[NAME_LEN];
    int enabled;
} cmd_t;

static cmd_t g_q[8];
static int g_q_n;
static pthread_mutex_t g_qmu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;

static uint64_t g_last_hex_ms;
static uint64_t g_last_watch_ms;
static uint64_t g_last_list_ms;

static int json_u64(cJSON *item, uint64_t *out)
{
    char *printed;
    char *end;
    unsigned long long v;

    if (item == NULL || out == NULL) {
        return -1;
    }
    if (cJSON_IsString(item) && item->valuestring) {
        return overlay_parse_u64(item->valuestring, out);
    }
    if (!cJSON_IsNumber(item) || item->valuedouble < 0) {
        return -1;
    }
    printed = cJSON_PrintUnformatted(item);
    if (printed == NULL) {
        return -1;
    }
    errno = 0;
    end = NULL;
    v = strtoull(printed, &end, 10);
    cJSON_free(printed);
    if (errno == ERANGE || end == printed) {
        return -1;
    }
    *out = (uint64_t)v;
    return 0;
}

static void q_push(const cmd_t *c)
{
    pthread_mutex_lock(&g_qmu);
    if (g_q_n < 8) {
        g_q[g_q_n++] = *c;
        pthread_cond_signal(&g_cv);
    }
    pthread_mutex_unlock(&g_qmu);
}

static int q_pop(cmd_t *c)
{
    int got = 0;
    pthread_mutex_lock(&g_qmu);
    if (g_q_n > 0) {
        *c = g_q[0];
        memmove(&g_q[0], &g_q[1], (size_t)(g_q_n - 1) * sizeof(cmd_t));
        g_q_n--;
        got = 1;
    }
    pthread_mutex_unlock(&g_qmu);
    return got;
}

void overlay_request_open(void)
{
    cmd_t c;
    overlay_state_t *st = overlay_state();
    memset(&c, 0, sizeof c);
    c.kind = CMD_OPEN;
    overlay_lock();
    st->open = 1;
    st->view = VIEW_LIVE;
    st->mode = MODE_BROWSE;
    overlay_unlock();
    q_push(&c);
}

void overlay_request_close(void)
{
    cmd_t c;
    overlay_state_t *st = overlay_state();
    memset(&c, 0, sizeof c);
    c.kind = CMD_CLOSE;
    overlay_lock();
    st->open = 0;
    st->mode = MODE_BROWSE;
    overlay_unlock();
    q_push(&c);
}

void overlay_request_write(uint64_t addr, const uint8_t *data, uint32_t n)
{
    cmd_t c;
    memset(&c, 0, sizeof c);
    c.kind = CMD_WRITE;
    c.addr = addr;
    c.n = n;
    if (n > 8) {
        n = 8;
        c.n = 8;
    }
    if (data) {
        memcpy(c.data, data, c.n);
    }
    q_push(&c);
}

void overlay_request_watch_add(uint64_t addr, uint32_t n)
{
    cmd_t c;
    memset(&c, 0, sizeof c);
    c.kind = CMD_WATCH_ADD;
    c.addr = addr;
    c.n = n;
    q_push(&c);
}

void overlay_request_watch_del(uint32_t id)
{
    cmd_t c;
    memset(&c, 0, sizeof c);
    c.kind = CMD_WATCH_DEL;
    c.id = id;
    q_push(&c);
}

void overlay_request_freeze_add(uint64_t addr, const uint8_t *data, uint32_t n)
{
    cmd_t c;
    memset(&c, 0, sizeof c);
    c.kind = CMD_FREEZE_ADD;
    c.addr = addr;
    c.n = n;
    if (n > FREEZE_DATA_MAX) {
        n = FREEZE_DATA_MAX;
        c.n = n;
    }
    if (data) {
        memcpy(c.data, data, c.n);
    }
    q_push(&c);
}

void overlay_request_freeze_del(uint32_t id)
{
    cmd_t c;
    memset(&c, 0, sizeof c);
    c.kind = CMD_FREEZE_DEL;
    c.id = id;
    q_push(&c);
}

void overlay_request_arm(void)
{
    cmd_t c;
    memset(&c, 0, sizeof c);
    c.kind = CMD_ARM;
    q_push(&c);
}

void overlay_request_cheat_toggle(const char *name, int enabled)
{
    cmd_t c;
    memset(&c, 0, sizeof c);
    c.kind = CMD_CHEAT_TOGGLE;
    overlay_copy_str(c.name, NAME_LEN, name);
    c.enabled = enabled;
    q_push(&c);
}

void overlay_request_maps(void)
{
    cmd_t c;
    memset(&c, 0, sizeof c);
    c.kind = CMD_MAPS;
    q_push(&c);
}

static void note_http_error(int status, const char *body)
{
    cJSON *root;
    cJSON *err;
    overlay_state_t *st = overlay_state();

    overlay_lock();
    st->plugin_ok = 0;
    overlay_unlock();
    if (body && body[0]) {
        root = cJSON_Parse(body);
        if (root) {
            err = cJSON_GetObjectItemCaseSensitive(root, "error");
            if (cJSON_IsString(err) && err->valuestring) {
                overlay_lock();
                overlay_set_error(err->valuestring);
                overlay_unlock();
                cJSON_Delete(root);
                return;
            }
            cJSON_Delete(root);
        }
    }
    overlay_lock();
    if (status == 0) {
        overlay_set_error("plugin not running / sandbox");
    } else {
        overlay_set_error("plugin HTTP error");
    }
    overlay_unlock();
    (void)status;
}

static int plugin_json(const char *method, const char *path, const char *body, cJSON **out)
{
    int status = 0;
    char *resp = NULL;
    size_t n = 0;
    cJSON *root;

    if (out) {
        *out = NULL;
    }
    if (http_request(method, path, body, &status, &resp, &n) != 0) {
        note_http_error(0, NULL);
        free(resp);
        return -1;
    }
    if (status != 200) {
        note_http_error(status, resp);
        free(resp);
        return -1;
    }
    overlay_lock();
    overlay_state()->plugin_ok = 1;
    overlay_clear_error();
    overlay_unlock();
    if (out == NULL) {
        free(resp);
        return 0;
    }
    root = cJSON_Parse(resp ? resp : "{}");
    free(resp);
    if (root == NULL) {
        return -1;
    }
    *out = root;
    return 0;
}

static void do_open(void)
{
    overlay_state_t *st = overlay_state();
    uint32_t pid;
    char body[896];
    cJSON *js = NULL;
    uint64_t fw_r = 0, fw_h = 0, fw_t = 0, f_r = 0, f_h = 0, f_t = 0, v_r = 0, v_h = 0, v_t = 0;

    /* GET /foreground needs overlay_open or armed (plugin require_dbg).
     * This ELF is injected into eboot — getpid() is the target. */
    overlay_lock();
    pid = st->pid;
    overlay_unlock();
    if (pid == 0) {
        pid = (uint32_t)getpid();
    }
    (void)overlay_hooks_resolve();
    overlay_hooks_export(&fw_r, &fw_h, &f_r, &f_h, &v_r, &v_h);
    overlay_hooks_tramps(&fw_t, &f_t, &v_t);
    snprintf(body, sizeof body,
             "{\"pid\":%u,\"flip_wl_real\":%" PRIu64 ",\"flip_wl_hook\":%" PRIu64
             ",\"flip_wl_tramp\":%" PRIu64 ",\"flip_real\":%" PRIu64 ",\"flip_hook\":%" PRIu64
             ",\"flip_tramp\":%" PRIu64 ",\"vo_real\":%" PRIu64 ",\"vo_hook\":%" PRIu64
             ",\"vo_tramp\":%" PRIu64 "}",
             pid, fw_r, fw_h, fw_t, f_r, f_h, f_t, v_r, v_h, v_t);
    if (plugin_json("POST", "/overlay/open", body, &js) == 0) {
        overlay_lock();
        st->pid = pid;
        st->plugin_ok = 1;
        overlay_unlock();
        cJSON_Delete(js);
        js = NULL;
        if (plugin_json("GET", "/maps", NULL, &js) == 0 && js) {
            cJSON *arr = cJSON_GetObjectItemCaseSensitive(js, "maps");
            uint64_t pick = 0;
            if (cJSON_IsArray(arr)) {
                int i, sz = cJSON_GetArraySize(arr);
                for (i = 0; i < sz; i++) {
                    cJSON *row = cJSON_GetArrayItem(arr, i);
                    cJSON *name = cJSON_GetObjectItemCaseSensitive(row, "name");
                    uint64_t s = 0, p = 0;
                    if (json_u64(cJSON_GetObjectItemCaseSensitive(row, "start"), &s) != 0) {
                        continue;
                    }
                    (void)json_u64(cJSON_GetObjectItemCaseSensitive(row, "prot"), &p);
                    if (name && cJSON_IsString(name) && name->valuestring &&
                        strcmp(name->valuestring, "executable") == 0 && (p & 2u)) {
                        pick = s;
                        break;
                    }
                    if (pick == 0 && (p & 2u)) {
                        pick = s;
                    }
                }
            }
            overlay_lock();
            if (st->peephole == 0 && pick != 0) {
                st->peephole = overlay_align16(pick);
            }
            overlay_unlock();
            cJSON_Delete(js);
        }
    }
}

static void do_close(void)
{
    cJSON *js = NULL;
    (void)plugin_json("POST", "/overlay/close", "{}", &js);
    cJSON_Delete(js);
    overlay_lock();
    overlay_state()->watch_n = 0;
    overlay_state()->hex_ok = 0;
    overlay_unlock();
}

static void do_write(cmd_t *c)
{
    char hex[17];
    char body[96];
    cJSON *js = NULL;

    if (c->n < 1 || c->n > WRITE_MAX) {
        overlay_lock();
        overlay_set_error("InvalidWriteSize");
        overlay_unlock();
        return;
    }
    overlay_hex_encode(c->data, c->n, hex, sizeof hex);
    snprintf(body, sizeof body, "{\"addr\":%" PRIu64 ",\"data\":\"%s\"}", c->addr, hex);
    (void)plugin_json("POST", "/write", body, &js);
    cJSON_Delete(js);
}

static void do_watch_add(cmd_t *c)
{
    char body[96];
    cJSON *js = NULL;
    uint32_t n = c->n;
    if (n != 1 && n != 2 && n != 4 && n != 8) {
        n = 4;
    }
    snprintf(body, sizeof body, "{\"addr\":%" PRIu64 ",\"n\":%u}", c->addr, n);
    (void)plugin_json("POST", "/watch", body, &js);
    cJSON_Delete(js);
}

static void do_watch_del(cmd_t *c)
{
    char path[32];
    cJSON *js = NULL;
    snprintf(path, sizeof path, "/watch/%u", c->id);
    (void)plugin_json("DELETE", path, NULL, &js);
    cJSON_Delete(js);
}

static void do_freeze_add(cmd_t *c)
{
    char hex[17];
    char body[96];
    cJSON *js = NULL;
    if (c->n < 1 || c->n > FREEZE_DATA_MAX) {
        overlay_lock();
        overlay_set_error("InvalidFreezeSize");
        overlay_unlock();
        return;
    }
    overlay_hex_encode(c->data, c->n, hex, sizeof hex);
    snprintf(body, sizeof body, "{\"addr\":%" PRIu64 ",\"data\":\"%s\"}", c->addr, hex);
    (void)plugin_json("POST", "/freeze", body, &js);
    cJSON_Delete(js);
}

static void do_freeze_del(cmd_t *c)
{
    char path[32];
    cJSON *js = NULL;
    snprintf(path, sizeof path, "/freeze/%u", c->id);
    (void)plugin_json("DELETE", path, NULL, &js);
    cJSON_Delete(js);
}

static void do_arm(void)
{
    overlay_state_t *st = overlay_state();
    cJSON *root;
    cJSON *arr;
    cJSON *js = NULL;
    char *body;
    int i;

    overlay_lock();
    root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "pid", st->pid);
    arr = cJSON_AddArrayToObject(root, "freezes");
    for (i = 0; i < st->freeze_n; i++) {
        cJSON *row = cJSON_CreateObject();
        char num[32];
        char hex[17];
        snprintf(num, sizeof num, "%" PRIu64, st->freezes[i].addr);
        cJSON_AddRawToObject(row, "addr", num);
        overlay_hex_encode(st->freezes[i].data, st->freezes[i].n, hex, sizeof hex);
        cJSON_AddStringToObject(row, "data", hex);
        cJSON_AddItemToArray(arr, row);
    }
    overlay_unlock();
    body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (body) {
        if (plugin_json("POST", "/arm", body, &js) == 0) {
            overlay_lock();
            st->armed = 1;
            overlay_unlock();
        }
        cJSON_Delete(js);
        cJSON_free(body);
    }
}

static void do_cheat_toggle(cmd_t *c)
{
    char body[NAME_LEN + 48];
    cJSON *js = NULL;
    snprintf(body, sizeof body, "{\"name\":\"%s\",\"enabled\":%s}", c->name,
             c->enabled ? "true" : "false");
    (void)plugin_json("POST", "/cheat/toggle", body, &js);
    cJSON_Delete(js);
}

static void do_maps(void)
{
    cJSON *js = NULL;
    cJSON *arr;
    overlay_state_t *st = overlay_state();
    int n = 0;

    if (plugin_json("GET", "/maps", NULL, &js) != 0) {
        return;
    }
    arr = cJSON_GetObjectItemCaseSensitive(js, "maps");
    overlay_lock();
    st->map_n = 0;
    st->map_sel = 0;
    if (cJSON_IsArray(arr)) {
        int i, sz = cJSON_GetArraySize(arr);
        for (i = 0; i < sz && n < MAPS_MAX; i++) {
            cJSON *row = cJSON_GetArrayItem(arr, i);
            cJSON *name = cJSON_GetObjectItemCaseSensitive(row, "name");
            cJSON *start = cJSON_GetObjectItemCaseSensitive(row, "start");
            cJSON *end = cJSON_GetObjectItemCaseSensitive(row, "end");
            cJSON *prot = cJSON_GetObjectItemCaseSensitive(row, "prot");
            uint64_t s = 0, e = 0, p = 0;
            if (json_u64(start, &s) != 0) {
                continue;
            }
            (void)json_u64(end, &e);
            (void)json_u64(prot, &p);
            overlay_copy_str(st->maps[n].name, 33, cJSON_IsString(name) ? name->valuestring : "");
            st->maps[n].start = s;
            st->maps[n].end = e;
            st->maps[n].prot = (uint16_t)p;
            n++;
        }
    }
    st->map_n = n;
    st->mode = MODE_MAPS;
    overlay_unlock();
    cJSON_Delete(js);
}

static void poll_hex(void)
{
    overlay_state_t *st = overlay_state();
    char path[80];
    cJSON *js = NULL;
    cJSON *data;
    uint64_t addr;
    uint32_t n;

    overlay_lock();
    addr = st->peephole;
    overlay_unlock();
    n = HEX_PEEPHOLE;
    snprintf(path, sizeof path, "/read?addr=%" PRIu64 "&n=%u", addr, n);
    if (plugin_json("GET", path, NULL, &js) != 0) {
        overlay_lock();
        st->hex_ok = 0;
        overlay_unlock();
        return;
    }
    data = cJSON_GetObjectItemCaseSensitive(js, "data");
    overlay_lock();
    if (cJSON_IsString(data) && data->valuestring) {
        size_t got = 0;
        if (overlay_parse_hex(data->valuestring, st->hex, HEX_PEEPHOLE, &got) == 0) {
            st->hex_n = (int)got;
            st->hex_ok = 1;
        }
    }
    overlay_unlock();
    cJSON_Delete(js);
}

static void poll_watch(void)
{
    overlay_state_t *st = overlay_state();
    cJSON *js = NULL;
    cJSON *vals;
    int i, sz;

    if (plugin_json("GET", "/watch/poll", NULL, &js) != 0) {
        return;
    }
    vals = cJSON_GetObjectItemCaseSensitive(js, "values");
    overlay_lock();
    if (cJSON_IsArray(vals)) {
        sz = cJSON_GetArraySize(vals);
        for (i = 0; i < sz; i++) {
            cJSON *row = cJSON_GetArrayItem(vals, i);
            uint64_t id = 0, addr = 0;
            cJSON *data = cJSON_GetObjectItemCaseSensitive(row, "data");
            int w;
            (void)json_u64(cJSON_GetObjectItemCaseSensitive(row, "id"), &id);
            (void)json_u64(cJSON_GetObjectItemCaseSensitive(row, "addr"), &addr);
            for (w = 0; w < st->watch_n; w++) {
                if (st->watches[w].id == (uint32_t)id) {
                    size_t got = 0;
                    st->watches[w].addr = addr;
                    if (cJSON_IsString(data) && data->valuestring &&
                        overlay_parse_hex(data->valuestring, st->watches[w].data, WATCH_N_MAX,
                                          &got) == 0) {
                        st->watches[w].n = (uint8_t)got;
                        st->watches[w].has_data = 1;
                    }
                    break;
                }
            }
        }
    }
    overlay_unlock();
    cJSON_Delete(js);
}

static void refresh_lists(void)
{
    overlay_state_t *st = overlay_state();
    cJSON *js = NULL;
    cJSON *arr;
    int i, sz, n;

    if (plugin_json("GET", "/watch", NULL, &js) == 0) {
        arr = cJSON_GetObjectItemCaseSensitive(js, "watches");
        overlay_lock();
        n = 0;
        if (cJSON_IsArray(arr)) {
            sz = cJSON_GetArraySize(arr);
            for (i = 0; i < sz && n < WATCH_MAX; i++) {
                cJSON *row = cJSON_GetArrayItem(arr, i);
                uint64_t id = 0, addr = 0, vn = 4;
                cJSON *lab = cJSON_GetObjectItemCaseSensitive(row, "label");
                (void)json_u64(cJSON_GetObjectItemCaseSensitive(row, "id"), &id);
                (void)json_u64(cJSON_GetObjectItemCaseSensitive(row, "addr"), &addr);
                (void)json_u64(cJSON_GetObjectItemCaseSensitive(row, "n"), &vn);
                st->watches[n].id = (uint32_t)id;
                st->watches[n].addr = addr;
                st->watches[n].n = (uint8_t)((vn == 1 || vn == 2 || vn == 4 || vn == 8) ? vn : 4);
                overlay_copy_str(st->watches[n].label, LABEL_LEN,
                                 cJSON_IsString(lab) ? lab->valuestring : "");
                n++;
            }
        }
        st->watch_n = n;
        overlay_unlock();
        cJSON_Delete(js);
        js = NULL;
    }
    if (plugin_json("GET", "/freeze", NULL, &js) == 0) {
        arr = cJSON_GetObjectItemCaseSensitive(js, "freezes");
        overlay_lock();
        n = 0;
        if (cJSON_IsArray(arr)) {
            sz = cJSON_GetArraySize(arr);
            for (i = 0; i < sz && n < FREEZE_MAX; i++) {
                cJSON *row = cJSON_GetArrayItem(arr, i);
                uint64_t id = 0, addr = 0;
                cJSON *data = cJSON_GetObjectItemCaseSensitive(row, "data");
                size_t got = 0;
                (void)json_u64(cJSON_GetObjectItemCaseSensitive(row, "id"), &id);
                (void)json_u64(cJSON_GetObjectItemCaseSensitive(row, "addr"), &addr);
                st->freezes[n].id = (uint32_t)id;
                st->freezes[n].addr = addr;
                if (cJSON_IsString(data) && data->valuestring &&
                    overlay_parse_hex(data->valuestring, st->freezes[n].data, FREEZE_DATA_MAX,
                                      &got) == 0) {
                    st->freezes[n].n = (uint8_t)got;
                }
                n++;
            }
        }
        st->freeze_n = n;
        overlay_unlock();
        cJSON_Delete(js);
        js = NULL;
    }
    if (plugin_json("GET", "/cheat", NULL, &js) == 0) {
        cJSON *ch = cJSON_GetObjectItemCaseSensitive(js, "cheat");
        cJSON *en = cJSON_GetObjectItemCaseSensitive(js, "enabled");
        overlay_lock();
        st->cheat_loaded = 0;
        st->cheat_mod_n = 0;
        st->cheat_name[0] = 0;
        if (ch && !cJSON_IsNull(ch) && cJSON_IsObject(ch)) {
            cJSON *name = cJSON_GetObjectItemCaseSensitive(ch, "name");
            cJSON *mods = cJSON_GetObjectItemCaseSensitive(ch, "mods");
            st->cheat_loaded = 1;
            overlay_copy_str(st->cheat_name, NAME_LEN, cJSON_IsString(name) ? name->valuestring : "");
            if (cJSON_IsArray(mods)) {
                sz = cJSON_GetArraySize(mods);
                for (i = 0; i < sz && i < CHEAT_MODS_MAX; i++) {
                    cJSON *m = cJSON_GetArrayItem(mods, i);
                    cJSON *mn = cJSON_GetObjectItemCaseSensitive(m, "name");
                    overlay_copy_str(st->mods[i].name, NAME_LEN,
                                     cJSON_IsString(mn) ? mn->valuestring : "");
                    st->mods[i].enabled = 0;
                    st->cheat_mod_n++;
                }
            }
            if (cJSON_IsArray(en)) {
                int e, m;
                sz = cJSON_GetArraySize(en);
                for (e = 0; e < sz; e++) {
                    cJSON *s = cJSON_GetArrayItem(en, e);
                    if (!cJSON_IsString(s) || !s->valuestring) {
                        continue;
                    }
                    for (m = 0; m < st->cheat_mod_n; m++) {
                        if (strcmp(st->mods[m].name, s->valuestring) == 0) {
                            st->mods[m].enabled = 1;
                        }
                    }
                }
            }
        }
        overlay_unlock();
        cJSON_Delete(js);
    }
    if (plugin_json("GET", "/status", NULL, &js) == 0) {
        cJSON *armed = cJSON_GetObjectItemCaseSensitive(js, "armed");
        overlay_lock();
        st->armed = cJSON_IsTrue(armed) ? 1 : 0;
        overlay_unlock();
        cJSON_Delete(js);
    }
}

static void bump_u8(uint8_t *data, int n, int nibble, int dir)
{
    int byte = nibble / 2;
    int hi = (nibble % 2) == 0;
    int v;
    if (byte < 0 || byte >= n) {
        return;
    }
    v = data[byte];
    if (hi) {
        int nib = (v >> 4) + dir;
        nib &= 0xf;
        data[byte] = (uint8_t)((nib << 4) | (v & 0xf));
    } else {
        int nib = (v & 0xf) + dir;
        nib &= 0xf;
        data[byte] = (uint8_t)((v & 0xf0) | nib);
    }
}

void overlay_on_input(uint32_t pressed)
{
    overlay_state_t *st = overlay_state();
    int w;

    overlay_lock();
    if (!st->open) {
        overlay_unlock();
        return;
    }
    if (st->poke_width != 1 && st->poke_width != 2 && st->poke_width != 4 && st->poke_width != 8) {
        st->poke_width = 4;
    }
    if ((pressed & PAD_L1) && st->mode == MODE_BROWSE) {
        st->view = (overlay_view_t)((st->view + VIEW_COUNT - 1) % VIEW_COUNT);
        st->sel = 0;
    }
    if ((pressed & PAD_R1) && st->mode == MODE_BROWSE) {
        st->view = (overlay_view_t)((st->view + 1) % VIEW_COUNT);
        st->sel = 0;
    }

    if (st->mode == MODE_GOTO) {
        if (pressed & PAD_LEFT) {
            if (st->goto_nibble > 0) {
                st->goto_nibble--;
            }
        }
        if (pressed & PAD_RIGHT) {
            if (st->goto_nibble < 15) {
                st->goto_nibble++;
            }
        }
        if (pressed & PAD_UP) {
            st->goto_addr ^= 0;
            {
                int sh = (15 - st->goto_nibble) * 4;
                uint64_t nib = (st->goto_addr >> sh) & 0xf;
                nib = (nib + 1) & 0xf;
                st->goto_addr = (st->goto_addr & ~(0xfull << sh)) | (nib << sh);
            }
        }
        if (pressed & PAD_DOWN) {
            int sh = (15 - st->goto_nibble) * 4;
            uint64_t nib = (st->goto_addr >> sh) & 0xf;
            nib = (nib + 15) & 0xf;
            st->goto_addr = (st->goto_addr & ~(0xfull << sh)) | (nib << sh);
        }
        if (pressed & PAD_CIRCLE) {
            st->mode = MODE_BROWSE;
        }
        if (pressed & PAD_CROSS) {
            st->peephole = overlay_align16(st->goto_addr);
            st->cursor = 0;
            st->hex_ok = 0;
            st->mode = MODE_BROWSE;
        }
        overlay_unlock();
        return;
    }

    if (st->mode == MODE_POKE) {
        if (pressed & PAD_LEFT) {
            if (st->poke_nibble > 0) {
                st->poke_nibble--;
            }
        }
        if (pressed & PAD_RIGHT) {
            if (st->poke_nibble + 1 < st->poke_width * 2) {
                st->poke_nibble++;
            }
        }
        if (pressed & PAD_UP) {
            bump_u8(st->poke_data, st->poke_width, st->poke_nibble, 1);
        }
        if (pressed & PAD_DOWN) {
            bump_u8(st->poke_data, st->poke_width, st->poke_nibble, -1);
        }
        if (pressed & PAD_L3) {
            if (st->poke_width == 1) {
                st->poke_width = 2;
            } else if (st->poke_width == 2) {
                st->poke_width = 4;
            } else if (st->poke_width == 4) {
                st->poke_width = 8;
            } else {
                st->poke_width = 1;
            }
            if (st->poke_nibble >= st->poke_width * 2) {
                st->poke_nibble = st->poke_width * 2 - 1;
            }
        }
        if (pressed & PAD_CIRCLE) {
            st->mode = MODE_BROWSE;
        }
        if (pressed & PAD_CROSS) {
            uint64_t a = st->poke_addr;
            uint8_t tmp[8];
            uint32_t n = (uint32_t)st->poke_width;
            memcpy(tmp, st->poke_data, 8);
            st->mode = MODE_BROWSE;
            overlay_unlock();
            overlay_request_write(a, tmp, n);
            return;
        }
        overlay_unlock();
        return;
    }

    if (st->mode == MODE_MAPS) {
        if (pressed & PAD_UP) {
            if (st->map_sel > 0) {
                st->map_sel--;
            }
        }
        if (pressed & PAD_DOWN) {
            if (st->map_sel + 1 < st->map_n) {
                st->map_sel++;
            }
        }
        if (pressed & PAD_CIRCLE) {
            st->mode = MODE_BROWSE;
        }
        if ((pressed & PAD_CROSS) && st->map_n > 0) {
            st->peephole = overlay_align16(st->maps[st->map_sel].start);
            st->cursor = 0;
            st->hex_ok = 0;
            st->mode = MODE_BROWSE;
            st->view = VIEW_LIVE;
        }
        overlay_unlock();
        return;
    }

    if (st->view == VIEW_LIVE) {
        if (pressed & PAD_UP) {
            st->cursor -= HEX_COLS;
        }
        if (pressed & PAD_DOWN) {
            st->cursor += HEX_COLS;
        }
        if (pressed & PAD_LEFT) {
            st->cursor--;
        }
        if (pressed & PAD_RIGHT) {
            st->cursor++;
        }
        if (st->cursor < 0) {
            st->cursor = 0;
        }
        if (st->cursor >= (int)HEX_PEEPHOLE) {
            st->cursor = (int)HEX_PEEPHOLE - 1;
        }
        if (pressed & PAD_SQUARE) {
            st->goto_addr = st->peephole + (uint64_t)st->cursor;
            st->goto_nibble = 0;
            st->mode = MODE_GOTO;
        }
        if (pressed & PAD_TRIANGLE) {
            overlay_unlock();
            overlay_request_maps();
            return;
        }
        if (pressed & PAD_L3) {
            uint64_t a = st->peephole + (uint64_t)st->cursor;
            uint32_t n = (uint32_t)st->poke_width;
            overlay_unlock();
            overlay_request_watch_add(a, n);
            return;
        }
        if (pressed & PAD_CROSS) {
            int i;
            st->poke_addr = st->peephole + (uint64_t)st->cursor;
            w = st->poke_width;
            if (st->cursor + w > (int)HEX_PEEPHOLE) {
                w = (int)HEX_PEEPHOLE - st->cursor;
                if (w >= 8) {
                    w = 8;
                } else if (w >= 4) {
                    w = 4;
                } else if (w >= 2) {
                    w = 2;
                } else {
                    w = 1;
                }
                st->poke_width = w;
            }
            memset(st->poke_data, 0, sizeof st->poke_data);
            if (st->hex_ok) {
                for (i = 0; i < st->poke_width; i++) {
                    st->poke_data[i] = st->hex[st->cursor + i];
                }
            }
            st->poke_nibble = 0;
            st->mode = MODE_POKE;
        }
        overlay_unlock();
        return;
    }

    if (st->view == VIEW_WATCH) {
        if (pressed & PAD_UP) {
            if (st->sel > 0) {
                st->sel--;
            }
        }
        if (pressed & PAD_DOWN) {
            if (st->sel + 1 < st->watch_n) {
                st->sel++;
            }
        }
        if ((pressed & PAD_SQUARE) && st->watch_n > 0) {
            uint32_t id = st->watches[st->sel].id;
            overlay_unlock();
            overlay_request_watch_del(id);
            return;
        }
        if ((pressed & PAD_CROSS) && st->watch_n > 0) {
            watch_row_t wrow = st->watches[st->sel];
            overlay_unlock();
            if (wrow.has_data) {
                overlay_request_freeze_add(wrow.addr, wrow.data, wrow.n);
            }
            return;
        }
        overlay_unlock();
        return;
    }

    if (st->view == VIEW_FREEZE) {
        if (pressed & PAD_UP) {
            if (st->sel > 0) {
                st->sel--;
            }
        }
        if (pressed & PAD_DOWN) {
            if (st->sel + 1 < st->freeze_n) {
                st->sel++;
            }
        }
        if ((pressed & PAD_SQUARE) && st->freeze_n > 0) {
            uint32_t id = st->freezes[st->sel].id;
            overlay_unlock();
            overlay_request_freeze_del(id);
            return;
        }
        if (pressed & PAD_CROSS) {
            overlay_unlock();
            overlay_request_arm();
            return;
        }
        overlay_unlock();
        return;
    }

    if (pressed & PAD_UP) {
        if (st->sel > 0) {
            st->sel--;
        }
    }
    if (pressed & PAD_DOWN) {
        if (st->sel + 1 < st->cheat_mod_n) {
            st->sel++;
        }
    }
    if ((pressed & PAD_CROSS) && st->cheat_mod_n > 0) {
        char name[NAME_LEN];
        int en = st->mods[st->sel].enabled ? 0 : 1;
        overlay_copy_str(name, NAME_LEN, st->mods[st->sel].name);
        overlay_unlock();
        overlay_request_cheat_toggle(name, en);
        return;
    }
    overlay_unlock();
}

static void dispatch(cmd_t *c)
{
    switch (c->kind) {
    case CMD_OPEN:
        do_open();
        refresh_lists();
        break;
    case CMD_CLOSE:
        do_close();
        break;
    case CMD_WRITE:
        do_write(c);
        break;
    case CMD_WATCH_ADD:
        do_watch_add(c);
        refresh_lists();
        break;
    case CMD_WATCH_DEL:
        do_watch_del(c);
        refresh_lists();
        break;
    case CMD_FREEZE_ADD:
        do_freeze_add(c);
        refresh_lists();
        break;
    case CMD_FREEZE_DEL:
        do_freeze_del(c);
        refresh_lists();
        break;
    case CMD_ARM:
        do_arm();
        break;
    case CMD_CHEAT_TOGGLE:
        do_cheat_toggle(c);
        refresh_lists();
        break;
    case CMD_MAPS:
        do_maps();
        break;
    default:
        break;
    }
}

static void *worker_main(void *arg)
{
    (void)arg;
    for (;;) {
        cmd_t c;
        overlay_state_t *st = overlay_state();
        int open;
        overlay_view_t view;
        uint64_t now;

        while (q_pop(&c)) {
            dispatch(&c);
        }
        overlay_lock();
        open = st->open;
        view = st->view;
        overlay_unlock();
        overlay_pad_poll();
        if (!open) {
            usleep(50 * 1000);
            continue;
        }
        now = overlay_now_ms();
        if (view == VIEW_LIVE && now - g_last_hex_ms >= HEX_PERIOD_MS) {
            g_last_hex_ms = now;
            poll_hex();
        }
        if (view == VIEW_WATCH && now - g_last_watch_ms >= WATCH_PERIOD_MS) {
            g_last_watch_ms = now;
            poll_watch();
        }
        if (now - g_last_list_ms >= LIST_PERIOD_MS) {
            g_last_list_ms = now;
            if (view == VIEW_WATCH || view == VIEW_FREEZE || view == VIEW_CHEATS) {
                refresh_lists();
            }
        }
        usleep(10 * 1000);
    }
    return NULL;
}

int overlay_worker_start(void)
{
    pthread_t th;
    overlay_state_t *st = overlay_state();

    overlay_lock();
    st->poke_width = 4;
    st->peephole = 0;
    overlay_unlock();
    if (pthread_create(&th, NULL, worker_main, NULL) != 0) {
        return -1;
    }
    (void)pthread_detach(th);
    return 0;
}
