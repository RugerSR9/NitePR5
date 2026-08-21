"""NitePR5 Phase 1–2 web app — FastAPI bound to nitepr5-core Session.

JSON shapes match the session API so the vanilla JS client keeps working.
NITEPR5_MOCK=1 uses MockTransport (pytest / local UI without a PS5).
"""

from __future__ import annotations

import logging
import os
from pathlib import Path

from fastapi import FastAPI, HTTPException, Query, Request
from fastapi.responses import FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

from nitepr5_core import (
    HEX_PEEPHOLE_DEFAULT,
    RESULTS_MAX,
    SCAN_ALIGN_U32,
    SCAN_REGIONS_DEFAULT,
    ConnectFailed,
    ForegroundInfo,
    InvalidReadSize,
    InvalidResultLimit,
    InvalidScanCompare,
    InvalidScanRegions,
    InvalidScanValue,
    MemoryMap,
    MockTransport,
    NitePR5Error,
    NoScan,
    NoTarget,
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
    resolve_host,
)

STATIC = Path(__file__).resolve().parent / "static"

logging.getLogger("nitepr5").setLevel(logging.INFO)


def _mock_requested() -> bool:
    return os.environ.get("NITEPR5_MOCK", "").strip().lower() in ("1", "true")


def _make_session() -> Session:
    if _mock_requested():
        return Session(MockTransport())
    return Session()


# One process-global Session. Logical attach only — never PT_ATTACH.
SESSION = _make_session()

app = FastAPI(title="NitePR5", docs_url=None, redoc_url=None)


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
    return JSONResponse(status_code=status, content={"detail": str(exc)})


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


app.mount("/static", StaticFiles(directory=str(STATIC)), name="static")


if __name__ == "__main__":
    import uvicorn

    uvicorn.run("app:app", host="127.0.0.1", port=1744, reload=False)
