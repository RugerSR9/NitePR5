"""HTTP peephole-read contract (ARCHITECTURE §5.3).

HTTP clients must never GET unbounded read — omit n, pass n="all", or
otherwise dump process RAM. The default window is HEX_PEEPHOLE_DEFAULT
(512 bytes). The hard cap is READ_MAX (4096). Refuse, do not crawl.
"""

from __future__ import annotations

from nitepr5_core import HEX_PEEPHOLE_DEFAULT, READ_MAX, InvalidReadSize, ReadTooLarge

# Sentinel the UI must never send as a read length.
UNBOUNDED_READ = "all"


def http_read_n(n: int | str | None = None) -> int:
    """Resolve GET /read length. Default 512; never "all" / unbounded."""
    if n == UNBOUNDED_READ:
        raise ReadTooLarge(-1)
    if n is None or n == "":
        return HEX_PEEPHOLE_DEFAULT
    try:
        length = int(n)
    except (TypeError, ValueError) as exc:
        raise InvalidReadSize(n) from exc
    if length <= 0:
        raise InvalidReadSize(length)
    if length > READ_MAX:
        raise ReadTooLarge(length)
    return length
