/* NitePR5 etaHEN plugin daemon (Phase 4). Freeze/cheats on-console; no overlay.
 * Already jailbroken — do not call IPC 9028. Writes only via 127.0.0.1:744.
 */

#include "nitepr5.h"
#include "dbg_client.h"
#include "fs_state.h"
#include "http.h"
#include "notify.h"
#include "cheats.h"

#include <signal.h>
#include <string.h>

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

int sceSysmoduleLoadModuleInternal(unsigned int id);
int sceNetCtlInit(void);

static nitepr5_state_t g_state;

nitepr5_state_t *nitepr5_state(void)
{
    return &g_state;
}

static void net_init(void)
{
    (void)sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
    (void)sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_USER_SERVICE);
    (void)sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_NETCTL);
    (void)sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_NET);
    (void)sceNetCtlInit();
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_IGN);
#endif
}

int main(void)
{
    nitepr5_state_t *st = nitepr5_state();

    memset(st, 0, sizeof *st);
    net_init();
    (void)fs_mkdirs();
    (void)fs_state_load();

    notify_start();

    /* Do not hold 127.0.0.1:744 while idle. A second debugger client plus
     * TS_RESCAN_ALIASING on the PC scan connection hangs the next PROC_READ
     * after a few Next Scans. Connect only when armed (or freeze_tick). */
    if (st->armed) {
        if (dbg_connect() == 0) {
            st->dbg = 1;
            notify_dbg_recovered();
        } else {
            st->dbg = 0;
            notify_dbg_missing();
        }
    } else {
        st->dbg = 0;
    }

    if (st->armed) {
        notify_armed();
        (void)cheats_apply_enabled();
    }

    http_run();
    return 0;
}
