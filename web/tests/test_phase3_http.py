"""Phase 3 HTTP + cap contract (ARCHITECTURE §5.3).

Core already covers write/watch/freeze/cheat in nitepr5_core/tests/test_phase3_core.py.
This module asserts the web contract: limit=257 refused, 65th watch refused,
33rd freeze refused, write round-trip, watch_poll, freeze_tick, GoldHEN load
with path-traversal refused. Phase 3 routes may still be missing (app.py
sibling); those tests skip. Contract + scan limit=257 must still pass.
"""

from __future__ import annotations

import json
from collections.abc import Iterator
from pathlib import Path

import pytest

from http_phase3_contract import (
    UNBOUNDED_WRITE,
    http_cheat_filename,
    http_write_data,
)
from nitepr5_core import (
    FREEZE_MAX,
    RESULTS_MAX,
    WATCH_MAX,
    WRITE_MAX,
    InvalidCheat,
    InvalidWriteSize,
    MockTransport,
    Session,
    WriteTooLarge,
)

_JS = Path(__file__).resolve().parents[1] / "static" / "app.js"
_CHEATS_DIR = Path(__file__).resolve().parents[1] / "cheats"
_CHEAT_FILENAME = "test_cusa00004_01.07.json"
_PATCH_OFFSET = MockTransport.WRITABLE_EBOOT_ADDR - MockTransport.RAM_BASE
_ON_HEX = "01000000"
_OFF_HEX = "00000000"
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
                    "on": _ON_HEX,
                    "off": _OFF_HEX,
                }
            ],
        }
    ],
    "credits": ["NitePR5"],
}

_WRITE_HTTP_PATHS = ("/api/write",)
_WATCH_HTTP_PATHS = ("/api/watch", "/api/watch/poll")
_FREEZE_HTTP_PATHS = ("/api/freeze", "/api/freeze/tick")
_CHEAT_HTTP_PATHS = (
    "/api/cheats",
    "/api/cheat/load",
    "/api/cheat/save",
    "/api/cheat/toggle",
    "/api/cheat",
)


def _route_paths(app: object) -> set[str]:
    return {getattr(r, "path", "") for r in getattr(app, "routes", ())}


def _require_http(app: object, *needed: str) -> None:
    paths = _route_paths(app)
    missing = [p for p in needed if p not in paths]
    if missing:
        pytest.skip(
            "Phase 3 HTTP routes not added yet (app.py sibling): "
            + ", ".join(missing)
        )


def _u32_hex(value: int) -> str:
    return value.to_bytes(4, "little").hex()


def _enabled_names(body: dict) -> list[str]:
    enabled = body.get("enabled")
    if enabled is None:
        return []
    if isinstance(enabled, dict):
        return [name for name, on in enabled.items() if on]
    if isinstance(enabled, list):
        names: list[str] = []
        for item in enabled:
            if isinstance(item, str):
                names.append(item)
            elif isinstance(item, dict) and item.get("name"):
                names.append(str(item["name"]))
        return names
    return []


def _file_names(body: dict) -> list[str]:
    files = body.get("files") or []
    names: list[str] = []
    for item in files:
        if isinstance(item, str):
            names.append(item)
        elif isinstance(item, dict):
            names.append(str(item.get("filename") or item.get("name") or ""))
    return names


# --- helper: write size + cheat basename (always run) ----------------------


def test_write_max_is_4096_not_unbounded() -> None:
    assert WRITE_MAX == 4096
    assert http_write_data("00") == b"\x00"
    assert http_write_data("aabbccdd") == bytes.fromhex("aabbccdd")
    assert len(http_write_data("00" * WRITE_MAX)) == WRITE_MAX
    assert http_write_data("00" * WRITE_MAX) != UNBOUNDED_WRITE


def test_http_write_refuses_empty_unbounded_and_oversize() -> None:
    with pytest.raises(WriteTooLarge):
        http_write_data(UNBOUNDED_WRITE)
    with pytest.raises(WriteTooLarge):
        http_write_data("00" * (WRITE_MAX + 1))
    with pytest.raises(InvalidWriteSize):
        http_write_data("")
    with pytest.raises(InvalidWriteSize):
        http_write_data("   ")
    with pytest.raises(InvalidWriteSize):
        http_write_data(None)
    with pytest.raises(InvalidWriteSize):
        http_write_data("gg")


def test_http_cheat_filename_refuses_traversal() -> None:
    assert http_cheat_filename(_CHEAT_FILENAME) == _CHEAT_FILENAME
    with pytest.raises(InvalidCheat):
        http_cheat_filename("../secrets.json")
    with pytest.raises(InvalidCheat):
        http_cheat_filename("foo/bar.json")
    with pytest.raises(InvalidCheat):
        http_cheat_filename("foo\\bar.json")
    with pytest.raises(InvalidCheat):
        http_cheat_filename("")
    with pytest.raises(InvalidCheat):
        http_cheat_filename("..")


# --- HTTP routes (skip until app.py sibling wires them) --------------------


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


def test_http_results_limit_257_refused_256_allowed(
    client_and_session: tuple[object, Session, MockTransport],
) -> None:
    """GET /api/scan/results?limit=257 is 400. Caps refuse; they do not truncate."""
    client, _session, _transport = client_and_session
    _require_http(client.app, "/api/scan/start", "/api/scan/results")

    too_many_first = client.get("/api/scan/results", params={"limit": 257})
    assert too_many_first.status_code == 400, too_many_first.text

    started = client.post("/api/scan/start", json={"value": MockTransport.PLANTED_U32})
    assert started.status_code == 200, started.text

    too_many = client.get("/api/scan/results", params={"limit": 257})
    assert too_many.status_code == 400, too_many.text
    detail = str(too_many.json()).lower()
    assert "256" in detail or "too many" in detail or "limit" in detail

    allowed = client.get("/api/scan/results", params={"limit": RESULTS_MAX})
    assert allowed.status_code == 200, allowed.text
    payload = allowed.json()
    assert payload["count"] <= RESULTS_MAX or payload["results"] == []
    assert len(payload["results"]) <= RESULTS_MAX


def test_http_65th_watch_refused(
    client_and_session: tuple[object, Session, MockTransport],
) -> None:
    client, _session, _transport = client_and_session
    _require_http(client.app, "/api/watch")

    last_ok = None
    for i in range(WATCH_MAX):
        last_ok = client.post(
            "/api/watch",
            json={"addr": MockTransport.WRITABLE_EBOOT_ADDR + i, "n": 4},
        )
        assert last_ok.status_code == 200, last_ok.text
    assert last_ok is not None
    body = last_ok.json()
    assert body["id"] == WATCH_MAX
    assert body["addr"] == MockTransport.WRITABLE_EBOOT_ADDR + (WATCH_MAX - 1)
    assert body["n"] == 4

    refused = client.post(
        "/api/watch",
        json={"addr": MockTransport.HEAP_A, "n": 4, "label": "overflow"},
    )
    assert refused.status_code == 400, refused.text
    listed = client.get("/api/watch")
    assert listed.status_code == 200, listed.text
    watches = listed.json()["watches"]
    assert len(watches) == WATCH_MAX


def test_http_33rd_freeze_refused(
    client_and_session: tuple[object, Session, MockTransport],
) -> None:
    client, _session, _transport = client_and_session
    _require_http(client.app, "/api/freeze")

    patch = _u32_hex(1)
    last_ok = None
    for i in range(FREEZE_MAX):
        last_ok = client.post(
            "/api/freeze",
            json={"addr": MockTransport.WRITABLE_EBOOT_ADDR + i, "data": patch},
        )
        assert last_ok.status_code == 200, last_ok.text
    assert last_ok is not None
    assert last_ok.json()["id"] == FREEZE_MAX

    refused = client.post(
        "/api/freeze",
        json={"addr": MockTransport.HEAP_A, "data": patch},
    )
    assert refused.status_code == 400, refused.text
    listed = client.get("/api/freeze")
    assert listed.status_code == 200, listed.text
    freezes = listed.json()["freezes"]
    assert len(freezes) == FREEZE_MAX


def test_http_write_round_trip_and_caps(
    client_and_session: tuple[object, Session, MockTransport],
) -> None:
    client, _session, _transport = client_and_session
    _require_http(client.app, "/api/write", "/api/read")

    addr = MockTransport.WRITABLE_EBOOT_ADDR
    payload = "aabbccdd"
    written = client.post("/api/write", json={"addr": addr, "data": payload})
    assert written.status_code == 200, written.text
    write_body = written.json()
    assert write_body["ok"] is True
    assert write_body["addr"] == addr
    assert write_body["n"] == 4

    read_back = client.get("/api/read", params={"addr": addr, "n": 4})
    assert read_back.status_code == 200, read_back.text
    assert read_back.json()["data"].lower() == payload

    empty = client.post("/api/write", json={"addr": addr, "data": ""})
    assert empty.status_code in (400, 422), empty.text

    oversize = client.post(
        "/api/write",
        json={"addr": addr, "data": "00" * (WRITE_MAX + 1)},
    )
    assert oversize.status_code == 400, oversize.text


def test_http_watch_poll_sees_write(
    client_and_session: tuple[object, Session, MockTransport],
) -> None:
    client, _session, _transport = client_and_session
    _require_http(client.app, "/api/watch", "/api/watch/poll", "/api/write")

    addr = MockTransport.WRITABLE_EBOOT_ADDR
    added = client.post("/api/watch", json={"addr": addr, "n": 4, "label": "score"})
    assert added.status_code == 200, added.text
    watch_id = added.json()["id"]

    payload = "05000000"
    written = client.post("/api/write", json={"addr": addr, "data": payload})
    assert written.status_code == 200, written.text

    polled = client.get("/api/watch/poll")
    assert polled.status_code == 200, polled.text
    values = polled.json()["values"]
    assert len(values) == 1
    assert values[0]["id"] == watch_id
    assert values[0]["addr"] == addr
    assert values[0]["data"].lower() == payload


def test_http_freeze_tick_writes(
    client_and_session: tuple[object, Session, MockTransport],
) -> None:
    client, _session, transport = client_and_session
    _require_http(client.app, "/api/freeze", "/api/freeze/tick", "/api/read")

    addr = MockTransport.WRITABLE_EBOOT_ADDR
    frozen = "02000000"
    added = client.post("/api/freeze", json={"addr": addr, "data": frozen})
    assert added.status_code == 200, added.text
    assert added.json()["data"].lower() == frozen

    transport.poke_u32(addr, 99)
    poked = client.get("/api/read", params={"addr": addr, "n": 4})
    assert poked.status_code == 200, poked.text
    assert poked.json()["data"].lower() == _u32_hex(99)

    ticked = client.post("/api/freeze/tick")
    assert ticked.status_code == 200, ticked.text
    assert ticked.json()["written"] == 1

    restored = client.get("/api/read", params={"addr": addr, "n": 4})
    assert restored.status_code == 200, restored.text
    assert restored.json()["data"].lower() == frozen


def test_http_cheat_load_save_toggle(
    client_and_session: tuple[object, Session, MockTransport],
) -> None:
    client, _session, _transport = client_and_session
    _require_http(client.app, *_CHEAT_HTTP_PATHS, "/api/read")

    _CHEATS_DIR.mkdir(parents=True, exist_ok=True)
    cheat_path = _CHEATS_DIR / _CHEAT_FILENAME
    cheat_path.write_text(json.dumps(_SAMPLE_CHEAT), encoding="utf-8")
    try:
        listed = client.get("/api/cheats")
        assert listed.status_code == 200, listed.text
        assert _CHEAT_FILENAME in _file_names(listed.json())

        loaded = client.post("/api/cheat/load", json={"filename": _CHEAT_FILENAME})
        assert loaded.status_code == 200, loaded.text

        current = client.get("/api/cheat")
        assert current.status_code == 200, current.text
        cheat_body = current.json()
        cheat = cheat_body.get("cheat") or cheat_body
        assert cheat["id"] == "CUSA00004"
        assert cheat["process"] == "eboot.bin"
        assert "Example" in json.dumps(cheat)

        toggled = client.post(
            "/api/cheat/toggle", json={"name": "Example", "enabled": True}
        )
        assert toggled.status_code == 200, toggled.text
        after_on = client.get("/api/read", params={"addr": MockTransport.WRITABLE_EBOOT_ADDR, "n": 4})
        assert after_on.status_code == 200, after_on.text
        assert after_on.json()["data"].lower() == _ON_HEX
        enabled_on = client.get("/api/cheat")
        assert enabled_on.status_code == 200, enabled_on.text
        assert "Example" in _enabled_names(enabled_on.json())

        toggled_off = client.post(
            "/api/cheat/toggle", json={"name": "Example", "enabled": False}
        )
        assert toggled_off.status_code == 200, toggled_off.text
        after_off = client.get("/api/read", params={"addr": MockTransport.WRITABLE_EBOOT_ADDR, "n": 4})
        assert after_off.status_code == 200, after_off.text
        assert after_off.json()["data"].lower() == _OFF_HEX

        saved = client.post("/api/cheat/save", json={"filename": _CHEAT_FILENAME})
        assert saved.status_code == 200, saved.text
        on_disk = json.loads(cheat_path.read_text(encoding="utf-8"))
        assert on_disk["id"] == "CUSA00004"
        assert "enabled" not in on_disk
        assert on_disk["mods"][0]["memory"][0]["on"] == _ON_HEX
        assert on_disk["mods"][0]["memory"][0]["offset"] == f"{_PATCH_OFFSET:x}"
    finally:
        cheat_path.unlink(missing_ok=True)


def test_http_cheat_path_traversal_refused(
    client_and_session: tuple[object, Session, MockTransport],
) -> None:
    """filename ../secrets.json and foo/bar.json → 400. Do not skip when the route exists."""
    client, session, _transport = client_and_session
    _require_http(client.app, "/api/cheat/load", "/api/cheat")

    web_dir = _CHEATS_DIR.parent
    bait_outside = web_dir / "secrets.json"
    nested_dir = _CHEATS_DIR / "foo"
    bait_nested = nested_dir / "bar.json"
    _CHEATS_DIR.mkdir(parents=True, exist_ok=True)
    nested_dir.mkdir(parents=True, exist_ok=True)
    bait_outside.write_text(json.dumps(_SAMPLE_CHEAT), encoding="utf-8")
    bait_nested.write_text(json.dumps(_SAMPLE_CHEAT), encoding="utf-8")
    try:
        escaped = client.post(
            "/api/cheat/load", json={"filename": "../secrets.json"}
        )
        assert escaped.status_code == 400, escaped.text
        nested = client.post(
            "/api/cheat/load", json={"filename": "foo/bar.json"}
        )
        assert nested.status_code == 400, nested.text
        assert session.loaded_cheat() is None
        current = client.get("/api/cheat")
        assert current.status_code in (200, 400), current.text
        if current.status_code == 200:
            body = current.json()
            cheat = body.get("cheat")
            assert cheat in (None, {})
    finally:
        bait_outside.unlink(missing_ok=True)
        bait_nested.unlink(missing_ok=True)
        try:
            nested_dir.rmdir()
        except OSError:
            pass


def test_ui_write_confirm_and_count_first() -> None:
    if not _JS.is_file():
        pytest.skip("web/static/app.js not present")
    text = _JS.read_text(encoding="utf-8")
    if "/api/write" not in text:
        pytest.skip("write UI not in app.js yet (static sibling)")
    assert "confirm" in text
    if "/api/scan/results" in text:
        assert "RESULTS_MAX" in text or "256" in text
        assert "count" in text.lower()
