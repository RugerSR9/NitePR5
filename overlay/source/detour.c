#include "overlay.h"

#include <stdint.h>

/* 0.572/0.573 CE-108255-1: in-game SPRX jmp and in-game kernel GOT patch
 * both run during launch (pad poll succeeds while the title is still
 * booting). Overlay must not mprotect GNM, parse eboot, or call Johns
 * kernel_* . Plugin (jailbroken) patches GOT on /overlay/open. */
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
