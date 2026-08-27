#ifndef NITEPR5_DETOUR_H
#define NITEPR5_DETOUR_H

/* In-process SPRX body hook (not eboot PLT). Original trampoline; not etaHEN Detour.cpp. */
int detour_install(void *target, void *hook, void **orig_out);

#endif
