"""Phase 4 HTTP + UI contract: plugin arm/disarm/status, freeze handoff.

Core covers Session.plugin_* in nitepr5_core/tests/test_phase4_plugin.py.
This module asserts FastAPI /api/plugin/* , PluginUnreachable → 503, and that
app.js skips /api/freeze/tick while state.pluginArmed.
"""

from __future__ import annotations

from collections.abc import Iterator
from pathlib import Path

import pytest

from http_phase4_plugin_contract import (
    PLUGIN_ARM_PATH,
    PLUGIN_DISARM_PATH,
    PLUGIN_HTTP_PATHS,
    PLUGIN_PORT,
    PLUGIN_STATUS_PATH,
    plugin_port_open,
    start_mock_plugin,
)
from nitepr5_core import MockTransport, Session

_JS = Path(__file__).resolve().parents[1] / "static" / "app.js"


def _route_paths(app: object) -> set[str]:
    return {getattr(r, "path", "") for r in getattr(app, "routes", ())}


@pytest.fixture
def client_and_session() -> Iterator[tuple[object, Session, MockTransport]]:
    app_mod = pytest.importorskip("app")
    pytest.importorskip("fastapi")
    if not hasattr(app_mod, "app"):
        pytest.skip("app.app not defined yet")
    try:
        from fastapi.testclient import TestClient
    except (ImportError, RuntimeError) as exc:
        pytest.skip(f"fastapi TestClient unavailable ({exc})")

    t = MockTransport()
    s = Session(t)
    s.connect("127.0.0.1")
    s.attach_target(MockTransport.EBOOT_PID)
    previous = app_mod.SESSION
    app_mod.SESSION = s
    try:
        yield TestClient(app_mod.app), s, t
    finally:
        s.disconnect()
        app_mod.SESSION = previous


def test_plugin_http_routes_exist(
    client_and_session: tuple[object, Session, MockTransport],
) -> None:
    client, _session, _transport = client_and_session
    paths = _route_paths(client.app)
    missing = [p for p in PLUGIN_HTTP_PATHS if p not in paths]
    assert not missing, "Phase 4 plugin HTTP routes missing: " + ", ".join(missing)


def test_plugin_http_unreachable_is_503(
    client_and_session: tuple[object, Session, MockTransport],
) -> None:
    client, _session, _transport = client_and_session
    paths = _route_paths(client.app)
    assert PLUGIN_ARM_PATH in paths
    assert PLUGIN_STATUS_PATH in paths
    if plugin_port_open("127.0.0.1", PLUGIN_PORT):
        pytest.skip(
            f"127.0.0.1:{PLUGIN_PORT} already accepts connections; "
            "cannot assert PluginUnreachable → 503"
        )

    armed = client.post(PLUGIN_ARM_PATH, json={})
    assert armed.status_code == 503, armed.text
    body = armed.json()
    assert body.get("error") == "PluginUnreachable"

    status = client.get(PLUGIN_STATUS_PATH)
    assert status.status_code == 503, status.text
    assert status.json().get("error") == "PluginUnreachable"


def test_plugin_http_arm_status_disarm(
    client_and_session: tuple[object, Session, MockTransport],
) -> None:
    client, session, _transport = client_and_session
    paths = _route_paths(client.app)
    missing = [p for p in PLUGIN_HTTP_PATHS if p not in paths]
    assert not missing, "missing " + ", ".join(missing)

    try:
        httpd, state, bound = start_mock_plugin("127.0.0.1", PLUGIN_PORT)
    except OSError:
        pytest.skip(
            f"127.0.0.1:{PLUGIN_PORT} is busy; FastAPI has no port override"
        )
    assert bound == PLUGIN_PORT
    try:
        session.freeze_add(
            addr=MockTransport.WRITABLE_EBOOT_ADDR, data=b"\x02\x00\x00\x00"
        )
        armed = client.post(PLUGIN_ARM_PATH, json={})
        assert armed.status_code == 200, armed.text
        assert state["arm_posts"] >= 1
        arm_body = armed.json()
        assert arm_body.get("armed") is True or arm_body.get("ok") is True

        status = client.get(PLUGIN_STATUS_PATH)
        assert status.status_code == 200, status.text
        st = status.json()
        assert st.get("armed") is True

        disarmed = client.post(PLUGIN_DISARM_PATH, json={})
        assert disarmed.status_code == 200, disarmed.text
        assert state["disarm_posts"] >= 1
        after = client.get(PLUGIN_STATUS_PATH)
        assert after.status_code == 200, after.text
        assert after.json().get("armed") is False
    finally:
        httpd.shutdown()
        httpd.server_close()


def test_http_freeze_tick_noop_while_plugin_armed(
    client_and_session: tuple[object, Session, MockTransport],
) -> None:
    client, session, transport = client_and_session
    paths = _route_paths(client.app)
    for needed in (*PLUGIN_HTTP_PATHS, "/api/freeze", "/api/freeze/tick"):
        assert needed in paths, f"missing {needed}"

    try:
        httpd, state, _bound = start_mock_plugin("127.0.0.1", PLUGIN_PORT)
    except OSError:
        pytest.skip(
            f"127.0.0.1:{PLUGIN_PORT} is busy; FastAPI has no port override"
        )

    try:
        added = client.post(
            "/api/freeze",
            json={"addr": MockTransport.WRITABLE_EBOOT_ADDR, "data": "02000000"},
        )
        assert added.status_code == 200, added.text
        baseline = client.post("/api/freeze/tick")
        assert baseline.status_code == 200, baseline.text
        assert baseline.json()["written"] == 1
        n_writes = len(transport.write_calls)

        armed = client.post(PLUGIN_ARM_PATH, json={})
        assert armed.status_code == 200, armed.text
        assert state["arm_posts"] >= 1

        ticked = client.post("/api/freeze/tick")
        assert ticked.status_code == 200, ticked.text
        assert ticked.json()["written"] == 0
        assert len(transport.write_calls) == n_writes

        disarmed = client.post(PLUGIN_DISARM_PATH, json={})
        assert disarmed.status_code == 200, disarmed.text
        resumed = client.post("/api/freeze/tick")
        assert resumed.status_code == 200, resumed.text
        assert resumed.json()["written"] == 1
        assert len(transport.write_calls) == n_writes + 1
    finally:
        httpd.shutdown()
        httpd.server_close()


def _js_fn(text: str, name: str, stop_names: tuple[str, ...]) -> str:
    start = text.find("function " + name)
    if start < 0:
        start = text.find("async function " + name)
    assert start >= 0, f"missing JS function {name}"
    end = len(text)
    for stop in stop_names:
        at = text.find(stop, start + 8)
        if 0 <= at < end:
            end = at
    return text[start:end]


def test_ui_skips_freeze_tick_when_plugin_armed() -> None:
    if not _JS.is_file():
        pytest.skip("web/static/app.js not present")
    text = _JS.read_text(encoding="utf-8")
    assert "pluginArmed" in text

    start = _js_fn(
        text,
        "startFreezeTick",
        ("function renderWatchTable", "function stopWatchPoll"),
    )
    assert "pluginArmed" in start

    tick = _js_fn(
        text,
        "tickFreeze",
        ("async function addFreeze", "function addFreeze"),
    )
    assert "pluginArmed" in tick
    armed_at = tick.find("pluginArmed")
    api_at = tick.find("/api/freeze/tick")
    assert armed_at >= 0
    assert api_at < 0 or armed_at < api_at


def test_ui_arm_stops_freeze_tick() -> None:
    if not _JS.is_file():
        pytest.skip("web/static/app.js not present")
    text = _JS.read_text(encoding="utf-8")
    assert PLUGIN_ARM_PATH in text
    arm_at = text.find(PLUGIN_ARM_PATH)
    fn_at = max(
        text.rfind("\n  function ", 0, arm_at),
        text.rfind("\n  async function ", 0, arm_at),
    )
    rest = text[arm_at:]
    n1 = rest.find("\n  function ")
    n2 = rest.find("\n  async function ")
    ends = [i for i in (n1, n2) if i >= 0]
    end = arm_at + min(ends) if ends else len(text)
    arm_fn = text[fn_at:end]
    assert "stopFreezeTick" in arm_fn


def test_ui_connect_does_not_call_plugin_disarm() -> None:
    if not _JS.is_file():
        pytest.skip("web/static/app.js not present")
    text = _JS.read_text(encoding="utf-8")
    connect = _js_fn(
        text,
        "onConnect",
        ("async function onDisconnect", "function onDisconnect"),
    )
    assert PLUGIN_DISARM_PATH not in connect
