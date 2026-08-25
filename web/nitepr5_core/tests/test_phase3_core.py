"""Phase 3 session tests: write, watch, freeze, GoldHEN JSON (MockTransport)."""

from __future__ import annotations

import json
import struct
import sys
import threading
import time
from pathlib import Path

import pytest

_WEB = Path(__file__).resolve().parents[2]
if str(_WEB) not in sys.path:
    sys.path.insert(0, str(_WEB))

from nitepr5_core import (  # noqa: E402
    FREEZE_MAX,
    READ_MAX,
    WATCH_MAX,
    WRITE_MAX,
    FreezeLimit,
    InvalidCheat,
    InvalidFreezeSize,
    InvalidWatchSize,
    InvalidWriteSize,
    MemoryMap,
    MockTransport,
    NoCheat,
    NoFreeze,
    NoMod,
    NoWatch,
    NotConnected,
    ScanActive,
    Session,
    WatchLimit,
    WriteTooLarge,
)
from nitepr5_core.cheat import (  # noqa: E402
    cheat_file_from_freezes,
    map_belongs_to_process,
    module_base_from_maps,
)
from nitepr5_core.types import FreezeEntry  # noqa: E402

_PATCH_OFFSET = MockTransport.WRITABLE_EBOOT_ADDR - MockTransport.RAM_BASE
_SAMPLE_CHEAT = {
    "name": "Game Title",
    "id": "CUSA00004",
    "version": "01.07",
    "process": "eboot.bin",
    "mods": [
        {
            "name": "Example",
            "description": "",
            "type": "checkbox",
            "memory": [
                {
                    "offset": f"{_PATCH_OFFSET:x}",
                    "on": "01000000",
                    "off": "00000000",
                }
            ],
        }
    ],
    "credits": ["NitePR5"],
}


@pytest.fixture
def connected() -> tuple[Session, MockTransport]:
    transport = MockTransport()
    session = Session(transport)
    session.connect("mock")
    session.attach_target(MockTransport.EBOOT_PID)
    yield session, transport
    session.disconnect()


def test_write_then_read_round_trip(
    connected: tuple[Session, MockTransport],
) -> None:
    session, transport = connected
    payload = b"\xaa\xbb\xcc\xdd"
    session.write(None, MockTransport.WRITABLE_EBOOT_ADDR, payload)
    assert session.read(None, MockTransport.WRITABLE_EBOOT_ADDR, 4) == payload
    assert transport.write_calls[-1] == (
        MockTransport.EBOOT_PID,
        MockTransport.WRITABLE_EBOOT_ADDR,
        payload,
    )


def test_write_rejects_empty_and_too_large(
    connected: tuple[Session, MockTransport],
) -> None:
    session, _transport = connected
    with pytest.raises(WriteTooLarge):
        session.write(None, MockTransport.WRITABLE_EBOOT_ADDR, b"\x00" * (READ_MAX + 1))
    with pytest.raises(WriteTooLarge):
        session.write(None, MockTransport.WRITABLE_EBOOT_ADDR, b"\x00" * (WRITE_MAX + 1))
    with pytest.raises(InvalidWriteSize):
        session.write(None, MockTransport.WRITABLE_EBOOT_ADDR, b"")


def test_watch_add_64_ok_65th_raises(
    connected: tuple[Session, MockTransport],
) -> None:
    session, _transport = connected
    for i in range(WATCH_MAX):
        entry = session.watch_add(addr=MockTransport.WRITABLE_EBOOT_ADDR, n=4)
        assert entry.id == i + 1
    with pytest.raises(WatchLimit):
        session.watch_add(addr=MockTransport.HEAP_A, n=4)


def test_watch_poll_sees_planted_then_written(
    connected: tuple[Session, MockTransport],
) -> None:
    session, _transport = connected
    entry = session.watch_add(
        addr=MockTransport.WRITABLE_EBOOT_ADDR, n=4, label="score"
    )
    dup = session.watch_add(addr=MockTransport.WRITABLE_EBOOT_ADDR, n=4)
    assert dup.id != entry.id
    values = session.watch_poll()
    assert len(values) == 2
    planted = MockTransport.PLANTED_U32.to_bytes(4, "little")
    assert values[0].id == entry.id
    assert values[0].addr == MockTransport.WRITABLE_EBOOT_ADDR
    assert values[0].data == planted
    session.write(None, MockTransport.WRITABLE_EBOOT_ADDR, b"\x05\x00\x00\x00")
    refreshed = session.watch_poll()
    assert refreshed[0].data == b"\x05\x00\x00\x00"
    assert refreshed[1].data == b"\x05\x00\x00\x00"


def test_watch_remove_missing_raises(
    connected: tuple[Session, MockTransport],
) -> None:
    session, _transport = connected
    entry = session.watch_add(addr=MockTransport.HEAP_A, n=4)
    session.watch_remove(entry.id)
    with pytest.raises(NoWatch):
        session.watch_remove(entry.id)
    assert session.watch_poll() == []


def test_watch_rejects_bad_size(
    connected: tuple[Session, MockTransport],
) -> None:
    session, _transport = connected
    with pytest.raises(InvalidWatchSize):
        session.watch_add(addr=MockTransport.HEAP_A, n=3)


def test_freeze_add_32_ok_33rd_raises(
    connected: tuple[Session, MockTransport],
) -> None:
    session, _transport = connected
    patch = b"\x01\x00\x00\x00"
    for i in range(FREEZE_MAX):
        entry = session.freeze_add(addr=MockTransport.HEAP_A, data=patch)
        assert entry.id == i + 1
    with pytest.raises(FreezeLimit):
        session.freeze_add(addr=MockTransport.HEAP_C, data=patch)


def test_freeze_tick_writes_frozen_bytes(
    connected: tuple[Session, MockTransport],
) -> None:
    session, transport = connected
    frozen = b"\x02\x00\x00\x00"
    session.freeze_add(addr=MockTransport.WRITABLE_EBOOT_ADDR, data=frozen)
    assert session.freeze_tick() == 1
    assert session.read(None, MockTransport.WRITABLE_EBOOT_ADDR, 4) == frozen
    transport.poke_u32(MockTransport.WRITABLE_EBOOT_ADDR, 99)
    assert session.freeze_tick() == 1
    assert session.read(None, MockTransport.WRITABLE_EBOOT_ADDR, 4) == frozen


def test_freeze_remove_missing_and_bad_size(
    connected: tuple[Session, MockTransport],
) -> None:
    session, _transport = connected
    with pytest.raises(InvalidFreezeSize):
        session.freeze_add(addr=MockTransport.HEAP_A, data=b"")
    with pytest.raises(InvalidFreezeSize):
        session.freeze_add(addr=MockTransport.HEAP_A, data=b"\x00" * 9)
    entry = session.freeze_add(addr=MockTransport.HEAP_A, data=b"\xff")
    session.freeze_remove(entry.id)
    with pytest.raises(NoFreeze):
        session.freeze_remove(entry.id)
    assert session.freeze_tick() == 0


def test_write_watch_freeze_fail_fast_when_scan_busy(
    connected: tuple[Session, MockTransport],
) -> None:
    session, _transport = connected
    session.watch_add(addr=MockTransport.WRITABLE_EBOOT_ADDR, n=4)
    session.freeze_add(addr=MockTransport.HEAP_A, data=b"\x01\x00\x00\x00")
    session._begin_busy()
    try:
        with pytest.raises(ScanActive):
            session.write(None, MockTransport.WRITABLE_EBOOT_ADDR, b"\x00\x00\x00\x00")
        with pytest.raises(ScanActive):
            session.watch_poll()
        with pytest.raises(ScanActive):
            session.freeze_tick()
        with pytest.raises(ScanActive):
            session.read(None, MockTransport.WRITABLE_EBOOT_ADDR, 4)
    finally:
        session._end_busy()
    session.write(None, MockTransport.WRITABLE_EBOOT_ADDR, b"\x11\x00\x00\x00")
    assert session.watch_poll()[0].data == b"\x11\x00\x00\x00"
    assert session.freeze_tick() == 1


def test_watch_freeze_serialize_with_hex_peer_not_scanactive(
    connected: tuple[Session, MockTransport],
) -> None:
    """Hex / watch / freeze share :744. Overlap must wait, not 400 ScanActive."""
    session, _transport = connected
    session.watch_add(addr=MockTransport.WRITABLE_EBOOT_ADDR, n=4)
    session.freeze_add(addr=MockTransport.HEAP_A, data=b"\x01\x00\x00\x00")
    errors: list[BaseException] = []

    def run_read() -> None:
        try:
            for _ in range(40):
                session.read(None, MockTransport.WRITABLE_EBOOT_ADDR, 16)
        except BaseException as exc:
            errors.append(exc)

    def run_watch() -> None:
        try:
            for _ in range(40):
                session.watch_poll()
        except BaseException as exc:
            errors.append(exc)

    def run_freeze() -> None:
        try:
            for _ in range(40):
                session.freeze_tick()
        except BaseException as exc:
            errors.append(exc)

    threads = [
        threading.Thread(target=run_read),
        threading.Thread(target=run_watch),
        threading.Thread(target=run_freeze),
    ]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join(timeout=10)
        assert not thread.is_alive()
    assert errors == []


def test_write_waits_for_peer_hold_longer_than_quarter_second(
    connected: tuple[Session, MockTransport],
) -> None:
    """A poke must not 400 just because hex/freeze held :744 for >250 ms."""
    session, _transport = connected
    started = threading.Event()
    release = threading.Event()

    def holder() -> None:
        with session._hold_io():
            started.set()
            release.wait(timeout=2)

    thread = threading.Thread(target=holder)
    thread.start()
    assert started.wait(timeout=2)
    time.sleep(0.35)
    session.write(None, MockTransport.WRITABLE_EBOOT_ADDR, b"\xab\x00\x00\x00")
    release.set()
    thread.join(timeout=2)
    assert session.read(None, MockTransport.WRITABLE_EBOOT_ADDR, 4) == b"\xab\x00\x00\x00"


def test_dropped_console_io_disconnects_so_later_calls_are_409(
    connected: tuple[Session, MockTransport],
) -> None:
    session, transport = connected
    session.watch_add(addr=MockTransport.WRITABLE_EBOOT_ADDR, n=4)
    session.freeze_add(addr=MockTransport.HEAP_A, data=b"\x01\x00\x00\x00")
    transport.drop_io = True
    with pytest.raises(NotConnected):
        session.read(None, MockTransport.WRITABLE_EBOOT_ADDR, 4)
    assert session.connected is False
    with pytest.raises(NotConnected):
        session.write(None, MockTransport.WRITABLE_EBOOT_ADDR, b"\x01\x00\x00\x00")
    with pytest.raises(NotConnected):
        session.watch_poll()
    with pytest.raises(NotConnected):
        session.freeze_tick()


def test_cheat_load_toggle_save_round_trip(
    connected: tuple[Session, MockTransport],
    tmp_path: Path,
) -> None:
    session, transport = connected
    src = tmp_path / "CUSA00004_01.07.json"
    src.write_text(json.dumps(_SAMPLE_CHEAT), encoding="utf-8")
    loaded = session.cheat_load(src)
    assert loaded.id == "CUSA00004"
    assert loaded.process == "eboot.bin"
    assert loaded.mods[0].name == "Example"
    assert loaded.mods[0].memory[0].on == b"\x01\x00\x00\x00"

    session.cheat_toggle("Example", True)
    assert session.read(None, MockTransport.WRITABLE_EBOOT_ADDR, 4) == b"\x01\x00\x00\x00"
    assert transport.write_calls[-1][1] == MockTransport.WRITABLE_EBOOT_ADDR

    session.cheat_toggle("Example", False)
    assert session.read(None, MockTransport.WRITABLE_EBOOT_ADDR, 4) == b"\x00\x00\x00\x00"

    dest = tmp_path / "roundtrip.json"
    session.cheat_save(dest)
    saved = json.loads(dest.read_text(encoding="utf-8"))
    assert set(saved) == {"name", "id", "version", "process", "mods", "credits"}
    assert "enabled" not in saved
    assert "enabled" not in saved["mods"][0]
    assert saved["mods"][0]["memory"][0]["on"] == "01000000"
    assert saved["mods"][0]["memory"][0]["off"] == "00000000"
    assert saved["mods"][0]["memory"][0]["offset"] == f"{_PATCH_OFFSET:x}"
    assert saved["credits"] == ["NitePR5"]
    reloaded = session.cheat_load(dest)
    assert reloaded.mods[0].memory[0].on == loaded.mods[0].memory[0].on


def test_cheat_errors(
    connected: tuple[Session, MockTransport],
    tmp_path: Path,
) -> None:
    session, _transport = connected
    with pytest.raises(NoCheat):
        session.cheat_toggle("Example", True)
    with pytest.raises(NoCheat):
        session.cheat_save(tmp_path / "missing.json")
    bad = tmp_path / "bad.json"
    bad.write_text("[]", encoding="utf-8")
    with pytest.raises(InvalidCheat):
        session.cheat_load(bad)
    src = tmp_path / "ok.json"
    src.write_text(json.dumps(_SAMPLE_CHEAT), encoding="utf-8")
    session.cheat_load(src)
    with pytest.raises(NoMod):
        session.cheat_toggle("No Such Mod", True)


def test_disconnect_clears_watches_and_freezes(
    connected: tuple[Session, MockTransport],
) -> None:
    session, _transport = connected
    session.watch_add(addr=MockTransport.HEAP_A, n=4)
    session.freeze_add(addr=MockTransport.HEAP_C, data=b"\x01\x00\x00\x00")
    session.disconnect()
    session.connect("mock")
    session.attach_target(MockTransport.EBOOT_PID)
    assert session.watch_poll() == []
    assert session.freeze_tick() == 0
    again = session.watch_add(addr=MockTransport.HEAP_A, n=4)
    assert again.id == 1


def test_retarget_clears_watches_and_freezes(
    connected: tuple[Session, MockTransport],
) -> None:
    session, _transport = connected
    session.watch_add(addr=MockTransport.HEAP_A, n=4)
    session.freeze_add(addr=MockTransport.HEAP_C, data=b"\x01\x00\x00\x00")
    session.attach_target(MockTransport.SHELL_PID)
    session.attach_target(MockTransport.EBOOT_PID)
    assert session.watch_poll() == []
    assert session.freeze_tick() == 0


def test_map_belongs_to_process_accepts_executable_and_path() -> None:
    assert map_belongs_to_process("eboot.bin", "eboot.bin")
    assert map_belongs_to_process("executable", "eboot.bin")
    assert map_belongs_to_process("Executable", "eboot.bin")
    assert map_belongs_to_process("/app0/eboot.bin", "eboot.bin")
    assert not map_belongs_to_process("heap", "eboot.bin")
    assert not map_belongs_to_process("libc.prx", "eboot.bin")
    assert map_belongs_to_process("libc.prx", "libc.prx")


def test_module_base_from_maps_uses_lowest_executable_start() -> None:
    rows = [
        MemoryMap(name="executable", start=0x1BC0000, end=0x1BF8000, offset=0, prot=3),
        MemoryMap(name="executable", start=0x400000, end=0x1BC0000, offset=0, prot=5),
        MemoryMap(name="heap", start=0x2E7C4E5B8, end=0x2E8C4E5B8, offset=0, prot=3),
    ]
    assert module_base_from_maps(rows, "eboot.bin") == 0x400000
    assert module_base_from_maps(rows, "missing.bin") is None


def _plant_executable_maps(
    session: Session, transport: MockTransport
) -> None:
    """CUSA13762-shaped names: ELF rows are 'executable', not 'eboot.bin'."""
    renamed: list[MemoryMap] = []
    for row in transport._maps[MockTransport.EBOOT_PID]:
        name = "executable" if row.name == "eboot.bin" else row.name
        renamed.append(
            MemoryMap(
                name=name,
                start=row.start,
                end=row.end,
                offset=row.offset,
                prot=row.prot,
            )
        )
    transport._maps[MockTransport.EBOOT_PID] = renamed
    session._maps_cache.clear()


def test_module_base_accepts_executable_map_name(
    connected: tuple[Session, MockTransport],
) -> None:
    session, transport = connected
    _plant_executable_maps(session, transport)
    assert session._module_base(MockTransport.EBOOT_PID, "eboot.bin") == MockTransport.RAM_BASE


def test_cheat_from_freezes_with_executable_maps(
    connected: tuple[Session, MockTransport],
    tmp_path: Path,
) -> None:
    session, transport = connected
    _plant_executable_maps(session, transport)
    frozen = b"\x02\x00\x00\x00"
    session.freeze_add(addr=MockTransport.WRITABLE_EBOOT_ADDR, data=frozen)
    session.freeze_add(addr=MockTransport.HEAP_A, data=b"\x64\x00\x00\x00")
    cheat = session.cheat_from_freezes(
        name="The Golf Club 2019",
        title_id="CUSA13762",
        version="01.02",
    )
    assert cheat.process == "eboot.bin"
    assert len(cheat.mods) == 2
    eboot_off = MockTransport.WRITABLE_EBOOT_ADDR - MockTransport.RAM_BASE
    heap_off = MockTransport.HEAP_A - MockTransport.RAM_BASE
    assert cheat.mods[0].memory[0].offset == format(eboot_off, "x")
    assert cheat.mods[0].memory[0].on == frozen
    assert cheat.mods[0].memory[0].off == b"\x00\x00\x00\x00"
    assert cheat.mods[1].memory[0].offset == format(heap_off, "x")
    dest = tmp_path / "CUSA13762_01.02.json"
    session.cheat_save(dest, cheat)
    loaded = session.cheat_load(dest)
    session.cheat_toggle(loaded.mods[0].name, True)
    assert session.read(None, MockTransport.WRITABLE_EBOOT_ADDR, 4) == frozen


def test_cheat_from_freezes_refuses_without_module_map(
    connected: tuple[Session, MockTransport],
) -> None:
    session, transport = connected
    transport._maps[MockTransport.EBOOT_PID] = [
        MemoryMap(
            name="heap",
            start=MockTransport.HEAP_A,
            end=MockTransport.HEAP_END,
            offset=0,
            prot=3,
        )
    ]
    session._maps_cache.clear()
    session.freeze_add(addr=MockTransport.HEAP_A, data=b"\x01\x00\x00\x00")
    with pytest.raises(InvalidCheat, match="executable"):
        session.cheat_from_freezes(name="Game", title_id="CUSA00000", version="00.00")


def test_cheat_from_freezes_refuses_empty_list(
    connected: tuple[Session, MockTransport],
) -> None:
    session, _transport = connected
    with pytest.raises(InvalidCheat, match="no freezes"):
        session.cheat_from_freezes(name="Game", title_id="CUSA00000", version="00.00")


def test_cheat_file_from_freezes_skips_addresses_below_base() -> None:
    entry = FreezeEntry(
        id=1,
        pid=1,
        addr=0x1000,
        data=b"\x01\x00\x00\x00",
    )
    with pytest.raises(InvalidCheat, match="inside"):
        cheat_file_from_freezes(
            [entry],
            base=0x2000,
            name="Game",
            title_id="CUSA00000",
            version="00.00",
        )


def test_proc_write_phased_sends_16_byte_packet_then_payload() -> None:
    """ps5dbg 0.1.1 embeds payload in datalen; the server then hangs on phase 2."""
    from ps5dbg.constants import Cmd, Status

    from nitepr5_core.transport import _PROC_WRITE_PACKET, proc_write_phased

    class _Conn:
        def __init__(self) -> None:
            self.requests: list[tuple[int, bytes]] = []
            self.raw: list[bytes] = []

        def send_request(self, cmd: int, body: bytes = b"") -> None:
            self.requests.append((int(cmd), bytes(body)))

        def _sendall(self, data: bytes) -> None:
            self.raw.append(bytes(data))

        def recv_exact(self, n: int) -> bytes:
            assert n == 4
            return struct.pack("<I", int(Status.SUCCESS))

    conn = _Conn()
    payload = b"\xaa\xbb\xcc\xdd"
    pid = 122
    addr = 12_478_130_028
    proc_write_phased(conn, pid, addr, payload)
    assert len(conn.requests) == 1
    cmd, body = conn.requests[0]
    assert cmd == int(Cmd.PROC_WRITE)
    assert body == struct.pack("<IQI", pid, addr, len(payload))
    assert len(body) == _PROC_WRITE_PACKET
    assert payload not in body
    assert conn.raw == [payload]
