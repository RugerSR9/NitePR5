/* PROC_ELF e_entry. The hijacked game thread must RET immediately.
 *
 * 0.52: kernel_init/CRT/for(;;) on this thread → boot freeze.
 * 0.53: libc pthread_create at handle 0x2 failed → silent, game booted.
 * 0.54: __crt_syscall (ptr_syscall += 0xa on an already-followed gadget),
 *       handle-sweep dlsym (garbage fn), scePthreadCreate TLS, thr_new
 *       → CE-108255-1. Do none of that here.
 *
 * args+0 is sys_dynlib_dlsym. Only call it as (handle, name, &out) and
 * require return 0. Heartbeat via sceKernelOpen. No spawn from this thread
 * (TLS crash). Overlay work stays in overlay_thread for a later injector.
 */

#include <stddef.h>

typedef int (*dlsym_fn)(int handle, const char *name, void *out);
typedef int (*open_fn)(const char *path, int flags, int mode);
typedef long (*write_fn)(int fd, const void *buf, unsigned long n);
typedef int (*close_fn)(int fd);

typedef struct overlay_args {
    dlsym_fn dynlib_dlsym;
    int *rwpipe;
    int *rwpair;
    long kpipe;
    long kdata;
    int *payloadout;
} overlay_args_t;

int __crt_start(void *args);

#define OVERLAY_ALIVE_PATH     "/data/nitepr5/overlay.alive"
#define OVERLAY_ALIVE_PATH_TMP "/tmp/nitepr5.overlay.alive"
#define O_WRONLY_CREAT_TRUNC   0x601
#define ALIVE_MODE             0666

static void *resolve(overlay_args_t *args, int handle, const char *name)
{
    void *fn = 0;
    int rc;

    if (args == NULL || args->dynlib_dlsym == NULL || name == NULL) {
        return NULL;
    }
    rc = args->dynlib_dlsym(handle, name, &fn);
    if (rc != 0 || fn == NULL) {
        return NULL;
    }
    return fn;
}

static void write_alive_path(overlay_args_t *args, const char *path)
{
    open_fn op;
    write_fn wr;
    close_fn cl;
    int fd;

    op = (open_fn)resolve(args, 0x1, "sceKernelOpen");
    if (op == NULL) {
        op = (open_fn)resolve(args, 0x2001, "sceKernelOpen");
    }
    if (op == NULL) {
        op = (open_fn)resolve(args, 0x1, "open");
    }
    if (op == NULL) {
        op = (open_fn)resolve(args, 0x2001, "open");
    }
    wr = (write_fn)resolve(args, 0x1, "sceKernelWrite");
    if (wr == NULL) {
        wr = (write_fn)resolve(args, 0x2001, "sceKernelWrite");
    }
    if (wr == NULL) {
        wr = (write_fn)resolve(args, 0x1, "write");
    }
    if (wr == NULL) {
        wr = (write_fn)resolve(args, 0x2001, "write");
    }
    cl = (close_fn)resolve(args, 0x1, "sceKernelClose");
    if (cl == NULL) {
        cl = (close_fn)resolve(args, 0x2001, "sceKernelClose");
    }
    if (cl == NULL) {
        cl = (close_fn)resolve(args, 0x1, "close");
    }
    if (cl == NULL) {
        cl = (close_fn)resolve(args, 0x2001, "close");
    }
    if (op == NULL || wr == NULL || cl == NULL || path == NULL) {
        return;
    }
    fd = op(path, O_WRONLY_CREAT_TRUNC, ALIVE_MODE);
    if (fd < 0) {
        return;
    }
    (void)wr(fd, "1\n", 2);
    (void)cl(fd);
}

static void write_alive(overlay_args_t *args)
{
    write_alive_path(args, OVERLAY_ALIVE_PATH);
    write_alive_path(args, OVERLAY_ALIVE_PATH_TMP);
}

/* Kept for a later spawn from a real game pthread (pad/flip), not from PROC_ELF. */
void *overlay_thread(void *arg)
{
    overlay_args_t *args = (overlay_args_t *)arg;

    write_alive(args);
    (void)__crt_start(args);
    for (;;) {
    }
    return NULL;
}

__attribute__((used, visibility("default"), no_stack_protector))
int overlay_start(overlay_args_t *args)
{
    if (args != NULL) {
        if (args->payloadout != NULL) {
            *args->payloadout = 0x4E505235; /* 'NPR5' */
        }
        write_alive(args);
    }
    return 0;
}
