#include "got_patch.h"

#include <ps5/kernel.h>
#include <ps5/nid.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define PT_LOAD        1u
#define PT_DYNAMIC     2u
#define DT_NULL        0
#define DT_PLTRELSZ    2
#define DT_STRTAB      5
#define DT_SYMTAB      6
#define DT_RELA        7
#define DT_RELASZ      8
#define DT_STRSZ       10
#define DT_SYMENT      11
#define DT_JMPREL      23
#define R_X86_64_JUMP_SLOT 7u
#define R_X86_64_GLOB_DAT  6u
#define RELA_CHUNK     256
#define DYN_MAX        512
#define PH_MAX         64
#define HANDLE_MAX     1024u
#define HANDLE_MISS    48

typedef struct {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf64_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf64_phdr_t;

typedef struct {
    uint64_t d_tag;
    uint64_t d_un;
} elf64_dyn_t;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
} elf64_rela_t;

typedef struct {
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} elf64_sym_t;

typedef struct {
    const char *nid;
    const char *cname;
    uint64_t hook;
} nid_ctx_t;

static int rd(pid_t pid, uint64_t addr, void *buf, size_t n)
{
    if (addr == 0 || buf == NULL || n == 0) {
        return -1;
    }
    if (kernel_proc_copyout(pid, (intptr_t)addr, buf, n) < 0) {
        return -1;
    }
    return 0;
}

static int readable(pid_t pid, uint64_t addr, size_t n)
{
    uint8_t b[8];

    if (addr == 0) {
        return 0;
    }
    if (n > sizeof b) {
        n = sizeof b;
    }
    return rd(pid, addr, b, n) == 0;
}

static uint64_t min_vaddr(const elf64_phdr_t *ph, uint16_t n)
{
    uint64_t min = UINT64_MAX;
    size_t i;

    for (i = 0; i < n; i++) {
        if (ph[i].p_type == PT_LOAD && ph[i].p_memsz != 0 && ph[i].p_vaddr < min) {
            min = ph[i].p_vaddr;
        }
    }
    return min == UINT64_MAX ? 0 : min;
}

/* etaHEN: imagebase + r_offset. Also try slide by min p_vaddr and already-relocated VA. */
static uint64_t va_pick(pid_t pid, uint64_t base, uint64_t minv, uint64_t p, size_t probe)
{
    uint64_t c[4];
    int i, n = 0;

    if (p == 0 || base == 0) {
        return 0;
    }
    c[n++] = base + p;
    if (p >= minv) {
        c[n++] = base + (p - minv);
    }
    c[n++] = p;
    for (i = 0; i < n; i++) {
        if (readable(pid, c[i], probe)) {
            return c[i];
        }
    }
    return 0;
}

static int name_matches(const char *name, const char *nid, const char *cname)
{
    if (name == NULL || name[0] == 0) {
        return 0;
    }
    if (nid && nid[0] && strncmp(name, nid, 11) == 0) {
        return 1;
    }
    if (cname && cname[0] && strcmp(name, cname) == 0) {
        return 1;
    }
    return 0;
}

static int patch_relas_nid(pid_t pid, uint64_t base, uint64_t minv, uint64_t rela_va,
                           size_t nrel, uint64_t symtab, uint64_t strtab, uint64_t strsz,
                           uint64_t syment, const char *nid, const char *cname, uint64_t hook)
{
    elf64_rela_t relas[RELA_CHUNK];
    size_t off;
    int patched = 0;

    if (rela_va == 0 || nrel == 0 || symtab == 0 || strtab == 0 || hook == 0) {
        return 0;
    }
    if (syment == 0) {
        syment = sizeof(elf64_sym_t);
    }
    for (off = 0; off < nrel; off += RELA_CHUNK) {
        size_t chunk = nrel - off;
        size_t i;

        if (chunk > RELA_CHUNK) {
            chunk = RELA_CHUNK;
        }
        if (rd(pid, rela_va + off * sizeof(elf64_rela_t), relas, chunk * sizeof(elf64_rela_t)) !=
            0) {
            continue;
        }
        for (i = 0; i < chunk; i++) {
            unsigned type = (unsigned)(relas[i].r_info & 0xffffffffu);
            uint32_t symidx = (uint32_t)(relas[i].r_info >> 32);
            elf64_sym_t sym;
            char name[16];
            uint64_t slot_va;
            uint64_t cur = 0;

            if (type != R_X86_64_JUMP_SLOT && type != R_X86_64_GLOB_DAT) {
                continue;
            }
            if (symidx == 0) {
                continue;
            }
            if (rd(pid, symtab + (uint64_t)symidx * syment, &sym, sizeof sym) != 0) {
                continue;
            }
            if (strsz != 0 && (uint64_t)sym.st_name >= strsz) {
                continue;
            }
            memset(name, 0, sizeof name);
            if (rd(pid, strtab + sym.st_name, name, 12) != 0) {
                continue;
            }
            name[11] = 0;
            if (!name_matches(name, nid, cname)) {
                continue;
            }
            slot_va = va_pick(pid, base, minv, relas[i].r_offset, 8);
            if (slot_va == 0 || (slot_va & 7ull) != 0) {
                continue;
            }
            if (rd(pid, slot_va, &cur, sizeof cur) != 0) {
                continue;
            }
            if (cur == hook) {
                patched++;
                continue;
            }
            if (kernel_proc_setlong(pid, (intptr_t)slot_va, (long)hook) == 0) {
                patched++;
            }
        }
    }
    return patched;
}

static int patch_handle_nid(pid_t pid, uint32_t handle, const char *nid, const char *cname,
                            uint64_t hook)
{
    intptr_t base_i;
    uint64_t base;
    uint64_t minv;
    elf64_ehdr_t eh;
    elf64_phdr_t ph[PH_MAX];
    elf64_dyn_t dyn[DYN_MAX];
    size_t i, dyn_n = 0, rela_n = 0, jmp_n = 0;
    uint64_t dyn_va = 0, rela_va = 0, jmp_va = 0, pltrelsz = 0;
    uint64_t strtab = 0, symtab = 0, strsz = 0, syment = sizeof(elf64_sym_t);
    int n;

    base_i = kernel_dynlib_mapbase_addr(pid, handle);
    if (base_i == 0) {
        return 0;
    }
    base = (uint64_t)base_i;
    if (rd(pid, base, &eh, sizeof eh) != 0) {
        return 0;
    }
    if (eh.e_ident[0] != 0x7f || eh.e_ident[1] != 'E' || eh.e_ident[2] != 'L' ||
        eh.e_ident[3] != 'F') {
        return 0;
    }
    if (eh.e_phentsize != sizeof(elf64_phdr_t) || eh.e_phnum == 0 || eh.e_phnum > PH_MAX) {
        return 0;
    }
    if (rd(pid, base + eh.e_phoff, ph, (size_t)eh.e_phnum * sizeof(elf64_phdr_t)) != 0) {
        return 0;
    }
    minv = min_vaddr(ph, eh.e_phnum);
    for (i = 0; i < eh.e_phnum; i++) {
        if (ph[i].p_type == PT_DYNAMIC && ph[i].p_memsz >= sizeof(elf64_dyn_t)) {
            dyn_va = va_pick(pid, base, minv, ph[i].p_vaddr, sizeof(elf64_dyn_t));
            dyn_n = (size_t)(ph[i].p_memsz / sizeof(elf64_dyn_t));
            break;
        }
    }
    if (dyn_va == 0 || dyn_n == 0 || dyn_n > DYN_MAX) {
        return 0;
    }
    if (rd(pid, dyn_va, dyn, dyn_n * sizeof(elf64_dyn_t)) != 0) {
        return 0;
    }
    for (i = 0; i < dyn_n && dyn[i].d_tag != DT_NULL; i++) {
        uint64_t u = dyn[i].d_un;

        if (dyn[i].d_tag == DT_RELA) {
            rela_va = va_pick(pid, base, minv, u, sizeof(elf64_rela_t));
        } else if (dyn[i].d_tag == DT_RELASZ) {
            rela_n = (size_t)(u / sizeof(elf64_rela_t));
        } else if (dyn[i].d_tag == DT_JMPREL) {
            jmp_va = va_pick(pid, base, minv, u, sizeof(elf64_rela_t));
        } else if (dyn[i].d_tag == DT_PLTRELSZ) {
            pltrelsz = u;
        } else if (dyn[i].d_tag == DT_STRTAB) {
            strtab = va_pick(pid, base, minv, u, 1);
        } else if (dyn[i].d_tag == DT_SYMTAB) {
            symtab = va_pick(pid, base, minv, u, sizeof(elf64_sym_t));
        } else if (dyn[i].d_tag == DT_STRSZ) {
            strsz = u;
        } else if (dyn[i].d_tag == DT_SYMENT && u != 0) {
            syment = u;
        }
    }
    jmp_n = (size_t)(pltrelsz / sizeof(elf64_rela_t));
    n = patch_relas_nid(pid, base, minv, jmp_va, jmp_n, symtab, strtab, strsz, syment, nid, cname,
                        hook);
    n += patch_relas_nid(pid, base, minv, rela_va, rela_n, symtab, strtab, strsz, syment, nid, cname,
                         hook);
    return n;
}

static int patch_handle_cb(pid_t pid, uint32_t handle, void *arg)
{
    nid_ctx_t *ctx = (nid_ctx_t *)arg;

    return patch_handle_nid(pid, handle, ctx->nid, ctx->cname, ctx->hook);
}

uint64_t got_resolve_sym(uint32_t pid, const char *mod, const char *sym)
{
    pid_t p = (pid_t)pid;
    uint32_t handle = 0;
    intptr_t addr;

    if (pid == 0 || mod == NULL || sym == NULL) {
        return 0;
    }
    if (kernel_dynlib_handle(p, mod, &handle) != 0) {
        return 0;
    }
    addr = kernel_dynlib_dlsym(p, handle, sym);
    return addr > 0 ? (uint64_t)addr : 0;
}

int got_patch_nid(uint32_t pid, const char *sym, uint64_t hook)
{
    pid_t p = (pid_t)pid;
    char nid[12];
    nid_ctx_t ctx;
    uint32_t eboot_h = 0;
    uint32_t h;
    int miss = 0;
    int started = 0;
    int n = 0;

    if (pid == 0 || sym == NULL || hook == 0) {
        return 0;
    }
    memset(nid, 0, sizeof nid);
    if (nid_encode(sym, nid) == NULL || nid[0] == 0) {
        return 0;
    }
    ctx.nid = nid;
    ctx.cname = sym;
    ctx.hook = hook;
    if (kernel_dynlib_handle(p, "eboot.bin", &eboot_h) == 0) {
        n += patch_handle_cb(p, eboot_h, &ctx);
    }
    for (h = 0; h < HANDLE_MAX; h++) {
        intptr_t base_i = kernel_dynlib_mapbase_addr(p, h);

        if (base_i == 0) {
            if (started && ++miss > HANDLE_MISS) {
                break;
            }
            continue;
        }
        started = 1;
        miss = 0;
        if (h == eboot_h && n > 0) {
            continue;
        }
        n += patch_handle_cb(p, h, &ctx);
    }
    return n;
}

/* Plugin combo SPRX trampoline CE-108255-1 (s=1 hardware 2026-08-27).
 * Re-entry stole the jmp and jumped into the middle of GNM. Dead. */
int got_sprx_detour(uint32_t pid, uint64_t real, uint64_t hook, uint64_t tramp)
{
    (void)pid;
    (void)real;
    (void)hook;
    (void)tramp;
    return -1;
}
