"""Hard caps and PS5Debug defaults for NitePR5 Phase 1."""

from __future__ import annotations

PS5DEBUG_PORT = 744
CONNECT_TIMEOUT = 10.0

# Hex peephole: UI default 512 B, fail closed above 4 KiB (ARCHITECTURE §5.3).
HEX_PEEPHOLE_DEFAULT = 512
READ_MAX = 4096

EBOOT_NAME = "eboot.bin"
