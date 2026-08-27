/* Inject overlay.elf into the running game via Johns elfldr map + remote
 * scePthreadCreate (9S/FPS path). Game detect uses SceSystemService. eboot pid
 * comes from :744 PROC_LIST, then the socket is dropped so ps5debug is not
 * tracing. Game thread RIP is not hijacked. Game R/W stays on :744 after the
 * inject window. Not PROC_ELF, not NineS, not elfldr 9021.
 */

#include "inject.h"
#include "dbg_client.h"
#include "nitepr5.h"
#include "notify.h"
#include "elfldr.h"
#include "pt.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define INJECT_LIST_CAP     4096
#define INJECT_SETTLE_MS    12000u
#define INJECT_BOOT_MS      8000u
#define INJECT_RETRY_MS     2000u
#define INJECT_POLL_MS      400u
#define INJECT_MAX_ATTEMPTS 5
#define INJECT_ALIVE_MS     5000u

int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceSystemServiceGetAppTitleId(int app_id, char *title_id);

static uint32_t g_injected_pid;
static int g_last_appid = -1;
static uint64_t g_due_ms;
static uint64_t g_last_poll_ms;
static int g_attempts;
static int g_just_started = 1;
static int g_missing_elf_told;
static int g_missing_dbg_told;
static int g_fail_told;
static uint64_t g_alive_due_ms;
static int g_alive_told;

static uint64_t now_ms(void)
{
    struct timeval tv;

    memset(&tv, 0, sizeof tv);
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ull + (uint64_t)tv.tv_usec / 1000ull;
}

static int is_game_tid(const char *tid)
{
    if (tid == NULL || tid[0] == 0) {
        return 0;
    }
    if (strncmp(tid, "NPXS", 4) == 0 || strncmp(tid, "ITEM", 4) == 0 ||
        strncmp(tid, "FAKE", 4) == 0) {
        return 0;
    }
    return strncmp(tid, "CUSA", 4) == 0 || strncmp(tid, "SCUS", 4) == 0 ||
           strncmp(tid, "PPSA", 4) == 0 || strncmp(tid, "SLUS", 4) == 0 ||
           strncmp(tid, "SLES", 4) == 0 || strncmp(tid, "SCES", 4) == 0 ||
           strncmp(tid, "ECAS", 4) == 0 || strncmp(tid, "ELAS", 4) == 0;
}

static int running_game(char tid_out[16], int *appid_out)
{
    char tid[256];
    int appid;

    if (tid_out == NULL || appid_out == NULL) {
        return 0;
    }
    appid = sceSystemServiceGetAppIdOfRunningBigApp();
    if (appid < 0) {
        return 0;
    }
    memset(tid, 0, sizeof tid);
    if (sceSystemServiceGetAppTitleId(appid, tid) != 0) {
        return 0;
    }
    tid[15] = 0;
    if (!is_game_tid(tid)) {
        return 0;
    }
    memset(tid_out, 0, 16);
    memcpy(tid_out, tid, 15);
    *appid_out = appid;
    return 1;
}

static const char *overlay_path(void)
{
    struct stat st;

    if (stat(OVERLAY_ELF_PATH, &st) == 0 && st.st_size > 16) {
        return OVERLAY_ELF_PATH;
    }
    if (stat(OVERLAY_ELF_ALT, &st) == 0 && st.st_size > 16) {
        return OVERLAY_ELF_ALT;
    }
    return NULL;
}

static int load_overlay_elf(uint8_t **out, uint32_t *n)
{
    const char *path;
    FILE *f;
    long sz;
    uint8_t *buf;

    *out = NULL;
    *n = 0;
    path = overlay_path();
    if (path == NULL) {
        return -1;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    sz = ftell(f);
    if (sz < 16 || sz > (long)DBG_ELF_MAX) {
        fclose(f);
        return -1;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    buf = (uint8_t *)malloc((size_t)sz);
    if (buf == NULL) {
        fclose(f);
        return -1;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    if (buf[0] != 0x7f || buf[1] != 'E' || buf[2] != 'L' || buf[3] != 'F') {
        free(buf);
        return -1;
    }
    *out = buf;
    *n = (uint32_t)sz;
    return 0;
}

static uint32_t find_eboot_pid(void)
{
    uint8_t *buf;
    uint32_t count = 0;
    uint32_t i;
    uint32_t found = 0;

    buf = (uint8_t *)malloc((size_t)INJECT_LIST_CAP * DBG_PROC_ENTRY_SIZE);
    if (buf == NULL) {
        return 0;
    }
    if (dbg_proc_list(buf, INJECT_LIST_CAP, &count) != 0) {
        free(buf);
        return 0;
    }
    for (i = 0; i < count; i++) {
        char name[33];
        int32_t pid = 0;

        dbg_decode_proc(buf + (size_t)i * DBG_PROC_ENTRY_SIZE, name, &pid);
        if (pid > 0 && nitepr5_ascii_ieq(nitepr5_basename(name), EBOOT_NAME)) {
            found = (uint32_t)pid;
            break;
        }
    }
    free(buf);
    return found;
}

static void dbg_release_if_idle(void)
{
    nitepr5_state_t *st = nitepr5_state();

    if (!st->armed && !st->overlay_open) {
        dbg_disconnect();
        st->dbg = 0;
    } else {
        st->dbg = dbg_connected() ? 1 : 0;
    }
}

static void dbg_restore_after_inject(int held)
{
    nitepr5_state_t *st = nitepr5_state();

    if (held || st->armed || st->overlay_open) {
        st->dbg = dbg_ensure() == 0 ? 1 : 0;
    } else {
        st->dbg = 0;
    }
}

uint32_t inject_pid(void)
{
    return g_injected_pid;
}

int inject_now(void)
{
    nitepr5_state_t *st = nitepr5_state();
    uint8_t *elf = NULL;
    uint32_t elf_n = 0;
    uint32_t pid;
    int held;
    int rc;

    if (g_injected_pid != 0 && st->overlay_open) {
        return INJECT_ALREADY;
    }

    if (load_overlay_elf(&elf, &elf_n) != 0) {
        return INJECT_MISSING_ELF;
    }

    held = dbg_connected();
    if (dbg_ensure() != 0) {
        free(elf);
        st->dbg = 0;
        return INJECT_NO_DBG;
    }
    st->dbg = 1;

    (void)unlink(OVERLAY_ALIVE_PATH);
    (void)unlink(OVERLAY_ALIVE_PATH_TMP);

    pid = find_eboot_pid();
    if (pid == 0) {
        free(elf);
        if (!held) {
            dbg_release_if_idle();
        }
        return INJECT_NO_EBOOT;
    }
    if (pid == g_injected_pid) {
        free(elf);
        if (!held) {
            dbg_release_if_idle();
        }
        return INJECT_ALREADY;
    }

    /* ps5debug must not be tracing eboot or PT_ATTACH fails. */
    dbg_disconnect();
    st->dbg = 0;

    if (pt_attach((pid_t)pid) != 0) {
        free(elf);
        dbg_restore_after_inject(held);
        return INJECT_FAIL;
    }

    rc = elfldr_inject((pid_t)pid, elf);
    free(elf);
    if (rc != 0) {
        (void)pt_detach((pid_t)pid, SIGCONT);
        dbg_restore_after_inject(held);
        return INJECT_FAIL;
    }

    g_injected_pid = pid;
    g_alive_due_ms = now_ms() + INJECT_ALIVE_MS;
    g_alive_told = 0;
    if (st->pid == 0) {
        st->pid = pid;
    }
    dbg_restore_after_inject(held);
    return INJECT_OK;
}

static void reset_title(void)
{
    g_last_appid = -1;
    g_injected_pid = 0;
    g_due_ms = 0;
    g_attempts = 0;
    g_missing_elf_told = 0;
    g_missing_dbg_told = 0;
    g_fail_told = 0;
    g_alive_due_ms = 0;
    g_alive_told = 0;
}

void inject_poll(void)
{
    char tid[16];
    int appid = 0;
    uint64_t t = now_ms();
    int rc;

    if (t - g_last_poll_ms < INJECT_POLL_MS) {
        return;
    }
    g_last_poll_ms = t;

    if (!running_game(tid, &appid)) {
        reset_title();
        g_just_started = 0;
        return;
    }

    if (appid != g_last_appid) {
        g_last_appid = appid;
        g_injected_pid = 0;
        g_attempts = 0;
        g_missing_elf_told = 0;
        g_missing_dbg_told = 0;
        g_fail_told = 0;
        g_alive_due_ms = 0;
        g_alive_told = 0;
        g_due_ms = t + (g_just_started ? INJECT_BOOT_MS : INJECT_SETTLE_MS);
        g_just_started = 0;
        return;
    }

    if (g_injected_pid != 0) {
        struct stat st;

        if (!g_alive_told && g_alive_due_ms != 0 && t >= g_alive_due_ms) {
            g_alive_told = 1;
            g_alive_due_ms = 0;
            if ((stat(OVERLAY_ALIVE_PATH, &st) == 0 && st.st_size > 0) ||
                (stat(OVERLAY_ALIVE_PATH_TMP, &st) == 0 && st.st_size > 0)) {
                notify_overlay_alive();
            } else {
                notify_overlay_silent();
            }
        }
        return;
    }
    if (g_attempts >= INJECT_MAX_ATTEMPTS) {
        return;
    }
    if (g_due_ms != 0 && t < g_due_ms) {
        return;
    }

    rc = inject_now();
    if (rc == INJECT_OK) {
        notify_overlay_injected();
        return;
    }
    if (rc == INJECT_ALREADY) {
        return;
    }

    g_attempts++;
    g_due_ms = t + INJECT_RETRY_MS;

    if (rc == INJECT_MISSING_ELF && !g_missing_elf_told) {
        g_missing_elf_told = 1;
        notify_overlay_missing_elf();
        g_attempts = INJECT_MAX_ATTEMPTS;
    } else if (rc == INJECT_NO_DBG && !g_missing_dbg_told) {
        g_missing_dbg_told = 1;
        notify_overlay_no_dbg();
    } else if (rc == INJECT_FAIL && !g_fail_told) {
        g_fail_told = 1;
        notify_overlay_fail();
    }
}
