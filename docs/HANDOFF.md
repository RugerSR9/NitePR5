# NitePR5 — Compressed handoff

Paste the **completed-phase** blocks into every worker brief. Workers have no chat history. Do not re-discover these facts.

Current board: [STATUS.md](STATUS.md). Spawn rules: [ORCHESTRATION.md](ORCHESTRATION.md). Parent: [../AGENTS.md](../AGENTS.md).

---

## Next orchestrator — start here (Phase 2)

Phases **0 and 1 are `done`**. Do **not** re-do them. The user asked to start Phase 2 in a **new** orchestrator session.

### Git (important)

Nothing in this tree has been committed unless the user did it themselves. **Do not `git commit` unless they ask.**

| State | Paths |
|---|---|
| **Untracked** | `web/` (all of Phase 1), `docs/HANDOFF.md` |
| **Modified** | `AGENTS.md`, `README.md`, `docs/STATUS.md`, `docs/ARCHITECTURE.md`, `docs/ORCHESTRATION.md`, `.cursor/rules/nitepr5.mdc`, `requirements.txt` |
| **Tracked (Phase 0)** | `scripts/check_ps5debug.py`, `scripts/send_ps5debug_elf.py`, `.env.example` |
| **Gitignored** | `.ps5debug-host` (contains `192.168.4.42`) |

If `web/` is missing, Phase 1 is gone — stop and restore from disk/backup. Do not start Phase 2 from docs alone.

### Read in this order

1. `AGENTS.md`
2. `docs/STATUS.md` — bump `active_phase: 2`, `phase_state: in_progress` when you start
3. `docs/ARCHITECTURE.md` §3, §5.1, §5.3, Phase 2 exit
4. `docs/ORCHESTRATION.md` Phase 2 playbook (below)
5. **This file** — paste Phase 0 + Phase 1 blocks into every worker prompt

### Phase 2 spawn order (serial core, then UI)

From ORCHESTRATION.md: **core scan first, alone**. Then UI. Optional parallel: region-classify helper + mock tests once `start/next/count` exist. Do not spawn UI that invents its own scanner.

| Agent | When | Allowed | Job |
|---|---|---|---|
| A Core | First, alone | `web/nitepr5_core/` | `scan_start` / `scan_next` / `scan_undo` / `scan_count` / `scan_results(limit)` |
| B UI | After A is in the tree | `web/static/`, `web/app.py` | First / Next / Undo; show **count**; fetch ≤256 rows only if count ≤256; disable buttons in flight |
| C Tests | After A (can parallel B) | `web/tests/` | Mock survivor sets; never unbounded GET |

**Exit (ARCHITECTURE):** find a known changing integer (ammo, money, timer) in **CUSA13762** with a few next-scans. Do not mark `done` from mocks.

**Job A extra:** `TURBOSCAN_*` first; `turboscan_regions` / region classify; skip XOM/uncached by default; unknown snapshot needs an explicit flag; **one scan per PID**; never `ps5.scan()` / `scan_aob_find_all`. Fallback: iterative `SCAN_START` / `COUNT` / `GET` (still server-side). `ps5.authenticate(flags=2)` is required before stateful scan/turbo. Default type: 4-byte unsigned, aligned. Default regions: writable + cached.

Cap: `scan_results(limit)` refuse limit > **256**. UI shows count when count > 256 (“narrow in-game, then Next”).

Forbidden in Phase 2: write, freeze, overlay, plugin, “All” as default, live-updating thousands of result rows.

### ps5dbg scan pointers (verify in the installed 0.1.1 package)

- `PS5Debug.authenticate(flags=2)` — scan auth
- `ps5dbg.turboscan`: `turboscan_caps`, `turboscan_start_resident`, `turboscan_count_resident`, `turboscan_get_resident`, `turboscan_end`, `turboscan_regions`
- Fallback: `ps5dbg.protocol.proc_scan_start` / count / get
- **Do not** use `PS5Debug.scan()` (whole process, all hits to PC)

### Hardware for the exit test

Keep **CUSA13762** (The Golf Club 2019) in the foreground. Pick a value that changes (score, timer, etc.). Host `192.168.4.42`. ps5debug-NG v1.3.0 must still be loaded (rest mode drops 744 — reconnect; UI must `resetTargetUi()` on Connect). `eboot.bin` pid **changes every launch** — always `foreground()`.

### Run

```powershell
python -m pip install -r requirements.txt
python -m pytest web/tests -q
python -m uvicorn app:app --app-dir web --host 127.0.0.1 --port 1744
```

`NITEPR5_MOCK=1` for UI without a console. Mock has no real turbo scan — extend `MockTransport` in Phase 2.

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

**Not implemented:** `write`, `scan_*`, `watch_*`, `freeze_*`, `cheat_*`.

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

### Phase 1 file tree (`web/` — untracked until someone commits)

```
web/app.py
web/nitepr5_core/{__init__,session,transport,host,types,errors,constants,py.typed}
web/nitepr5_core/tests/test_session.py
web/static/{index.html,app.js,style.css}
web/tests/{conftest.py,test_phase1_mock.py,http_read_contract.py,test_http_read_contract.py}
```

`app.js` is LF, ~356 lines. One process-global `SESSION` in `app.py`. Extra UI-only route: `GET /api/defaults`.

---

## Not done (do not start until asked)

| Phase | Job |
|---|---|
| 2 | Scan loop: turbo `scan_start/next/undo/count/results(limit)` |
| 3 | Hex poke, watch ≤64 @ 10 Hz, freeze ≤32 @ 15 Hz, GoldHEN JSON |
| 4 | etaHEN plugin daemon |
| 5 | Overlay spike B2 **or** B3 |
