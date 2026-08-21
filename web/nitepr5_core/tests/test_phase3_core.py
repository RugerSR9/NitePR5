"""Phase 3 session tests: write, watch, freeze, GoldHEN JSON (MockTransport)."""

from __future__ import annotations

import json
import sys
import threading
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
    MockTransport,
    NoCheat,
    NoFreeze,
    NoMod,
    NoWatch,
    ScanActive,
    Session,
    WatchLimit,
    WriteTooLarge,
)

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


def test_write_watch_freeze_fail_fast_when_console_io_held(
    connected: tuple[Session, MockTransport],
) -> None:
    session, _transport = connected
    session.watch_add(addr=MockTransport.WRITABLE_EBOOT_ADDR, n=4)
    session.freeze_add(addr=MockTransport.HEAP_A, data=b"\x01\x00\x00\x00")
    barrier = threading.Barrier(2)

    def holder() -> None:
        session._io.acquire()
        try:
            barrier.wait()
            barrier.wait()
        finally:
            session._io.release()

    thread = threading.Thread(target=holder)
    thread.start()
    barrier.wait()
    try:
        with pytest.raises(ScanActive):
            session.write(None, MockTransport.WRITABLE_EBOOT_ADDR, b"\x00\x00\x00\x00")
        with pytest.raises(ScanActive):
            session.watch_poll()
        with pytest.raises(ScanActive):
            session.freeze_tick()
    finally:
        barrier.wait()
        thread.join()
    session.write(None, MockTransport.WRITABLE_EBOOT_ADDR, b"\x11\x00\x00\x00")
    assert session.watch_poll()[0].data == b"\x11\x00\x00\x00"
    assert session.freeze_tick() == 1


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
