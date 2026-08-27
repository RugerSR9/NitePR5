#include "overlay.h"

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#ifndef SCE_SYSMODULE_INTERNAL_SYSTEM_SERVICE
#define SCE_SYSMODULE_INTERNAL_SYSTEM_SERVICE 0x80000010u
#endif
#ifndef SCE_SYSMODULE_INTERNAL_USER_SERVICE
#define SCE_SYSMODULE_INTERNAL_USER_SERVICE 0x80000011u
#endif
#ifndef SCE_SYSMODULE_INTERNAL_NETCTL
#define SCE_SYSMODULE_INTERNAL_NETCTL 0x80000018u
#endif
#ifndef SCE_SYSMODULE_INTERNAL_NET
#define SCE_SYSMODULE_INTERNAL_NET 0x8000001Cu
#endif

#ifndef PROT_READ
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4
#endif

int sceSysmoduleLoadModuleInternal(unsigned int id) __attribute__((weak));
int sceNetCtlInit(void) __attribute__((weak));
int sceKernelMprotect(void *addr, size_t len, int prot) __attribute__((weak));
int kernel_set_ucred_authid(int pid, unsigned long authid) __attribute__((weak));
int kernel_set_ucred_caps(int pid, unsigned char caps[16]) __attribute__((weak));
int kernel_mprotect(int pid, unsigned long addr, unsigned long len, int prot) __attribute__((weak));

static void write_alive(void)
{
    FILE *f;

    f = fopen("/data/nitepr5/overlay.alive", "w");
    if (f != NULL) {
        fputs("1\n", f);
        fclose(f);
    }
    f = fopen("/tmp/nitepr5.overlay.alive", "w");
    if (f != NULL) {
        fputs("1\n", f);
        fclose(f);
    }
}

static void widen_for_sockets(void)
{
    unsigned char caps[16];
    int pid = (int)getpid();

    /* Localhost TCP to NTPR50001 :1745 after inject. Not for game RAM R/W. */
    memset(caps, 0xff, sizeof caps);
    if (kernel_set_ucred_authid) {
        (void)kernel_set_ucred_authid(pid, 0x4800000000000007ul);
    }
    if (kernel_set_ucred_caps) {
        (void)kernel_set_ucred_caps(pid, caps);
    }
}

static void net_init(void)
{
    if (sceSysmoduleLoadModuleInternal) {
        (void)sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
        (void)sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_USER_SERVICE);
        (void)sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_NETCTL);
        (void)sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_NET);
    }
    if (sceNetCtlInit) {
        (void)sceNetCtlInit();
    }
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif
}

static void mprotect_ready(void)
{
    char buf[64];
    int i;

    memset(buf, 0, sizeof buf);
    for (i = 0; i < 2; i++) {
        if (sceKernelMprotect &&
            sceKernelMprotect(buf, sizeof buf, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
            return;
        }
        if (kernel_mprotect &&
            kernel_mprotect((int)getpid(), (unsigned long)(uintptr_t)buf, sizeof buf,
                            PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
            return;
        }
        sleep(1);
    }
}

/* Real overlay work. Must not run on the hijacked game thread (that thread
 * has to RET to the RIP elfldr stacked). Heartbeat here so "entry is up"
 * means this pthread actually started.
 */
static void *overlay_boot(void *arg)
{
    overlay_state_t *st = overlay_state();

    (void)arg;
    memset(st, 0, sizeof *st);
    st->poke_width = 4;
    st->pid = (uint32_t)getpid();

    widen_for_sockets();
    write_alive();
    overlay_notify("NitePR5 overlay running");
    net_init();
    (void)overlay_worker_start();
    mprotect_ready();

    if (overlay_hooks_install() != 0) {
        overlay_lock();
        overlay_set_error("hook failed (flip/pad)");
        overlay_unlock();
        overlay_notify("NitePR5 hook failed — pad poll still on");
    }

    for (;;) {
        sleep(0x4000);
    }
    return NULL;
}

int main(void)
{
    pthread_t th;

    /* elfldr_exec stacked the original RIP. Return so CRT payload_terminate
     * RETs into the game. Do not sleep here (0.52 boot freeze).
     */
    if (pthread_create(&th, NULL, overlay_boot, NULL) != 0) {
        return 1;
    }
    (void)pthread_detach(th);
    return 0;
}
