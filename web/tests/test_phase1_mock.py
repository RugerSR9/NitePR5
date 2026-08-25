"""Phase 1 mock-backend tests. Fail closed on peephole caps (ARCHITECTURE §5.3).

Session API used here: discover, connect, disconnect, processes, foreground,
attach_target (logical only), maps, read. No scan / write / freeze.
"""

from __future__ import annotations

import inspect
from pathlib import Path

import pytest

from nitepr5_core import (
    HEX_PEEPHOLE_DEFAULT,
    READ_MAX,
    ForegroundInfo,
    InvalidAddress,
    InvalidReadSize,
    MockTransport,
    NoTarget,
    NotConnected,
    ReadTooLarge,
    Session,
)

# Debugger APIs that must not exist on the mock (no PT_ATTACH / ptrace).
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


def test_hex_caps_are_fail_closed() -> None:
    assert HEX_PEEPHOLE_DEFAULT == 512
    assert READ_MAX == 4096


def test_read_512_and_4096_succeed_4097_and_0_fail(connected: tuple[Session, MockTransport]) -> None:
    session, _transport = connected
    session.attach_target(MockTransport.EBOOT_PID)
    addr = MockTransport.RAM_BASE

    peephole = session.read(MockTransport.EBOOT_PID, addr, 512)
    assert len(peephole) == 512

    at_cap = session.read(MockTransport.EBOOT_PID, addr, 4096)
    assert len(at_cap) == 4096

    with pytest.raises(ReadTooLarge):
        session.read(MockTransport.EBOOT_PID, addr, 4097)
    with pytest.raises(InvalidReadSize):
        session.read(MockTransport.EBOOT_PID, addr, 0)


def test_read_rejects_negative_and_oversize_addr(
    connected: tuple[Session, MockTransport],
) -> None:
    """PROC_READ packs address as u64; a JS 32-bit align used to send negatives."""
    session, _transport = connected
    session.attach_target(MockTransport.EBOOT_PID)
    with pytest.raises(InvalidAddress):
        session.read(None, -389259952, 512)
    with pytest.raises(InvalidAddress):
        session.read(None, 1 << 64, 16)
    with pytest.raises(InvalidAddress):
        session.read(None, True, 16)  # type: ignore[arg-type]
    data = session.read(None, MockTransport.RAM_BASE, 16)
    assert len(data) == 16
    assert MockTransport.RAM_BASE > 0xFFFFFFFF


def test_reconnect_clears_target_and_maps_cache(
    connected: tuple[Session, MockTransport],
) -> None:
    """Session.connect() disconnects first — same contract the UI must follow."""
    session, transport = connected
    session.attach_target(MockTransport.EBOOT_PID)
    session.maps()
    assert session.target_pid == MockTransport.EBOOT_PID
    assert transport.maps_fetch_count == 1

    session.connect("127.0.0.1")
    assert session.target_pid is None
    with pytest.raises(NoTarget):
        session.maps()
    assert transport.maps_fetch_count == 1


def test_maps_cached_until_refresh(connected: tuple[Session, MockTransport]) -> None:
    session, transport = connected
    session.attach_target(MockTransport.EBOOT_PID)

    first = session.maps()
    assert transport.maps_fetch_count == 1
    assert first

    second = session.maps()
    assert transport.maps_fetch_count == 1
    assert [m.start for m in first] == [m.start for m in second]

    session.maps(refresh=True)
    assert transport.maps_fetch_count == 2


def test_attach_target_is_logical_not_debugger_attach(
    connected: tuple[Session, MockTransport],
) -> None:
    session, transport = connected

    for name in _FORBIDDEN_ATTACH:
        assert not hasattr(transport, name), f"MockTransport must not expose {name}"
        assert name not in dir(transport)
        assert name not in dir(MockTransport)
        assert not hasattr(MockTransport, name)

    # Logical target only: do not call a debugger attach on the transport.
    body = inspect.getsource(Session.attach_target)
    assert "self._transport.attach" not in body
    assert "ptrace" not in body.lower()

    before = transport.maps_fetch_count
    session.attach_target(MockTransport.EBOOT_PID)
    assert session.target_pid == MockTransport.EBOOT_PID
    assert transport.maps_fetch_count == before


def test_not_connected_before_connect(session: Session) -> None:
    assert not session.connected
    with pytest.raises(NotConnected):
        session.processes()
    with pytest.raises(NotConnected):
        session.foreground()
    with pytest.raises(NotConnected):
        session.attach_target(MockTransport.EBOOT_PID)
    with pytest.raises(NotConnected):
        session.maps(MockTransport.EBOOT_PID)
    with pytest.raises(NotConnected):
        session.read(MockTransport.EBOOT_PID, MockTransport.RAM_BASE, 16)


def test_foreground_pid_zero_is_home(connected: tuple[Session, MockTransport]) -> None:
    session, transport = connected
    transport._foreground = ForegroundInfo(
        pid=0,
        name="",
        titleid="",
        contentid="",
        app_ver="",
    )
    fg = session.foreground()
    assert fg.pid == 0

    session.attach_target(0)
    assert session.target_pid == 0


def test_ui_connect_resets_attach_before_api_connect() -> None:
    """Reconnect must drop client attach/hex-poll before POST /api/connect."""
    js = Path(__file__).resolve().parents[1] / "static" / "app.js"
    text = js.read_text(encoding="utf-8")
    connect_at = text.find("async function onConnect")
    reset_at = text.find("resetTargetUi()", connect_at)
    api_at = text.find("/api/connect", connect_at)
    assert connect_at != -1
    assert 0 <= connect_at < reset_at < api_at

