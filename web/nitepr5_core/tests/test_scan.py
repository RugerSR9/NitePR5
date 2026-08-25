"""Phase 2 scan tests against MockTransport (no PS5 required)."""

from __future__ import annotations

import inspect
import sys
from pathlib import Path

import pytest

_WEB = Path(__file__).resolve().parents[2]
if str(_WEB) not in sys.path:
    sys.path.insert(0, str(_WEB))

from nitepr5_core import (  # noqa: E402
    RESULTS_MAX,
    SCAN_REGIONS_DEFAULT,
    InvalidResultLimit,
    InvalidScanRegions,
    InvalidScanValue,
    MockTransport,
    NoScan,
    ResultsTooMany,
    ScanActive,
    ScanHit,
    ScanUnsupported,
    Session,
)
from nitepr5_core.scan import (  # noqa: E402
    classify_maps,
    classify_turbo_regions,
    snapshot_bitmap_bytes,
    snapshot_fits,
    turbo_start_flags,
    u32_from_bytes,
)
from nitepr5_core.constants import SNAP_BITMAP_MAX  # noqa: E402
from nitepr5_core.types import MemoryMap, ScanRegion  # noqa: E402
from nitepr5_core.transport import Ps5dbgTransport  # noqa: E402

_FORBIDDEN_ATTACH = frozenset(
    {
        "attach",
        "PT_ATTACH",
        "pt_attach",
        "ptrace",
        "PT_TRACE",
        "debug_session",
        "debug_attach",
    }
)


@pytest.fixture
def connected() -> tuple[Session, MockTransport]:
    transport = MockTransport()
    session = Session(transport)
    session.connect("mock")
    session.attach_target(MockTransport.EBOOT_PID)
    yield session, transport
    session.disconnect()


def test_exact_u32_finds_planted_writable_only(
    connected: tuple[Session, MockTransport],
) -> None:
    session, transport = connected
    count = session.scan_start(value=MockTransport.PLANTED_U32)
    assert count == 3
    assert session.scan_count() == 3
    hits = session.scan_results(256)
    addrs = {h.addr for h in hits}
    assert addrs == {
        MockTransport.WRITABLE_EBOOT_ADDR,
        MockTransport.HEAP_A,
        MockTransport.HEAP_C,
    }
    assert MockTransport.DECOY_RX_ADDR not in addrs
    assert MockTransport.GPU_ADDR not in addrs
    assert transport.regions_probe_count == 1
    assert transport.last_regions_probe_bytes == 1
    assert transport.last_use_aliasing is True
    assert transport.iterative_start_calls == 0
    for hit in hits:
        assert isinstance(hit, ScanHit)
        assert u32_from_bytes(hit.current) == MockTransport.PLANTED_U32


def test_next_scan_changed_increased_decreased_exact(
    connected: tuple[Session, MockTransport],
) -> None:
    session, transport = connected

    assert session.scan_start(value=100) == 3
    transport.poke_u32(MockTransport.HEAP_A, 101)
    assert session.scan_next(compare="changed") == 1
    hits = session.scan_results(256)
    assert len(hits) == 1
    assert hits[0].addr == MockTransport.HEAP_A

    session.disconnect()
    session.connect("mock")
    session.attach_target(MockTransport.EBOOT_PID)
    transport.poke_u32(MockTransport.HEAP_A, 100)
    assert session.scan_start(value=100) == 3
    transport.poke_u32(MockTransport.HEAP_A, 150)
    assert session.scan_next(compare="increased") == 1
    assert session.scan_results(256)[0].addr == MockTransport.HEAP_A

    session.disconnect()
    session.connect("mock")
    session.attach_target(MockTransport.EBOOT_PID)
    transport.poke_u32(MockTransport.HEAP_A, 100)
    assert session.scan_start(value=100) == 3
    transport.poke_u32(MockTransport.HEAP_C, 90)
    assert session.scan_next(compare="decreased") == 1
    assert session.scan_results(256)[0].addr == MockTransport.HEAP_C

    session.disconnect()
    session.connect("mock")
    session.attach_target(MockTransport.EBOOT_PID)
    transport.poke_u32(MockTransport.HEAP_A, 100)
    transport.poke_u32(MockTransport.HEAP_C, 100)
    assert session.scan_start(value=100) == 3
    transport.poke_u32(MockTransport.HEAP_A, 0)
    assert session.scan_next(compare="exact", value=100) == 2
    addrs = {h.addr for h in session.scan_results(256)}
    assert addrs == {MockTransport.WRITABLE_EBOOT_ADDR, MockTransport.HEAP_C}
    assert transport.last_rescan_aliasing is True


def test_undo_restores_previous_count_and_results(
    connected: tuple[Session, MockTransport],
) -> None:
    session, transport = connected
    assert session.scan_start(value=100) == 3
    first = session.scan_results(256)
    transport.poke_u32(MockTransport.HEAP_A, 101)
    assert session.scan_next(compare="changed") == 1
    assert len(session.scan_results(256)) == 1
    restored = session.scan_undo()
    assert restored == 3
    assert session.scan_count() == 3
    back = session.scan_results(256)
    assert {h.addr for h in back} == {h.addr for h in first}
    assert session.scan_undo() is None
    with pytest.raises(NoScan):
        session.scan_count()
    with pytest.raises(NoScan):
        session.scan_undo()


def test_scan_results_caps_and_no_unbounded_get(
    connected: tuple[Session, MockTransport],
) -> None:
    session, transport = connected
    val = 0x51515151
    for i in range(300):
        transport.poke_u32(MockTransport.RAM_BASE + 0x2000 + i * 4, val)
    count = session.scan_start(value=val)
    assert count == 300
    assert session.scan_count() > RESULTS_MAX
    before = transport.get_resident_calls
    assert session.scan_results(256) == []
    assert session.scan_results(1) == []
    assert transport.get_resident_calls == before
    assert transport.unbounded_get_attempted is False
    with pytest.raises(ResultsTooMany):
        session.scan_results(257)
    with pytest.raises(InvalidResultLimit):
        session.scan_results(0)
    with pytest.raises(InvalidResultLimit):
        session.scan_results(-1)


def test_default_regions_not_all_unknown_requires_flag(
    connected: tuple[Session, MockTransport],
) -> None:
    session, _transport = connected
    sig = inspect.signature(Session.scan_start)
    assert sig.parameters["regions"].default == SCAN_REGIONS_DEFAULT
    assert sig.parameters["regions"].default != "all"
    assert sig.parameters["unknown"].default is False
    assert sig.parameters["value_type"].default == "u32"
    assert sig.parameters["compare"].default == "exact"

    with pytest.raises(InvalidScanRegions):
        session.scan_start(value=100, regions="all")
    with pytest.raises(InvalidScanValue):
        session.scan_start()

    n_exact = session.scan_start(value=100)
    assert n_exact == 3
    assert session.scan_undo() is None
    n_unk = session.scan_start(unknown=True)
    assert n_unk > n_exact
    assert n_unk > RESULTS_MAX
    assert session.scan_results(256) == []


def test_second_scan_start_replaces_previous_hunt(
    connected: tuple[Session, MockTransport],
) -> None:
    session, _transport = connected
    session.scan_start(value=100)
    assert session.scan_count() == 3
    # First Scan starts a new hunt (does not open a second :744 session).
    assert session.scan_start(value=200) == 1
    assert session.scan_count() == 1
    with pytest.raises(ScanActive):
        session.attach_target(MockTransport.SHELL_PID)
    session.attach_target(MockTransport.EBOOT_PID)
    session.scan_undo()
    with pytest.raises(NoScan):
        session.scan_count()


def test_disconnect_ends_scan(connected: tuple[Session, MockTransport]) -> None:
    session, _transport = connected
    session.scan_start(value=100)
    session.disconnect()
    session.connect("mock")
    with pytest.raises(NoScan):
        session.scan_count()


def test_classify_maps_skips_executable() -> None:
    maps = [
        MemoryMap(name="rx", start=0, end=0x1000, offset=0, prot=5),
        MemoryMap(name="rw", start=0x1000, end=0x2000, offset=0, prot=3),
        MemoryMap(name="rwx", start=0x2000, end=0x3000, offset=0, prot=7),
        MemoryMap(name="SceGnm", start=0x3000, end=0x4000, offset=0, prot=3),
    ]
    assert classify_maps(maps) == [(0x1000, 0x2000)]


def test_classify_turbo_regions_skips_uncached_and_exec() -> None:
    regions = [
        ScanRegion(start=0, end=0x1000, prot=5, uncached=False),
        ScanRegion(start=0x1000, end=0x2000, prot=3, uncached=False),
        ScanRegion(start=0x2000, end=0x3000, prot=3, uncached=True),
        ScanRegion(start=0x3000, end=0x4000, prot=7, uncached=False),
    ]
    assert classify_turbo_regions(regions) == [(0x1000, 0x2000)]


def test_snapshot_fits_bitmap_cap() -> None:
    small = [(0, 4096)]
    assert snapshot_fits(small)
    # 448 MiB bitmap ≈ 14.3 GiB of u32-aligned slots — one oversized segment.
    huge = [(0, 16 * 1024 * 1024 * 1024)]
    assert snapshot_bitmap_bytes(huge) > SNAP_BITMAP_MAX
    assert not snapshot_fits(huge)


def test_turbo_start_flags_aliasing_not_parallel() -> None:
    from nitepr5_core.constants import (
        TS_PARALLEL_COMPARE,
        TS_SERVER_RESIDENT,
        TS_SNAPSHOT,
        TS_SNAPSHOT_SEGMENTS,
        TS_USE_ALIASING,
    )

    flags = turbo_start_flags(snapshot=False, use_aliasing=True)
    assert flags & TS_USE_ALIASING
    assert flags & TS_SERVER_RESIDENT
    assert flags & TS_SNAPSHOT_SEGMENTS
    assert not (flags & TS_SNAPSHOT)
    assert not (flags & TS_PARALLEL_COMPARE)

    snap = turbo_start_flags(snapshot=True, use_aliasing=True)
    assert snap & TS_SNAPSHOT
    assert snap & TS_USE_ALIASING


def test_resident_overflow_uses_snapshot_then_exact(
    connected: tuple[Session, MockTransport],
) -> None:
    session, transport = connected
    transport.decline_resident = True
    count = session.scan_start(value=100)
    assert count == 3
    addrs = {h.addr for h in session.scan_results(256)}
    assert addrs == {
        MockTransport.WRITABLE_EBOOT_ADDR,
        MockTransport.HEAP_A,
        MockTransport.HEAP_C,
    }
    assert transport.iterative_start_calls == 0


def test_resident_and_snapshot_decline_does_not_stream_hits(
    connected: tuple[Session, MockTransport],
) -> None:
    session, transport = connected
    transport.decline_resident = True
    transport.decline_snapshot = True
    with pytest.raises(ScanUnsupported, match="snapshot"):
        session.scan_start(value=100)
    assert transport.iterative_start_calls == 0


def test_region_probe_failure_falls_back_to_maps(
    connected: tuple[Session, MockTransport],
) -> None:
    session, transport = connected
    transport.fail_regions_probe = True
    count = session.scan_start(value=100)
    assert count == 3
    addrs = {h.addr for h in session.scan_results(256)}
    assert MockTransport.GPU_ADDR not in addrs
    assert transport.regions_probe_count == 1


def test_no_pt_attach_or_debug_session_or_dump_scan() -> None:
    for cls in (MockTransport, Ps5dbgTransport, Session):
        for name in _FORBIDDEN_ATTACH:
            assert not hasattr(cls, name), f"{cls.__name__} must not expose {name}"

    src = "".join(
        inspect.getsource(fn)
        for fn in (
            Session.scan_start,
            Session.scan_next,
            Session.scan_undo,
            Session.scan_count,
            Session.scan_results,
            Ps5dbgTransport.scan_start_turbo,
            Ps5dbgTransport.scan_get_resident,
            Ps5dbgTransport.scan_start_iterative,
        )
    )
    assert "debug_session" not in src
    assert "ptrace" not in src.lower()
    assert "scan_aob_find_all" not in src
    assert "scan_aob" not in src
    assert "PT_ATTACH" not in src


def test_scan_wait_drops_recv_timeout_then_restores() -> None:
    from nitepr5_core.constants import CONNECT_TIMEOUT, SCAN_IO_TIMEOUT

    class _Sock:
        def __init__(self) -> None:
            self._timeout: float | None = CONNECT_TIMEOUT

        def gettimeout(self) -> float | None:
            return self._timeout

        def settimeout(self, timeout: float | None) -> None:
            self._timeout = timeout

    class _Conn:
        def __init__(self) -> None:
            self._sock = _Sock()

    class _Ps5:
        def __init__(self) -> None:
            self.connection = _Conn()

    transport = Ps5dbgTransport()
    transport._ps5 = _Ps5()
    sock = transport._ps5.connection._sock
    assert sock.gettimeout() == CONNECT_TIMEOUT
    assert SCAN_IO_TIMEOUT is None
    with transport.scan_wait():
        assert sock.gettimeout() is None
    assert sock.gettimeout() == CONNECT_TIMEOUT


def test_read_fails_fast_when_scan_busy(
    connected: tuple[Session, MockTransport],
) -> None:
    session, _transport = connected
    session._begin_busy()
    try:
        with pytest.raises(ScanActive):
            session.read(MockTransport.EBOOT_PID, MockTransport.RAM_BASE, 16)
    finally:
        session._end_busy()
    peephole = session.read(MockTransport.EBOOT_PID, MockTransport.RAM_BASE, 16)
    assert len(peephole) == 16
