/* Thin PS5Debug client on 127.0.0.1:744. Subset of ps5dbg 0.1.1 / PROTOCOL.md v1.3.0.
 * Not a protocol fork. No turbo scan, no PT_ATTACH, no DEBUG_*, no PROC_WRITE_MULTI.
 */

#include "dbg_client.h"
#include "nitepr5.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>

#define DBG_MAX_COUNT 4096

static int g_fd = -1;

static void put_u32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put_u64le(uint8_t *p, uint64_t v)
{
    put_u32le(p, (uint32_t)v);
    put_u32le(p + 4, (uint32_t)(v >> 32));
}

static uint32_t get_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t get_u64le(const uint8_t *p)
{
    return (uint64_t)get_u32le(p) | ((uint64_t)get_u32le(p + 4) << 32);
}

static uint16_t get_u16le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void cstr_fixed(char *dst, size_t cap, const uint8_t *src, size_t n)
{
    size_t i;

    if (dst == NULL || cap == 0) {
        return;
    }
    for (i = 0; i + 1 < cap && i < n && src[i]; i++) {
        dst[i] = (char)src[i];
    }
    dst[i] = 0;
}

void dbg_decode_map(const uint8_t *row58, char name[33], uint64_t *start, uint64_t *end,
                    uint64_t *offset, uint16_t *prot)
{
    /* char name[32] + u64 start + u64 end + u64 offset + u16 prot (ps5dbg VmMap). */
    if (row58 == NULL) {
        return;
    }
    cstr_fixed(name, 33, row58, 32);
    if (start) {
        *start = get_u64le(row58 + 32);
    }
    if (end) {
        *end = get_u64le(row58 + 40);
    }
    if (offset) {
        *offset = get_u64le(row58 + 48);
    }
    if (prot) {
        *prot = get_u16le(row58 + 56);
    }
}

void dbg_decode_proc(const uint8_t *row36, char name[33], int32_t *pid)
{
    /* char name[32] + int32 pid (ps5dbg ProcInfo). */
    if (row36 == NULL) {
        return;
    }
    cstr_fixed(name, 33, row36, 32);
    if (pid) {
        *pid = (int32_t)get_u32le(row36 + 32);
    }
}

void dbg_decode_foreground(const uint8_t *row140, uint32_t *pid, char titleid[17],
                           char contentid[65], char name[41], char app_ver[17])
{
    /* u32 pid + titleid[16] + contentid[64] + name[40] + app_ver[16]. */
    if (row140 == NULL) {
        return;
    }
    if (pid) {
        *pid = get_u32le(row140);
    }
    cstr_fixed(titleid, 17, row140 + 4, 16);
    cstr_fixed(contentid, 65, row140 + 20, 64);
    cstr_fixed(name, 41, row140 + 84, 40);
    cstr_fixed(app_ver, 17, row140 + 124, 16);
}

static int send_all(int fd, const void *buf, size_t n)
{
    const uint8_t *p = (const uint8_t *)buf;

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

static int recv_all(int fd, void *buf, size_t n)
{
    uint8_t *p = (uint8_t *)buf;

    while (n > 0) {
        ssize_t r = recv(fd, p, n, 0);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (r == 0) {
            return -1;
        }
        p += (size_t)r;
        n -= (size_t)r;
    }
    return 0;
}

void dbg_disconnect(void)
{
    if (g_fd >= 0) {
        close(g_fd);
        g_fd = -1;
    }
}

int dbg_connected(void)
{
    return g_fd >= 0;
}

int dbg_ensure(void)
{
    if (dbg_connected()) {
        return 0;
    }
    return dbg_connect();
}

static int send_request(uint32_t cmd, const void *body, uint32_t datalen)
{
    uint8_t hdr[12];

    if (g_fd < 0) {
        return -1;
    }
    put_u32le(hdr + 0, DBG_MAGIC);
    put_u32le(hdr + 4, cmd);
    put_u32le(hdr + 8, datalen);
    if (send_all(g_fd, hdr, 12) != 0) {
        dbg_disconnect();
        return -1;
    }
    if (datalen > 0 && body != NULL) {
        if (send_all(g_fd, body, datalen) != 0) {
            dbg_disconnect();
            return -1;
        }
    }
    return 0;
}

/* Success is the raw little-endian word 0x80000000. Do not bitswap. */
static int read_status(void)
{
    uint8_t buf[4];
    uint32_t st;

    if (g_fd < 0) {
        return -1;
    }
    if (recv_all(g_fd, buf, 4) != 0) {
        dbg_disconnect();
        return -1;
    }
    st = get_u32le(buf);
    if (st != DBG_SUCCESS) {
        dbg_disconnect();
        return -1;
    }
    return 0;
}

int dbg_connect(void)
{
    int fd;
    int flags;
    int rc;
    int err;
    socklen_t elen;
    struct sockaddr_in addr;
    struct pollfd pfd;
    struct timeval tv;

    dbg_disconnect();

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

#ifdef SO_NOSIGPIPE
    {
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    }
#endif

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(NITEPR5_DBG_PORT);
    if (inet_pton(AF_INET, NITEPR5_DBG_HOST, &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        close(fd);
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(fd);
        return -1;
    }

    rc = connect(fd, (struct sockaddr *)&addr, sizeof addr);
    if (rc < 0 && errno != EINPROGRESS && errno != EINTR) {
        close(fd);
        return -1;
    }

    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    if (poll(&pfd, 1, 1000) <= 0) {
        close(fd);
        return -1;
    }

    err = 0;
    elen = sizeof err;
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen) < 0 || err != 0) {
        close(fd);
        return -1;
    }

    if (fcntl(fd, F_SETFL, flags) < 0) {
        close(fd);
        return -1;
    }

    memset(&tv, 0, sizeof tv);
    tv.tv_sec = 2;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

    g_fd = fd;
    return 0;
}

int dbg_nop(void)
{
    if (send_request(PROC_NOP, NULL, 0) != 0) {
        return -1;
    }
    return read_status();
}

int dbg_proc_write(uint32_t pid, uint64_t addr, const uint8_t *data, uint32_t length)
{
    uint8_t packet[DBG_WRITE_PACKET_LEN];

    if (length > 0 && data == NULL) {
        return -1;
    }

    /* Phase 1: request body is exactly 16 bytes so datalen 16. Never pack payload here. */
    memset(packet, 0, sizeof packet);
    put_u32le(packet + 0, pid);
    put_u64le(packet + 4, addr);
    put_u32le(packet + 12, length);

    if (send_request(PROC_WRITE, packet, DBG_WRITE_PACKET_LEN) != 0) {
        return -1;
    }
    if (read_status() != 0) {
        return -1;
    }

    /* Phase 2: payload is a separate send, not concatenated into datalen. */
    if (length > 0) {
        if (send_all(g_fd, data, length) != 0) {
            dbg_disconnect();
            return -1;
        }
    }
    return read_status();
}

int dbg_proc_read(uint32_t pid, uint64_t addr, uint8_t *out, uint32_t length)
{
    uint8_t packet[DBG_READ_PACKET_LEN];

    if (out == NULL || length < 1 || length > DBG_READ_MAX) {
        return -1;
    }

    /* One-phase: request body is exactly 16 bytes. Reply is SUCCESS then `length` bytes. */
    memset(packet, 0, sizeof packet);
    put_u32le(packet + 0, pid);
    put_u64le(packet + 4, addr);
    put_u32le(packet + 12, length);

    if (send_request(PROC_READ, packet, DBG_READ_PACKET_LEN) != 0) {
        return -1;
    }
    if (read_status() != 0) {
        return -1;
    }
    if (recv_all(g_fd, out, length) != 0) {
        dbg_disconnect();
        return -1;
    }
    return 0;
}

static int dbg_set_timeout(int sec)
{
    struct timeval tv;

    if (g_fd < 0) {
        return -1;
    }
    memset(&tv, 0, sizeof tv);
    tv.tv_sec = sec;
    if (setsockopt(g_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) != 0) {
        return -1;
    }
    return setsockopt(g_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

int dbg_proc_elf(uint32_t pid, const uint8_t *elf, uint32_t length)
{
    uint8_t packet[DBG_ELF_PACKET_LEN];

    if (elf == NULL || length < 16 || length > DBG_ELF_MAX) {
        return -1;
    }
    if (elf[0] != 0x7f || elf[1] != 'E' || elf[2] != 'L' || elf[3] != 'F') {
        return -1;
    }

    /* Phase 1: request body is exactly 8 bytes so datalen 8. Never pack ELF here. */
    memset(packet, 0, sizeof packet);
    put_u32le(packet + 0, pid);
    put_u32le(packet + 4, length);

    (void)dbg_set_timeout(20);
    if (send_request(PROC_ELF, packet, DBG_ELF_PACKET_LEN) != 0) {
        (void)dbg_set_timeout(2);
        return -1;
    }
    if (read_status() != 0) {
        return -1;
    }

    /* Phase 2: ELF is a separate send, not concatenated into datalen. */
    if (send_all(g_fd, elf, length) != 0) {
        dbg_disconnect();
        return -1;
    }
    if (read_status() != 0) {
        return -1;
    }
    (void)dbg_set_timeout(2);
    return 0;
}

static int recv_count_then(uint32_t *count, void *entries, uint32_t cap, uint32_t elem)
{
    uint8_t nbuf[4];
    uint32_t n;
    uint32_t take;
    uint32_t extra;
    uint8_t skip[256];

    if (recv_all(g_fd, nbuf, 4) != 0) {
        dbg_disconnect();
        return -1;
    }
    n = get_u32le(nbuf);
    if (n > DBG_MAX_COUNT) {
        dbg_disconnect();
        return -1;
    }
    take = n;
    if (take > cap) {
        take = cap;
    }
    if (take > 0 && entries != NULL) {
        if (recv_all(g_fd, entries, (size_t)take * elem) != 0) {
            dbg_disconnect();
            return -1;
        }
    }
    extra = n - take;
    while (extra > 0) {
        uint32_t chunk = extra;
        if (chunk > (uint32_t)(sizeof skip / elem)) {
            chunk = (uint32_t)(sizeof skip / elem);
        }
        if (chunk == 0) {
            chunk = 1;
        }
        if (recv_all(g_fd, skip, (size_t)chunk * elem) != 0) {
            dbg_disconnect();
            return -1;
        }
        extra -= chunk;
    }
    *count = take;
    return 0;
}

int dbg_proc_list(void *entries, uint32_t cap, uint32_t *count)
{
    if (count == NULL) {
        return -1;
    }
    if (send_request(PROC_LIST, NULL, 0) != 0) {
        return -1;
    }
    if (read_status() != 0) {
        return -1;
    }
    return recv_count_then(count, entries, cap, DBG_PROC_ENTRY_SIZE);
}

int dbg_proc_maps(uint32_t pid, void *entries, uint32_t cap, uint32_t *count)
{
    uint8_t body[4];

    if (count == NULL) {
        return -1;
    }
    put_u32le(body, pid);
    if (send_request(PROC_MAPS, body, 4) != 0) {
        return -1;
    }
    if (read_status() != 0) {
        return -1;
    }
    return recv_count_then(count, entries, cap, DBG_MAP_ENTRY_SIZE);
}

int dbg_proc_info(uint32_t pid, void *out188)
{
    uint8_t body[4];

    if (out188 == NULL) {
        return -1;
    }
    put_u32le(body, pid);
    if (send_request(PROC_INFO, body, 4) != 0) {
        return -1;
    }
    if (read_status() != 0) {
        return -1;
    }
    if (recv_all(g_fd, out188, DBG_PROC_INFO_SIZE) != 0) {
        dbg_disconnect();
        return -1;
    }
    return 0;
}

int dbg_foreground(void *out140)
{
    if (out140 == NULL) {
        return -1;
    }
    if (send_request(CONSOLE_FOREGROUND_APP, NULL, 0) != 0) {
        return -1;
    }
    if (read_status() != 0) {
        return -1;
    }
    if (recv_all(g_fd, out140, DBG_FOREGROUND_SIZE) != 0) {
        dbg_disconnect();
        return -1;
    }
    return 0;
}

static int map_belongs(const char *map_name, const char *process)
{
    const char *base;
    const char *proc;

    if (map_name == NULL || map_name[0] == 0) {
        return 0;
    }
    proc = (process && process[0]) ? process : EBOOT_NAME;
    base = nitepr5_basename(map_name);
    if (strcmp(map_name, proc) == 0 || strcmp(base, proc) == 0) {
        return 1;
    }
    if (nitepr5_ascii_ieq(proc, EBOOT_NAME) && nitepr5_ascii_ieq(base, EXECUTABLE_MAP_NAME)) {
        return 1;
    }
    return 0;
}

int dbg_module_base(uint32_t pid, const char *process, uint64_t *out_base)
{
    uint8_t *buf;
    uint32_t count = 0;
    uint32_t i;
    int found = 0;
    uint64_t best = 0;

    if (out_base == NULL) {
        return -1;
    }
    buf = (uint8_t *)malloc((size_t)DBG_MAX_COUNT * DBG_MAP_ENTRY_SIZE);
    if (buf == NULL) {
        return -1;
    }
    if (dbg_proc_maps(pid, buf, DBG_MAX_COUNT, &count) != 0) {
        free(buf);
        return -1;
    }
    for (i = 0; i < count; i++) {
        const uint8_t *row = buf + (size_t)i * DBG_MAP_ENTRY_SIZE;
        char name[33];
        uint64_t start;

        memcpy(name, row, 32);
        name[32] = 0;
        start = get_u64le(row + 32);
        if (!map_belongs(name, process)) {
            continue;
        }
        if (!found || start < best) {
            best = start;
            found = 1;
        }
    }
    free(buf);
    if (!found) {
        return -1;
    }
    *out_base = best;
    return 0;
}
