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
#define PROC_WRITE             0xBDAA0003u
#define PROC_MAPS              0xBDAA0004u
#define PROC_INFO              0xBDAA000Au
#define PROC_NOP               0xBDAACC06u
#define CONSOLE_FOREGROUND_APP 0xBDDD0006u

#define DBG_PROC_ENTRY_SIZE  36
#define DBG_MAP_ENTRY_SIZE   58
#define DBG_PROC_INFO_SIZE   188
#define DBG_FOREGROUND_SIZE  140
#define DBG_WRITE_PACKET_LEN 16

int dbg_connect(void);
void dbg_disconnect(void);
int dbg_connected(void);
int dbg_nop(void);

/* Two-phase PROC_WRITE: 16-byte <IQI pid,addr,length> then ACK, then payload, then FINAL.
 * Never concatenate payload into the request body (ps5dbg 0.1.1 protocol.proc_write hangs :744).
 */
int dbg_proc_write(uint32_t pid, uint64_t addr, const uint8_t *data, uint32_t length);

int dbg_proc_list(void *entries, uint32_t cap, uint32_t *count);
int dbg_proc_maps(uint32_t pid, void *entries, uint32_t cap, uint32_t *count);
int dbg_proc_info(uint32_t pid, void *out188);
int dbg_foreground(void *out140);

/* Lowest maps.start whose name is process, its basename, or "executable" when process is eboot.bin. */
int dbg_module_base(uint32_t pid, const char *process, uint64_t *out_base);

#endif
