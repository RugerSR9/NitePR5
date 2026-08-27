#ifndef NITEPR5_DETOUR_H
#define NITEPR5_DETOUR_H

/* 0.572 inline-patched GNM/VideoOut SPRX text (mprotect RWX + 14-byte jmp)
 * during boot → CE-108255-1. Do not revive SPRX body hooks.
 * 0.573: swap eboot GOT/JMPREL slots (8-byte pointer). orig is the SPRX
 * export; no trampoline. */
int detour_install(void *target, void *hook, void **orig_out);
int got_hook_install(void *real, void *hook, void **orig_out);

#endif
