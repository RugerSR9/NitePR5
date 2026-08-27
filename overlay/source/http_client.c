#include "overlay.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#define HTTP_CAP (256 * 1024)

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

static int connect_timeout(int fd, const struct sockaddr *sa, socklen_t sl, int ms)
{
    int fl = fcntl(fd, F_GETFL, 0);
    struct pollfd pfd;
    int err = 0;
    socklen_t elen = sizeof err;

    if (fl >= 0) {
        (void)fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    }
    if (connect(fd, sa, sl) == 0) {
        if (fl >= 0) {
            (void)fcntl(fd, F_SETFL, fl);
        }
        return 0;
    }
    if (errno != EINPROGRESS && errno != EWOULDBLOCK) {
        return -1;
    }
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    if (poll(&pfd, 1, ms) <= 0) {
        return -1;
    }
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen) != 0 || err != 0) {
        return -1;
    }
    if (fl >= 0) {
        (void)fcntl(fd, F_SETFL, fl);
    }
    return 0;
}

int http_request(const char *method, const char *path, const char *body, int *status, char **out,
                 size_t *out_n)
{
    int fd = -1;
    struct sockaddr_in addr;
    char hdr[512];
    size_t body_n;
    int hn;
    struct timeval tv;
    char *buf = NULL;
    size_t got = 0;
    size_t cap = 4096;
    char *sep;
    int code = 0;
    char *p;

    if (status) {
        *status = 0;
    }
    if (out) {
        *out = NULL;
    }
    if (out_n) {
        *out_n = 0;
    }
    if (method == NULL || path == NULL) {
        return -1;
    }
    body_n = body ? strlen(body) : 0;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
#ifdef SO_NOSIGPIPE
    {
        int on = 1;
        (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
    }
#endif
    tv.tv_sec = HTTP_TIMEOUT_MS / 1000;
    tv.tv_usec = (HTTP_TIMEOUT_MS % 1000) * 1000;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)PLUGIN_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect_timeout(fd, (struct sockaddr *)&addr, sizeof addr, HTTP_TIMEOUT_MS) != 0) {
        close(fd);
        return -1;
    }
    hn = snprintf(hdr, sizeof hdr,
                  "%s %s HTTP/1.1\r\n"
                  "Host: 127.0.0.1:%d\r\n"
                  "Content-Type: application/json\r\n"
                  "Content-Length: %zu\r\n"
                  "Connection: close\r\n"
                  "\r\n",
                  method, path, PLUGIN_PORT, body_n);
    if (hn <= 0 || send_all(fd, hdr, (size_t)hn) != 0) {
        close(fd);
        return -1;
    }
    if (body_n && send_all(fd, body, body_n) != 0) {
        close(fd);
        return -1;
    }
    buf = (char *)malloc(cap);
    if (buf == NULL) {
        close(fd);
        return -1;
    }
    for (;;) {
        ssize_t r;
        if (got + 1024 >= cap) {
            char *nb;
            size_t nc = cap * 2;
            if (nc > HTTP_CAP) {
                nc = HTTP_CAP;
            }
            if (got + 1 >= nc) {
                break;
            }
            nb = (char *)realloc(buf, nc);
            if (nb == NULL) {
                break;
            }
            buf = nb;
            cap = nc;
        }
        r = recv(fd, buf + got, cap - got - 1, 0);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (r == 0) {
            break;
        }
        got += (size_t)r;
    }
    close(fd);
    buf[got] = 0;
    if (got < 8 || strncmp(buf, "HTTP/", 5) != 0) {
        free(buf);
        return -1;
    }
    p = strchr(buf, ' ');
    if (p) {
        code = atoi(p + 1);
    }
    if (status) {
        *status = code;
    }
    sep = strstr(buf, "\r\n\r\n");
    if (sep) {
        char *bodyp = sep + 4;
        size_t bn = strlen(bodyp);
        char *copy = (char *)malloc(bn + 1);
        if (copy) {
            memcpy(copy, bodyp, bn + 1);
            if (out) {
                *out = copy;
            } else {
                free(copy);
            }
            if (out_n) {
                *out_n = bn;
            }
        }
    } else if (out) {
        *out = buf;
        buf = NULL;
        if (out_n) {
            *out_n = got;
        }
    }
    free(buf);
    return 0;
}
