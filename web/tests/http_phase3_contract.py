"""HTTP Phase 3 contract (ARCHITECTURE §5.3 + GoldHEN cheats).

Write, watch, freeze, and cheat filenames fail closed: refuse empty / oversize
writes, refuse the 65th watch and 33rd freeze at the session layer, and never
resolve a cheat path outside web/cheats/.
"""

from __future__ import annotations

from pathlib import Path

from nitepr5_core import WRITE_MAX, InvalidCheat, InvalidWriteSize, WriteTooLarge

# Sentinel the UI must never send as write data (dump / unbounded poke).
UNBOUNDED_WRITE = "all"


def http_write_data(data: str | None = None) -> bytes:
    """Decode POST /api/write ``data`` hex. Empty and > WRITE_MAX refuse."""
    if data == UNBOUNDED_WRITE:
        raise WriteTooLarge(-1)
    if data is None or not isinstance(data, str):
        raise InvalidWriteSize(data)
    text = data.strip()
    if not text:
        raise InvalidWriteSize(0)
    try:
        raw = bytes.fromhex(text)
    except ValueError as exc:
        raise InvalidWriteSize(data) from exc
    if not raw:
        raise InvalidWriteSize(0)
    if len(raw) > WRITE_MAX:
        raise WriteTooLarge(len(raw))
    return raw


def http_cheat_filename(filename: object) -> str:
    """Basename-only cheat files under web/cheats/. Refuse traversal."""
    if not isinstance(filename, str) or not filename.strip():
        raise InvalidCheat("cheat filename required")
    name = filename.strip()
    if name != Path(name).name or name in {".", ".."}:
        raise InvalidCheat(
            "cheat filename must be a single path segment under web/cheats/"
        )
    return name
