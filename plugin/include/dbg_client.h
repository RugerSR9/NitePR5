#ifndef NITEPR5_DBG_CLIENT_H
#define NITEPR5_DBG_CLIENT_H

#include <stdint.h>

/* Thin localhost :744 client. Opcodes/layouts from ps5dbg 0.1.1 / PROTOCOL.md v1.3.0.
 * Not a protocol fork. Status success is the raw word 0x80000000 (do not bitswap).
 * PROC_AUTH is omitted; add magic 0xBB40E64D flags=0 only if PROC_WRITE is refused.
 */

#define DBG_MAGIC   0xFFAABBCCu
#define DBG_SUCCESS 0x80000000u

#define PROC_LIST              0xBDAA0001u
#define PROC_READ              0xBDAA0002u
#define PROC_WRITE             0xBDAA0003u
#define PROC_MAPS              0xBDAA0004u
#define PROC_ELF               0xBDAA0007u
#define PROC_INFO              0xBDAA000Au
#define PROC_NOP               0xBDAACC06u
#define CONSOLE_FOREGROUND_APP 0xBDDD0006u

#define DBG_PROC_ENTRY_SIZE  36
#define DBG_MAP_ENTRY_SIZE   58
#define DBG_PROC_INFO_SIZE   188
#define DBG_FOREGROUND_SIZE  140
#define DBG_WRITE_PACKET_LEN 16
#define DBG_READ_PACKET_LEN  16
#define DBG_ELF_PACKET_LEN   8
#define DBG_READ_MAX         4096u
#define DBG_ELF_MAX          (8u * 1024u * 1024u)

int dbg_connect(void);
void dbg_disconnect(void);
int dbg_connected(void);
/* ensure_dbg: if !dbg_connected() then dbg_connect(). Same g_fd for overlay I/O and freeze. */
int dbg_ensure(void);
int dbg_nop(void);

/* Two-phase PROC_WRITE: 16-byte <IQI pid,addr,length> then ACK, then payload, then FINAL.
 * Never concatenate payload into the request body (ps5dbg 0.1.1 protocol.proc_write hangs :744).
 */
int dbg_proc_write(uint32_t pid, uint64_t addr, const uint8_t *data, uint32_t length);

/* One-phase PROC_READ: 16-byte <IQI pid,addr,length> then SUCCESS then exactly length bytes.
 * No extra phase, no length prefix. Not two-phase like WRITE. Assumes length in 1..DBG_READ_MAX.
 */
int dbg_proc_read(uint32_t pid, uint64_t addr, uint8_t *out, uint32_t length);

/* Two-phase PROC_ELF (ps5debug-NG PROTOCOL.md): 8-byte <II pid,length> then ACK,
 * then ELF bytes, then FINAL. Maps into the target as a remote thread — not PT_ATTACH
 * from this plugin. length 16..DBG_ELF_MAX. Must be a 64-bit ELF.
 */
int dbg_proc_elf(uint32_t pid, const uint8_t *elf, uint32_t length);

/* Decode raw PROC_MAPS / PROC_LIST / CONSOLE_FOREGROUND_APP rows (ps5dbg VmMap / ProcInfo). */
void dbg_decode_map(const uint8_t *row58, char name[33], uint64_t *start, uint64_t *end,
                    uint64_t *offset, uint16_t *prot);
void dbg_decode_proc(const uint8_t *row36, char name[33], int32_t *pid);
void dbg_decode_foreground(const uint8_t *row140, uint32_t *pid, char titleid[17],
                           char contentid[65], char name[41], char app_ver[17]);

int dbg_proc_list(void *entries, uint32_t cap, uint32_t *count);
int dbg_proc_maps(uint32_t pid, void *entries, uint32_t cap, uint32_t *count);
int dbg_proc_info(uint32_t pid, void *out188);
int dbg_foreground(void *out140);

/* Lowest maps.start whose name is process, its basename, or "executable" when process is eboot.bin. */
int dbg_module_base(uint32_t pid, const char *process, uint64_t *out_base);

#endif
