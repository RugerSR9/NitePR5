#include "got_patch.h"

#include <ps5/kernel.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define PT_DYNAMIC     2u
#define DT_NULL        0
#define DT_PLTRELSZ    2
#define DT_RELA        7
#define DT_RELASZ      8
#define DT_JMPREL      23
#define R_X86_64_JUMP_SLOT 7u
#define R_X86_64_GLOB_DAT  6u
#define RELA_MAX       4096
#define DYN_MAX        512
#define PH_MAX         64

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

static uint64_t in_image(uint64_t base, size_t imagesz, uint64_t p)
{
    if (p >= base && p < base + imagesz) {
        return p;
    }
    if (p < (uint64_t)imagesz) {
        return base + p;
    }
    return 0;
}

static size_t image_size_from_phdrs(const elf64_phdr_t *ph, uint16_t n)
{
    size_t i, max = 0;

    for (i = 0; i < n; i++) {
        uint64_t end = ph[i].p_vaddr + ph[i].p_memsz;
        if (end > max) {
            max = (size_t)end;
        }
    }
    if (max == 0 || max > 0x20000000ull) {
        return 0;
    }
    return max;
}

static int patch_relas(pid_t pid, uint64_t base, size_t imagesz, uint64_t rela_va,
                       size_t n, uint64_t real, uint64_t hook)
{
    elf64_rela_t relas[RELA_MAX];
    size_t i;
    int patched = 0;

    if (rela_va == 0 || n == 0 || n > RELA_MAX) {
        return 0;
    }
    if (rd(pid, rela_va, relas, n * sizeof(elf64_rela_t)) != 0) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        unsigned type = (unsigned)(relas[i].r_info & 0xffffffffu);
        uint64_t slot_va;
        uint64_t cur = 0;

        if (type != R_X86_64_JUMP_SLOT && type != R_X86_64_GLOB_DAT) {
            continue;
        }
        slot_va = in_image(base, imagesz, relas[i].r_offset);
        if (slot_va == 0 || (slot_va & 7ull) != 0) {
            continue;
        }
        if (rd(pid, slot_va, &cur, sizeof cur) != 0) {
            continue;
        }
        if (cur != real || cur == hook) {
            continue;
        }
        if (kernel_proc_setlong(pid, (intptr_t)slot_va, (long)hook) == 0) {
            patched++;
        }
    }
    return patched;
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

int got_patch_eboot(uint32_t pid, uint64_t real, uint64_t hook)
{
    pid_t p = (pid_t)pid;
    uint32_t handle = 0;
    intptr_t base_i;
    uint64_t base;
    elf64_ehdr_t eh;
    elf64_phdr_t ph[PH_MAX];
    elf64_dyn_t dyn[DYN_MAX];
    size_t imagesz, i, dyn_n = 0, rela_n = 0, jmp_n = 0;
    uint64_t dyn_va = 0, rela_va = 0, jmp_va = 0, pltrelsz = 0;
    int n;

    if (pid == 0 || real == 0 || hook == 0 || real == hook) {
        return 0;
    }
    if (kernel_dynlib_handle(p, "eboot.bin", &handle) != 0) {
        handle = 0;
    }
    base_i = kernel_dynlib_mapbase_addr(p, handle);
    if (base_i == 0 && handle != 0) {
        base_i = kernel_dynlib_mapbase_addr(p, 0);
    }
    if (base_i == 0) {
        return -1;
    }
    base = (uint64_t)base_i;
    if (rd(p, base, &eh, sizeof eh) != 0) {
        return -1;
    }
    if (eh.e_ident[0] != 0x7f || eh.e_ident[1] != 'E' || eh.e_ident[2] != 'L' ||
        eh.e_ident[3] != 'F') {
        return -1;
    }
    if (eh.e_phentsize != sizeof(elf64_phdr_t) || eh.e_phnum == 0 || eh.e_phnum > PH_MAX) {
        return -1;
    }
    if (rd(p, base + eh.e_phoff, ph, (size_t)eh.e_phnum * sizeof(elf64_phdr_t)) != 0) {
        return -1;
    }
    imagesz = image_size_from_phdrs(ph, eh.e_phnum);
    if (imagesz == 0) {
        return -1;
    }
    for (i = 0; i < eh.e_phnum; i++) {
        if (ph[i].p_type == PT_DYNAMIC && ph[i].p_memsz >= sizeof(elf64_dyn_t)) {
            dyn_va = in_image(base, imagesz, ph[i].p_vaddr);
            dyn_n = (size_t)(ph[i].p_memsz / sizeof(elf64_dyn_t));
            break;
        }
    }
    if (dyn_va == 0 || dyn_n == 0 || dyn_n > DYN_MAX) {
        return -1;
    }
    if (rd(p, dyn_va, dyn, dyn_n * sizeof(elf64_dyn_t)) != 0) {
        return -1;
    }
    for (i = 0; i < dyn_n && dyn[i].d_tag != DT_NULL; i++) {
        if (dyn[i].d_tag == DT_RELA) {
            rela_va = in_image(base, imagesz, dyn[i].d_un);
        } else if (dyn[i].d_tag == DT_RELASZ) {
            rela_n = (size_t)(dyn[i].d_un / sizeof(elf64_rela_t));
        } else if (dyn[i].d_tag == DT_JMPREL) {
            jmp_va = in_image(base, imagesz, dyn[i].d_un);
        } else if (dyn[i].d_tag == DT_PLTRELSZ) {
            pltrelsz = dyn[i].d_un;
        }
    }
    jmp_n = (size_t)(pltrelsz / sizeof(elf64_rela_t));
    n = patch_relas(p, base, imagesz, jmp_va, jmp_n, real, hook);
    n += patch_relas(p, base, imagesz, rela_va, rela_n, real, hook);
    return n;
}
