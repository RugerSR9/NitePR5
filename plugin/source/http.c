#include "http.h"
#include "cheats.h"
#include "dbg_client.h"
#include "freeze.h"
#include "fs_state.h"
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

#define HTTP_HDR_MAX  8192
#define HTTP_BODY_MAX (256 * 1024)

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
                        char **body, size_t *body_n)
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

    if (!cJSON_IsObject(row)) {
        return -1;
    }
    addr = cJSON_GetObjectItemCaseSensitive(row, "addr");
    data = cJSON_GetObjectItemCaseSensitive(row, "data");
    if (!cJSON_IsNumber(addr) || addr->valuedouble < 0) {
        return -1;
    }
    if (!cJSON_IsString(data) || data->valuestring == NULL) {
        return -1;
    }
    if (nitepr5_parse_hex(data->valuestring, out->data, FREEZE_DATA_MAX, &n) != 0 ||
        n < 1 || n > FREEZE_DATA_MAX) {
        return -1;
    }
    out->addr = (uint64_t)addr->valuedouble;
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
    dbg_disconnect();
    st->dbg = 0;
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

static void http_handle_client(int fd)
{
    char method[16];
    char path[128];
    char *body = NULL;
    size_t body_n = 0;
    int rc;

    rc = recv_request(fd, method, sizeof method, path, sizeof path, &body, &body_n);
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
    }
}
