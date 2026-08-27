#ifndef NITEPR5_INJECT_H
#define NITEPR5_INJECT_H

#include <stdint.h>

/* Auto-inject overlay.elf into the foreground CUSA/PPSA eboot via Johns
 * elfldr (pt_attach + elfldr_exec). :744 is used only to list eboot pid, then
 * dropped for the attach window so ps5debug is not tracing. Call inject_poll()
 * from the HTTP loop (~67 ms). Do not sleep.
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
