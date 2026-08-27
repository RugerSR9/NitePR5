#ifndef NITEPR5_INJECT_H
#define NITEPR5_INJECT_H

#include <stdint.h>

/* Auto-inject overlay.elf into the foreground CUSA/PPSA eboot via PROC_ELF on
 * 127.0.0.1:744 (ps5debug-NG). Call inject_poll() from the HTTP loop (~67 ms).
 * Do not sleep.
 */

#define INJECT_OK          0
#define INJECT_ALREADY     1
#define INJECT_MISSING_ELF -1
#define INJECT_NO_DBG      -2
#define INJECT_NO_EBOOT    -3
#define INJECT_FAIL        -4

void inject_poll(void);
int inject_now(void);
uint32_t inject_pid(void);

#endif
