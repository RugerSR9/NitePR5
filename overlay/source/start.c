/* PROC_ELF e_entry. Hijacked game thread must RET (0.52 parked it → freeze).
 * args+0 is sys_dynlib_dlsym (syscall), not libc pthread_create. 0.53 spawn
 * failed so the worker never ran (silent, game booted). Init syscalls, then
 * scePthreadCreate / thr_new. Do not import libc (PLT unbound).
 */

#include <stddef.h>

typedef int (*dlsym_fn)(int handle, const char *name, void *out);
typedef int (*notify_fn)(int device, void *req, unsigned long size, int unk);
typedef int (*getpid_fn)(void);
typedef int (*pthread_create_fn)(void *thread, void *attr, void *(*entry)(void *), void *arg);
typedef int (*sce_pthread_create_fn)(void *thread, void *attr, void *(*entry)(void *), void *arg,
                                     const char *name);
typedef int (*sce_attr_init_fn)(void *attr);
typedef int (*sce_attr_stack_fn)(void *attr, unsigned long size);

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

typedef struct thr_param {
    void (*start_func)(void *);
    void *arg;
    char *stack_base;
    unsigned long stack_size;
    char *tls_base;
    unsigned long tls_size;
    long *child_tid;
    long *parent_tid;
    int flags;
    void *rtp;
} thr_param_t;

int __crt_syscall_init(void *args) __attribute__((weak));
long __crt_syscall(long sysno, ...) __attribute__((weak));
int __kernel_init(void *args) __attribute__((weak));
int __crt_start(void *args);
int kernel_set_ucred_authid(int pid, unsigned long authid) __attribute__((weak));
int kernel_set_ucred_caps(int pid, unsigned char caps[16]) __attribute__((weak));

#define OVERLAY_ALIVE_PATH     "/data/nitepr5/overlay.alive"
#define OVERLAY_ALIVE_PATH_TMP "/tmp/nitepr5.overlay.alive"
#define O_WRONLY_CREAT_TRUNC   0x601
#define ALIVE_MODE             0666
#define SYS_write              4
#define SYS_open               5
#define SYS_close              6
#define SYS_mmap               477
#define SYS_thr_new            455
#define SYS_sprx_dlsym         591
#define MAP_ANON_PRIVATE       0x1002
#define PROT_READ_WRITE        3
#define STACK_SIZE             (256u * 1024u)

static const int k_handles[] = {
    0x1, 0x2001, 0x2, 0x2002, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0
};

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

    if (name == NULL) {
        return NULL;
    }
    if (args != NULL && args->dynlib_dlsym != NULL) {
        (void)args->dynlib_dlsym(handle, name, &fn);
        if (fn) {
            return fn;
        }
    }
    if (__crt_syscall) {
        fn = 0;
        (void)__crt_syscall(SYS_sprx_dlsym, handle, name, &fn);
    }
    return fn;
}

static void *resolve_any(overlay_args_t *args, const char *name)
{
    int i;
    void *fn;

    for (i = 0; k_handles[i]; i++) {
        fn = resolve(args, k_handles[i], name);
        if (fn) {
            return fn;
        }
    }
    return NULL;
}

static void raw_notify(overlay_args_t *args, const char *msg)
{
    notify_fn fn;
    notify_request_t req;

    fn = (notify_fn)resolve_any(args, "sceKernelSendNotificationRequest");
    if (fn == NULL) {
        return;
    }
    zero_bytes(&req, sizeof req);
    copy_cstr(req.message, sizeof req.message, msg);
    (void)fn(0, &req, sizeof req, 0);
}

static void write_alive_path(const char *path)
{
    long fd;

    if (__crt_syscall == NULL || path == NULL) {
        return;
    }
    fd = __crt_syscall(SYS_open, path, O_WRONLY_CREAT_TRUNC, ALIVE_MODE);
    if (fd < 0) {
        return;
    }
    (void)__crt_syscall(SYS_write, fd, "1\n", 2);
    (void)__crt_syscall(SYS_close, fd);
}

static void write_alive(overlay_args_t *args)
{
    (void)args;
    write_alive_path(OVERLAY_ALIVE_PATH);
    write_alive_path(OVERLAY_ALIVE_PATH_TMP);
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
    if (__crt_syscall) {
        pid = (int)__crt_syscall(20);
    }
    if (pid <= 0) {
        gp = (getpid_fn)resolve_any(args, "getpid");
        if (gp) {
            pid = gp();
        }
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

static void overlay_thr_entry(void *arg)
{
    (void)overlay_thread(arg);
}

static int spawn_pthread(overlay_args_t *args)
{
    sce_pthread_create_fn sce;
    pthread_create_fn pc;
    sce_attr_init_fn attr_init;
    sce_attr_stack_fn attr_stack;
    unsigned char attr[256];
    void *th = 0;
    void *attrp = NULL;

    sce = (sce_pthread_create_fn)resolve_any(args, "scePthreadCreate");
    attr_init = (sce_attr_init_fn)resolve_any(args, "scePthreadAttrInit");
    attr_stack = (sce_attr_stack_fn)resolve_any(args, "scePthreadAttrSetstacksize");
    if (sce != NULL) {
        zero_bytes(attr, sizeof attr);
        if (attr_init && attr_init(attr) == 0) {
            if (attr_stack) {
                (void)attr_stack(attr, STACK_SIZE);
            }
            attrp = attr;
        }
        if (sce(&th, attrp, overlay_thread, args, "NitePR5ov") == 0) {
            return 0;
        }
        if (attrp != NULL && sce(&th, NULL, overlay_thread, args, "NitePR5ov") == 0) {
            return 0;
        }
    }
    pc = (pthread_create_fn)resolve_any(args, "pthread_create");
    if (pc != NULL && pc(&th, NULL, overlay_thread, args) == 0) {
        return 0;
    }
    return -1;
}

static int spawn_thr_new(overlay_args_t *args)
{
    thr_param_t p;
    long stack;
    long child = 0;
    long parent = 0;

    if (__crt_syscall == NULL) {
        return -1;
    }
    stack = __crt_syscall(SYS_mmap, 0, (long)STACK_SIZE, PROT_READ_WRITE, MAP_ANON_PRIVATE, -1, 0);
    if (stack <= 0) {
        return -1;
    }
    zero_bytes(&p, sizeof p);
    p.start_func = overlay_thr_entry;
    p.arg = args;
    p.stack_base = (char *)(unsigned long)stack;
    p.stack_size = STACK_SIZE;
    p.child_tid = &child;
    p.parent_tid = &parent;
    p.flags = 0;
    if (__crt_syscall(SYS_thr_new, &p, (long)sizeof p) == 0) {
        return 0;
    }
    return -1;
}

__attribute__((used, visibility("default"), no_stack_protector))
int overlay_start(overlay_args_t *args)
{
    /* Jump, not call: RET resumes the interrupted game frame. Do not hang. */
    if (args != NULL) {
        if (__crt_syscall_init) {
            (void)__crt_syscall_init(args);
        }
        write_alive(args);
        if (spawn_pthread(args) != 0) {
            (void)spawn_thr_new(args);
        }
    }
    return 0;
}
