"""Phase 2 scan HTTP + unbounded-GET contract (ARCHITECTURE §5.3).

Core already covers exact/next/undo/caps in nitepr5_core/tests/test_scan.py.
This module asserts the web contract: default results limit 256, never "all",
count-first empty page when count > 256, and Session never issues an
unbounded turbo GET. Scan routes may still be missing (Agent B); those tests
skip. Contract + Session/Mock cap tests must still pass.
"""

from __future__ import annotations

import inspect
import re
from collections.abc import Iterator
from pathlib import Path

import pytest

from http_scan_contract import UNBOUNDED_RESULTS, http_scan_results_limit
from nitepr5_core import (
    RESULTS_MAX,
    InvalidResultLimit,
    MockTransport,
    ResultsTooMany,
    Session,
)
from nitepr5_core.transport import Ps5dbgTransport

_JS = Path(__file__).resolve().parents[1] / "static" / "app.js"
_SCAN_HTTP_PATHS = (
    "/api/scan/start",
    "/api/scan/next",
    "/api/scan/undo",
    "/api/scan/count",
    "/api/scan/results",
)


def _plant_u32s(transport: MockTransport, value: int, n: int) -> None:
    for i in range(n):
        transport.poke_u32(MockTransport.RAM_BASE + 0x2000 + i * 4, value)


def _route_paths(app: object) -> set[str]:
    return {getattr(r, "path", "") for r in getattr(app, "routes", ())}


def _require_scan_http(app: object, *needed: str) -> None:
    paths = _route_paths(app)
    missing = [p for p in (needed or _SCAN_HTTP_PATHS) if p not in paths]
    if missing:
        pytest.skip(
            "scan HTTP routes not added yet (Agent B): " + ", ".join(missing)
        )


# --- helper: never unbounded results limit ---------------------------------


def test_default_scan_results_limit_is_256_not_all() -> None:
    assert RESULTS_MAX == 256
    assert http_scan_results_limit() == 256
    assert http_scan_results_limit(None) == 256
    assert http_scan_results_limit("") == 256
    assert http_scan_results_limit() != UNBOUNDED_RESULTS
    assert http_scan_results_limit() != "all"


def test_http_scan_results_refuses_unbounded_and_oversize() -> None:
    with pytest.raises(ResultsTooMany):
        http_scan_results_limit("all")
    with pytest.raises(ResultsTooMany):
        http_scan_results_limit(257)
    with pytest.raises(ResultsTooMany):
        http_scan_results_limit(RESULTS_MAX + 1)
    with pytest.raises(InvalidResultLimit):
        http_scan_results_limit(0)
    with pytest.raises(InvalidResultLimit):
        http_scan_results_limit(-1)
    with pytest.raises(InvalidResultLimit):
        http_scan_results_limit("nope")
    assert http_scan_results_limit(1) == 1
    assert http_scan_results_limit(256) == 256
    assert http_scan_results_limit("256") == 256


def test_openapi_scan_results_default_limit_is_256_not_all() -> None:
    """Skip when UI app has no GET /api/scan/results yet. Never default to 'all'."""
    app_mod = pytest.importorskip("app")
    pytest.importorskip("fastapi")
    if not hasattr(app_mod, "app"):
        pytest.skip("app.app not defined yet")

    app = app_mod.app
    schema = app.openapi()
    spec = (schema.get("paths") or {}).get("/api/scan/results")
    if not spec:
        pytest.skip("no GET /api/scan/results in OpenAPI yet (Agent B)")

    get = spec.get("get")
    if not get:
        pytest.skip("GET /api/scan/results missing from OpenAPI")

    params = get.get("parameters") or []
    limit_params = [p for p in params if p.get("name") == "limit"]
    assert limit_params, "GET /api/scan/results must declare limit (default 256, never omitted-as-unbounded)"
    for param in limit_params:
        default = param.get("schema", {}).get("default", param.get("default"))
        assert default != "all"
        assert default != UNBOUNDED_RESULTS
        assert default == RESULTS_MAX == 256


# --- Session / Mock: never request unbounded GET ---------------------------


def test_session_scan_results_257_raises() -> None:
    t = MockTransport()
    s = Session(t)
    s.connect("127.0.0.1")
    s.attach_target(MockTransport.EBOOT_PID)
    try:
        with pytest.raises(ResultsTooMany):
            s.scan_results(257)
        assert t.get_resident_calls == 0
        assert t.unbounded_get_attempted is False
    finally:
        s.disconnect()


def test_session_count_over_256_returns_empty_without_get() -> None:
    t = MockTransport()
    s = Session(t)
    s.connect("127.0.0.1")
    s.attach_target(MockTransport.EBOOT_PID)
    try:
        val = 0x51515151
        _plant_u32s(t, val, 300)
        count = s.scan_start(value=val)
        assert count == 300
        assert s.scan_count() > RESULTS_MAX
        before = t.get_resident_calls
        assert s.scan_results(256) == []
        assert s.scan_results(1) == []
        assert t.get_resident_calls == before
        assert t.unbounded_get_attempted is False
        with pytest.raises(ResultsTooMany):
            s.scan_results(257)
        assert t.get_resident_calls == before
        assert t.unbounded_get_attempted is False
    finally:
        s.disconnect()


def test_mock_scan_get_resident_refuses_count_over_256() -> None:
    src = inspect.getsource(MockTransport.scan_get_resident)
    assert "RESULTS_MAX" in src
    assert "count > RESULTS_MAX" in src

    t = MockTransport()
    t.connect("127.0.0.1")
    try:
        t.scan_get_resident(0, 257, 4)
    except (ResultsTooMany, ValueError) as exc:
        assert "256" in str(exc) or "unbounded" in str(exc).lower()
    else:
        assert t.unbounded_get_attempted is True

    ps5_src = inspect.getsource(Ps5dbgTransport.scan_get_resident)
    assert "RESULTS_MAX" in ps5_src
    assert "count > RESULTS_MAX" in ps5_src
    assert "unbounded" in ps5_src.lower()


# --- HTTP routes (skip until Agent B wires them) ---------------------------


@pytest.fixture
def client_and_session() -> Iterator[tuple[object, Session, MockTransport]]:
    app_mod = pytest.importorskip("app")
    pytest.importorskip("fastapi")
    if not hasattr(app_mod, "app"):
        pytest.skip("app.app not defined yet")
    if not any(p.startswith("/api/scan/") for p in _route_paths(app_mod.app)):
        pytest.skip("scan HTTP routes not added yet (Agent B)")
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


def test_http_scan_start_count_then_capped_results(
    client_and_session: tuple[object, Session, MockTransport],
) -> None:
    client, _session, _transport = client_and_session
    _require_scan_http(client.app, "/api/scan/start", "/api/scan/results", "/api/scan/count")

    started = client.post("/api/scan/start", json={"value": MockTransport.PLANTED_U32})
    assert started.status_code == 200, started.text
    body = started.json()
    assert body["count"] == 3

    counted = client.get("/api/scan/count")
    assert counted.status_code == 200, counted.text
    assert counted.json()["count"] == 3

    page = client.get("/api/scan/results")
    assert page.status_code == 200, page.text
    payload = page.json()
    assert payload["count"] == 3
    assert isinstance(payload["results"], list)
    assert len(payload["results"]) == 3

    capped = client.get("/api/scan/results", params={"limit": 256})
    assert capped.status_code == 200, capped.text
    assert len(capped.json()["results"]) == 3


def test_http_scan_next_and_undo(
    client_and_session: tuple[object, Session, MockTransport],
) -> None:
    client, _session, transport = client_and_session
    _require_scan_http(
        client.app,
        "/api/scan/start",
        "/api/scan/next",
        "/api/scan/undo",
        "/api/scan/count",
    )

    started = client.post("/api/scan/start", json={"value": 100})
    assert started.status_code == 200, started.text
    assert started.json()["count"] == 3

    transport.poke_u32(MockTransport.HEAP_A, 101)
    nxt = client.post("/api/scan/next", json={"compare": "changed"})
    assert nxt.status_code == 200, nxt.text
    assert nxt.json()["count"] == 1

    undone = client.post("/api/scan/undo")
    assert undone.status_code == 200, undone.text
    undo_body = undone.json()
    assert undo_body["count"] == 3
    assert undo_body["ended"] is False

    ended = client.post("/api/scan/undo")
    assert ended.status_code == 200, ended.text
    end_body = ended.json()
    assert end_body["count"] is None
    assert end_body["ended"] is True


def test_http_results_limit_257_is_400(
    client_and_session: tuple[object, Session, MockTransport],
) -> None:
    client, _session, _transport = client_and_session
    _require_scan_http(client.app, "/api/scan/start", "/api/scan/results")

    started = client.post("/api/scan/start", json={"value": MockTransport.PLANTED_U32})
    assert started.status_code == 200, started.text

    too_many = client.get("/api/scan/results", params={"limit": 257})
    assert too_many.status_code == 400, too_many.text
    detail = str(too_many.json()).lower()
    assert "256" in detail or "too many" in detail or "limit" in detail


def test_http_count_over_256_returns_empty_results_no_get(
    client_and_session: tuple[object, Session, MockTransport],
) -> None:
    client, _session, transport = client_and_session
    _require_scan_http(client.app, "/api/scan/start", "/api/scan/results")

    val = 0x51515151
    _plant_u32s(transport, val, 300)
    started = client.post("/api/scan/start", json={"value": val})
    assert started.status_code == 200, started.text
    assert started.json()["count"] == 300

    before = transport.get_resident_calls
    page = client.get("/api/scan/results", params={"limit": 256})
    assert page.status_code == 200, page.text
    payload = page.json()
    assert payload["count"] == 300
    assert payload["results"] == []
    assert transport.get_resident_calls == before
    assert transport.unbounded_get_attempted is False

    omitted = client.get("/api/scan/results")
    assert omitted.status_code == 200, omitted.text
    omitted_body = omitted.json()
    assert omitted_body["count"] == 300
    assert omitted_body["results"] == []
    assert transport.get_resident_calls == before


def test_http_start_regions_all_and_missing_value_are_400(
    client_and_session: tuple[object, Session, MockTransport],
) -> None:
    client, _session, _transport = client_and_session
    _require_scan_http(client.app, "/api/scan/start")

    all_regions = client.post(
        "/api/scan/start", json={"value": 100, "regions": "all"}
    )
    assert all_regions.status_code == 400, all_regions.text

    missing_value = client.post("/api/scan/start", json={})
    assert missing_value.status_code == 400, missing_value.text

    exact_no_value = client.post("/api/scan/start", json={"compare": "exact"})
    assert exact_no_value.status_code == 400, exact_no_value.text


def test_http_results_limit_all_is_rejected(
    client_and_session: tuple[object, Session, MockTransport],
) -> None:
    client, _session, _transport = client_and_session
    _require_scan_http(client.app, "/api/scan/start", "/api/scan/results")

    started = client.post("/api/scan/start", json={"value": MockTransport.PLANTED_U32})
    assert started.status_code == 200, started.text

    dumped = client.get("/api/scan/results", params={"limit": "all"})
    assert dumped.status_code in (400, 422), dumped.text


# --- UI: never fetch results without count-first, never unbounded ----------


def test_ui_scan_never_unbounded_results_fetch() -> None:
    if not _JS.is_file():
        pytest.skip("web/static/app.js not present")
    text = _JS.read_text(encoding="utf-8")
    if "/api/scan/" not in text:
        pytest.skip("scan UI not in app.js yet")

    lowered = text.lower()
    assert "n=all" not in lowered
    assert "limit=all" not in lowered
    assert 'limit="all"' not in text
    assert "limit='all'" not in text
    assert "limit=257" not in text
    assert re.search(r"limit\s*[:=]\s*257\b", text) is None
    for match in re.finditer(r"limit\s*[:=]\s*(\d+)", text):
        assert int(match.group(1)) <= RESULTS_MAX

    if "/api/scan/results" not in text:
        return

    idx = 0
    found = False
    while True:
        at = text.find("/api/scan/results", idx)
        if at == -1:
            break
        found = True
        fn_at = max(text.rfind("function ", 0, at), text.rfind("=>", 0, at), 0)
        window = text[fn_at:at]
        count_first = (
            "/api/scan/count" in window
            or "scan/count" in window
            or "RESULTS_MAX" in window
            or re.search(r"count\s*[<>=]=?\s*256\b", window) is not None
            or re.search(r"256\s*[<>=]=?\s*count\b", window) is not None
            or re.search(r"count\s*[<>=]=?\s*RESULTS_MAX", window) is not None
        )
        assert count_first, (
            "GET /api/scan/results must be count-first (check count vs 256 "
            "before fetching rows)"
        )
        idx = at + 1
    assert found


def test_ui_pauses_hex_poll_during_scan() -> None:
    if not _JS.is_file():
        pytest.skip("web/static/app.js not present")
    text = _JS.read_text(encoding="utf-8")
    if "async function runScan" not in text:
        pytest.skip("scan UI not in app.js yet")
    tick_at = text.find("async function tickHex")
    tick = text[tick_at:text.find("async function onDiscover", tick_at)]
    assert "scanBusy" in tick
    run_at = text.find("async function runScan")
    run = text[run_at:text.find("function stopHexPoll", run_at)]
    assert "stopHexPoll()" in run
    start_at = text.find("function startHexPoll")
    start = text[start_at:text.find("function resetTargetUi", start_at)]
    assert "scanBusy" in start


def test_ui_jump_keeps_64bit_scan_addrs() -> None:
    """Clicking a heap scan hit must not ToInt32-align (addr & ~0xf → negative)."""
    if not _JS.is_file():
        pytest.skip("web/static/app.js not present")
    text = _JS.read_text(encoding="utf-8")
    if "function jumpToAddr" not in text:
        pytest.skip("jumpToAddr not in app.js yet")

    assert "addr & ~0xf" not in text
    assert "function alignPeephole" in text
    assert "function asAddr" in text
    assert "% ROW_BYTES" in text

    jump_at = text.find("function jumpToAddr")
    jump = text[jump_at:text.find("function renderMaps", jump_at)]
    assert "alignPeephole" in jump
    assert "asAddr" in jump
    assert ">>>" not in jump
    assert "& ~" not in jump

    parse_at = text.find("function parseGoto")
    parse = text[parse_at:text.find("function u32leFromHex", parse_at)]
    assert "asAddr" in parse
    assert ">>> 0" not in parse

    render_at = text.find("function renderScanResults")
    render = text[render_at:text.find("async function applyScanCount", render_at)]
    assert "jumpToAddr(addr, addr)" in render
