# NitePR5 — Compressed handoff

Paste the **completed-phase** blocks into every worker brief. Workers have no chat history. Do not re-discover these facts.

Current board: [STATUS.md](STATUS.md). Spawn rules: [ORCHESTRATION.md](ORCHESTRATION.md). Parent: [../AGENTS.md](../AGENTS.md).

---

## Next orchestrator — start here (Phase 2 hardware **or** Phase 3)

Phases **0 and 1 are `done`**. Phase **2 is `code_complete`** (mock **49 passed**). Do **not** re-do 0–2. Do **not** mark Phase 2 `done` from mocks.

**Preferred next step:** run the Phase 2 hardware exit on **CUSA13762** with the **current** tree (timeout + no `TURBOSCAN_REGIONS` probe). Restart uvicorn after every pull. If the user overrides, start Phase 3 while 2 stays `code_complete`.

### Git

Phase 1 is on `main` (PR #1). Phase 2 + timeout/probe fixes may still be **uncommitted** — **commit only if the user asks.** Gitignored: `.ps5debug-host` (`192.168.4.42`).

### Read in this order

1. `AGENTS.md`
2. `docs/STATUS.md`
3. `docs/ARCHITECTURE.md` §3, §5.1, §5.3, Phase 2–3 exit
4. `docs/ORCHESTRATION.md` Phase 3 playbook
5. **This file** — paste Phase 0 + 1 + **2 (including hardware lessons)** into every worker prompt

### Phase 2 hardware exit (do this before calling Phase 2 `done`)

Keep **CUSA13762** (The Golf Club 2019) in the foreground. Restart uvicorn so this tree is loaded. Connect `http://127.0.0.1:1744` → attach `eboot.bin` → First Scan a known changing integer (score/timer) → change it in-game → Next Scan a few times.

```powershell
python -m pytest web/tests web/nitepr5_core/tests -q
python -m uvicorn app:app --app-dir web --host 127.0.0.1 --port 1744
```

**Do not confuse hex poll with a hung scan.** After attach, uvicorn will spam `GET /api/read?addr=29097984&n=512&pid=…` at 4 Hz. That is the peephole (`0x1bc0000`), not First Scan. `POST /api/scan/start` is logged only when it **finishes**. Hex status should read `peephole ~4 Hz`; during a hunt, `paused (scan)` and those `/api/read` lines must stop.

Host `192.168.4.42`. ps5debug-NG v1.3.0 (elfldr 9021). Rest mode drops 744. `eboot.bin` pid **changes every launch**.

### Phase 3 spawn order (after core watch/freeze exist, parallel on **different files**)

From ORCHESTRATION.md:

| Agent | Files | Job |
|---|---|---|
| A | core watch/freeze (`web/nitepr5_core/` only) | `watch_*`, `freeze_*` caps 64 / 32; bulk R/W; 10 Hz / 15 Hz owned by UI timers, not the protocol layer |
| B | `web/static` hex+watch | peephole **poke with confirm**; watch table |
| C | `web/` cheats | GoldHEN JSON load/save/toggle; `web/cheats/` |
| D | `web/tests/` | refuse 257th result, 65th watch, 33rd freeze |

Serial: **A first** if B/C need Session methods. Then B+C+D parallel on disjoint paths. Do not create `plugin/` or `overlay/`. One `:744` lock already exists — watch/freeze timers must not interleave with scan I/O.

**Exit:** change a value in-game from hex; freeze it; save a cheat; reload and toggle.

Forbidden: overlay, plugin, pointer/AOB/disasm, unbounded polling, `PT_ATTACH`.

---

## Phase 0 — Environment (done 2026-08-21)

**Exit:** this PC listed processes and identified the foreground app via `ps5dbg`.

### Hardware

| Item | Value |
|---|---|
| PS5 LAN IP | `192.168.4.42` (gitignored `.ps5debug-host`) |
| Firmware | **9.60** (`fw_version()` → `960`) |
| HEN | etaHEN only |
| Debugger | **ps5debug-NG by OSR v1.3.0** (OpenSourcereR) |
| How it is loaded | Send `ps5debug-NG_v1.3.0.elf` to etaHEN **elfldr TCP 9021** (`scripts/send_ps5debug_elf.py`) |
| How it is **not** loaded | etaHEN Toolbox / Debug Settings **PS5Debug** toggle — old Sistr0/CTN blob, firmware-gated at FW **8.00+**. The “not supported on this firmware” toast is expected. Do not set `PS5Debug=1` in `config.ini`. |
| Ports | TCP **744** commands, TCP **755** interrupts, UDP **1010** discover |

### Host resolution

`--host` / `-H`, then env `NITEPR5_PS5_HOST` or `PS5_IP`, then `.ps5debug-host`, then `scripts/ps5_ip.txt`. `--discover` is extra (UDP 1010).

### Scripts

```powershell
python scripts/send_ps5debug_elf.py --host 192.168.4.42 path\to\ps5debug-NG_v1.3.0.elf
python scripts/check_ps5debug.py --host 192.168.4.42
python scripts/check_ps5debug.py --discover
```

### ps5dbg 0.1.1 cheat sheet

```python
from ps5dbg import PS5Debug
from ps5dbg.discover import discover  # NOT re-exported from ps5dbg
# PS5Debug(host, port=744, timeout=10.0)
# ping(); fw_version() -> int; branding().brand
# procs() -> list[ProcInfo]           # .pid, .name
# foreground_app() -> ForegroundApp   # pid==0 => home
# proc_info(pid) -> ProcInfoDetail
# maps(pid) -> list[VmMap]            # .name, .start, .end, .offset, .prot
# read(pid, address, length) -> bytes # KWARG IS address NOT addr
```

### Traps

- README `read(..., addr=...)` **TypeErrors**. Use `address=`.
- `read()` auto-chunks past 1 MiB — never call it unbounded.
- `debug_session().attach` / `PT_ATTACH` flags the game (AppContext). Session `attach_target` is **logical only**.
- Do not use `scan()` or `scan_aob_find_all()` (dumps hits / RAM to the PC). Phase 2 must use **turbo scan** (`TURBOSCAN_*`).

---

## Phase 1 — Web connect + peephole (done 2026-08-21)

**Exit:** open a `CUSA*` title, select it, see ≤512 live bytes. Refresh ≤4 Hz, pauseable. No scan/write.

### What shipped

| Piece | Path |
|---|---|
| Session core | `web/nitepr5_core/` |
| FastAPI | `web/app.py` — one process-global `Session` |
| UI | `web/static/` vanilla JS, `fetch('/api/...')` only |
| Tests | `web/nitepr5_core/tests/` + `web/tests/` (`python -m pytest web/tests -q` → **11 passed**) |
| Caps | `HEX_PEEPHOLE_DEFAULT=512`, `READ_MAX=4096` |

### Import (put `web/` on `sys.path`; true for `uvicorn --app-dir web`)

```python
from nitepr5_core import (
    Session, MockTransport, Ps5dbgTransport,
    discover, resolve_host,
    ProcessInfo, ForegroundInfo, MemoryMap,
    HEX_PEEPHOLE_DEFAULT, READ_MAX,
    NitePR5Error, NotConnected, ConnectFailed, NoTarget,
    ReadTooLarge, InvalidReadSize,
)
```

### Session methods implemented (Phase 1 only)

- `discover(*, timeout=2.0) -> list[str]`
- `connect(host: str) -> None` / `disconnect()` / context manager
- `processes() -> list[ProcessInfo]`  # pid, name, titleid when cheap
- `foreground() -> ForegroundInfo`    # pid 0 is valid (home)
- `attach_target(pid: int) -> None`   # **stores pid**; not PT_ATTACH
- `maps(pid=None, *, refresh=False) -> list[MemoryMap]`  # cached per pid; metadata only (name, start, end, offset, prot, `.perms`, `.size`)
- `read(pid, addr: int, n: int) -> bytes`  # default pid = attached; reject `n<=0` and `n>4096`

**Not implemented in Phase 1:** `write`, `scan_*`, `watch_*`, `freeze_*`, `cheat_*`. Phase 2 added `scan_*`.

Addresses are `int` in Python. Hex only at the UI.

### Mock

`Session(MockTransport())`. Env **`NITEPR5_MOCK=1`** (or `true`) makes `web/app.py` use the mock. Tests use `MockTransport` directly. `maps_fetch_count` proves cache vs `refresh=True`.

### REST (localhost **127.0.0.1:1744** only — never `0.0.0.0`)

| Method | Path | JSON |
|---|---|---|
| GET | `/api/defaults` | `{host}` from `resolve_host()` — **UI-only**, not a session method |
| POST | `/api/discover` | `{hosts:[...]}` |
| POST | `/api/connect` | `{host}` → `{ok, host}` |
| POST | `/api/disconnect` | `{ok}` |
| GET | `/api/processes` | `{processes:[{pid,name,titleid}]}` |
| GET | `/api/foreground` | `{pid,name,titleid,contentid,app_ver}` |
| POST | `/api/attach_target` | `{pid}` → `{ok,pid}` logical |
| GET | `/api/maps?refresh=false` | `{maps:[{name,start,end,offset,prot,perms,size}]}` metadata, not hex |
| GET | `/api/read?addr=&n=512` | optional `pid` → `{addr,n,data}` hex string |

Errors: `NotConnected` → 409, `ConnectFailed` → 503 (rest-mode hint), `NoTarget` / `ReadTooLarge` / `InvalidReadSize` → 400. Missing `n` defaults to **512**, never dump-all.

### UI peephole

16 bytes/row × 32 rows = **512 B**. Poll **250 ms (4 Hz)**. Pause/Resume. Stop the timer on `visibilitychange` (hidden). First **writable** map is the peephole base. Maps table is metadata only. **Connect again** must `resetTargetUi()` before `/api/connect` (stop hex poll, clear attached pid / maps / peephole). Server `Session.connect()` already `disconnect()`s.

```powershell
python -m uvicorn app:app --app-dir web --host 127.0.0.1 --port 1744
# UI without console:
$env:NITEPR5_MOCK = "1"
```

### Live hardware (exit test)

| Item | Value |
|---|---|
| Title | The Golf Club 2019 |
| titleid | **CUSA13762** |
| contentid | `UP1001-CUSA13762_00-THEGOLFCLUB3HBST` |
| app_ver | `01.02` |
| process | `eboot.bin` **pid 115** (pid is per-launch; re-read `foreground()`) |
| maps | **370** metadata rows; first writable `executable` `0x1bc0000` `rw-` size 229376 |
| read | two × **512** bytes from that start; not a dump |

### Phase 1 orchestration (what we spawned)

1. **Core first, alone** — `web/nitepr5_core/`
2. **Then parallel** — UI stubs (`web/app.py`, `web/static/`) and mock tests (`web/tests/`) on disjoint paths
3. **Then wire-up** — stubs → `Session`; JS kept `fetch('/api/...')`

### Traps for Phase 2+

- Do not add a second memory path in the UI. Scan lives in `nitepr5_core`.
- Do not fetch unbounded scan results. Count first; cap **256** rows.
- Do not scan “All” by default. Writable cached regions.
- Do not open a second `:744` scan session against the same PID.
- Maps: no auto-refresh; refresh on process change or a button. 370 rows of **metadata** is OK; dumping those regions is not.
- `plugin/` and `overlay/` still forbidden until those phases.
- Reconnect bug (fixed): Connect must call `resetTargetUi()` **before** `POST /api/connect`. Hex poll used to keep the old pid after the server had dropped `target_pid`. Regression: `test_ui_connect_resets_attach_before_api_connect`, `test_reconnect_clears_target_and_maps_cache`.

### Phase 1 file tree (`web/` — on `main` via PR #1)

```
web/app.py
web/nitepr5_core/{__init__,session,transport,host,types,errors,constants,py.typed}
web/nitepr5_core/tests/test_session.py
web/static/{index.html,app.js,style.css}
web/tests/{conftest.py,test_phase1_mock.py,http_read_contract.py,test_http_read_contract.py}
```

`app.js` is LF. One process-global `SESSION` in `app.py`. Extra UI-only route: `GET /api/defaults`.

---

## Phase 2 — Scan loop (`code_complete` 2026-08-21; hardware exit **not** passed)

**Job:** classic NitePR search loop on the PC. Count-first; turbo on-console; ≤256 result rows.

**Exit (ARCHITECTURE, not yet):** find a known changing integer in **CUSA13762** with a few next-scans. Do not mark `done` from mocks. Two live attempts were made; both failed for reasons below (now fixed in tree). Re-run the exit on a **restarted** uvicorn.

`python -m pytest web/tests web/nitepr5_core/tests -q` → **49 passed**.

### What shipped

| Piece | Path |
|---|---|
| Scan helpers | `web/nitepr5_core/scan.py` (classify, segment gap-fill, snapshot drain) |
| Session + transport | `scan_start/next/undo/count/results`; turbo + iterative fallback |
| FastAPI | `POST /api/scan/start\|next\|undo`, `GET /api/scan/count\|results` |
| UI | Scan panel between maps and hex: First / Next / Undo, **count first**, table only if count ≤256 |
| Tests | `web/nitepr5_core/tests/test_scan.py`, `web/tests/http_scan_contract.py`, `web/tests/test_phase2_scan_http.py` |
| Caps | `RESULTS_MAX=256`; `SCAN_REGIONS_DEFAULT=writable_cached`; unknown flag required |
| Scan I/O | `SCAN_IO_TIMEOUT is None`; `Transport.scan_wait()`; Session `_io` RLock |

### Session API (Phase 2)

```python
scan_start(pid=None, *, value_type="u32", compare="exact", value=None, alignment=4, unknown=False, regions="writable_cached") -> int
scan_next(*, compare: str, value: int | None = None) -> int
scan_undo() -> int | None   # None = hunt ended (undo of first scan)
scan_count() -> int
scan_results(limit: int = 256) -> list[ScanHit]  # [] if count > 256; does not GET
```

`ScanHit(addr: int, current: bytes, previous: bytes | None = None)`

Errors: `ResultsTooMany`, `InvalidResultLimit`, `ScanActive` (`ScanInFlight` alias), `ScanUnsupported`, `NoScan`, `UndoTooLarge`, `InvalidScanRegions`, `InvalidScanValue`, `InvalidScanCompare`

**Replace vs ScanActive:** a new `scan_start` **ends the previous idle hunt** (like `connect()`). `ScanActive` if `_busy` (op in flight), if peephole `read()` cannot take `_io` (another thread holds :744), or `attach_target` to a **different** pid during a hunt.

**Still not implemented:** `write`, `watch_*`, `freeze_*`, `cheat_*`.

Compare names: `exact` / `equal`, `increased`, `decreased`, `changed`, `unchanged`. Unknown first scan: `unknown=True` or `compare="unknown"` — never the default.

Undo: empty stack → `turboscan_end` and hunt ends (`scan_undo() is None`). Next-scan undo restores a previous generation **only if that count was ≤256**; else `UndoTooLarge`.

### Region classify (locked — do not “fix” back to TURBOSCAN_REGIONS)

`Session._scan_segments` uses **cached `maps()` only**: `classify_maps` keeps **writable AND NOT executable** (`rw-` heaps / eboot data). Skips r-x and rwx. (Default region name is still `writable_cached`; there is no uncached bit on `MemoryMap`, so this is prot-only.)

**Do not call `turboscan_regions` / `CMD_PROC_TURBOSCAN_REGIONS` on first scan.** With `probe_bytes=0` the server **always** probe-reads **64 KiB from every readable region** (CUSA13762 ≈ **370** maps), including uncached GPU (~40 MB/s). That runs **before** turbo start, looks like a hang, and is not a value scan. `classify_turbo_regions` still exists for a later opt-in; `_scan_segments` must not call it. Mock: `regions_probe_count == 0` after `scan_start`.

Logger `nitepr5` (INFO): `first scan: N rw- segments, X MiB (no region-probe)`. `web/app.py` sets that logger to INFO.

### Turbo path (ps5dbg 0.1.1)

- `authenticate(flags=2)` before stateful scan (`scan_caps` does not need auth)
- Caps → classify from maps → `turboscan_start_resident` (one range) or **segment gap-fill**
- Multi-segment: flags `TS_SERVER_RESIDENT | TS_SNAPSHOT_SEGMENTS` (`TS_SNAPSHOT` **clear**). After the two value acks, send `u32 count` then `count × {u64 addr; u32 length}` (12 bytes each). Packet address/length ignored. Bound `1 .. 1048576`. Reply `{u32 resident_stored; u64 count}`. `resident_stored==0` → declined. Implemented in `scan.turbo_start_resident_segments` using `ps5dbg.turboscan` packing + `Cmd` + `connection` — **no new opcodes**. `turboscan_start_resident` in ps5dbg 0.1.1 does **not** send this list.
- Next: `turboscan_count_resident` (progress is a stream of `u64` until `0xFFFFFFFFFFFFFFFF` — **8-byte recvs**)
- Results: `turboscan_get_resident` only if `count ≤ 256`
- Unknown: `TS_SNAPSHOT | TS_SNAPSHOT_SEGMENTS | TS_SERVER_RESIDENT`. Drain plan/progress/summary; never download RAM. No iterative fallback for unknown → `ScanUnsupported`. ps5dbg raises `NotImplementedError` if you pass `TS_SNAPSHOT` into `turboscan_start_resident`; use `turbo_start_snapshot_segments`.
- Fallback if no turbo caps / resident declined: `protocol.proc_scan_start/count/get` (client-held candidates, still ≤256 rows out). Unknown never uses this.
- **Never** `PS5Debug.scan()` / `scan_aob_find_all()`.

### :744 I/O — timeouts, lock, hex poll (learned live 2026-08-21)

ps5dbg `Connection` default timeout is **10s**. Turbo first/next can sit **silent for minutes** (segmented START waits on a 12-byte summary; COUNT waits on `recv_u64` progress). That fired:

```text
timed out reading 8 bytes from 192.168.4.42:744. Rest mode drops TCP 744; …
```

**False rest-mode alarm.** The 8 bytes are a progress `u64`, not a dropped socket. FastAPI maps `ConnectionLost` → `NotConnected` and appends the rest-mode hint.

**Locked behavior:**

- `SCAN_IO_TIMEOUT is None` (blocking recv) via `Ps5dbgTransport.scan_wait()` around all scan RPCs. Restore previous timeout after.
- TCP keepalive on connect (`SO_KEEPALIVE`; Windows `SIO_KEEPALIVE_VALS` idle 30s / interval 5s) so a **real** dropped 744 is still detected.
- One process-global `SESSION`. FastAPI sync routes run in a **threadpool** — hex poll at 4 Hz **must not** share :744 with an in-flight scan (protocol desync).
- `Session._io` is a `threading.RLock`. Scan ops take it blocking. `read()` / maps fetch take it **non-blocking** → `ScanActive` if the scan owns the socket.
- UI: `runScan` sets `scanBusy`, `stopHexPoll()`, status `paused (scan)` **before** `POST /api/scan/*`. `tickHex` / `startHexPoll` bail if `scanBusy`. Resume hex in `finally`. Regression: `test_ui_pauses_hex_poll_during_scan`, `test_scan_wait_drops_recv_timeout_then_restores`, `test_read_fails_fast_when_console_io_held`.

Uvicorn access logs print when a request **completes**. A long `POST /api/scan/start` is **invisible** until it returns. Ctrl+C during a hunt can log `scan/start 200` on shutdown — that does not mean it was fast.

### REST

| Method | Path | JSON |
|---|---|---|
| POST | `/api/scan/start` | `{value, compare?, unknown?, value_type?, alignment?, regions?}` → `{count}` |
| POST | `/api/scan/next` | `{compare, value?}` → `{count}` |
| POST | `/api/scan/undo` | `{count: int\|null, ended: bool}` |
| GET | `/api/scan/count` | `{count}` |
| GET | `/api/scan/results?limit=256` | `{count, results:[{addr, current, previous}]}` ; `addr` int; bytes as hex |

Scan errors → 400 (`NotConnected` still 409). UI does **not** fetch results if count > 256 (“N matches — narrow in-game, then Next Scan”). Click a result row → retarget peephole (`/api/read` only, no write). `resetTargetUi()` clears scan UI (also bumps `scanGen`).

### UI

Vanilla JS IIFE, `fetch('/api/...')` only. Scan section in `index.html` between maps and hex. Value: decimal or `0x` hex. First: Exact, optional Unknown checkbox. Next compare select. Buttons disabled while `scanBusy`; Next/Undo disabled until a hunt exists. Hex status: `peephole ~4 Hz` vs `paused (scan)`.

### Mock

`MockTransport.PLANTED_U32 = 100` at `WRITABLE_EBOOT_ADDR`, `HEAP_A`, `HEAP_C`. r-x decoy at `DECOY_RX_ADDR` must not match. `poke_u32` for next-scan. `get_resident_calls` / `unbounded_get_attempted` / `regions_probe_count`. `NITEPR5_MOCK=1` still uses mock (no real turbo).

### Live hardware lessons (do not re-discover)

| Observation | Meaning |
|---|---|
| `GET /api/read?addr=29097984&n=512&pid=115` repeating 200 | Peephole 4 Hz at **`0x1bc0000`** (first writable `executable` rw- map, size 229376). Normal after attach. |
| Same pid **115**, title **CUSA13762** / app_ver **01.02** | Same launch as Phase 1 exit until the game is closed; pid changes next launch. |
| `GET /api/read` → **409** after uvicorn restart | Old tab still polling; `NotConnected`. Refresh the page and Connect again. |
| `timed out reading 8 bytes` + rest-mode hint | 10s socket timeout during turbo progress — **fixed** (`scan_wait`). Reconnect once if the old socket desynced. |
| First Scan “hung”, then `POST /api/scan/start 200` on Ctrl+C | Hunt was in flight; uvicorn logs completion only. Cause: **`TURBOSCAN_REGIONS` 64 KiB × ~370 maps** — **fixed** (maps classify only). |
| Hex poll continuing during a hunt | Must not happen with current JS. If it does, uvicorn was not restarted. |

### Orchestration (what we spawned)

1. **Core first, alone** — `web/nitepr5_core/`
2. **Then parallel** — UI (`web/app.py`, `web/static/`) and HTTP tests (`web/tests/`)
3. Orchestrator unify: First Scan replaces idle hunt; then timeout/lock/hex-pause; then drop `TURBOSCAN_REGIONS` probe

### Traps for Phase 3+

- Watch/freeze/write live in `nitepr5_core`. No second memory path. UI timers (~10 Hz / 15 Hz) must take the same `:744` `_io` lock (or pause during scan) — do not interleave protocol on one socket.
- Count first; cap **256** results. Never `regions="all"`. Never `PS5Debug.scan()`.
- Do not open a second `:744` connection for scans (survivors are per TCP session).
- Do not call `TURBOSCAN_REGIONS` with default `probe_bytes=0` on the hot path.
- Keep the segment-list gap-fill; do not reimplement TCP. `turboscan_start_resident` still has no segment list in ps5dbg 0.1.1.
- `plugin/` and `overlay/` still forbidden.
- After code changes: **restart uvicorn**. Browser cache of `app.js` may need a hard refresh.

### Phase 2 file tree (added on top of Phase 1)

```
web/nitepr5_core/scan.py
web/nitepr5_core/tests/test_scan.py
web/tests/http_scan_contract.py
web/tests/test_phase2_scan_http.py
```

Also touched: `session.py`, `transport.py`, `constants.py` (`SCAN_IO_TIMEOUT`, `RESULTS_MAX`), `errors.py`, `types.py` (`ScanHit`), `__init__.py`, `app.py`, `static/{index.html,app.js,style.css}`.

---

## Not done (do not start until asked)

| Phase | Job |
|---|---|
| 2 hardware | CUSA13762 changing-integer hunt (required before `done`) |
| 3 | Hex poke, watch ≤64 @ 10 Hz, freeze ≤32 @ 15 Hz, GoldHEN JSON |
| 4 | etaHEN plugin daemon |
| 5 | Overlay spike B2 **or** B3 |
