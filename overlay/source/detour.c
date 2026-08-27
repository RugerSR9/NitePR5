#include "overlay.h"

#include <stdint.h>

/* Overlay must not mprotect GNM or call Johns kernel_* (0.572 SPRX jmp at
 * inject CE-108255-1). Plugin (jailbroken) patches GOT/SPRX on /overlay/open. */
int detour_install(void *target, void *hook, void **orig_out)
{
    (void)target;
    (void)hook;
    (void)orig_out;
    return -1;
}

int got_hook_install(void *real, void *hook, void **orig_out)
{
    (void)real;
    (void)hook;
    (void)orig_out;
    return -1;
}
