"""Phase 4 session tests: plugin freeze handoff (MockTransport + local HTTP mock).

When the plugin is armed it owns freeze. Session.freeze_tick() must not write
via :744. Disarm restores PC ticks. Caps stay FreezeLimit at 32. The plugin
client is HTTP to :1745, not a second PS5Debug connect().
"""

from __future__ import annotations

import inspect
import json
import socket
import sys
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from typing import Any

import pytest

_WEB = Path(__file__).resolve().parents[2]
if str(_WEB) not in sys.path:
    sys.path.insert(0, str(_WEB))

from nitepr5_core import (  # noqa: E402
    FREEZE_MAX,
    FreezeLimit,
    MockTransport,
    Session,
)

try:
    from nitepr5_core.constants import PLUGIN_PORT as _PLUGIN_PORT
except ImportError:  # sibling may land PLUGIN_PORT with the web bind
    _PLUGIN_PORT = 1745

_PATCH = b"\x01\x00\x00\x00"


@pytest.fixture
def connected() -> tuple[Session, MockTransport]:
    transport = MockTransport()
    session = Session(transport)
    # Local mock plugin listens on 127.0.0.1 — not a real PS5.
    session.connect("127.0.0.1")
    session.attach_target(MockTransport.EBOOT_PID)
    yield session, transport
    session.disconnect()


def _is_armed(session: Session) -> bool:
    armed = session.plugin_armed
    if callable(armed):
        return bool(armed())
    return bool(armed)


def _plugin_call(session: Session, name: str, **want: Any) -> Any:
    fn = getattr(session, name)
    try:
        params = inspect.signature(fn).parameters
    except (TypeError, ValueError):
        return fn()
    has_var_kw = any(
        p.kind is inspect.Parameter.VAR_KEYWORD for p in params.values()
    )
    kwargs = {}
    for key, val in want.items():
        if val is None:
            continue
        if has_var_kw or key in params:
            kwargs[key] = val
    return fn(**kwargs)


def _start_mock_plugin(host: str = "127.0.0.1", port: int = 0):
    state: dict[str, Any] = {
        "armed": False,
        "pid": 0,
        "freeze_count": 0,
        "cheat_id": "",
        "enabled": [],
        "dbg": True,
        "arm_posts": 0,
        "disarm_posts": 0,
        "status_gets": 0,
    }

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt: str, *args: object) -> None:
            return

        def _read_json(self) -> dict:
            n = int(self.headers.get("Content-Length") or 0)
            if n <= 0:
                return {}
            raw = self.rfile.read(n)
            if not raw:
                return {}
            try:
                parsed = json.loads(raw.decode())
            except (UnicodeDecodeError, json.JSONDecodeError):
                return {}
            return parsed if isinstance(parsed, dict) else {}

        def _send(self, obj: dict, code: int = 200) -> None:
            body = json.dumps(obj).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:
            path = self.path.split("?", 1)[0]
            if path == "/status":
                state["status_gets"] += 1
                self._send(
                    {
                        "ok": True,
                        "armed": state["armed"],
                        "pid": state["pid"],
                        "freeze_count": state["freeze_count"],
                        "cheat_id": state["cheat_id"],
                        "enabled": list(state["enabled"]),
                        "dbg": state["dbg"],
                    }
                )
                return
            self._send({"ok": False}, 404)

        def do_POST(self) -> None:
            path = self.path.split("?", 1)[0]
            body = self._read_json()
            if path == "/arm":
                state["arm_posts"] += 1
                state["armed"] = True
                state["pid"] = int(body.get("pid") or 0)
                freezes = body.get("freezes") or []
                state["freeze_count"] = len(freezes) if isinstance(freezes, list) else 0
                cheat = body.get("cheat")
                if isinstance(cheat, dict):
                    state["cheat_id"] = str(cheat.get("id") or "")
                if "enabled" in body:
                    enabled = body.get("enabled") or []
                    state["enabled"] = list(enabled) if isinstance(enabled, list) else []
                self._send(
                    {
                        "ok": True,
                        "armed": True,
                        "freeze_count": state["freeze_count"],
                    }
                )
                return
            if path == "/disarm":
                state["disarm_posts"] += 1
                state["armed"] = False
                self._send({"ok": True, "armed": False})
                return
            self._send({"ok": False}, 404)

    httpd = HTTPServer((host, port), Handler)
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    bound = int(httpd.server_address[1])
    return httpd, state, bound


def _supports_plugin_kw(session: Session, method: str, name: str) -> bool:
    fn = getattr(session, method)
    try:
        params = inspect.signature(fn).parameters
    except (TypeError, ValueError):
        return False
    if name in params:
        return True
    return any(p.kind is inspect.Parameter.VAR_KEYWORD for p in params.values())


def _start_plugin_for(session: Session):
    """Bind an ephemeral port when Session accepts port=; else PLUGIN_PORT 1745."""
    if _supports_plugin_kw(session, "plugin_arm", "port"):
        httpd, state, port = _start_mock_plugin("127.0.0.1", 0)
        return httpd, state, port
    try:
        httpd, state, port = _start_mock_plugin("127.0.0.1", int(_PLUGIN_PORT))
    except OSError:
        pytest.skip(
            f"127.0.0.1:{_PLUGIN_PORT} is busy and Session.plugin_arm has no "
            "port= override; live-socket plugin test skipped"
        )
    return httpd, state, port


def _arm_kwargs(session: Session, port: int) -> dict[str, Any]:
    want: dict[str, Any] = {}
    if _supports_plugin_kw(session, "plugin_arm", "host"):
        want["host"] = "127.0.0.1"
    if _supports_plugin_kw(session, "plugin_arm", "port"):
        want["port"] = port
    return want


def _status_kwargs(session: Session, port: int) -> dict[str, Any]:
    want: dict[str, Any] = {}
    if _supports_plugin_kw(session, "plugin_status", "host"):
        want["host"] = "127.0.0.1"
    if _supports_plugin_kw(session, "plugin_status", "port"):
        want["port"] = port
    return want


def _disarm_kwargs(session: Session, port: int) -> dict[str, Any]:
    want: dict[str, Any] = {}
    if _supports_plugin_kw(session, "plugin_disarm", "host"):
        want["host"] = "127.0.0.1"
    if _supports_plugin_kw(session, "plugin_disarm", "port"):
        want["port"] = port
    return want


def test_session_exposes_plugin_arm_disarm_status_armed() -> None:
    assert hasattr(Session, "plugin_arm")
    assert hasattr(Session, "plugin_disarm")
    assert hasattr(Session, "plugin_status")
    assert hasattr(Session, "plugin_armed")
    assert callable(Session.plugin_arm)
    assert callable(Session.plugin_disarm)
    assert callable(Session.plugin_status)


def test_33rd_freeze_still_raises_freezelimit(
    connected: tuple[Session, MockTransport],
) -> None:
    """Caps refuse; Session still raises FreezeLimit on the 33rd pin."""
    session, _transport = connected
    for _i in range(FREEZE_MAX):
        session.freeze_add(addr=MockTransport.HEAP_A, data=_PATCH)
    with pytest.raises(FreezeLimit):
        session.freeze_add(addr=MockTransport.HEAP_C, data=_PATCH)


def test_plugin_arm_freeze_tick_is_noop_no_744_write(
    connected: tuple[Session, MockTransport],
) -> None:
    session, transport = connected
    session.freeze_add(addr=MockTransport.WRITABLE_EBOOT_ADDR, data=_PATCH)
    written = session.freeze_tick()
    assert written == 1
    n_writes = len(transport.write_calls)
    assert n_writes >= 1

    httpd, state, port = _start_plugin_for(session)
    try:
        _plugin_call(session, "plugin_arm", **_arm_kwargs(session, port))
        assert state["arm_posts"] >= 1
        assert _is_armed(session) is True
        assert session.freeze_tick() == 0
        assert len(transport.write_calls) == n_writes
    finally:
        httpd.shutdown()
        httpd.server_close()


def test_plugin_disarm_freeze_tick_writes_again(
    connected: tuple[Session, MockTransport],
) -> None:
    session, transport = connected
    session.freeze_add(addr=MockTransport.WRITABLE_EBOOT_ADDR, data=_PATCH)
    httpd, state, port = _start_plugin_for(session)
    try:
        _plugin_call(session, "plugin_arm", **_arm_kwargs(session, port))
        n_writes = len(transport.write_calls)
        assert session.freeze_tick() == 0
        assert len(transport.write_calls) == n_writes

        _plugin_call(session, "plugin_disarm", **_disarm_kwargs(session, port))
        assert state["disarm_posts"] >= 1
        assert _is_armed(session) is False
        assert session.freeze_tick() == 1
        assert len(transport.write_calls) == n_writes + 1
    finally:
        httpd.shutdown()
        httpd.server_close()


def test_plugin_disarm_after_disconnect_uses_saved_lan_host(
    connected: tuple[Session, MockTransport],
) -> None:
    """disconnect() must not auto-disarm; Disarm still hits :1745 via _plugin_host."""
    session, _transport = connected
    httpd, state, port = _start_plugin_for(session)
    try:
        _plugin_call(session, "plugin_arm", **_arm_kwargs(session, port))
        session.disconnect()
        assert _is_armed(session) is True
        session.plugin_disarm()
        assert state["disarm_posts"] >= 1
        assert _is_armed(session) is False
    finally:
        httpd.shutdown()
        httpd.server_close()


def test_plugin_arm_does_not_call_transport_connect(
    connected: tuple[Session, MockTransport],
) -> None:
    """Plugin client is HTTP to :1745. Arm must not open a second :744."""
    session, transport = connected
    session.freeze_add(addr=MockTransport.HEAP_A, data=_PATCH)
    extra: list[tuple[tuple, dict]] = []
    orig = transport.connect

    def _logged(*args: object, **kwargs: object) -> None:
        extra.append((args, kwargs))
        return orig(*args, **kwargs)

    transport.connect = _logged  # type: ignore[method-assign]
    httpd, state, port = _start_plugin_for(session)
    try:
        _plugin_call(session, "plugin_arm", **_arm_kwargs(session, port))
        assert extra == []
        assert state["arm_posts"] >= 1
        _plugin_call(session, "plugin_status", **_status_kwargs(session, port))
        assert extra == []
        _plugin_call(session, "plugin_disarm", **_disarm_kwargs(session, port))
        assert extra == []
    finally:
        transport.connect = orig  # type: ignore[method-assign]
        httpd.shutdown()
        httpd.server_close()


def test_plugin_arm_targets_injected_host_not_session_ps5_ip(
    connected: tuple[Session, MockTransport],
) -> None:
    """Unit tests inject 127.0.0.1. Default arm uses the connected PS5 host."""
    session, transport = connected
    if not _supports_plugin_kw(session, "plugin_arm", "host"):
        pytest.skip("plugin_arm has no host=; default is session.host:PLUGIN_PORT")
    session.disconnect()
    session.connect("192.168.4.42")
    session.attach_target(MockTransport.EBOOT_PID)
    session.freeze_add(addr=MockTransport.HEAP_A, data=_PATCH)
    httpd, state, port = _start_plugin_for(session)
    try:
        _plugin_call(
            session,
            "plugin_arm",
            host="127.0.0.1",
            port=port if _supports_plugin_kw(session, "plugin_arm", "port") else None,
        )
        assert state["arm_posts"] >= 1
        assert session.host == "192.168.4.42"
        assert transport.host == "192.168.4.42"
    finally:
        httpd.shutdown()
        httpd.server_close()


def test_plugin_unreachable_when_nothing_listens(
    connected: tuple[Session, MockTransport],
) -> None:
    from nitepr5_core import PluginUnreachable

    session, _transport = connected
    closed = _unused_local_port()
    kwargs: dict[str, Any] = {}
    if _supports_plugin_kw(session, "plugin_arm", "host"):
        kwargs["host"] = "127.0.0.1"
    if _supports_plugin_kw(session, "plugin_arm", "port"):
        kwargs["port"] = closed
    elif _port_is_open("127.0.0.1", int(_PLUGIN_PORT)):
        pytest.skip(
            f"127.0.0.1:{_PLUGIN_PORT} already accepts connections; "
            "cannot assert PluginUnreachable"
        )
    with pytest.raises(PluginUnreachable):
        _plugin_call(session, "plugin_arm", **kwargs)


def _unused_local_port() -> int:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(("127.0.0.1", 0))
    port = int(sock.getsockname()[1])
    sock.close()
    return port


def _port_is_open(host: str, port: int) -> bool:
    try:
        with socket.create_connection((host, port), timeout=0.2):
            return True
    except OSError:
        return False
