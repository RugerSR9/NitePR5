"""NitePR5 Phase 1–3 web app — FastAPI bound to nitepr5-core Session.

JSON shapes match the session API so the vanilla JS client keeps working.
NITEPR5_MOCK=1 uses MockTransport (pytest / local UI without a PS5).
"""

from __future__ import annotations

import logging
import os
import re
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, HTTPException, Query, Request
from fastapi.responses import FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

from tracing import flush_tracing, instrument_app

from nitepr5_core import (
    HEX_PEEPHOLE_DEFAULT,
    RESULTS_MAX,
    SCAN_ALIGN_U32,
    SCAN_REGIONS_DEFAULT,
    WATCH_SIZE_DEFAULT,
    ConnectFailed,
    FreezeEntry,
    FreezeLimit,
    ForegroundInfo,
    InvalidAddress,
    InvalidCheat,
    InvalidFreezeSize,
    InvalidReadSize,
    InvalidResultLimit,
    InvalidScanCompare,
    InvalidScanRegions,
    InvalidScanValue,
    InvalidWatchSize,
    InvalidWriteSize,
    MemoryMap,
    MockTransport,
    NitePR5Error,
    NoCheat,
    NoFreeze,
    NoMod,
    NoScan,
    NoTarget,
    NoWatch,
    NotConnected,
    ProcessInfo,
    ReadTooLarge,
    ResultsTooMany,
    ScanActive,
    ScanHit,
    ScanInFlight,
    ScanUnsupported,
    Session,
    UndoTooLarge,
    WatchEntry,
    WatchLimit,
    WatchValue,
    WriteTooLarge,
    resolve_host,
)
from nitepr5_core.cheat import cheat_to_dict, parse_cheat_dict
from nitepr5_core.constants import EBOOT_NAME

STATIC = Path(__file__).resolve().parent / "static"
CHEATS_DIR = Path(__file__).resolve().parent / "cheats"
_CHEAT_FILENAME_RE = re.compile(r"^[A-Za-z0-9._-]+\.json$")
_HEX_DIGITS = frozenset("0123456789abcdefABCDEF")

logging.getLogger("nitepr5").setLevel(logging.INFO)


def _mock_requested() -> bool:
    return os.environ.get("NITEPR5_MOCK", "").strip().lower() in ("1", "true")


def _make_session() -> Session:
    if _mock_requested():
        return Session(MockTransport())
    return Session()


# One process-global Session. Logical attach only — never PT_ATTACH.
SESSION = _make_session()


@asynccontextmanager
async def _lifespan(_app: FastAPI):
    yield
    flush_tracing()


app = FastAPI(title="NitePR5", docs_url=None, redoc_url=None, lifespan=_lifespan)


class ConnectBody(BaseModel):
    host: str = Field(..., min_length=1)


class AttachBody(BaseModel):
    pid: int


class ScanStartBody(BaseModel):
    value: int | None = None
    compare: str = "exact"
    unknown: bool = False
    value_type: str = "u32"
    alignment: int = SCAN_ALIGN_U32
    regions: str = SCAN_REGIONS_DEFAULT
    pid: int | None = None


class ScanNextBody(BaseModel):
    compare: str = Field(..., min_length=1)
    value: int | None = None


class WriteBody(BaseModel):
    addr: int
    data: str
    pid: int | None = None


class WatchBody(BaseModel):
    addr: int
    n: int = WATCH_SIZE_DEFAULT
    label: str = ""


class FreezeBody(BaseModel):
    addr: int
    data: str


class CheatLoadBody(BaseModel):
    filename: str


class CheatSaveBody(BaseModel):
    filename: str
    cheat: dict | None = None
    from_freezes: bool = False
    name: str = ""
    id: str = ""
    version: str = ""
    process: str = EBOOT_NAME


class CheatToggleBody(BaseModel):
    name: str
    enabled: bool


_HTTP_400 = (
    NoTarget,
    ReadTooLarge,
    InvalidReadSize,
    NoScan,
    ScanActive,
    ScanInFlight,
    ScanUnsupported,
    ResultsTooMany,
    InvalidResultLimit,
    UndoTooLarge,
    InvalidScanRegions,
    InvalidScanValue,
    InvalidScanCompare,
    InvalidAddress,
    WatchLimit,
    FreezeLimit,
    NoWatch,
    NoFreeze,
    InvalidWriteSize,
    WriteTooLarge,
    InvalidWatchSize,
    InvalidFreezeSize,
    InvalidCheat,
    NoCheat,
    NoMod,
)


def _process_json(proc: ProcessInfo) -> dict:
    return {"pid": proc.pid, "name": proc.name, "titleid": proc.titleid}


def _foreground_json(fg: ForegroundInfo) -> dict:
    return {
        "pid": fg.pid,
        "name": fg.name,
        "titleid": fg.titleid,
        "contentid": fg.contentid,
        "app_ver": fg.app_ver,
    }


def _map_json(row: MemoryMap) -> dict:
    return {
        "name": row.name,
        "start": row.start,
        "end": row.end,
        "offset": row.offset,
        "prot": row.prot,
        "perms": row.perms,
        "size": row.size,
    }


def _hit_json(hit: ScanHit) -> dict:
    return {
        "addr": hit.addr,
        "current": hit.current.hex(),
        "previous": None if hit.previous is None else hit.previous.hex(),
    }


def _hex_bytes(raw: str) -> bytes:
    """Decode a no-separator hex string. Empty / odd / non-hex → InvalidWriteSize."""
    if not isinstance(raw, str):
        raise InvalidWriteSize(raw)
    s = raw.strip()
    if not s or len(s) % 2 != 0 or any(c not in _HEX_DIGITS for c in s):
        raise InvalidWriteSize(s if s else raw)
    return bytes.fromhex(s)


def _watch_json(entry: WatchEntry) -> dict:
    return {
        "id": entry.id,
        "pid": entry.pid,
        "addr": entry.addr,
        "n": entry.n,
        "label": entry.label,
    }


def _watch_value_json(sample: WatchValue) -> dict:
    return {"id": sample.id, "addr": sample.addr, "data": sample.data.hex()}


def _freeze_json(entry: FreezeEntry) -> dict:
    return {
        "id": entry.id,
        "pid": entry.pid,
        "addr": entry.addr,
        "data": entry.data.hex(),
    }


def _cheat_payload() -> dict:
    cheat = SESSION.loaded_cheat()
    return {
        "cheat": None if cheat is None else cheat_to_dict(cheat),
        "enabled": SESSION.cheat_enabled_names(),
    }


def _cheat_path(filename: str) -> Path:
    if not isinstance(filename, str):
        raise InvalidCheat(f"invalid cheat filename: {filename!r}")
    name = filename.strip()
    if not _CHEAT_FILENAME_RE.fullmatch(name):
        raise InvalidCheat(f"invalid cheat filename: {filename!r}")
    base = CHEATS_DIR.resolve()
    dest = (CHEATS_DIR / name).resolve()
    if dest.parent != base:
        raise InvalidCheat(f"invalid cheat filename: {filename!r}")
    return dest


@app.exception_handler(NitePR5Error)
async def nitepr5_error_handler(_request: Request, exc: NitePR5Error) -> JSONResponse:
    if isinstance(exc, NotConnected):
        status = 409
    elif isinstance(exc, ConnectFailed):
        status = 503
    elif isinstance(exc, _HTTP_400):
        status = 400
    else:
        status = 500
    logging.getLogger("nitepr5").warning("%s: %s", type(exc).__name__, exc)
    return JSONResponse(
        status_code=status,
        content={"detail": str(exc) or type(exc).__name__, "error": type(exc).__name__},
    )


@app.get("/")
def index() -> FileResponse:
    return FileResponse(STATIC / "index.html")


@app.get("/api/defaults")
def api_defaults() -> dict:
    """Optional host prefill. UI still works if this is unused."""
    return {"host": resolve_host()}


@app.post("/api/discover")
def api_discover() -> dict:
    return {"hosts": SESSION.discover()}


@app.post("/api/connect")
def api_connect(body: ConnectBody) -> dict:
    host = body.host.strip()
    if not host:
        raise HTTPException(status_code=400, detail="no PS5 host")
    SESSION.connect(host)
    return {"ok": True, "host": SESSION.host or host}


@app.post("/api/disconnect")
def api_disconnect() -> dict:
    SESSION.disconnect()
    return {"ok": True}


@app.get("/api/processes")
def api_processes() -> dict:
    return {"processes": [_process_json(p) for p in SESSION.processes()]}


@app.get("/api/foreground")
def api_foreground() -> dict:
    return _foreground_json(SESSION.foreground())


@app.post("/api/attach_target")
def api_attach_target(body: AttachBody) -> dict:
    SESSION.attach_target(body.pid)
    return {"ok": True, "pid": body.pid}


@app.get("/api/maps")
def api_maps(refresh: bool = Query(False)) -> dict:
    return {"maps": [_map_json(row) for row in SESSION.maps(refresh=refresh)]}


@app.get("/api/read")
def api_read(
    addr: int = Query(...),
    n: int = Query(HEX_PEEPHOLE_DEFAULT),
    pid: int | None = Query(None),
) -> dict:
    data = SESSION.read(pid, addr, n)
    return {"addr": addr, "n": n, "data": data.hex()}


@app.post("/api/scan/start")
def api_scan_start(body: ScanStartBody) -> dict:
    count = SESSION.scan_start(
        body.pid,
        value_type=body.value_type,
        compare=body.compare,
        value=body.value,
        alignment=body.alignment,
        unknown=body.unknown,
        regions=body.regions,
    )
    return {"count": count}


@app.post("/api/scan/next")
def api_scan_next(body: ScanNextBody) -> dict:
    count = SESSION.scan_next(compare=body.compare, value=body.value)
    return {"count": count}


@app.post("/api/scan/undo")
def api_scan_undo() -> dict:
    count = SESSION.scan_undo()
    return {"count": count, "ended": count is None}


@app.get("/api/scan/count")
def api_scan_count() -> dict:
    return {"count": SESSION.scan_count()}


@app.get("/api/scan/results")
def api_scan_results(limit: int = Query(RESULTS_MAX)) -> dict:
    if not isinstance(limit, int) or isinstance(limit, bool) or limit <= 0:
        raise InvalidResultLimit(limit)
    if limit > RESULTS_MAX:
        raise ResultsTooMany(limit)
    count = SESSION.scan_count()
    hits = SESSION.scan_results(limit)
    return {"count": count, "results": [_hit_json(h) for h in hits]}


@app.post("/api/write")
def api_write(body: WriteBody) -> dict:
    data = _hex_bytes(body.data)
    SESSION.write(body.pid, body.addr, data)
    return {"ok": True, "addr": body.addr, "n": len(data)}


@app.post("/api/watch")
def api_watch_add(body: WatchBody) -> dict:
    entry = SESSION.watch_add(addr=body.addr, n=body.n, label=body.label)
    return _watch_json(entry)


@app.get("/api/watch/poll")
def api_watch_poll() -> dict:
    return {"values": [_watch_value_json(v) for v in SESSION.watch_poll()]}


@app.get("/api/watch")
def api_watch_list() -> dict:
    return {"watches": [_watch_json(w) for w in SESSION.watches()]}


@app.delete("/api/watch/{id}")
def api_watch_remove(id: int) -> dict:
    SESSION.watch_remove(id)
    return {"ok": True}


@app.post("/api/freeze")
def api_freeze_add(body: FreezeBody) -> dict:
    data = _hex_bytes(body.data)
    entry = SESSION.freeze_add(addr=body.addr, data=data)
    return _freeze_json(entry)


@app.post("/api/freeze/tick")
def api_freeze_tick() -> dict:
    return {"written": SESSION.freeze_tick()}


@app.get("/api/freeze")
def api_freeze_list() -> dict:
    return {"freezes": [_freeze_json(f) for f in SESSION.freezes()]}


@app.delete("/api/freeze/{id}")
def api_freeze_remove(id: int) -> dict:
    SESSION.freeze_remove(id)
    return {"ok": True}


@app.get("/api/cheats")
def api_cheats() -> dict:
    if not CHEATS_DIR.is_dir():
        return {"files": []}
    names = [
        p.name
        for p in CHEATS_DIR.iterdir()
        if p.is_file() and _CHEAT_FILENAME_RE.fullmatch(p.name)
    ]
    names.sort()
    return {"files": names}


@app.post("/api/cheat/load")
def api_cheat_load(body: CheatLoadBody) -> dict:
    cheat = SESSION.cheat_load(_cheat_path(body.filename))
    return {"cheat": cheat_to_dict(cheat), "enabled": SESSION.cheat_enabled_names()}


@app.post("/api/cheat/save")
def api_cheat_save(body: CheatSaveBody) -> dict:
    dest = _cheat_path(body.filename)
    if body.from_freezes:
        parsed = SESSION.cheat_from_freezes(
            name=body.name,
            title_id=body.id,
            version=body.version,
            process=body.process or EBOOT_NAME,
        )
    else:
        parsed = parse_cheat_dict(body.cheat) if body.cheat is not None else None
    CHEATS_DIR.mkdir(parents=True, exist_ok=True)
    SESSION.cheat_save(dest, parsed)
    loaded = SESSION.loaded_cheat()
    return {
        "ok": True,
        "filename": dest.name,
        "cheat": cheat_to_dict(loaded) if loaded is not None else None,
    }


@app.post("/api/cheat/toggle")
def api_cheat_toggle(body: CheatToggleBody) -> dict:
    SESSION.cheat_toggle(body.name, body.enabled)
    return {"ok": True, "name": body.name, "enabled": body.enabled}


@app.get("/api/cheat")
def api_cheat() -> dict:
    return _cheat_payload()


app.mount("/static", StaticFiles(directory=str(STATIC)), name="static")

instrument_app(app, SESSION)


if __name__ == "__main__":
    import uvicorn

    uvicorn.run("app:app", host="127.0.0.1", port=1744, reload=False)
