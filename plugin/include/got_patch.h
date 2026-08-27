#ifndef NITEPR5_GOT_PATCH_H
#define NITEPR5_GOT_PATCH_H

#include <stdint.h>

/* Privileged (plugin) eboot GOT/JMPREL pointer swap. Do not run this
 * inside overlay.elf. real/hook 0 is a no-op. Returns slots written. */
int got_patch_eboot(uint32_t pid, uint64_t real, uint64_t hook);
uint64_t got_resolve_sym(uint32_t pid, const char *mod, const char *sym);

#endif
