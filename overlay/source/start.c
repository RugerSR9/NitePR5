/* PROC_ELF e_entry. The hijacked game thread must return immediately.
 * 0.52 ran kernel_init/CRT/for(;;) on that thread — boot froze, alive
 * file never appeared. Spawn a new thread for CRT; do not import libc
 * (PLT is unbound until rtld).
 */

#include <stddef.h>

typedef int (*dlsym_fn)(int handle, const char *name, void *out);
typedef int (*notify_fn)(int device, void *req, unsigned long size, int unk);
typedef int (*open_fn)(const char *path, int flags, int mode);
typedef long (*write_fn)(int fd, const void *buf, unsigned long n);
typedef int (*close_fn)(int fd);
typedef int (*getpid_fn)(void);
typedef int (*pthread_create_fn)(void *thread, void *attr, void *(*entry)(void *), void *arg);
typedef int (*sce_pthread_create_fn)(void *thread, void *attr, void *(*entry)(void *), void *arg,
                                     const char *name);

typedef struct overlay_args {
    dlsym_fn dynlib_dlsym;
    int *rwpipe;
    int *rwpair;
    long kpipe;
    long kdata;
    int *payloadout;
} overlay_args_t;

typedef struct notify_request {
    char useless1[45];
    char message[3075];
} notify_request_t;

int __crt_syscall_init(void *args) __attribute__((weak));
int __kernel_init(void *args) __attribute__((weak));
int __crt_start(void *args);
int kernel_set_ucred_authid(int pid, unsigned long authid) __attribute__((weak));
int kernel_set_ucred_caps(int pid, unsigned char caps[16]) __attribute__((weak));

#define OVERLAY_ALIVE_PATH "/data/nitepr5/overlay.alive"
#define O_WRONLY_CREAT_TRUNC 0x601 /* FreeBSD O_WRONLY|O_CREAT|O_TRUNC */
#define ALIVE_MODE 0666

static void zero_bytes(void *p, unsigned long n)
{
    unsigned char *b = (unsigned char *)p;
    unsigned long i;

    for (i = 0; i < n; i++) {
        b[i] = 0;
    }
}

static void copy_cstr(char *dst, unsigned long cap, const char *src)
{
    unsigned long i;

    if (dst == NULL || cap == 0) {
        return;
    }
    i = 0;
    if (src != NULL) {
        while (i + 1 < cap && src[i]) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = 0;
}

static void *resolve(overlay_args_t *args, int handle, const char *name)
{
    void *fn = 0;

    if (args == NULL || args->dynlib_dlsym == NULL || name == NULL) {
        return NULL;
    }
    args->dynlib_dlsym(handle, name, &fn);
    return fn;
}

static void raw_notify(overlay_args_t *args, const char *msg)
{
    notify_fn fn;
    notify_request_t req;

    fn = (notify_fn)resolve(args, 0x2001, "sceKernelSendNotificationRequest");
    if (fn == NULL) {
        fn = (notify_fn)resolve(args, 0x1, "sceKernelSendNotificationRequest");
    }
    if (fn == NULL) {
        return;
    }
    zero_bytes(&req, sizeof req);
    copy_cstr(req.message, sizeof req.message, msg);
    (void)fn(0, &req, sizeof req, 0);
}

static void write_alive(overlay_args_t *args)
{
    open_fn op;
    write_fn wr;
    close_fn cl;
    int fd;
    const char *body = "1\n";

    op = (open_fn)resolve(args, 0x2, "open");
    wr = (write_fn)resolve(args, 0x2, "write");
    cl = (close_fn)resolve(args, 0x2, "close");
    if (op == NULL) {
        op = (open_fn)resolve(args, 0x1, "open");
    }
    if (wr == NULL) {
        wr = (write_fn)resolve(args, 0x1, "write");
    }
    if (cl == NULL) {
        cl = (close_fn)resolve(args, 0x1, "close");
    }
    if (op == NULL || wr == NULL || cl == NULL) {
        return;
    }
    fd = op(OVERLAY_ALIVE_PATH, O_WRONLY_CREAT_TRUNC, ALIVE_MODE);
    if (fd < 0) {
        return;
    }
    (void)wr(fd, body, 2);
    (void)cl(fd);
}

static void raise_caps(overlay_args_t *args)
{
    unsigned char caps[16];
    getpid_fn gp;
    int pid;
    unsigned i;

    for (i = 0; i < sizeof caps; i++) {
        caps[i] = 0xff;
    }
    pid = 0;
    gp = (getpid_fn)resolve(args, 0x1, "getpid");
    if (gp == NULL) {
        gp = (getpid_fn)resolve(args, 0x2001, "getpid");
    }
    if (gp) {
        pid = gp();
    }
    if (pid <= 0) {
        return;
    }
    if (kernel_set_ucred_authid) {
        (void)kernel_set_ucred_authid(pid, 0x4800000000000007ul);
    }
    if (kernel_set_ucred_caps) {
        (void)kernel_set_ucred_caps(pid, caps);
    }
}

static void *overlay_thread(void *arg)
{
    overlay_args_t *args = (overlay_args_t *)arg;

    write_alive(args);
    if (__crt_syscall_init) {
        (void)__crt_syscall_init(args);
    }
    if (__kernel_init && __kernel_init(args) == 0) {
        raise_caps(args);
        write_alive(args);
    }
    raw_notify(args, "NitePR5 overlay entry");
    (void)__crt_start(args);
    raw_notify(args, "NitePR5 CRT returned");
    for (;;) {
    }
    return NULL;
}

static int spawn_overlay_thread(overlay_args_t *args)
{
    pthread_create_fn pc;
    sce_pthread_create_fn sce;
    void *th = 0;

    pc = (pthread_create_fn)resolve(args, 0x2, "pthread_create");
    if (pc != NULL && pc(&th, NULL, overlay_thread, args) == 0) {
        return 0;
    }
    sce = (sce_pthread_create_fn)resolve(args, 0x1, "scePthreadCreate");
    if (sce == NULL) {
        sce = (sce_pthread_create_fn)resolve(args, 0x2001, "scePthreadCreate");
    }
    if (sce != NULL && sce(&th, NULL, overlay_thread, args, "NitePR5") == 0) {
        return 0;
    }
    return -1;
}

__attribute__((used, visibility("default"), no_stack_protector))
int overlay_start(overlay_args_t *args)
{
    /* Jump, not call: RET resumes the interrupted game frame. Do not hang. */
    if (args != NULL) {
        (void)spawn_overlay_thread(args);
    }
    return 0;
}
