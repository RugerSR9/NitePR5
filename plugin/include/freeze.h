#ifndef NITEPR5_FREEZE_H
#define NITEPR5_FREEZE_H

/* Cap 32; data 1..8 bytes. Writes via two-phase PROC_WRITE. No turbo scan. */
void freeze_tick(void);

#endif
