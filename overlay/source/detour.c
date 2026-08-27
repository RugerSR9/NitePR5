#include "overlay.h"

#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <ps5/kernel.h>

#define PT_DYNAMIC     2u
#define DT_NULL        0
#define DT_PLTRELSZ    2
#define DT_RELA        7
#define DT_RELASZ      8
#define DT_JMPREL      23
#define R_X86_64_JUMP_SLOT 7u
#define R_X86_64_GLOB_DAT  6u
#define RELA_MAX       16384
#define EI_MAG0        0
#define ELFMAG0        0x7f
#define ELFMAG1        'E'
#define ELFMAG2        'L'
#define ELFMAG3        'F'

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

/* 0.572 CE-108255-1: mprotect+jmp on GNM/VO SPRX while the game is flipping.
 * elfldr_inject already restored game caps; this thread must not RWX driver
 * code. Keep the symbol so old call sites compile; always fail. */
int detour_install(void *target, void *hook, void **orig_out)
{
    (void)target;
    (void)hook;
    (void)orig_out;
    return -1;
}

static int is_self(const uint8_t *base, size_t imagesz)
{
    uintptr_t me = (uintptr_t)(void *)&got_hook_install;

    if (base == NULL || imagesz == 0) {
        return 1;
    }
    return me >= (uintptr_t)base && me < (uintptr_t)base + imagesz;
}

static const uint8_t *in_image(const uint8_t *base, size_t imagesz, uint64_t p)
{
    uintptr_t b = (uintptr_t)base;

    if (p >= b && p < b + imagesz) {
        return (const uint8_t *)(uintptr_t)p;
    }
    if (p < imagesz) {
        return base + (size_t)p;
    }
    return NULL;
}

static size_t elf_image_size(const uint8_t *base)
{
    const elf64_ehdr_t *eh;
    const elf64_phdr_t *ph;
    size_t i, max = 0;

    eh = (const elf64_ehdr_t *)base;
    if (eh->e_phentsize != sizeof(elf64_phdr_t) || eh->e_phnum == 0 || eh->e_phnum > 128) {
        return 0;
    }
    if (eh->e_phoff > 0x1000000ull) {
        return 0;
    }
    ph = (const elf64_phdr_t *)(base + eh->e_phoff);
    for (i = 0; i < eh->e_phnum; i++) {
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

static int write_got_slot(void **slot, void *val)
{
    pid_t pid = getpid();

    if (slot == NULL) {
        return -1;
    }
    if (kernel_proc_setlong(pid, (intptr_t)slot, (long)(uintptr_t)val) == 0) {
        return 0;
    }
    return -1;
}

static int patch_rela_table(const uint8_t *base, size_t imagesz, const elf64_rela_t *rela,
                            size_t n, void *real, void *hook)
{
    size_t i;
    int patched = 0;

    if (rela == NULL || n == 0 || n > RELA_MAX) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        unsigned type = (unsigned)(rela[i].r_info & 0xffffffffu);
        const uint8_t *slotp;
        void **slot;
        void *cur;

        if (type != R_X86_64_JUMP_SLOT && type != R_X86_64_GLOB_DAT) {
            continue;
        }
        slotp = in_image(base, imagesz, rela[i].r_offset);
        if (slotp == NULL || ((uintptr_t)slotp & 7u) != 0) {
            continue;
        }
        slot = (void **)slotp;
        cur = *slot;
        if (cur != real || cur == hook) {
            continue;
        }
        if (write_got_slot(slot, hook) == 0) {
            patched++;
        }
    }
    return patched;
}

static int patch_image_got(const uint8_t *base, void *real, void *hook)
{
    const elf64_ehdr_t *eh;
    const elf64_phdr_t *ph;
    const elf64_dyn_t *dyn = NULL;
    const elf64_rela_t *rela = NULL;
    const elf64_rela_t *jmprel = NULL;
    size_t imagesz, i, rela_n = 0, jmp_n = 0, dyn_n = 0;
    uint64_t pltrelsz = 0;
    int n;

    if (base == NULL || real == NULL || hook == NULL) {
        return 0;
    }
    eh = (const elf64_ehdr_t *)base;
    if (eh->e_ident[EI_MAG0] != ELFMAG0 || eh->e_ident[1] != ELFMAG1 ||
        eh->e_ident[2] != ELFMAG2 || eh->e_ident[3] != ELFMAG3) {
        return 0;
    }
    imagesz = elf_image_size(base);
    if (imagesz == 0 || is_self(base, imagesz)) {
        return 0;
    }
    if (eh->e_phentsize != sizeof(elf64_phdr_t) || eh->e_phnum == 0 || eh->e_phnum > 128) {
        return 0;
    }
    ph = (const elf64_phdr_t *)(base + eh->e_phoff);
    for (i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type == PT_DYNAMIC && ph[i].p_memsz >= sizeof(elf64_dyn_t)) {
            dyn = (const elf64_dyn_t *)in_image(base, imagesz, ph[i].p_vaddr);
            dyn_n = (size_t)(ph[i].p_memsz / sizeof(elf64_dyn_t));
            break;
        }
    }
    if (dyn == NULL || dyn_n == 0 || dyn_n > 4096) {
        return 0;
    }
    for (i = 0; i < dyn_n && dyn[i].d_tag != DT_NULL; i++) {
        if (dyn[i].d_tag == DT_RELA) {
            rela = (const elf64_rela_t *)in_image(base, imagesz, dyn[i].d_un);
        } else if (dyn[i].d_tag == DT_RELASZ) {
            rela_n = (size_t)(dyn[i].d_un / sizeof(elf64_rela_t));
        } else if (dyn[i].d_tag == DT_JMPREL) {
            jmprel = (const elf64_rela_t *)in_image(base, imagesz, dyn[i].d_un);
        } else if (dyn[i].d_tag == DT_PLTRELSZ) {
            pltrelsz = dyn[i].d_un;
        }
    }
    jmp_n = (size_t)(pltrelsz / sizeof(elf64_rela_t));
    n = patch_rela_table(base, imagesz, jmprel, jmp_n, real, hook);
    n += patch_rela_table(base, imagesz, rela, rela_n, real, hook);
    return n;
}

static intptr_t eboot_mapbase(void)
{
    pid_t pid = getpid();
    uint32_t handle = 0;
    intptr_t base;

    if (kernel_dynlib_handle(pid, "eboot.bin", &handle) == 0) {
        base = kernel_dynlib_mapbase_addr(pid, handle);
        if (base) {
            return base;
        }
    }
    return kernel_dynlib_mapbase_addr(pid, 0);
}

int got_hook_install(void *real, void *hook, void **orig_out)
{
    intptr_t base;
    int n;

    if (real == NULL || hook == NULL || orig_out == NULL) {
        return -1;
    }
    if (real == hook) {
        return -1;
    }
    base = eboot_mapbase();
    if (base == 0) {
        return -1;
    }
    n = patch_image_got((const uint8_t *)base, real, hook);
    if (n <= 0) {
        return -1;
    }
    *orig_out = real;
    return 0;
}
