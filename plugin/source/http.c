#include "http.h"
#include "cheats.h"
#include "dbg_client.h"
#include "freeze.h"
#include "fs_state.h"
#include "inject.h"
#include "got_patch.h"
#include "nitepr5.h"
#include "notify.h"
#include "cJSON.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdint.h>

#define HTTP_HDR_MAX  8192
#define HTTP_BODY_MAX (256 * 1024)
#define HTTP_QUERY_MAX 256
#define HTTP_LIST_CAP  4096

static int send_all(int fd, const void *buf, size_t n)
{
    const unsigned char *p = (const unsigned char *)buf;

    while (n > 0) {
#ifdef MSG_NOSIGNAL
        ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
#else
        ssize_t w = send(fd, p, n, 0);
#endif
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (w == 0) {
            return -1;
        }
        p += (size_t)w;
        n -= (size_t)w;
    }
    return 0;
}

static void http_send(int fd, int code, const char *json)
{
    char hdr[256];
    const char *st;
    size_t jn;
    int hn;

    if (json == NULL) {
        json = "{\"ok\":false,\"error\":\"BadRequest\"}";
    }
    jn = strlen(json);
    if (code == 200) {
        st = "200 OK";
    } else if (code == 400) {
        st = "400 Bad Request";
    } else {
        st = "500 Internal Server Error";
    }
    hn = snprintf(hdr, sizeof hdr,
                  "HTTP/1.1 %s\r\n"
                  "Content-Type: application/json\r\n"
                  "Content-Length: %zu\r\n"
                  "Connection: close\r\n"
                  "\r\n",
                  st, jn);
    if (hn > 0) {
        send_all(fd, hdr, (size_t)hn);
        send_all(fd, json, jn);
    }
}

static void http_err(int fd, const char *error)
{
    char buf[160];

    snprintf(buf, sizeof buf, "{\"ok\":false,\"error\":\"%s\"}", error ? error : "BadRequest");
    http_send(fd, 400, buf);
}

static int path_is(const char *path, const char *want)
{
    size_t n;

    if (path == NULL || want == NULL) {
        return 0;
    }
    n = strlen(want);
    if (strncmp(path, want, n) != 0) {
        return 0;
    }
    return path[n] == 0 || (path[n] == '/' && path[n + 1] == 0);
}

static int path_id(const char *path, const char *pfx, uint32_t *id)
{
    size_t n;
    char *end;
    unsigned long v;

    if (path == NULL || pfx == NULL || id == NULL) {
        return 0;
    }
    n = strlen(pfx);
    if (strncmp(path, pfx, n) != 0) {
        return 0;
    }
    if (path[n] < '0' || path[n] > '9') {
        return 0;
    }
    errno = 0;
    v = strtoul(path + n, &end, 10);
    if (errno == ERANGE || end == path + n || *end != 0 || v > 0xfffffffful) {
        return 0;
    }
    *id = (uint32_t)v;
    return 1;
}

static int query_val(const char *query, const char *key, char *out, size_t cap)
{
    size_t klen;
    const char *p;

    if (query == NULL || key == NULL || out == NULL || cap == 0) {
        return -1;
    }
    out[0] = 0;
    klen = strlen(key);
    p = query;
    while (*p) {
        const char *amp;
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            size_t n;
            p += klen + 1;
            amp = strchr(p, '&');
            n = amp ? (size_t)(amp - p) : strlen(p);
            if (n >= cap) {
                n = cap - 1;
            }
            memcpy(out, p, n);
            out[n] = 0;
            return 0;
        }
        amp = strchr(p, '&');
        if (amp == NULL) {
            break;
        }
        p = amp + 1;
    }
    return -1;
}

static void json_add_u64(cJSON *obj, const char *key, uint64_t v)
{
    char num[32];

    snprintf(num, sizeof num, "%" PRIu64, v);
    cJSON_AddRawToObject(obj, key, num);
}

static int json_u64(cJSON *item, uint64_t *out)
{
    char *printed;
    char *end;
    unsigned long long v;

    if (item == NULL || out == NULL) {
        return -1;
    }
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        return nitepr5_parse_u64(item->valuestring, out);
    }
    if (!cJSON_IsNumber(item) || item->valuedouble < 0) {
        return -1;
    }
    printed = cJSON_PrintUnformatted(item);
    if (printed != NULL) {
        errno = 0;
        end = NULL;
        v = strtoull(printed, &end, 10);
        if (errno != ERANGE && end != printed && *end == 0) {
            cJSON_free(printed);
            *out = (uint64_t)v;
            return 0;
        }
        cJSON_free(printed);
    }
    *out = (uint64_t)item->valuedouble;
    return 0;
}

static int query_pid(const char *query, uint32_t fallback, uint32_t *out)
{
    char buf[32];
    uint64_t v;

    if (out == NULL) {
        return -1;
    }
    if (query_val(query, "pid", buf, sizeof buf) != 0) {
        *out = fallback;
        return 0;
    }
    if (nitepr5_parse_u64(buf, &v) != 0 || v > 0xffffffffull) {
        return -1;
    }
    *out = (uint32_t)v;
    return 0;
}

static int require_dbg(int fd)
{
    nitepr5_state_t *st = nitepr5_state();

    if (!st->armed && !st->overlay_open) {
        http_err(fd, "NotConnected");
        return -1;
    }
    if (dbg_ensure() != 0) {
        st->dbg = 0;
        http_err(fd, "NotConnected");
        return -1;
    }
    st->dbg = 1;
    return 0;
}

static void http_send_obj(int fd, int code, cJSON *o)
{
    char *js;

    if (o == NULL) {
        http_err(fd, "BadRequest");
        return;
    }
    js = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    http_send(fd, code, js ? js : "{\"ok\":false,\"error\":\"BadRequest\"}");
    if (js) {
        cJSON_free(js);
    }
}

static int watch_n_ok(uint32_t n)
{
    return n == 1 || n == 2 || n == 4 || n == 8;
}

static int starts_ci(const char *s, const char *pfx)
{
    while (*pfx) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*pfx)) {
            return 0;
        }
        s++;
        pfx++;
    }
    return 1;
}

static int parse_content_length(const char *hdrs, size_t n, size_t *out)
{
    const char *p = hdrs;
    const char *end = hdrs + n;

    *out = 0;
    while (p < end) {
        const char *eol = p;
        while (eol < end && *eol != '\n') {
            eol++;
        }
        if (starts_ci(p, "content-length:")) {
            const char *v = p + 15;
            unsigned long val = 0;
            while (v < eol && (*v == ' ' || *v == '\t')) {
                v++;
            }
            while (v < eol && *v >= '0' && *v <= '9') {
                val = val * 10ul + (unsigned long)(*v - '0');
                v++;
            }
            *out = (size_t)val;
            return 0;
        }
        p = (eol < end) ? eol + 1 : end;
    }
    return -1;
}

static int recv_request(int fd, char *method, size_t method_cap, char *path, size_t path_cap,
                        char *query, size_t query_cap, char **body, size_t *body_n)
{
    char hdr[HTTP_HDR_MAX];
    size_t n = 0;
    char *sep = NULL;
    size_t header_end = 0;
    size_t clen = 0;
    int have_clen = 0;
    char *line_end;
    char *sp1;
    char *sp2;
    size_t already;

    *body = NULL;
    *body_n = 0;
    method[0] = 0;
    path[0] = 0;
    if (query != NULL && query_cap > 0) {
        query[0] = 0;
    }

    while (n < sizeof hdr - 1) {
        ssize_t r = recv(fd, hdr + n, sizeof hdr - 1 - n, 0);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (r == 0) {
            return -1;
        }
        n += (size_t)r;
        hdr[n] = 0;
        sep = strstr(hdr, "\r\n\r\n");
        if (sep != NULL) {
            header_end = (size_t)(sep - hdr) + 4;
            break;
        }
        sep = strstr(hdr, "\n\n");
        if (sep != NULL) {
            header_end = (size_t)(sep - hdr) + 2;
            break;
        }
    }
    if (sep == NULL) {
        return -1;
    }

    if (parse_content_length(hdr, header_end, &clen) == 0) {
        have_clen = 1;
    }

    line_end = strstr(hdr, "\r\n");
    if (line_end == NULL) {
        line_end = strchr(hdr, '\n');
    }
    if (line_end == NULL || line_end > hdr + header_end) {
        return -1;
    }
    *line_end = 0;
    sp1 = strchr(hdr, ' ');
    if (sp1 == NULL) {
        return -1;
    }
    *sp1 = 0;
    strncpy(method, hdr, method_cap - 1);
    method[method_cap - 1] = 0;
    sp2 = strchr(sp1 + 1, ' ');
    if (sp2 != NULL) {
        *sp2 = 0;
    }
    {
        char *q = strchr(sp1 + 1, '?');
        if (q != NULL) {
            *q = 0;
            if (query != NULL && query_cap > 0) {
                strncpy(query, q + 1, query_cap - 1);
                query[query_cap - 1] = 0;
            }
        }
    }
    strncpy(path, sp1 + 1, path_cap - 1);
    path[path_cap - 1] = 0;

    if (!have_clen) {
        clen = 0;
    }
    if (clen > HTTP_BODY_MAX) {
        return -2;
    }

    already = n > header_end ? n - header_end : 0;
    if (already > clen) {
        already = clen;
    }
    *body = (char *)malloc(clen + 1);
    if (*body == NULL) {
        return -1;
    }
    if (already > 0) {
        memcpy(*body, hdr + header_end, already);
    }
    while (already < clen) {
        ssize_t r = recv(fd, *body + already, clen - already, 0);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            free(*body);
            *body = NULL;
            return -1;
        }
        if (r == 0) {
            free(*body);
            *body = NULL;
            return -1;
        }
        already += (size_t)r;
    }
    (*body)[clen] = 0;
    *body_n = clen;
    return 0;
}

static void handle_status(int fd)
{
    nitepr5_state_t *st = nitepr5_state();
    cJSON *o = cJSON_CreateObject();
    cJSON *en;
    char *js;
    int i;

    st->dbg = dbg_connected() ? 1 : 0;
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddBoolToObject(o, "armed", st->armed ? 1 : 0);
    cJSON_AddNumberToObject(o, "pid", (double)st->pid);
    cJSON_AddNumberToObject(o, "freeze_count", st->freeze_count);
    cJSON_AddStringToObject(o, "cheat_id", st->cheat.loaded ? st->cheat.id : "");
    en = cJSON_AddArrayToObject(o, "enabled");
    for (i = 0; i < st->enabled_count; i++) {
        cJSON_AddItemToArray(en, cJSON_CreateString(st->enabled[i]));
    }
    cJSON_AddBoolToObject(o, "dbg", st->dbg ? 1 : 0);
    cJSON_AddBoolToObject(o, "overlay_open", st->overlay_open ? 1 : 0);
    cJSON_AddNumberToObject(o, "watch_count", st->watch_count);
    cJSON_AddBoolToObject(o, "overlay_injected", inject_pid() != 0);
    cJSON_AddNumberToObject(o, "overlay_pid", (double)inject_pid());
    js = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    http_send(fd, 200, js ? js : "{\"ok\":false,\"error\":\"BadRequest\"}");
    if (js) {
        cJSON_free(js);
    }
}

static int parse_freeze_row(cJSON *row, freeze_entry_t *out)
{
    cJSON *addr;
    cJSON *data;
    size_t n = 0;
    uint64_t va = 0;

    if (!cJSON_IsObject(row)) {
        return -1;
    }
    addr = cJSON_GetObjectItemCaseSensitive(row, "addr");
    data = cJSON_GetObjectItemCaseSensitive(row, "data");
    if (addr == NULL || !cJSON_IsString(data) || data->valuestring == NULL) {
        return -1;
    }
    if (json_u64(addr, &va) != 0) {
        return -1;
    }
    if (nitepr5_parse_hex(data->valuestring, out->data, FREEZE_DATA_MAX, &n) != 0 ||
        n < 1 || n > FREEZE_DATA_MAX) {
        return -1;
    }
    out->id = 0;
    out->addr = va;
    out->n = (uint8_t)n;
    return 0;
}

static void handle_arm(int fd, const char *body, size_t body_n)
{
    nitepr5_state_t *st = nitepr5_state();
    cJSON *root;
    cJSON *pid_j;
    cJSON *frs;
    cJSON *ch;
    cJSON *en;
    freeze_entry_t local[FREEZE_MAX];
    int freeze_n = 0;
    uint32_t pid;
    int i;

    (void)body_n;
    root = cJSON_Parse(body ? body : "");
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        http_err(fd, "BadRequest");
        return;
    }

    pid_j = cJSON_GetObjectItemCaseSensitive(root, "pid");
    if (!cJSON_IsNumber(pid_j) || pid_j->valuedouble < 0) {
        cJSON_Delete(root);
        http_err(fd, "BadRequest");
        return;
    }
    pid = (uint32_t)pid_j->valuedouble;

    frs = cJSON_GetObjectItemCaseSensitive(root, "freezes");
    if (frs == NULL || cJSON_IsNull(frs)) {
        freeze_n = 0;
    } else if (!cJSON_IsArray(frs)) {
        cJSON_Delete(root);
        http_err(fd, "BadRequest");
        return;
    } else {
        int sz = cJSON_GetArraySize(frs);
        if (sz > FREEZE_MAX) {
            cJSON_Delete(root);
            http_err(fd, "FreezeLimit");
            return;
        }
        for (i = 0; i < sz; i++) {
            if (parse_freeze_row(cJSON_GetArrayItem(frs, i), &local[freeze_n]) != 0) {
                cJSON_Delete(root);
                http_err(fd, "InvalidFreezeSize");
                return;
            }
            freeze_n++;
        }
    }

    ch = cJSON_GetObjectItemCaseSensitive(root, "cheat");
    if (ch != NULL && !cJSON_IsNull(ch)) {
        char *js;
        if (!cJSON_IsObject(ch)) {
            cJSON_Delete(root);
            http_err(fd, "InvalidCheat");
            return;
        }
        js = cJSON_PrintUnformatted(ch);
        if (js == NULL || cheats_load_json(js, strlen(js)) != 0) {
            if (js) {
                cJSON_free(js);
            }
            cJSON_Delete(root);
            http_err(fd, "InvalidCheat");
            return;
        }
        cJSON_free(js);
        cheats_save_goldhen_file();
    } else {
        cheats_clear();
    }

    en = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    if (en != NULL && cJSON_IsArray(en)) {
        const char *names[ENABLED_MAX];
        int nc = 0;
        int sz = cJSON_GetArraySize(en);
        for (i = 0; i < sz && nc < ENABLED_MAX; i++) {
            cJSON *s = cJSON_GetArrayItem(en, i);
            if (cJSON_IsString(s) && s->valuestring) {
                names[nc++] = s->valuestring;
            }
        }
        cheats_replace_enabled(names, nc);
    } else {
        cheats_replace_enabled(NULL, 0);
    }

    st->pid = pid;
    st->freeze_count = freeze_n;
    memcpy(st->freezes, local, sizeof(freeze_entry_t) * (size_t)freeze_n);
    for (i = 0; i < freeze_n; i++) {
        st->freezes[i].id = (uint32_t)(i + 1);
    }
    st->next_freeze_id = (uint32_t)freeze_n + 1;
    if (st->next_freeze_id == 0) {
        st->next_freeze_id = 1;
    }
    st->armed = 1;

    (void)fs_state_save();

    if (dbg_connect() == 0) {
        st->dbg = 1;
        notify_dbg_recovered();
        (void)cheats_apply_enabled();
    } else {
        st->dbg = 0;
        notify_dbg_missing();
    }
    notify_armed();

    cJSON_Delete(root);
    {
        cJSON *o = cJSON_CreateObject();
        char *js;
        cJSON_AddBoolToObject(o, "ok", 1);
        cJSON_AddBoolToObject(o, "armed", 1);
        cJSON_AddNumberToObject(o, "freeze_count", st->freeze_count);
        js = cJSON_PrintUnformatted(o);
        cJSON_Delete(o);
        http_send(fd, 200, js ? js : "{\"ok\":true,\"armed\":true,\"freeze_count\":0}");
        if (js) {
            cJSON_free(js);
        }
    }
}

static void handle_disarm(int fd)
{
    nitepr5_state_t *st = nitepr5_state();

    st->armed = 0;
    if (!st->overlay_open) {
        dbg_disconnect();
        st->dbg = 0;
    } else {
        st->dbg = dbg_connected() ? 1 : 0;
    }
    (void)fs_state_save();
    http_send(fd, 200, "{\"ok\":true,\"armed\":false}");
}

static void handle_cheat_toggle(int fd, const char *body)
{
    cJSON *root = cJSON_Parse(body ? body : "");
    cJSON *name;
    cJSON *en;
    int enabled;
    nitepr5_state_t *st = nitepr5_state();

    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        http_err(fd, "BadRequest");
        return;
    }
    name = cJSON_GetObjectItemCaseSensitive(root, "name");
    en = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    if (!cJSON_IsString(name) || name->valuestring == NULL || name->valuestring[0] == 0) {
        cJSON_Delete(root);
        http_err(fd, "BadRequest");
        return;
    }
    if (!cJSON_IsBool(en)) {
        cJSON_Delete(root);
        http_err(fd, "BadRequest");
        return;
    }
    enabled = cJSON_IsTrue(en) ? 1 : 0;
    if (!st->cheat.loaded) {
        cJSON_Delete(root);
        http_err(fd, "NoCheat");
        return;
    }
    if (cheats_find_mod(name->valuestring) < 0) {
        cJSON_Delete(root);
        http_err(fd, "NoMod");
        return;
    }
    (void)cheats_toggle(name->valuestring, enabled);
    {
        cJSON *o = cJSON_CreateObject();
        char *js;
        cJSON_AddBoolToObject(o, "ok", 1);
        cJSON_AddStringToObject(o, "name", name->valuestring);
        cJSON_AddBoolToObject(o, "enabled", enabled);
        js = cJSON_PrintUnformatted(o);
        cJSON_Delete(o);
        cJSON_Delete(root);
        http_send(fd, 200, js ? js : "{\"ok\":true}");
        if (js) {
            cJSON_free(js);
        }
    }
}

static void handle_cheat_load(int fd, const char *body, size_t body_n)
{
    cJSON *root;
    cJSON *fn;
    cJSON *mods;
    nitepr5_state_t *st = nitepr5_state();

    root = cJSON_Parse(body ? body : "");
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        http_err(fd, "InvalidCheat");
        return;
    }
    fn = cJSON_GetObjectItemCaseSensitive(root, "filename");
    mods = cJSON_GetObjectItemCaseSensitive(root, "mods");
    if (cJSON_IsString(fn) && fn->valuestring && mods == NULL) {
        if (!nitepr5_valid_cheat_filename(fn->valuestring)) {
            cJSON_Delete(root);
            http_err(fd, "InvalidFilename");
            return;
        }
        if (cheats_load_filename(fn->valuestring) != 0) {
            cJSON_Delete(root);
            http_err(fd, "InvalidCheat");
            return;
        }
    } else {
        if (cheats_load_json(body, body_n) != 0) {
            cJSON_Delete(root);
            http_err(fd, "InvalidCheat");
            return;
        }
        cheats_save_goldhen_file();
    }
    (void)fs_state_save();
    {
        cJSON *o = cJSON_CreateObject();
        char *js;
        cJSON_AddBoolToObject(o, "ok", 1);
        cJSON_AddStringToObject(o, "id", st->cheat.loaded ? st->cheat.id : "");
        cJSON_AddStringToObject(o, "name", st->cheat.loaded ? st->cheat.name : "");
        js = cJSON_PrintUnformatted(o);
        cJSON_Delete(o);
        cJSON_Delete(root);
        http_send(fd, 200, js ? js : "{\"ok\":true}");
        if (js) {
            cJSON_free(js);
        }
    }
}

static void handle_overlay_open(int fd, const char *body)
{
    nitepr5_state_t *st = nitepr5_state();
    cJSON *root;
    cJSON *pid_j;
    uint32_t pid;
    cJSON *o;
    uint64_t flip_wl_real = 0, flip_wl_hook = 0, flip_wl_tramp = 0, flip_real = 0, flip_hook = 0,
             flip_tramp = 0, vo_real = 0, vo_hook = 0, vo_tramp = 0;
    int flip_n = 0, vo_n = 0, n = 0, gnm_real_ok = 0, vo_real_ok = 0, r_ok = 0, sprx = 0,
        sprx_flip_wl = 0, sprx_flip = 0, sprx_vo = 0, got_err = 0;
    char msg[80];

    root = cJSON_Parse((body && body[0]) ? body : "{}");
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        http_err(fd, "BadRequest");
        return;
    }
    pid_j = cJSON_GetObjectItemCaseSensitive(root, "pid");
    if (pid_j != NULL && !cJSON_IsNull(pid_j)) {
        uint64_t v;
        if (json_u64(pid_j, &v) != 0 || v > 0xffffffffull) {
            cJSON_Delete(root);
            http_err(fd, "BadRequest");
            return;
        }
        pid = (uint32_t)v;
        st->pid = pid;
        (void)fs_state_save();
    } else {
        pid = st->pid;
    }
    (void)json_u64(cJSON_GetObjectItemCaseSensitive(root, "flip_wl_real"), &flip_wl_real);
    (void)json_u64(cJSON_GetObjectItemCaseSensitive(root, "flip_wl_hook"), &flip_wl_hook);
    (void)json_u64(cJSON_GetObjectItemCaseSensitive(root, "flip_wl_tramp"), &flip_wl_tramp);
    (void)json_u64(cJSON_GetObjectItemCaseSensitive(root, "flip_real"), &flip_real);
    (void)json_u64(cJSON_GetObjectItemCaseSensitive(root, "flip_hook"), &flip_hook);
    (void)json_u64(cJSON_GetObjectItemCaseSensitive(root, "flip_tramp"), &flip_tramp);
    (void)json_u64(cJSON_GetObjectItemCaseSensitive(root, "vo_real"), &vo_real);
    (void)json_u64(cJSON_GetObjectItemCaseSensitive(root, "vo_hook"), &vo_hook);
    (void)json_u64(cJSON_GetObjectItemCaseSensitive(root, "vo_tramp"), &vo_tramp);
    cJSON_Delete(root);
    if (pid == 0) {
        http_err(fd, "NoTarget");
        return;
    }
    st->overlay_open = 1;
    if (dbg_ensure() == 0) {
        st->dbg = 1;
        notify_dbg_recovered();
    } else {
        st->dbg = 0;
        notify_dbg_missing();
    }
    /* NID PLT (etaHEN imagebase+r_offset), then combo-only SPRX trampoline.
     * Overlay never patches. Do not steal scePadReadState PLT. */
    if (flip_wl_hook) {
        uint64_t real = got_resolve_sym(pid, "libSceGnmDriverForNeoMode.sprx",
                                        "sceGnmSubmitAndFlipCommandBuffersForWorkload");
        if (real == 0) {
            real = got_resolve_sym(pid, "libSceGnmDriver.sprx",
                                   "sceGnmSubmitAndFlipCommandBuffersForWorkload");
        }
        if (real == 0) {
            real = flip_wl_real;
        }
        if (real) {
            gnm_real_ok = 1;
        }
        n = got_patch_nid(pid, "sceGnmSubmitAndFlipCommandBuffersForWorkload", flip_wl_hook);
        if (n > 0) {
            flip_n = n;
        } else if (real && flip_wl_tramp &&
                   got_sprx_detour(pid, real, flip_wl_hook, flip_wl_tramp) > 0) {
            flip_n = 1;
            sprx_flip_wl = 1;
            sprx = 1;
        }
    }
    if (flip_n == 0 && flip_hook) {
        uint64_t real = got_resolve_sym(pid, "libSceGnmDriverForNeoMode.sprx",
                                        "sceGnmSubmitAndFlipCommandBuffers");
        if (real == 0) {
            real = got_resolve_sym(pid, "libSceGnmDriver.sprx",
                                   "sceGnmSubmitAndFlipCommandBuffers");
        }
        if (real == 0) {
            real = flip_real;
        }
        if (real) {
            gnm_real_ok = 1;
        }
        n = got_patch_nid(pid, "sceGnmSubmitAndFlipCommandBuffers", flip_hook);
        if (n > 0) {
            flip_n = n;
        } else if (real && flip_tramp &&
                   got_sprx_detour(pid, real, flip_hook, flip_tramp) > 0) {
            flip_n = 1;
            sprx_flip = 1;
            sprx = 1;
        }
    }
    if (vo_hook) {
        uint64_t real = got_resolve_sym(pid, "libSceVideoOut.sprx", "sceVideoOutRegisterBuffers");
        if (real == 0) {
            real = got_resolve_sym(pid, "libSceVideoOutForNeoMode.sprx",
                                   "sceVideoOutRegisterBuffers");
        }
        if (real == 0) {
            real = vo_real;
        }
        if (real) {
            vo_real_ok = 1;
        }
        n = got_patch_nid(pid, "sceVideoOutRegisterBuffers", vo_hook);
        if (n > 0) {
            vo_n = n;
        } else if (real && vo_tramp && got_sprx_detour(pid, real, vo_hook, vo_tramp) > 0) {
            vo_n = 1;
            sprx_vo = 1;
            sprx = 1;
        }
    }
    r_ok = (gnm_real_ok || vo_real_ok) ? 1 : 0;
    if (flip_n > 0 || vo_n > 0) {
        got_err = 0;
    } else if (!r_ok) {
        got_err = 1;
    } else {
        got_err = 2;
    }
    snprintf(msg, sizeof msg, "NitePR5 got gnm=%d vo=%d r=%d s=%d", flip_n, vo_n, r_ok, sprx);
    notify_toast(msg);
    o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddBoolToObject(o, "overlay_open", 1);
    cJSON_AddNumberToObject(o, "pid", (double)st->pid);
    cJSON_AddBoolToObject(o, "dbg", st->dbg ? 1 : 0);
    cJSON_AddNumberToObject(o, "got_flip", (double)flip_n);
    cJSON_AddNumberToObject(o, "got_vo", (double)vo_n);
    cJSON_AddNumberToObject(o, "got_real", (double)r_ok);
    cJSON_AddNumberToObject(o, "got_sprx", (double)sprx);
    cJSON_AddNumberToObject(o, "got_err", (double)got_err);
    cJSON_AddBoolToObject(o, "sprx_flip_wl", sprx_flip_wl);
    cJSON_AddBoolToObject(o, "sprx_flip", sprx_flip);
    cJSON_AddBoolToObject(o, "sprx_vo", sprx_vo);
    http_send_obj(fd, 200, o);
}

static void handle_overlay_close(int fd)
{
    nitepr5_state_t *st = nitepr5_state();

    st->overlay_open = 0;
    st->watch_count = 0;
    st->next_watch_id = 1;
    memset(st->watches, 0, sizeof st->watches);
    if (!st->armed) {
        dbg_disconnect();
        st->dbg = 0;
    } else {
        st->dbg = dbg_connected() ? 1 : 0;
    }
    http_send(fd, 200, "{\"ok\":true,\"overlay_open\":false}");
}

static void handle_overlay_inject(int fd)
{
    cJSON *o;
    int rc;

    rc = inject_now();
    o = cJSON_CreateObject();
    if (rc == INJECT_OK) {
        notify_overlay_injected();
        cJSON_AddBoolToObject(o, "ok", 1);
        cJSON_AddNumberToObject(o, "pid", (double)inject_pid());
        http_send_obj(fd, 200, o);
        return;
    }
    if (rc == INJECT_ALREADY) {
        cJSON_AddBoolToObject(o, "ok", 1);
        cJSON_AddBoolToObject(o, "already", 1);
        cJSON_AddNumberToObject(o, "pid", (double)inject_pid());
        http_send_obj(fd, 200, o);
        return;
    }
    cJSON_Delete(o);
    if (rc == INJECT_MISSING_ELF) {
        http_err(fd, "NoOverlayElf");
    } else if (rc == INJECT_NO_DBG) {
        http_err(fd, "NotConnected");
    } else if (rc == INJECT_NO_EBOOT) {
        http_err(fd, "NoTarget");
    } else {
        http_err(fd, "InjectFailed");
    }
}

static void handle_read(int fd, const char *query)
{
    nitepr5_state_t *st = nitepr5_state();
    char abuf[32];
    char nbuf[32];
    uint64_t addr = 0;
    uint64_t n64 = 0;
    uint32_t n;
    uint32_t pid = 0;
    uint8_t data[READ_MAX];
    char hex[READ_MAX * 2 + 1];
    cJSON *o;

    if (query_val(query, "addr", abuf, sizeof abuf) != 0 || nitepr5_parse_u64(abuf, &addr) != 0) {
        http_err(fd, "BadRequest");
        return;
    }
    if (query_val(query, "n", nbuf, sizeof nbuf) == 0) {
        if (nitepr5_parse_u64(nbuf, &n64) != 0 || n64 == 0) {
            http_err(fd, "InvalidReadSize");
            return;
        }
        if (n64 > READ_MAX) {
            http_err(fd, "ReadTooLarge");
            return;
        }
        n = (uint32_t)n64;
    } else {
        n = HEX_PEEPHOLE_DEFAULT;
    }
    if (query_pid(query, st->pid, &pid) != 0) {
        http_err(fd, "BadRequest");
        return;
    }
    if (pid == 0) {
        http_err(fd, "NoTarget");
        return;
    }
    if (require_dbg(fd) != 0) {
        return;
    }
    if (dbg_proc_read(pid, addr, data, n) != 0) {
        st->dbg = 0;
        http_err(fd, "NotConnected");
        return;
    }
    nitepr5_hex_encode(data, n, hex, sizeof hex);
    o = cJSON_CreateObject();
    json_add_u64(o, "addr", addr);
    cJSON_AddNumberToObject(o, "n", (double)n);
    cJSON_AddStringToObject(o, "data", hex);
    http_send_obj(fd, 200, o);
}

static void handle_write(int fd, const char *body)
{
    nitepr5_state_t *st = nitepr5_state();
    cJSON *root;
    cJSON *addr_j;
    cJSON *data_j;
    cJSON *pid_j;
    uint64_t addr = 0;
    uint32_t pid;
    uint8_t data[WRITE_MAX];
    size_t n = 0;
    cJSON *o;

    root = cJSON_Parse(body ? body : "");
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        http_err(fd, "BadRequest");
        return;
    }
    addr_j = cJSON_GetObjectItemCaseSensitive(root, "addr");
    data_j = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (json_u64(addr_j, &addr) != 0) {
        cJSON_Delete(root);
        http_err(fd, "BadRequest");
        return;
    }
    if (!cJSON_IsString(data_j) || data_j->valuestring == NULL ||
        nitepr5_parse_hex(data_j->valuestring, data, WRITE_MAX, &n) != 0 || n < 1) {
        cJSON_Delete(root);
        http_err(fd, "InvalidWriteSize");
        return;
    }
    pid_j = cJSON_GetObjectItemCaseSensitive(root, "pid");
    if (pid_j != NULL && !cJSON_IsNull(pid_j)) {
        uint64_t v;
        if (json_u64(pid_j, &v) != 0 || v > 0xffffffffull) {
            cJSON_Delete(root);
            http_err(fd, "BadRequest");
            return;
        }
        pid = (uint32_t)v;
    } else {
        pid = st->pid;
    }
    cJSON_Delete(root);
    if (pid == 0) {
        http_err(fd, "NoTarget");
        return;
    }
    if (require_dbg(fd) != 0) {
        return;
    }
    if (dbg_proc_write(pid, addr, data, (uint32_t)n) != 0) {
        st->dbg = 0;
        http_err(fd, "NotConnected");
        return;
    }
    o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    json_add_u64(o, "addr", addr);
    cJSON_AddNumberToObject(o, "n", (double)n);
    http_send_obj(fd, 200, o);
}

static void handle_maps(int fd, const char *query)
{
    nitepr5_state_t *st = nitepr5_state();
    uint32_t pid = 0;
    uint8_t *buf;
    uint32_t count = 0;
    uint32_t i;
    cJSON *o;
    cJSON *arr;

    if (query_pid(query, st->pid, &pid) != 0) {
        http_err(fd, "BadRequest");
        return;
    }
    if (pid == 0) {
        http_err(fd, "NoTarget");
        return;
    }
    if (require_dbg(fd) != 0) {
        return;
    }
    buf = (uint8_t *)malloc((size_t)HTTP_LIST_CAP * DBG_MAP_ENTRY_SIZE);
    if (buf == NULL) {
        http_send(fd, 500, "{\"ok\":false,\"error\":\"BadRequest\"}");
        return;
    }
    if (dbg_proc_maps(pid, buf, HTTP_LIST_CAP, &count) != 0) {
        free(buf);
        st->dbg = 0;
        http_err(fd, "NotConnected");
        return;
    }
    o = cJSON_CreateObject();
    arr = cJSON_AddArrayToObject(o, "maps");
    for (i = 0; i < count; i++) {
        cJSON *row = cJSON_CreateObject();
        char name[33];
        uint64_t start = 0;
        uint64_t end = 0;
        uint64_t offset = 0;
        uint16_t prot = 0;

        dbg_decode_map(buf + (size_t)i * DBG_MAP_ENTRY_SIZE, name, &start, &end, &offset, &prot);
        cJSON_AddStringToObject(row, "name", name);
        json_add_u64(row, "start", start);
        json_add_u64(row, "end", end);
        json_add_u64(row, "offset", offset);
        cJSON_AddNumberToObject(row, "prot", (double)prot);
        cJSON_AddItemToArray(arr, row);
    }
    free(buf);
    http_send_obj(fd, 200, o);
}

static void handle_processes(int fd)
{
    nitepr5_state_t *st = nitepr5_state();
    uint8_t *buf;
    uint32_t count = 0;
    uint32_t i;
    cJSON *o;
    cJSON *arr;

    if (require_dbg(fd) != 0) {
        return;
    }
    buf = (uint8_t *)malloc((size_t)HTTP_LIST_CAP * DBG_PROC_ENTRY_SIZE);
    if (buf == NULL) {
        http_send(fd, 500, "{\"ok\":false,\"error\":\"BadRequest\"}");
        return;
    }
    if (dbg_proc_list(buf, HTTP_LIST_CAP, &count) != 0) {
        free(buf);
        st->dbg = 0;
        http_err(fd, "NotConnected");
        return;
    }
    o = cJSON_CreateObject();
    arr = cJSON_AddArrayToObject(o, "processes");
    for (i = 0; i < count; i++) {
        cJSON *row = cJSON_CreateObject();
        char name[33];
        int32_t pid = 0;

        dbg_decode_proc(buf + (size_t)i * DBG_PROC_ENTRY_SIZE, name, &pid);
        cJSON_AddNumberToObject(row, "pid", (double)pid);
        cJSON_AddStringToObject(row, "name", name);
        cJSON_AddItemToArray(arr, row);
    }
    free(buf);
    http_send_obj(fd, 200, o);
}

static void handle_foreground(int fd)
{
    nitepr5_state_t *st = nitepr5_state();
    uint8_t row[DBG_FOREGROUND_SIZE];
    uint32_t pid = 0;
    char titleid[17];
    char contentid[65];
    char name[41];
    char app_ver[17];
    cJSON *o;

    if (require_dbg(fd) != 0) {
        return;
    }
    if (dbg_foreground(row) != 0) {
        st->dbg = 0;
        http_err(fd, "NotConnected");
        return;
    }
    dbg_decode_foreground(row, &pid, titleid, contentid, name, app_ver);
    o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "pid", (double)pid);
    cJSON_AddStringToObject(o, "name", name);
    cJSON_AddStringToObject(o, "titleid", titleid);
    cJSON_AddStringToObject(o, "contentid", contentid);
    cJSON_AddStringToObject(o, "app_ver", app_ver);
    http_send_obj(fd, 200, o);
}

static void handle_attach(int fd, const char *body)
{
    nitepr5_state_t *st = nitepr5_state();
    cJSON *root;
    cJSON *pid_j;
    uint64_t v;
    uint32_t pid;
    cJSON *o;

    root = cJSON_Parse(body ? body : "");
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        http_err(fd, "BadRequest");
        return;
    }
    pid_j = cJSON_GetObjectItemCaseSensitive(root, "pid");
    if (json_u64(pid_j, &v) != 0 || v > 0xffffffffull) {
        cJSON_Delete(root);
        http_err(fd, "BadRequest");
        return;
    }
    pid = (uint32_t)v;
    cJSON_Delete(root);
    st->pid = pid;
    (void)fs_state_save();
    o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", 1);
    cJSON_AddNumberToObject(o, "pid", (double)pid);
    http_send_obj(fd, 200, o);
}

static cJSON *watch_to_json(const watch_entry_t *w)
{
    cJSON *row = cJSON_CreateObject();

    cJSON_AddNumberToObject(row, "id", (double)w->id);
    json_add_u64(row, "addr", w->addr);
    cJSON_AddNumberToObject(row, "n", (double)w->n);
    cJSON_AddStringToObject(row, "label", w->label);
    return row;
}

static void handle_watch_list(int fd)
{
    nitepr5_state_t *st = nitepr5_state();
    cJSON *o = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(o, "watches");
    int i;

    for (i = 0; i < st->watch_count; i++) {
        cJSON_AddItemToArray(arr, watch_to_json(&st->watches[i]));
    }
    http_send_obj(fd, 200, o);
}

static void handle_watch_add(int fd, const char *body)
{
    nitepr5_state_t *st = nitepr5_state();
    cJSON *root;
    cJSON *addr_j;
    cJSON *n_j;
    cJSON *lab;
    uint64_t addr = 0;
    uint32_t n = 4;
    watch_entry_t *w;

    root = cJSON_Parse(body ? body : "");
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        http_err(fd, "BadRequest");
        return;
    }
    addr_j = cJSON_GetObjectItemCaseSensitive(root, "addr");
    if (json_u64(addr_j, &addr) != 0) {
        cJSON_Delete(root);
        http_err(fd, "BadRequest");
        return;
    }
    n_j = cJSON_GetObjectItemCaseSensitive(root, "n");
    if (n_j != NULL && !cJSON_IsNull(n_j)) {
        uint64_t vn;
        if (json_u64(n_j, &vn) != 0) {
            cJSON_Delete(root);
            http_err(fd, "InvalidWatchSize");
            return;
        }
        n = (uint32_t)vn;
    }
    if (!watch_n_ok(n)) {
        cJSON_Delete(root);
        http_err(fd, "InvalidWatchSize");
        return;
    }
    if (st->watch_count >= WATCH_MAX) {
        cJSON_Delete(root);
        http_err(fd, "WatchLimit");
        return;
    }
    w = &st->watches[st->watch_count];
    memset(w, 0, sizeof *w);
    w->id = st->next_watch_id++;
    if (st->next_watch_id == 0) {
        st->next_watch_id = 1;
    }
    w->addr = addr;
    w->n = (uint8_t)n;
    lab = cJSON_GetObjectItemCaseSensitive(root, "label");
    if (cJSON_IsString(lab) && lab->valuestring) {
        nitepr5_copy_str(w->label, WATCH_LABEL_LEN, lab->valuestring);
    }
    st->watch_count++;
    cJSON_Delete(root);
    http_send_obj(fd, 200, watch_to_json(w));
}

static int find_watch(uint32_t id)
{
    nitepr5_state_t *st = nitepr5_state();
    int i;

    for (i = 0; i < st->watch_count; i++) {
        if (st->watches[i].id == id) {
            return i;
        }
    }
    return -1;
}

static void handle_watch_del(int fd, uint32_t id)
{
    nitepr5_state_t *st = nitepr5_state();
    int i = find_watch(id);

    if (i < 0) {
        http_err(fd, "NoWatch");
        return;
    }
    if (i + 1 < st->watch_count) {
        memmove(&st->watches[i], &st->watches[i + 1],
                sizeof(watch_entry_t) * (size_t)(st->watch_count - i - 1));
    }
    st->watch_count--;
    memset(&st->watches[st->watch_count], 0, sizeof(watch_entry_t));
    http_send(fd, 200, "{\"ok\":true}");
}

static void handle_watch_poll(int fd)
{
    nitepr5_state_t *st = nitepr5_state();
    cJSON *o;
    cJSON *arr;
    int i;

    if (st->pid == 0) {
        http_err(fd, "NoTarget");
        return;
    }
    if (require_dbg(fd) != 0) {
        return;
    }
    o = cJSON_CreateObject();
    arr = cJSON_AddArrayToObject(o, "values");
    for (i = 0; i < st->watch_count; i++) {
        watch_entry_t *w = &st->watches[i];
        uint8_t data[8];
        char hex[17];
        cJSON *row;

        if (dbg_proc_read(st->pid, w->addr, data, w->n) != 0) {
            cJSON_Delete(o);
            st->dbg = 0;
            http_err(fd, "NotConnected");
            return;
        }
        nitepr5_hex_encode(data, w->n, hex, sizeof hex);
        row = cJSON_CreateObject();
        cJSON_AddNumberToObject(row, "id", (double)w->id);
        json_add_u64(row, "addr", w->addr);
        cJSON_AddStringToObject(row, "data", hex);
        cJSON_AddItemToArray(arr, row);
    }
    http_send_obj(fd, 200, o);
}

static cJSON *freeze_to_json(const freeze_entry_t *fr)
{
    cJSON *row = cJSON_CreateObject();
    char hex[FREEZE_DATA_MAX * 2 + 1];

    nitepr5_hex_encode(fr->data, fr->n, hex, sizeof hex);
    cJSON_AddNumberToObject(row, "id", (double)fr->id);
    json_add_u64(row, "addr", fr->addr);
    cJSON_AddStringToObject(row, "data", hex);
    return row;
}

static void handle_freeze_list(int fd)
{
    nitepr5_state_t *st = nitepr5_state();
    cJSON *o = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(o, "freezes");
    int i;

    for (i = 0; i < st->freeze_count; i++) {
        cJSON_AddItemToArray(arr, freeze_to_json(&st->freezes[i]));
    }
    http_send_obj(fd, 200, o);
}

static void handle_freeze_add(int fd, const char *body)
{
    nitepr5_state_t *st = nitepr5_state();
    cJSON *root;
    freeze_entry_t row;
    freeze_entry_t *fr;

    root = cJSON_Parse(body ? body : "");
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        http_err(fd, "BadRequest");
        return;
    }
    if (parse_freeze_row(root, &row) != 0) {
        cJSON_Delete(root);
        http_err(fd, "InvalidFreezeSize");
        return;
    }
    cJSON_Delete(root);
    if (st->freeze_count >= FREEZE_MAX) {
        http_err(fd, "FreezeLimit");
        return;
    }
    fr = &st->freezes[st->freeze_count];
    *fr = row;
    fr->id = st->next_freeze_id++;
    if (st->next_freeze_id == 0) {
        st->next_freeze_id = 1;
    }
    st->freeze_count++;
    (void)fs_state_save();
    http_send_obj(fd, 200, freeze_to_json(fr));
}

static int find_freeze(uint32_t id)
{
    nitepr5_state_t *st = nitepr5_state();
    int i;

    for (i = 0; i < st->freeze_count; i++) {
        if (st->freezes[i].id == id) {
            return i;
        }
    }
    return -1;
}

static void handle_freeze_del(int fd, uint32_t id)
{
    nitepr5_state_t *st = nitepr5_state();
    int i = find_freeze(id);

    if (i < 0) {
        http_err(fd, "NoFreeze");
        return;
    }
    if (i + 1 < st->freeze_count) {
        memmove(&st->freezes[i], &st->freezes[i + 1],
                sizeof(freeze_entry_t) * (size_t)(st->freeze_count - i - 1));
    }
    st->freeze_count--;
    memset(&st->freezes[st->freeze_count], 0, sizeof(freeze_entry_t));
    (void)fs_state_save();
    http_send(fd, 200, "{\"ok\":true}");
}

static void handle_cheat_get(int fd)
{
    nitepr5_state_t *st = nitepr5_state();
    cJSON *o = cJSON_CreateObject();
    cJSON *en;
    int i;

    if (!st->cheat.loaded) {
        cJSON_AddNullToObject(o, "cheat");
    } else {
        cJSON *ch = cJSON_CreateObject();
        cJSON *mods;
        cJSON_AddStringToObject(ch, "name", st->cheat.name);
        cJSON_AddStringToObject(ch, "id", st->cheat.id);
        cJSON_AddStringToObject(ch, "version", st->cheat.version);
        cJSON_AddStringToObject(ch, "process", st->cheat.process);
        mods = cJSON_AddArrayToObject(ch, "mods");
        for (i = 0; i < st->cheat.mod_count; i++) {
            cJSON *m = cJSON_CreateObject();
            cJSON_AddStringToObject(m, "name", st->cheat.mods[i].name);
            cJSON_AddItemToArray(mods, m);
        }
        cJSON_AddItemToObject(o, "cheat", ch);
    }
    en = cJSON_AddArrayToObject(o, "enabled");
    for (i = 0; i < st->enabled_count; i++) {
        cJSON_AddItemToArray(en, cJSON_CreateString(st->enabled[i]));
    }
    http_send_obj(fd, 200, o);
}

static void http_handle_client(int fd)
{
    char method[16];
    char path[128];
    char query[HTTP_QUERY_MAX];
    char *body = NULL;
    size_t body_n = 0;
    uint32_t id = 0;
    int rc;

    rc = recv_request(fd, method, sizeof method, path, sizeof path, query, sizeof query, &body,
                      &body_n);
    if (rc == -2) {
        http_err(fd, "BadRequest");
        return;
    }
    if (rc != 0) {
        free(body);
        return;
    }

    if (strcmp(method, "GET") == 0 && path_is(path, "/status")) {
        handle_status(fd);
    } else if (strcmp(method, "POST") == 0 && path_is(path, "/overlay/open")) {
        handle_overlay_open(fd, body);
    } else if (strcmp(method, "POST") == 0 && path_is(path, "/overlay/close")) {
        handle_overlay_close(fd);
    } else if (strcmp(method, "POST") == 0 && path_is(path, "/overlay/inject")) {
        handle_overlay_inject(fd);
    } else if (strcmp(method, "GET") == 0 && path_is(path, "/read")) {
        handle_read(fd, query);
    } else if (strcmp(method, "POST") == 0 && path_is(path, "/write")) {
        handle_write(fd, body);
    } else if (strcmp(method, "GET") == 0 && path_is(path, "/maps")) {
        handle_maps(fd, query);
    } else if (strcmp(method, "GET") == 0 && path_is(path, "/processes")) {
        handle_processes(fd);
    } else if (strcmp(method, "GET") == 0 && path_is(path, "/foreground")) {
        handle_foreground(fd);
    } else if (strcmp(method, "POST") == 0 && path_is(path, "/attach")) {
        handle_attach(fd, body);
    } else if (strcmp(method, "GET") == 0 && path_is(path, "/watch/poll")) {
        handle_watch_poll(fd);
    } else if (strcmp(method, "GET") == 0 && path_is(path, "/watch")) {
        handle_watch_list(fd);
    } else if (strcmp(method, "POST") == 0 && path_is(path, "/watch")) {
        handle_watch_add(fd, body);
    } else if (strcmp(method, "DELETE") == 0 && path_id(path, "/watch/", &id)) {
        handle_watch_del(fd, id);
    } else if (strcmp(method, "GET") == 0 && path_is(path, "/freeze")) {
        handle_freeze_list(fd);
    } else if (strcmp(method, "POST") == 0 && path_is(path, "/freeze")) {
        handle_freeze_add(fd, body);
    } else if (strcmp(method, "DELETE") == 0 && path_id(path, "/freeze/", &id)) {
        handle_freeze_del(fd, id);
    } else if (strcmp(method, "GET") == 0 && path_is(path, "/cheat")) {
        handle_cheat_get(fd);
    } else if (strcmp(method, "POST") == 0 && path_is(path, "/arm")) {
        handle_arm(fd, body, body_n);
    } else if (strcmp(method, "POST") == 0 && path_is(path, "/disarm")) {
        handle_disarm(fd);
    } else if (strcmp(method, "POST") == 0 && path_is(path, "/cheat/toggle")) {
        handle_cheat_toggle(fd, body);
    } else if (strcmp(method, "POST") == 0 && path_is(path, "/cheat/load")) {
        handle_cheat_load(fd, body, body_n);
    } else {
        http_err(fd, "BadRequest");
    }
    free(body);
}

void http_run(void)
{
    int listen_fd;
    int on = 1;
    struct sockaddr_in addr;

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        for (;;) {
            freeze_tick();
            inject_poll();
            usleep((useconds_t)POLL_TIMEOUT_MS * 1000);
        }
    }
#ifdef SO_NOSIGPIPE
    setsockopt(listen_fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(NITEPR5_HTTP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(listen_fd, 8) != 0) {
        close(listen_fd);
        for (;;) {
            freeze_tick();
            inject_poll();
            usleep((useconds_t)POLL_TIMEOUT_MS * 1000);
        }
    }

    for (;;) {
        struct pollfd pfd;
        int pr;
        nitepr5_state_t *st = nitepr5_state();

        pfd.fd = listen_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        pr = poll(&pfd, 1, POLL_TIMEOUT_MS);
        if (pr > 0 && (pfd.revents & POLLIN)) {
            int cfd = accept(listen_fd, NULL, NULL);
            if (cfd >= 0) {
                struct timeval tv;
                memset(&tv, 0, sizeof tv);
                tv.tv_sec = 2;
                setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
                setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
                http_handle_client(cfd);
                close(cfd);
            }
        }
        if (st->armed) {
            freeze_tick();
        }
        inject_poll();
    }
}
