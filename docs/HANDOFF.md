# NitePR5 — Compressed handoff

Paste the **completed-phase** blocks into every worker brief. Workers have no chat history. Do not re-discover these facts.

Current board: [STATUS.md](STATUS.md). Spawn rules: [ORCHESTRATION.md](ORCHESTRATION.md). Parent: [../AGENTS.md](../AGENTS.md).

---

## Next orchestrator — start here (2026-08-23)

**Do not start Phase 4.** Do not create `plugin/` or `overlay/`. Do not re-implement Phases 0–3.

| Fact | Value |
|---|---|
| Phases 0–1 | `done` |
| Phases 2–3 | `code_complete` (hardware exits **not** run) |
| Mock tests | `python -m pytest web/tests web/nitepr5_core/tests -q` → **81 passed** (2026-08-21 after overflow/desync fix) |
| Last user work | Live First Scan on CUSA13762 failed; then asked to compress this handoff (context full) |
| Git | Phase 1 on `main` (PR #1). Phase 2–3 + scan fix may be **uncommitted**. **Commit only if the user asks.** |

### Last live incident (must re-test after reconnect)

User First Scan (exact, common value) then:

1. `server declined unknown snapshot (snapshot_ok=0)`
2. Next command: `timed out reading 4 bytes from 192.168.4.42:744` + rest-mode hint

**Cause (fixed in tree, not re-verified on hardware):** exact first scan overflowed the 256 MiB resident match list. Old path used **single-range** resident START, which streams the full hit list on overflow; draining that as a u64 stream **desynced :744**. Then the client retried a **full-region snapshot**, which the console declined (`snapshot_ok=0` = ENOSPC / bitmap > 448 MiB / `/data` full). The 4-byte timeout is leftover protocol, **not** rest mode.

**Fix in tree:**

- Always **segmented** resident START (even for one range). Segmented overflow = empty sentinel, not a dump.
- Drain overflow with `drain_result_blocks` (u64 length + payload until sentinel), never `_recv_u64_stream`.
- Snapshot+exact COUNT retry **only if** `snapshot_fits()` (bitmap ≤ `SNAP_BITMAP_MAX` = 448 MiB). Else `ScanUnsupported(TOO_MANY_MATCHES)` — tell the user to First Scan a less common in-game value.
- `classify_maps` fallback skips `SceGnm` by name (no PCD bit without TURBOSCAN_REGIONS).
- ConnectionLost copy now says reconnect / desync; rest mode is secondary.

Helpers: `web/nitepr5_core/scan.py` — `snapshot_fits`, `snapshot_bitmap_bytes`, `drain_result_blocks`, `TOO_MANY_MATCHES`. Tests: `test_snapshot_fits_bitmap_cap`, maps-fallback GPU excluded (`count == 3`).

### What the next agent should do

1. Restart uvicorn + hard-refresh the browser (`app.js` cache).
2. User: **Disconnect then Connect** (or restart uvicorn). The old :744 session is likely desynced.
3. Re-run First Scan on **CUSA13762** with a **less common changing integer** (not 0/1). Unknown-initial on the whole writable set can still decline if the snapshot is huge.
4. If that hunt works, continue Phase 2 exit (Next Scan a few times) and Phase 3 exit (poke confirm → freeze → save GoldHEN JSON in `web/cheats/` → reload + toggle).
5. Only then mark Phase 2/3 `done`. Still no Phase 4 unless the user overrides.

```powershell
python -m pytest web/tests web/nitepr5_core/tests -q
python -m uvicorn app:app --app-dir web --host 127.0.0.1 --port 1744
```

UI: `http://127.0.0.1:1744` → attach `eboot.bin`. Host `192.168.4.42`. ps5debug-NG v1.3.0 via elfldr **9021** (not Toolbox PS5Debug). Rest mode **does** drop 744 after sleep; that is separate from the 4-byte desync.

**Hex/watch poll ≠ hung scan.** After attach, uvicorn spams `GET /api/read` ~4 Hz and `GET /api/watch/poll` ~10 Hz. `POST /api/scan/start` logs only when it **finishes**. During a hunt: `paused (scan)`; watch/freeze timers must stop.

Gitignored: `.ps5debug-host` (`192.168.4.42`), `web/cheats/**` except `.gitkeep`.

### Read in this order

1. `AGENTS.md`
2. `docs/STATUS.md`
3. `docs/ARCHITECTURE.md` §3, §5.1, §5.3, Phase 3–4 exit
4. **This file** — paste Phase 0 + 1 + 2 + **3** into every worker prompt
5. `docs/ORCHESTRATION.md` Phase 4 playbook — **only** after Phase 3 `done` or user override

Forbidden until Phase 4/5: overlay, plugin, pointer/AOB/disasm, unbounded polling, `PT_ATTACH`.

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

**Phase 2 did not implement** `write` / `watch_*` / `freeze_*` / `cheat_*` — **Phase 3 added them.** Do not re-add.

Compare names: `exact` / `equal`, `increased`, `decreased`, `changed`, `unchanged`. Unknown first scan: `unknown=True` or `compare="unknown"` — never the default.

Undo: empty stack → `turboscan_end` and hunt ends (`scan_undo() is None`). Next-scan undo restores a previous generation **only if that count was ≤256**; else `UndoTooLarge`.

### Region classify

`Session._scan_segments` calls **`TURBOSCAN_REGIONS` with `probe_bytes=1`** (never 0). The 1-byte probe still returns leaf-PTE **PCD** (uncached). Then `classify_turbo_regions` keeps writable, non-executable, **cached** ranges. If the probe fails (`None`), fall back to cached `maps()` + `classify_maps` (prot-only, no PCD bit).

**Never pass `probe_bytes=0`.** That is the server default of **64 KiB × every readable map** (CUSA13762 ≈ **370**, including Garlic ~40 MB/s) and hangs before turbo starts. Mock: `regions_probe_count == 1` and `last_regions_probe_bytes == 1` after `scan_start`; planted `SceGnm` GPU hit is excluded.

Logger `nitepr5` (INFO): `first scan: N rw- segments, X MiB (pcd|maps-fallback)`. `web/app.py` sets that logger to INFO.

### Turbo path (ps5dbg 0.1.1)

- `authenticate(flags=2)` before stateful scan (`scan_caps` does not need auth)
- Caps → cheap region classify (`probe_bytes=1`) → `turboscan_start_resident` (one range) or **segment gap-fill**
- Multi-segment: flags `TS_SERVER_RESIDENT | TS_SNAPSHOT_SEGMENTS` plus **`TS_USE_ALIASING` when `TSE_ALIASING` is advertised** (`TS_SNAPSHOT` **clear**; never `TS_PARALLEL_COMPARE` on resident). After the two value acks, send `u32 count` then `count × {u64 addr; u32 length}` (12 bytes each). Packet address/length ignored. Bound `1 .. 1048576`. Reply `{u32 resident_stored; u64 count}`. `resident_stored==0` → declined. Implemented in `scan.turbo_start_resident_segments` using `ps5dbg.turboscan` packing + `Cmd` + `connection` — **no new opcodes**. `turboscan_start_resident` in ps5dbg 0.1.1 does **not** send this list.
- Next: `turboscan_count_resident` with **`TS_RESCAN_ALIASING` when advertised** (progress is a stream of `u64` until `0xFFFFFFFFFFFFFFFF` — **8-byte recvs**)
- Results: `turboscan_get_resident` only if `count ≤ 256`
- Unknown: `TS_SNAPSHOT | TS_SNAPSHOT_SEGMENTS | TS_SERVER_RESIDENT` (+ aliasing if advertised). Drain plan/progress/summary; never download RAM. No iterative fallback for unknown → `ScanUnsupported`. ps5dbg raises `NotImplementedError` if you pass `TS_SNAPSHOT` into `turboscan_start_resident`; use `turbo_start_snapshot_segments`.
- Fallback **only if turbo caps are missing**: `protocol.proc_scan_start/count/get` (client-held candidates, still ≤256 rows out). If turbo exists but `resident_stored==0` on an exact first scan, **retry snapshot + exact COUNT** (bitmap stays on the console; common values overflow the 256 MiB match list). If snapshot is missing or declined → `ScanUnsupported`. Never stream the hit list. Unknown never uses iterative.
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

`MockTransport.PLANTED_U32 = 100` at `WRITABLE_EBOOT_ADDR`, `HEAP_A`, `HEAP_C`. r-x decoy at `DECOY_RX_ADDR` must not match. Uncached `SceGnm` at `GPU_ADDR` must not match when the cheap region probe works. `poke_u32` for next-scan. `get_resident_calls` / `unbounded_get_attempted` / `regions_probe_count` / `last_use_aliasing` / `last_rescan_aliasing`. `NITEPR5_MOCK=1` still uses mock (no real turbo).

### Live hardware lessons (do not re-discover)

| Observation | Meaning |
|---|---|
| `GET /api/read?addr=29097984&n=512&pid=115` repeating 200 | Peephole 4 Hz at **`0x1bc0000`** (first writable `executable` rw- map, size 229376). Normal after attach. |
| Same pid **115**, title **CUSA13762** / app_ver **01.02** | Same launch as Phase 1 exit until the game is closed; pid changes next launch. |
| `GET /api/read` → **409** after uvicorn restart | Old tab still polling; `NotConnected`. Refresh the page and Connect again. |
| `timed out reading 8 bytes` + rest-mode hint | 10s socket timeout during turbo progress — **fixed** (`scan_wait`). Reconnect once if the old socket desynced. |
| First Scan “hung”, then `POST /api/scan/start 200` on Ctrl+C | Hunt was in flight; uvicorn logs completion only. Cause: **`TURBOSCAN_REGIONS` 64 KiB × ~370 maps** — **fixed** (`probe_bytes=1` + skip PCD). |
| Hex poll continuing during a hunt | Must not happen with current JS. If it does, uvicorn was not restarted. |

### Orchestration (what we spawned)

1. **Core first, alone** — `web/nitepr5_core/`
2. **Then parallel** — UI (`web/app.py`, `web/static/`) and HTTP tests (`web/tests/`)
3. Orchestrator unify: First Scan replaces idle hunt; then timeout/lock/hex-pause; then cheap `TURBOSCAN_REGIONS` (`probe_bytes=1`) + aliasing flags

### Traps for Phase 3+

- Watch/freeze/write live in `nitepr5_core`. No second memory path. UI timers (~10 Hz / 15 Hz) must take the same `:744` `_io` lock (or pause during scan) — do not interleave protocol on one socket.
- Count first; cap **256** results. Never `regions="all"`. Never `PS5Debug.scan()`.
- Do not open a second `:744` connection for scans (survivors are per TCP session).
- Do not call `TURBOSCAN_REGIONS` with `probe_bytes=0` on the hot path (64 KiB × all readable maps). Use `probe_bytes=1`.
- Do not set `TS_PARALLEL_COMPARE` on resident START (streaming-only; over-subscribes with aliasing).
- Do not iterative-fallback when turbo resident declines (`resident_stored=0`). Retry snapshot + exact COUNT **only if** the membership bitmap would be ≤ 448 MiB (`TS_SNAP_BITMAP_MAX`). Otherwise tell the user to pick a less common value — never stream the hit list.
- Always use **segmented** resident START (even for one range). Single-range overflow streams the full hit list; draining that as `_recv_u64_stream` desyncs :744 (`timed out reading 4 bytes` on the next command). Segmented overflow is an empty sentinel.
- `snapshot_ok=0` is ENOSPC / bitmap too large / `/data` full — not rest mode. Reconnect after a desynced scan.
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

## Phase 3 — Hex, watch, freeze, JSON (`code_complete` 2026-08-21; hardware exit **not** passed)

**Job:** first shippable editor without a TV UI.

**Exit (ARCHITECTURE, not yet):** change a value in-game from hex; freeze it; save a cheat; reload the file and toggle it. Do not mark `done` from mocks.

`python -m pytest web/tests web/nitepr5_core/tests -q` → **81 passed** (includes overflow/desync tests).

### What shipped

| Piece | Path |
|---|---|
| Session | `write`, `watch_*`, `freeze_*`, `cheat_*`; list helpers `watches()` / `freezes()` / `loaded_cheat()` / `cheat_enabled_names()` |
| GoldHEN parse | `web/nitepr5_core/cheat.py` |
| FastAPI | write / watch / freeze / cheat routes on the process-global `SESSION` |
| Cheats dir | `web/cheats/` (gitignore `web/cheats/**`, keep `.gitkeep`) |
| UI | poke with `window.confirm`; watch table ~10 Hz; freeze tick ~15 Hz; cheat load/save/toggle |
| Tests | `web/nitepr5_core/tests/test_phase3_core.py`, `web/tests/test_phase3_http.py`, `web/tests/http_phase3_contract.py` |
| Caps | `WRITE_MAX=4096`, `WATCH_MAX=64`, `FREEZE_MAX=32`, `RESULTS_MAX=256` — refuse, do not truncate |

### Session API (Phase 3)

```python
write(pid, addr, data: bytes) -> None          # 1..4096; _hold_io(block=False)
watch_add(pid=None, *, addr, n=4, label="") -> WatchEntry
watch_remove(watch_id) -> None
watch_poll() -> list[WatchValue]               # no thread; UI polls ~100 ms
watches() -> list[WatchEntry]
freeze_add(pid=None, *, addr, data: bytes) -> FreezeEntry  # data 1..8
freeze_remove(freeze_id) -> None
freeze_tick() -> int                           # no 15 Hz loop in core
freezes() -> list[FreezeEntry]
cheat_load(path) -> CheatFile
cheat_save(path, cheat=None) -> None           # no extra JSON keys (no enabled)
cheat_toggle(name, enabled: bool) -> None      # writes on/off at module_base+offset
loaded_cheat() -> CheatFile | None
cheat_enabled_names() -> list[str]
```

Watch `n` in `{1,2,4,8}`. Duplicate watches allowed. 65th → `WatchLimit`. 33rd freeze → `FreezeLimit`. Disconnect and retarget clear watches/freezes. Ids restart at 1.

Module base for cheats = **lowest `maps().start`** whose `name` equals `cheat.process` (usually `eboot.bin`). `offset` is hex relative to that base.

`write` / `watch_poll` / `freeze_tick` / `cheat_toggle` take `_io` **non-blocking** → `ScanActive` during a hunt. Transport `write` is `PS5Debug.write(pid, address=…, data=…)`. No `PROC_WRITE_MULTI` opcode (ps5dbg 0.1.1 loops per patch).

### REST (bytes = hex strings; addresses = int)

| Method | Path | JSON |
|---|---|---|
| POST | `/api/write` | `{addr, data, pid?}` → `{ok, addr, n}` |
| POST | `/api/watch` | `{addr, n?=4, label?}` → `{id,pid,addr,n,label}` |
| GET | `/api/watch` | `{watches:[…]}` |
| GET | `/api/watch/poll` | `{values:[{id,addr,data}]}` |
| DELETE | `/api/watch/{id}` | `{ok}` |
| POST | `/api/freeze` | `{addr, data}` → `{id,pid,addr,data}` |
| GET | `/api/freeze` | `{freezes:[…]}` |
| POST | `/api/freeze/tick` | `{written}` |
| DELETE | `/api/freeze/{id}` | `{ok}` |
| GET | `/api/cheats` | `{files:[basename,…]}` sorted; `web/cheats/` |
| POST | `/api/cheat/load` | `{filename}` → `{cheat, enabled}` |
| POST | `/api/cheat/save` | `{filename, cheat?}` → `{ok, filename}` |
| POST | `/api/cheat/toggle` | `{name, enabled}` → `{ok, name, enabled}` |
| GET | `/api/cheat` | `{cheat: null\|dict, enabled:[str]}` |

Cheat `filename` must match `^[A-Za-z0-9._-]+\.json$` (no slashes / `..`). Traversal → `InvalidCheat` 400. Never `/data/etaHEN/cheats/`.

New 400s: `WatchLimit`, `FreezeLimit`, `NoWatch`, `NoFreeze`, `InvalidWriteSize`, `WriteTooLarge`, `InvalidWatchSize`, `InvalidFreezeSize`, `InvalidCheat`, `NoCheat`, `NoMod`. Empty / odd / non-hex `data` → `InvalidWriteSize`. `ScanActive` still 400. `NotConnected` 409.

### UI

Vanilla JS IIFE. Hex bytes are clickable spans → prompt → **confirm** → `POST /api/write`. Watch poll 100 ms; freeze tick ~67 ms; both pause on `scanBusy`, hex Pause, and `visibilitychange` hidden. `resetTargetUi()` still runs **before** `/api/connect` and also stops watch/freeze timers. Scan count-first unchanged (no results fetch if count > 256).

### Orchestration (what we spawned)

1. **Core first, alone** — `web/nitepr5_core/` write/watch/freeze/cheat
2. **Then parallel** — UI (`web/static`), HTTP (`web/app.py` + `web/cheats/`), tests (`web/tests/`)

### Traps for Phase 4+

- Watch/freeze timers must keep using the same `:744` `_io` lock (fail fast `ScanActive`). Do not open a second debugger socket for freeze.
- Do not put a 10 Hz / 15 Hz loop in the plugin’s copy of the protocol until Phase 4 moves freeze on-console on purpose.
- Keep GoldHEN JSON only. Do not write etaHEN’s cheat folder unless the user exports there.
- After code changes: **restart uvicorn** and hard-refresh `app.js`.
- `plugin/` and `overlay/` still forbidden until those phases.

### Phase 3 file tree (added on top of Phase 2)

```
web/nitepr5_core/cheat.py
web/nitepr5_core/tests/test_phase3_core.py
web/tests/http_phase3_contract.py
web/tests/test_phase3_http.py
web/cheats/.gitkeep
```

Also touched: `session.py`, `transport.py` (`write` + `write_calls`), `constants.py`, `errors.py`, `types.py`, `__init__.py`, `app.py`, `static/{index.html,app.js,style.css}`.

---

## Not done (do not start until asked)

| Phase | Job |
|---|---|
| 2 hardware | CUSA13762 changing-integer hunt (required before Phase 2 `done`) |
| 3 hardware | poke, freeze, save JSON, reload + toggle (required before Phase 3 `done`) |
| 4 | etaHEN plugin daemon |
| 5 | Overlay spike B2 **or** B3 |
