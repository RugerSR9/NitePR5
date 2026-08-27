#include "freeze.h"
#include "dbg_client.h"
#include "nitepr5.h"
#include "notify.h"

void freeze_tick(void)
{
    nitepr5_state_t *st = nitepr5_state();
    int i;
    int was;

    if (!st->armed) {
        return;
    }

    was = dbg_connected();
    if (dbg_ensure() != 0) {
        st->dbg = 0;
        notify_dbg_missing();
        return;
    }
    if (!was) {
        st->dbg = 1;
        notify_dbg_recovered();
    }

    if (st->freeze_count <= 0) {
        if (dbg_nop() != 0) {
            st->dbg = 0;
            notify_dbg_missing();
        }
        return;
    }

    for (i = 0; i < st->freeze_count; i++) {
        freeze_entry_t *fr = &st->freezes[i];
        if (dbg_proc_write(st->pid, fr->addr, fr->data, fr->n) != 0) {
            st->dbg = 0;
            notify_dbg_missing();
            return;
        }
    }
    st->dbg = 1;
}
