"""Scan helpers: compare/type maps, region classify, turbo segment gap-fill.

Session never dumps RAM. Turbo START with a segment list and snapshot
progress drain live here so we reuse ``ps5dbg.turboscan`` packing instead of
a new TCP stack.
"""

from __future__ import annotations

import struct

from .constants import (
    PROT_EXEC,
    PROT_WRITE,
    RESULTS_MAX,
    SCAN_ALIGN_U32,
    SCAN_COMPARE_CHANGED,
    SCAN_COMPARE_DECREASED,
    SCAN_COMPARE_EXACT,
    SCAN_COMPARE_INCREASED,
    SCAN_COMPARE_UNCHANGED,
    SCAN_COMPARE_UNKNOWN,
    SCAN_SEGMENT_MAX,
    SCAN_SEGMENT_MIN,
    SCAN_VALUE_UINT32,
    SNAP_BITMAP_MAX,
    TS_SERVER_RESIDENT,
    TS_SNAPSHOT,
    TS_SNAPSHOT_SEGMENTS,
    TS_USE_ALIASING,
    UNCACHED_MAP_NAMES,
)
from .errors import InvalidScanCompare, InvalidScanValue, ScanUnsupported
from .types import ScanHit, ScanRegion

_VALUE_TYPES = {
    "u32": SCAN_VALUE_UINT32,
    "uint32": SCAN_VALUE_UINT32,
    "UINT32": SCAN_VALUE_UINT32,
}

_COMPARE_TYPES = {
    "exact": SCAN_COMPARE_EXACT,
    "equal": SCAN_COMPARE_EXACT,
    "exact_value": SCAN_COMPARE_EXACT,
    "increased": SCAN_COMPARE_INCREASED,
    "increased_value": SCAN_COMPARE_INCREASED,
    "decreased": SCAN_COMPARE_DECREASED,
    "decreased_value": SCAN_COMPARE_DECREASED,
    "changed": SCAN_COMPARE_CHANGED,
    "changed_value": SCAN_COMPARE_CHANGED,
    "unchanged": SCAN_COMPARE_UNCHANGED,
    "unchanged_value": SCAN_COMPARE_UNCHANGED,
    "unknown": SCAN_COMPARE_UNKNOWN,
    "unknown_initial": SCAN_COMPARE_UNKNOWN,
}

_FIRST_SCAN_COMPARES = frozenset({SCAN_COMPARE_EXACT, SCAN_COMPARE_UNKNOWN})
_NEXT_SCAN_COMPARES = frozenset(
    {
        SCAN_COMPARE_EXACT,
        SCAN_COMPARE_INCREASED,
        SCAN_COMPARE_DECREASED,
        SCAN_COMPARE_CHANGED,
        SCAN_COMPARE_UNCHANGED,
    }
)

_U32_SIZE = 4
_SENTINEL_U64 = 0xFFFFFFFFFFFFFFFF
_SNAPSHOT_PROGRESS_MAX = 10_000_000
_RESULT_BLOCK_MAX = 16 * 1024 * 1024

TOO_MANY_MATCHES = (
    "too many exact matches to keep on the console (256 MiB resident list). "
    "Change the value in-game and First Scan a less common number — "
    "will not download the hit list"
)


def parse_value_type(name: str) -> int:
    key = (name or "").strip()
    if key not in _VALUE_TYPES:
        raise InvalidScanValue(
            f"unsupported value type {name!r}; Phase 2 default is u32"
        )
    return _VALUE_TYPES[key]


def parse_compare(name: str) -> int:
    key = (name or "").strip().lower()
    if key not in _COMPARE_TYPES:
        raise InvalidScanCompare(
            f"unknown compare {name!r}; use exact/increased/decreased/changed/unchanged"
        )
    return _COMPARE_TYPES[key]


def require_first_compare(compare: int) -> None:
    if compare not in _FIRST_SCAN_COMPARES:
        raise InvalidScanCompare(
            "first scan supports exact (default) or unknown=True / compare='unknown'"
        )


def require_next_compare(compare: int) -> None:
    if compare not in _NEXT_SCAN_COMPARES:
        raise InvalidScanCompare(
            "next scan supports exact/increased/decreased/changed/unchanged"
        )


def value_size(value_type: int) -> int:
    if value_type != SCAN_VALUE_UINT32:
        raise InvalidScanValue(f"unsupported value type id {value_type}")
    return _U32_SIZE


def parse_u32_value(value: int | None, *, required: bool) -> int:
    if value is None:
        if required:
            raise InvalidScanValue("exact compare requires a u32 value")
        return 0
    if not isinstance(value, int) or isinstance(value, bool):
        raise InvalidScanValue(f"u32 value must be int, got {value!r}")
    if value < 0 or value > 0xFFFFFFFF:
        raise InvalidScanValue(f"u32 value out of range: {value}")
    return value


def u32_from_bytes(data: bytes) -> int:
    if len(data) < _U32_SIZE:
        raise InvalidScanValue(f"need {_U32_SIZE} bytes, got {len(data)}")
    return struct.unpack("<I", data[:_U32_SIZE])[0]


def u32_to_bytes(value: int) -> bytes:
    return struct.pack("<I", value & 0xFFFFFFFF)


def match_u32(compare: int, current: int, previous: int, value: int) -> bool:
    """Unsigned u32 compare. Unknown drops all-zero slots."""
    cur = current & 0xFFFFFFFF
    prev = previous & 0xFFFFFFFF
    val = value & 0xFFFFFFFF
    if compare == SCAN_COMPARE_EXACT:
        return cur == val
    if compare == SCAN_COMPARE_INCREASED:
        return cur > prev
    if compare == SCAN_COMPARE_DECREASED:
        return cur < prev
    if compare == SCAN_COMPARE_CHANGED:
        return cur != prev
    if compare == SCAN_COMPARE_UNCHANGED:
        return cur == prev
    if compare == SCAN_COMPARE_UNKNOWN:
        return cur != 0
    raise InvalidScanCompare(f"unsupported compare id {compare}")


def merge_ranges(ranges: list[tuple[int, int]]) -> list[tuple[int, int]]:
    """Merge adjacent/overlapping [start, end) ranges. Drop empty."""
    cleaned = [(s, e) for s, e in ranges if e > s]
    if not cleaned:
        return []
    cleaned.sort()
    out: list[tuple[int, int]] = [cleaned[0]]
    for start, end in cleaned[1:]:
        prev_s, prev_e = out[-1]
        if start <= prev_e:
            out[-1] = (prev_s, max(prev_e, end))
        else:
            out.append((start, end))
    return out


def ranges_to_segments(ranges: list[tuple[int, int]]) -> list[tuple[int, int]]:
    """Convert [start, end) to (address, length), splitting lengths that exceed u32."""
    segments: list[tuple[int, int]] = []
    max_len = 0xFFFFFFFF
    for start, end in merge_ranges(ranges):
        addr = start
        remaining = end - start
        while remaining > 0:
            chunk = remaining if remaining <= max_len else max_len
            segments.append((addr, chunk))
            addr += chunk
            remaining -= chunk
    if not (SCAN_SEGMENT_MIN <= len(segments) <= SCAN_SEGMENT_MAX):
        raise ScanUnsupported(
            f"segment count {len(segments)} not in {SCAN_SEGMENT_MIN}..{SCAN_SEGMENT_MAX}"
        )
    return segments


def classify_maps(maps) -> list[tuple[int, int]]:
    """Writable, non-executable maps (rw- heaps / eboot data).

    Fallback when TURBOSCAN_REGIONS is unavailable. No uncached bit on
    ``MemoryMap`` — prefer ``classify_turbo_regions`` after a 1-byte probe.
    Skip r-x code, rwx, and known GPU names (SceGnm) even without PCD.
    """
    out: list[tuple[int, int]] = []
    for m in maps:
        if not (m.prot & PROT_WRITE):
            continue
        if m.prot & PROT_EXEC:
            continue
        if m.name in UNCACHED_MAP_NAMES:
            continue
        out.append((m.start, m.end))
    return out


def classify_turbo_regions(regions: list[ScanRegion]) -> list[tuple[int, int]]:
    """Writable, non-executable, cached (PCD/uncached skipped)."""
    out: list[tuple[int, int]] = []
    for r in regions:
        if not (r.prot & PROT_WRITE):
            continue
        if r.prot & PROT_EXEC:
            continue
        if r.uncached:
            continue
        out.append((r.start, r.end))
    return out


def turbo_start_flags(*, snapshot: bool, use_aliasing: bool) -> int:
    """Resident multi-segment START flags. Never sets TS_PARALLEL_COMPARE."""
    flags = TS_SERVER_RESIDENT | TS_SNAPSHOT_SEGMENTS
    if snapshot:
        flags |= TS_SNAPSHOT
    if use_aliasing:
        flags |= TS_USE_ALIASING
    return flags


def snapshot_bitmap_bytes(
    segments: list[tuple[int, int]],
    *,
    value_size: int = _U32_SIZE,
    alignment: int = SCAN_ALIGN_U32,
) -> int:
    """ps5debug-NG membership bitmap size (always RAM)."""
    step = alignment if alignment > 0 else value_size
    slots = 0
    for _addr, length in segments:
        if length >= value_size:
            slots += (length - value_size) // step + 1
    return (slots + 7) >> 3


def snapshot_fits(
    segments: list[tuple[int, int]],
    *,
    value_size: int = _U32_SIZE,
    alignment: int = SCAN_ALIGN_U32,
) -> bool:
    return snapshot_bitmap_bytes(
        segments, value_size=value_size, alignment=alignment
    ) <= SNAP_BITMAP_MAX


def drain_result_blocks(conn) -> None:
    """Discard TURBOSCAN result blocks (u64 length + payload) until sentinel.

    Segmented resident overflow sends an empty stream (immediate sentinel).
    Single-range overflow can stream full hits — we drop the payload so :744
    stays in sync and we never keep the list. Do not use _recv_u64_stream:
    that treats block_len as a bare u64 and desyncs the socket.
    """
    from ps5dbg.wire import recv_u64

    while True:
        block_len = recv_u64(conn)
        if block_len == _SENTINEL_U64:
            return
        if block_len > _RESULT_BLOCK_MAX:
            raise ScanUnsupported(
                f"refusing to drain a {block_len} byte scan block "
                f"(max {_RESULT_BLOCK_MAX}); reconnect if :744 is stuck"
            )
        conn.recv_exact(int(block_len))


def hits_from_resident(
    rows: list[tuple[int, bytes, bytes, bytes | None]],
) -> list[ScanHit]:
    return [
        ScanHit(addr=addr, current=current, previous=previous)
        for addr, current, previous, _first in rows
    ]


def cap_get_count(limit: int, count: int) -> int:
    return min(limit, count, RESULTS_MAX)


def _start_body(
    pid: int,
    address: int,
    length: int,
    value_type: int,
    compare_type: int,
    alignment: int,
    value_len: int,
    flags: int,
) -> bytes:
    return struct.pack(
        "<IQIBBBI",
        pid,
        address,
        length,
        value_type & 0xFF,
        compare_type & 0xFF,
        alignment & 0xFF,
        value_len,
    ) + struct.pack("<I", flags & 0xFFFFFFFF)


def _send_segments(conn, segments: list[tuple[int, int]]) -> None:
    if not (SCAN_SEGMENT_MIN <= len(segments) <= SCAN_SEGMENT_MAX):
        raise ScanUnsupported(
            f"segment count {len(segments)} not in {SCAN_SEGMENT_MIN}..{SCAN_SEGMENT_MAX}"
        )
    blob = struct.pack("<I", len(segments))
    blob += b"".join(struct.pack("<QI", addr, length) for addr, length in segments)
    conn._sendall(blob)


def turbo_start_resident_segments(
    conn,
    pid: int,
    segments: list[tuple[int, int]],
    value_type: int,
    compare_type: int,
    alignment: int,
    value: int,
    *,
    use_aliasing: bool = False,
) -> tuple[int, int]:
    """TS_SERVER_RESIDENT | TS_SNAPSHOT_SEGMENTS (TS_SNAPSHOT clear).

    After the two value acks the server reads the segment list. Packet
    address/length are ignored. Reply is still ``{u32 resident_stored; u64 count}``.
    """
    import ps5dbg.turboscan as ts
    from ps5dbg.constants import Cmd
    from ps5dbg.protocol import _scan_value_bytes
    from ps5dbg.wire import expect_success

    vbytes = _scan_value_bytes(value_type, value)
    flags = turbo_start_flags(snapshot=False, use_aliasing=use_aliasing)
    body = _start_body(pid, 0, 0, value_type, compare_type, alignment, len(vbytes), flags)
    ts._send_value_after_ack(conn, Cmd.PROC_TURBOSCAN_START, body, vbytes)
    _send_segments(conn, segments)
    resident_stored, count = struct.unpack("<IQ", conn.recv_exact(12))
    if resident_stored == 0:
        drain_result_blocks(conn)
    expect_success(conn, context="CMD_PROC_TURBOSCAN_START segmented resident (final)")
    return resident_stored, count


def turbo_start_snapshot_segments(
    conn,
    pid: int,
    segments: list[tuple[int, int]],
    value_type: int,
    alignment: int,
    *,
    use_aliasing: bool = False,
) -> int:
    """Unknown first scan: TS_SNAPSHOT | TS_SNAPSHOT_SEGMENTS | TS_SERVER_RESIDENT.

    Does not set TS_SNAPSHOT_INCLUDE_ZEROS (drop all-zero slots). Drains the
    progress stream; never downloads RAM. Raises ScanUnsupported if declined.
    """
    import ps5dbg.turboscan as ts
    from ps5dbg.constants import Cmd
    from ps5dbg.errors import ConnectionLost, ProtocolError
    from ps5dbg.protocol import _scan_value_bytes
    from ps5dbg.wire import expect_success, recv_u64

    vbytes = _scan_value_bytes(value_type, 0)
    flags = turbo_start_flags(snapshot=True, use_aliasing=use_aliasing)
    body = _start_body(
        pid, 0, 0, value_type, SCAN_COMPARE_UNKNOWN, alignment, len(vbytes), flags
    )
    ts._send_value_after_ack(conn, Cmd.PROC_TURBOSCAN_START, body, vbytes)
    _send_segments(conn, segments)
    try:
        plan = conn.recv_exact(16)
        _slot_count, total_bytes = struct.unpack("<QQ", plan)
    except (OSError, struct.error, ProtocolError, ConnectionLost) as exc:
        raise ScanUnsupported(f"unsafe snapshot plan: {exc}") from exc
    for _ in range(_SNAPSHOT_PROGRESS_MAX):
        try:
            done = recv_u64(conn)
        except (OSError, ProtocolError, ConnectionLost) as exc:
            raise ScanUnsupported(f"unsafe snapshot progress: {exc}") from exc
        if done == _SENTINEL_U64:
            break
        if total_bytes and done > total_bytes:
            raise ScanUnsupported("unsafe snapshot progress (bytes_done > total_bytes)")
    else:
        raise ScanUnsupported("snapshot progress stream did not terminate")
    try:
        snapshot_ok, survivor_count = struct.unpack("<IQ", conn.recv_exact(12))
    except (OSError, struct.error, ProtocolError, ConnectionLost) as exc:
        raise ScanUnsupported(f"unsafe snapshot trailer: {exc}") from exc
    expect_success(conn, context="CMD_PROC_TURBOSCAN_START snapshot (final)")
    if snapshot_ok == 0:
        raise ScanUnsupported(
            "console declined the snapshot (ENOSPC / bitmap too large / "
            "/data full). Change the value in-game and First Scan a less "
            "common number; reconnect if the next command times out"
        )
    return survivor_count


def alignment_or_default(alignment: int) -> int:
    if not isinstance(alignment, int) or isinstance(alignment, bool) or alignment <= 0:
        raise InvalidScanValue(f"alignment must be > 0, got {alignment!r}")
    return alignment if alignment else SCAN_ALIGN_U32
