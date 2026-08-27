#include "overlay.h"

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

int sceSysmoduleLoadModuleInternal(unsigned int id) __attribute__((weak));
int sceNetCtlInit(void) __attribute__((weak));

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

int main(void)
{
    overlay_state_t *st = overlay_state();

    memset(st, 0, sizeof *st);
    st->poke_width = 4;
    st->pid = (uint32_t)getpid();

    /* This thread is a game pthread created by the plugin, not a stolen
     * boot thread. Do not change the title's authid/caps (0.56 → XMB).
     * Do not return (CRT .fini on a live eboot → CE-108255-1).
     */
    write_alive();
    overlay_notify("NitePR5 overlay running");
    net_init();
    (void)overlay_worker_start();
    /* 0.573: do not hook GNM/VO here. 0.572 SPRX body detours during
     * inject → CE-108255-1. Worker installs eboot GOT hooks after pad poll. */

    for (;;) {
        sleep(0x4000);
    }
    return 0;
}
