#ifndef NITEPR5_DETOUR_H
#define NITEPR5_DETOUR_H

/* Overlay never installs GNM/VO hooks. Stubs stay so the symbols link. */
int detour_install(void *target, void *hook, void **orig_out);
int got_hook_install(void *real, void *hook, void **orig_out);

#endif
