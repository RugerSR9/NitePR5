"""Hard caps and PS5Debug defaults for NitePR5."""

from __future__ import annotations

PS5DEBUG_PORT = 744
CONNECT_TIMEOUT = 10.0

# Turbo scan can sit silent for minutes (progress is a u64 every so often, or
# a 12-byte summary only after all segments). ps5dbg Connection defaults to
# 10s; that fires ``timed out reading 8 bytes``. Scan I/O waits indefinitely
# (TCP keepalive still detects a dropped 744 / rest mode).
SCAN_IO_TIMEOUT: float | None = None

# Hex peephole: UI default 512 B, fail closed above 4 KiB (ARCHITECTURE §5.3).
HEX_PEEPHOLE_DEFAULT = 512
READ_MAX = 4096
WRITE_MAX = READ_MAX

# Watch list: user-pinned addresses only. UI polls ~10 Hz; core has no timer.
WATCH_MAX = 64
WATCH_SIZES = frozenset({1, 2, 4, 8})
WATCH_SIZE_DEFAULT = 4

# Freeze list: UI ticks ~15 Hz; core freeze_tick writes once per call.
FREEZE_MAX = 32
FREEZE_SIZE_MIN = 1
FREEZE_SIZE_MAX = 8

# Scan results: count-first UI; never fetch more than this many rows.
RESULTS_MAX = 256

# Default first scan: UINT32 (ps5dbg ScanValueType.UINT32 = 4), 4-byte aligned,
# exact compare, writable+cached regions. Never default regions to "all".
SCAN_VALUE_UINT32 = 4
SCAN_ALIGN_U32 = 4
SCAN_REGIONS_DEFAULT = "writable_cached"

# ps5dbg ScanCompareType (scan_compare.c).
SCAN_COMPARE_EXACT = 0
SCAN_COMPARE_INCREASED = 5
SCAN_COMPARE_DECREASED = 7
SCAN_COMPARE_CHANGED = 9
SCAN_COMPARE_UNCHANGED = 10
SCAN_COMPARE_UNKNOWN = 11

# Turbo engine bits / request flags (ps5dbg.turboscan). Duplicated so Session
# can classify without importing ps5dbg.
TSE_ALIASING = 0x02
TSE_SERVER_RESIDENT = 0x04
TSE_SNAPSHOT = 0x08
TSE_SNAPSHOT_SEGMENTS = 0x10
TSE_RESCAN_ALIASING = 0x200
TS_USE_ALIASING = 0x01
TS_SERVER_RESIDENT = 0x02
TS_SNAPSHOT = 0x04
TS_SNAPSHOT_SEGMENTS = 0x10
TS_PARALLEL_COMPARE = 0x80
TS_RESCAN_ALIASING = 0x100

# TURBOSCAN_REGIONS: 0 means server default 64 KiB per readable map (hangs).
# 1 byte still returns the leaf-PTE PCD (uncached) bit.
SCAN_REGIONS_PROBE_BYTES = 1

# Segmented TURBOSCAN_START: u32 count then count × {u64 addr; u32 length}.
SCAN_SEGMENT_MIN = 1
SCAN_SEGMENT_MAX = 1_048_576

# ps5debug-NG TS_SNAP_BITMAP_MAX. Membership bitmap is always RAM; above this
# the server replies snapshot_ok=0 (ENOSPC / too large).
SNAP_BITMAP_MAX = 448 * 1024 * 1024

# Name heuristic when TURBOSCAN_REGIONS is missing (no PCD bit on maps()).
UNCACHED_MAP_NAMES = frozenset({"SceGnm"})

PROT_READ = 1
PROT_WRITE = 2
PROT_EXEC = 4

EBOOT_NAME = "eboot.bin"
# ps5debug-NG names the main ELF mappings "executable", not "eboot.bin"
# (CUSA13762 live: first writable map name is executable @ 0x1bc0000).
EXECUTABLE_MAP_NAME = "executable"
