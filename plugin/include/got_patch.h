#ifndef NITEPR5_GOT_PATCH_H
#define NITEPR5_GOT_PATCH_H

#include <stdint.h>

/* Privileged (plugin) patches. Do not run these inside overlay.elf.
 *
 * got_patch_nid: etaHEN-style JMPREL match (NID in dynsym, slot =
 * mapbase+r_offset). Walks every dynlib, not just eboot.bin. Does not
 * require the live GOT pointer to equal dlsym(real).
 *
 * got_sprx_detour: dead. Combo SPRX trampoline CE-108255-1 (s=1). */
int got_patch_nid(uint32_t pid, const char *sym, uint64_t hook);
int got_sprx_detour(uint32_t pid, uint64_t real, uint64_t hook, uint64_t tramp);
uint64_t got_resolve_sym(uint32_t pid, const char *mod, const char *sym);

#endif
