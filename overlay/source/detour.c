#include "overlay.h"

#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef PROT_READ
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4
#endif
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS 0x1000
#endif
#ifndef MAP_PRIVATE
#define MAP_PRIVATE 0x0002
#endif
#ifndef MAP_FAILED
#define MAP_FAILED ((void *)-1)
#endif

#ifndef PAGE_SIZE
#define PAGE_SIZE 0x4000u
#endif

#define JMP_LEN 14

int sceKernelMprotect(void *addr, size_t len, int prot) __attribute__((weak));
int kernel_mprotect(int pid, unsigned long addr, unsigned long len, int prot) __attribute__((weak));

static int protect_rwx(void *addr, size_t len)
{
    uintptr_t a = (uintptr_t)addr;
    uintptr_t s = a & ~((uintptr_t)PAGE_SIZE - 1);
    size_t n = (a + len + PAGE_SIZE - 1 - s);
    pid_t pid = getpid();

    if (n < PAGE_SIZE) {
        n = PAGE_SIZE;
    }
    if (sceKernelMprotect && sceKernelMprotect((void *)s, n, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
        return 0;
    }
    if (kernel_mprotect && kernel_mprotect((int)pid, (unsigned long)s, (unsigned long)n,
                                          PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
        return 0;
    }
    return -1;
}

/* Original x64 length walk (enough to copy >=14 bytes of a Sony SPRX export). */
static int is_prefix(uint8_t b)
{
    return b == 0x26 || b == 0x2E || b == 0x36 || b == 0x3E || b == 0x64 || b == 0x65 || b == 0x66 ||
           b == 0x67 || b == 0xF0 || b == 0xF2 || b == 0xF3 || (b >= 0x40 && b <= 0x4F);
}

static int modrm_len(const uint8_t *p, int addr64)
{
    uint8_t mod = (uint8_t)(p[0] >> 6);
    uint8_t rm = (uint8_t)(p[0] & 7);
    int n = 1;

    if (mod != 3 && rm == 4) {
        uint8_t sib = p[1];
        n++;
        if (mod == 0 && (sib & 7) == 5) {
            n += 4;
        }
    }
    if (mod == 1) {
        n += 1;
    } else if (mod == 2) {
        n += 4;
    } else if (mod == 0 && rm == 5 && addr64) {
        n += 4;
    }
    return n;
}

static int imm_for_opcode(uint8_t op, int op66, int rex_w)
{
    switch (op) {
    case 0x04: case 0x0C: case 0x14: case 0x1C:
    case 0x24: case 0x2C: case 0x34: case 0x3C:
    case 0x6A: case 0xA8: case 0xB0: case 0xB1:
    case 0xB2: case 0xB3: case 0xB4: case 0xB5:
    case 0xB6: case 0xB7: case 0xCD: case 0xD4:
    case 0xD5: case 0xE4: case 0xE5: case 0xE6:
    case 0xE7: case 0x80: case 0x82: case 0x83:
    case 0xC0: case 0xC1: case 0xC6:
        return 1;
    case 0xC2: case 0xCA:
        return 2;
    case 0x05: case 0x0D: case 0x15: case 0x1D:
    case 0x25: case 0x2D: case 0x35: case 0x3D:
    case 0x68: case 0x81: case 0xA9: case 0xC7:
    case 0xE8: case 0xE9:
        return rex_w ? 4 : (op66 ? 2 : 4);
    case 0xB8: case 0xB9: case 0xBA: case 0xBB:
    case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        return rex_w ? 8 : (op66 ? 2 : 4);
    case 0xA0: case 0xA1: case 0xA2: case 0xA3:
        return 8;
    default:
        return 0;
    }
}

static int insn_len(const uint8_t *p)
{
    int i = 0;
    int op66 = 0;
    int rex_w = 0;
    int addr64 = 1;
    uint8_t op;
    int has_modrm = 0;
    int imm;

    while (i < 15 && is_prefix(p[i])) {
        if (p[i] == 0x66) {
            op66 = 1;
        }
        if (p[i] == 0x67) {
            addr64 = 0;
        }
        if (p[i] >= 0x40 && p[i] <= 0x4F && (p[i] & 8)) {
            rex_w = 1;
        }
        i++;
    }
    if (i >= 15) {
        return -1;
    }
    op = p[i++];
    if (op == 0x0F) {
        if (i >= 15) {
            return -1;
        }
        op = p[i++];
        if (op == 0x38 || op == 0x3A) {
            if (i >= 15) {
                return -1;
            }
            i++;
            has_modrm = 1;
            if (op == 0x3A) {
                imm = 1;
            } else {
                imm = 0;
            }
            if (has_modrm) {
                int m = modrm_len(p + i, addr64);
                i += m;
            }
            return i + imm;
        }
        has_modrm = 1;
        if (op >= 0x80 && op <= 0x8F) {
            imm = 4;
        } else if (op == 0x70 + (op & 0) ) {
            imm = 0;
        } else {
            imm = 0;
        }
        if (op >= 0x80 && op <= 0x8F) {
            imm = 4;
        }
        if (has_modrm) {
            int m = modrm_len(p + i, addr64);
            i += m;
        }
        return i + imm;
    }
    if ((op >= 0x00 && op <= 0x03) || (op >= 0x08 && op <= 0x0B) || (op >= 0x10 && op <= 0x13) ||
        (op >= 0x18 && op <= 0x1B) || (op >= 0x20 && op <= 0x23) || (op >= 0x28 && op <= 0x2B) ||
        (op >= 0x30 && op <= 0x33) || (op >= 0x38 && op <= 0x3B) || op == 0x63 || op == 0x69 ||
        op == 0x6B || (op >= 0x80 && op <= 0x8F) || (op >= 0xC0 && op <= 0xC1) || op == 0xC6 ||
        op == 0xC7 || (op >= 0xD0 && op <= 0xD3) || (op >= 0xF6 && op <= 0xF7) ||
        (op >= 0xFE && op <= 0xFF) || op == 0x62 || op == 0x8D || op == 0x8F) {
        has_modrm = 1;
    }
    if (op >= 0x40 && op <= 0x5F) {
        return i;
    }
    if (op >= 0x70 && op <= 0x7F) {
        return i + 1;
    }
    if (op == 0xEB || op == 0xE3) {
        return i + 1;
    }
    if (op == 0xC3 || op == 0xCB || op == 0xC9 || op == 0x90 || op == 0x99 || op == 0x9B ||
        op == 0x9E || op == 0x9F || op == 0xCC || op == 0xCE || op == 0xCF || op == 0xF4 ||
        op == 0xF5 || op == 0xF8 || op == 0xF9 || op == 0xFA || op == 0xFB || op == 0xFC ||
        op == 0xFD || op == 0x9C || op == 0x9D || op == 0x50 + (op & 7) || op == 0xA4 ||
        op == 0xA5 || op == 0xA6 || op == 0xA7 || op == 0xAA || op == 0xAB || op == 0xAC ||
        op == 0xAD || op == 0xAE || op == 0xAF) {
        return i;
    }
    if (has_modrm) {
        int m = modrm_len(p + i, addr64);
        i += m;
    }
    imm = imm_for_opcode(op, op66, rex_w);
    if (op == 0x69) {
        imm = rex_w ? 4 : (op66 ? 2 : 4);
    }
    if (op == 0x6B) {
        imm = 1;
    }
    return i + imm;
}

static void write_abs_jmp(uint8_t *dst, void *to)
{
    /* FF 25 00 00 00 00 + 64-bit dest  (14 bytes) */
    dst[0] = 0xFF;
    dst[1] = 0x25;
    dst[2] = 0x00;
    dst[3] = 0x00;
    dst[4] = 0x00;
    dst[5] = 0x00;
    memcpy(dst + 6, &to, 8);
}

int detour_install(void *target, void *hook, void **orig_out)
{
    uint8_t *src = (uint8_t *)target;
    int stolen = 0;
    int n;
    uint8_t *stub;
    size_t stub_n;

    if (target == NULL || hook == NULL || orig_out == NULL) {
        return -1;
    }
    while (stolen < JMP_LEN) {
        n = insn_len(src + stolen);
        if (n <= 0 || n > 15) {
            return -1;
        }
        stolen += n;
        if (stolen > 32) {
            return -1;
        }
    }
    stub_n = (size_t)stolen + JMP_LEN + 16;
    stub = (uint8_t *)mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (stub == MAP_FAILED || stub == NULL) {
        stub = (uint8_t *)malloc(stub_n);
        if (stub == NULL) {
            return -1;
        }
        memset(stub, 0x90, stub_n);
        if (protect_rwx(stub, stub_n) != 0) {
            free(stub);
            return -1;
        }
    } else {
        memset(stub, 0x90, PAGE_SIZE);
    }
    memcpy(stub, src, (size_t)stolen);
    write_abs_jmp(stub + stolen, src + stolen);
    if (protect_rwx(src, (size_t)stolen + 16) != 0) {
        return -1;
    }
    write_abs_jmp(src, hook);
    *orig_out = stub;
    return 0;
}
