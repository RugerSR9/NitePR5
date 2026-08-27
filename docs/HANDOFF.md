# NitePR5 — Compressed handoff

Paste the **completed-phase** blocks into every worker brief. Workers have no chat history. Do not re-discover these facts.

Current board: [STATUS.md](STATUS.md). Spawn rules: [ORCHESTRATION.md](ORCHESTRATION.md). Parent: [../AGENTS.md](../AGENTS.md).

---

## Next orchestrator — start here (2026-08-27)

Phases **0–4 are `done`** on hardware. **Phase 5 is `code_complete` (B3)** — hardware exit not run. Do not re-implement Phases 0–4. **No PS5 ELF cross-compile** on this Windows PC. GitHub Actions builds `nitepr5.plugin` and `overlay.elf`. **Do not kill the overlay spike.**

| Fact | Value |
|---|---|
| Phases 0–3 | `done` (CUSA13762: hunt, poke, freeze, GoldHEN save/reload/toggle) |
| Phase 4 | `done` — NTPR50001 loads; plugin freeze overwrites web pokes (user 2026-08-26) |
| Mock tests | `python -m pytest web/tests web/nitepr5_core/tests -q` → **121 passed** (2026-08-25 Phase 4 plugin bind) |
| Phase 5 | **B3** `code_complete`. Overlay+plugin **0.571** int3 stager; hooks off. |

### Phase 4 locked contract (user 2026-08-25)

Paste into every Phase 4 worker brief.

- **Handoff:** when the plugin is armed, **it owns freeze**. Web **stops** `POST /api/freeze/tick`. Scan / hex / watch stay on the PC (`:744` LAN). Plugin writes via **`127.0.0.1:744`**.
- **Title** `NTPR50001` (`^[A-Za-z]{4}\d{5}$`), version `0.40`. **Command port 1745** (HTTP, JSON). Not 744 / 9021 / 9028 / 9999 / 1744. Do not use `NPR500001` (3 letters + 6 digits — etaHEN will not load it).
- **Caps:** freeze ≤32, data 1–8 bytes, tick 15 Hz. 33rd freeze → refuse. Cheat toggle uses GoldHEN `on`/`off` at `module_base+offset`. `executable` maps count as `eboot.bin`.
- **PROC_WRITE:** two-phase only (16-byte `<IQI` pid, addr, length LE → ACK → payload → FINAL). Never pack payload into `datalen`.
- **Persist:** `/data/nitepr5/state.json` + cheats in `/data/nitepr5/cheats/`. Never `/data/etaHEN/cheats/`.
- **Notify (B1):** toast on start / armed / `:744` missing. No ShellUI, no game PRX, no pad, no `libhijacker` into the title.
- **No ELF on this Windows PC.** Do not install a PS5 toolchain, WSL, or Docker locally. GitHub Actions compiles (`workflow_dispatch` artifact or Release asset).

**Command channel (plugin listens on LAN :1745):**

| Method | Path | Body / result |
|---|---|---|
| GET | `/status` | `{ok, armed, pid, freeze_count, cheat_id, enabled:[str], dbg:bool}` |
| POST | `/arm` | `{pid, freezes:[{addr, data}], cheat?: GoldHEN object, enabled?:[str]}` → `{ok, armed:true, freeze_count}` — `data` hex; max 32; persist state |
| POST | `/disarm` | `{ok, armed:false}` — stop freeze tick; keep files |
| POST | `/cheat/toggle` | `{name, enabled}` → `{ok, name, enabled}` |
| POST | `/cheat/load` | GoldHEN JSON body or `{filename}` under `/data/nitepr5/cheats/` |

Addresses are JSON integers (not hex strings). Bytes are hex strings. Empty freeze list + no cheat is a valid disarm-equivalent arm.

### Phase 4 SDK facts (explore 2026-08-25)

- Install: USB `<usb>/etaHEN/plugins/` (priority) or `/data/etaHEN/plugins/`. Toolbox kill/run. File is **`.plugin`** = `etaHEN_PLUGIN\0TID\0version\0` + ELF (`plugin/scripts/make_plugin.py`). TID `^[A-Za-z]{4}\d{5}$`, version `^\d\.\d{2}$`. Locked **NTPR50001**. CI: `.github/workflows/plugin.yml`.
- CMake: copy **utility_daemon** (not Injector / Error_Disabling). `PLUGIN_TITLE_ID NTPR50001`, `PLUGIN_VERSION 0.40`, basename `nitepr5`. Link `SceLibcInternal SceSystemService SceNet SceSysmodule SceUserService SceNetCtl kernel_sys`. **No** `hijacker`, **no** `ScePad`.
- Notify: classic `notify_request` `{char useless1[45]; char message[3075];}` + `sceKernelSendNotificationRequest(0,&req,sizeof req,0)`. Do not use libhijacker `printf_notification`.
- Sockets: **POSIX** `socket/bind/listen/accept` on `0.0.0.0:1745` (utility_daemon `tcp.c`). Not sceHttp2.
- JSON: vendor MIT **cJSON 1.7.17** to `plugin/third_party/cjson/`. Plugins are already jailbroken; **no 9028**. `mkdir` `/data/nitepr5/` is OK.
- Thin `:744` client (not a protocol fork): header `<III` magic `0xFFAABBCC`, cmd, datalen. Status raw word `SUCCESS=0x80000000` (already bitswapped; do not un-swap). `PROC_WRITE=0xBDAA0003`: send 16-byte `<IQI` pid,addr,len **only** (datalen=16), ACK, then payload, FINAL. Never concatenate payload into the request body. Also: `PROC_LIST=0xBDAA0001`, `PROC_MAPS=0xBDAA0004` (body u32 pid; entries 58 B), `CONSOLE_FOREGROUND_APP=0xBDDD0006` (140 B). No turbo, no `PT_*`, no `PROC_AUTH` unless a write is refused.
- Do not copy: libhijacker, Game_Plugin_Loader, Plugin_samples/PS5Debug, pad hooks, SDK `sceNotificationSend`.

### What the next agent should do

Phase 4 is **`done`**. Phase 5 backend is **B3**, **code_complete** (not hardware-done). Plugin **0.571** auto-injects via **int3 stager** (not `pt_call(scePthreadCreate)` — that followed the overlay thread and smashed regs, CE-108255-1 after "injected"). Overlay **0.571** `overlay_gate` waits ~2 s then CRT; `main()` never returns; no game ucred widen; **GNM/pad hooks off** (pad poll only). Do not kill the overlay spike. Do not send overlay to elfldr **9021**. Do not NineS `:9033`.

### Phase 4 plugin tree

`plugin/` exists. Title NTPR50001 / **0.571**. HTTP :1745. Inject: int3 stager after elfldr_load. Heartbeat `/data/nitepr5/overlay.alive`. ELF is built in GitHub Actions, not on this Windows PC.

`make_plugin.py` uses stock etaHEN TID `^[A-Za-z]{4}\d{5}$`. **NTPR50001** matches. **NPR500001** did not (3+6) and etaHEN refused to load it.

Notify missing `:744` is rate-limited (`g_dbg_missing_told`). Freeze tick ~67 ms poll loop. Caps 32. GoldHEN + `executable` module base. Persist `/data/nitepr5/state.json`.

### Phase 5 Wave 0 (explore 2026-08-26) — paste into Phase 5 workers

User: full on-TV editor (Live / Watch / Freeze / Cheats / related views). No turbo scan on TV. Overlay talks to plugin `:1745` only — **never a second `:744`**. **Backend locked B3** (user 2026-08-26). B2 is dead.

| Backend | Verdict |
|---|---|
| **B2** ShellUI PUI | **NO-GO.** Overlay Labels + DualSense shortcuts are Toolbox-in-`SceShellUI` only. No plugin PUI API. Fork required. |
| **B3** in-game ELF | **GO with caveats.** Inject + GNM flip tick + pad PLT exist. `fps_elf` only toasts FPS; translucent multi-view HUD is **greenfield**. |
| Plugin I/O | **GO.** `PROC_READ=0xBDAA0002` one-phase (`<IQI` pid,addr,len → SUCCESS → `length` bytes). Not two-phase. Cap `n` 1..4096. |

**B3 spike shape (if user confirms):** `overlay/` = Johns SDK ELF (fps_elf-shaped), deploy `/data/nitepr5/overlay.elf`. Plugin auto-injects via Johns elfldr_inject at game launch — **do not** steal `scePadReadState` PLT if etaHEN FPS already did (HookGame fights). Draw+pad in the ELF. Memory path: HTTP `127.0.0.1:1745` → NTPR50001 → `:744`. In-process game `memcpy` **forbidden**. Combo: L1+R1+Touchpad open/close; Cross commit; Circle cancel. Leave ShellUI shortcuts alone.

**Do not kill B3.** User 2026-08-27: overlay spike is not optional.

**Wave 2 overlay (source 2026-08-27):** Inject: plugin **0.571** int3 stager ~12 s after launch. Overlay **0.571** `overlay_gate` then CRT; hooks off; pad poll only.

**Wave 1 plugin `:1745` (implemented 2026-08-26, version 0.57):** See `plugin/README.md`. `overlay_open` RAM-only. Watches RAM-only, cleared on `/overlay/close`. Freeze persists. `POST /disarm` keeps `:744` if overlay open. Session web API unchanged. Overlay open must happen **before** `/foreground`/`/read` (plugin `require_dbg`). Injected ELF should `POST /overlay/open` with `getpid()`. Auto-inject lists eboot on `:744` then **disconnects** before `pt_attach`. `POST /overlay/inject` for a manual retry.

### Last live incident (fixed; hardware re-verified 2026-08-25)

User First Scan (exact, common value) then:

1. `server declined unknown snapshot (snapshot_ok=0)`
2. Next command: `timed out reading 4 bytes from 192.168.4.42:744` + rest-mode hint

**Cause (fixed; user confirmed editor loop works):** exact first scan overflowed the 256 MiB resident match list. Old path used **single-range** resident START, which streams the full hit list on overflow; draining that as a u64 stream **desynced :744**. Then the client retried a **full-region snapshot**, which the console declined (`snapshot_ok=0` = ENOSPC / bitmap > 448 MiB / `/data` full). The 4-byte timeout is leftover protocol, **not** rest mode.

**Fix in tree:**

- Always **segmented** resident START (even for one range). Segmented overflow = empty sentinel, not a dump.
- Drain overflow with `drain_result_blocks` (u64 length + payload until sentinel), never `_recv_u64_stream`.
- Snapshot+exact COUNT retry **only if** `snapshot_fits()` (bitmap ≤ `SNAP_BITMAP_MAX` = 448 MiB). Else `ScanUnsupported(TOO_MANY_MATCHES)` — tell the user to First Scan a less common in-game value.
- `classify_maps` fallback skips `SceGnm` by name (no PCD bit without TURBOSCAN_REGIONS).
- ConnectionLost copy now says reconnect / desync; rest mode is secondary.

Helpers: `web/nitepr5_core/scan.py` — `snapshot_fits`, `snapshot_bitmap_bytes`, `drain_result_blocks`, `TOO_MANY_MATCHES`. Tests: `test_snapshot_fits_bitmap_cap`, maps-fallback GPU excluded (`count == 3`).

### Freeze / cheat save (fixed 2026-08-24; hardware re-verified 2026-08-25)

Live: `POST /api/freeze/tick` and `GET /api/watch/poll` (and often `GET /api/read`) returned **400** in a tight loop. Save freezes as cheat: **“No eboot.bin map — cannot compute offsets”**.

**Cause:** hex @ 4 Hz, watch @ 10 Hz, freeze @ 15 Hz all used `_hold_io(block=False)`, so any overlap was `ScanActive`. Live `maps()` name the ELF **`executable`**, not `eboot.bin` (CUSA13762 first writable was `executable` @ `0x1bc0000`). The UI matched `name === "eboot.bin"` only.

**Fix in tree:** `_busy` fail-fast only; peers **wait** (no 250 ms timeout — that 400'd `POST /api/write` while hex/freeze held :744). ConnectionLost **disconnects** the client so later I/O is 409, not a 10 s hang loop. UI `onLostConnection` stops polls. `map_belongs_to_process` treats `executable` as eboot. `cheat_from_freezes` + `{from_freezes: true}`.

Tests: `test_write_waits_for_peer_hold_longer_than_quarter_second`, `test_dropped_console_io_disconnects_so_later_calls_are_409`, `test_http_dropped_console_is_409_not_repeated_400`, `test_cheat_from_freezes_with_executable_maps`.

**One browser tab.** Two tabs both poll :744 through one Session and will fight. Close extras, restart uvicorn, Connect again.

### PROC_WRITE hang (fixed 2026-08-24; hardware re-verified 2026-08-25)

Live: `GET /api/watch/poll` 200, then `NotConnected: timed out reading 4 bytes from 192.168.4.42:744` on `POST /api/write` 409. Later read/watch also 409.

**Cause:** ps5dbg 0.1.1 `protocol.proc_write` does `send_request(PROC_WRITE, 16-byte packet + payload)` so `datalen` includes the payload. ps5debug-NG reads `datalen` as the request body, ACKs, then `net_recv_all(length)` for a **second** copy of the payload. The client times out on the **second** 4-byte status. Reads still work (one status + body). PROTOCOL.md: packet only → ACK → stream payload → FINAL.

**Fix in tree:** `proc_write_phased` in `web/nitepr5_core/transport.py` — `Cmd.PROC_WRITE` + `Connection.send_request` with the **16-byte packet only**, then `_sendall(payload)`. Do **not** call `PS5Debug.write()` / `protocol.proc_write` until upstream is fixed. UI `withPollsPaused` stops hex/watch/freeze around a poke so they do not interleave on :744.

Tests: `test_proc_write_phased_sends_16_byte_packet_then_payload`, `test_ui_pauses_polls_during_poke`.

Hardware and UI notes (still true):

```powershell
python -m pytest web/tests web/nitepr5_core/tests -q
python -m uvicorn app:app --app-dir web --host 127.0.0.1 --port 1744
```

UI: `http://127.0.0.1:1744` → Connect → auto-opens `eboot.bin` when present. Hard-refresh after static changes. Host `192.168.4.42`. ps5debug-NG v1.3.0 via elfldr **9021** (not Toolbox PS5Debug). Rest mode **does** drop 744 after sleep; that is separate from the 4-byte desync.

**Hex/watch/freeze poll ≠ hung scan.** After attach, uvicorn logs `GET /api/read` ~4 Hz, `GET /api/watch/poll` ~10 Hz, and `POST /api/freeze/tick` ~15 Hz — those must be **200**, not 400. `POST /api/scan/start` logs only when it **finishes**. During a hunt: `paused (scan)`; watch/freeze timers must stop.

Gitignored: `.ps5debug-host` (`192.168.4.42`), `web/cheats/**` except `.gitkeep`.

### Read in this order

1. `AGENTS.md`
2. `docs/STATUS.md`
3. `docs/ARCHITECTURE.md` §3, §5.1, §5.3, Phase 3–4 exit
4. **This file** — paste Phase 0 + 1 + 2 + **3** into every worker prompt
5. `docs/ORCHESTRATION.md` Phase 5 — B3 source complete; hardware exit is plugin auto-inject + poke

Forbidden: B2+B3 together, pointer/AOB/disasm, unbounded polling, `PT_ATTACH` for editor R/W (plugin B3 elfldr inject window is the exception), second `:744` from overlay, ELF cross-compile on this Windows PC (CI is allowed).

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

## Phase 2 — Scan loop (`done` 2026-08-25)

**Job:** classic NitePR search loop on the PC. Count-first; turbo on-console; ≤256 result rows.

**Exit (ARCHITECTURE, passed):** find a known changing integer in **CUSA13762** with a few next-scans. User confirmed 2026-08-25. Early live attempts failed (segmented START / snapshot cap / desync — see Next orchestrator); those fixes shipped before the passing hunt.

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
- Next: `turboscan_count_resident` **without** `TS_RESCAN_ALIASING` (progress is still a stream of `u64` until `0xFFFFFFFFFFFFFFFF` — **8-byte recvs**). Then `PROC_NOP` with the normal 10s timeout before hex/watch resume. Do not OR rescan-aliasing: the plugin (or any second :744 client) over-subscribes it; live Next Scan 200 then peephole `timed out reading 4 bytes`.
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

Vanilla JS IIFE, `fetch('/api/...')` only. Scan lives in a **drawer**, not a stacked page section. Value: decimal or `0x` hex. First: Exact, optional Unknown checkbox. Next compare select. Buttons disabled while `scanBusy`; Next/Undo disabled until a hunt exists. Hex status: `live` vs `paused (scan)`.

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

## Phase 3 — Hex, watch, freeze, JSON (`done` 2026-08-25)

**Job:** first shippable editor without a TV UI.

**Exit (ARCHITECTURE, passed):** change a value in-game from hex; freeze it; save a cheat; reload the file and toggle it. User confirmed 2026-08-25.

`python -m pytest web/tests web/nitepr5_core/tests -q` → see STATUS (includes PROC_WRITE two-phase + freeze-tick lock + executable-map cheat tests).

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

Module base for cheats = **lowest `maps().start`** whose name is `cheat.process`, its basename, **or `executable` when process is `eboot.bin`** (live CUSA13762). `offset` is hex relative to that base. `POST /api/cheat/save` with `{from_freezes: true, name, id, version}` builds GoldHEN from the freeze list (`Session.cheat_from_freezes`).

`write` / `watch_poll` / `freeze_tick` / `cheat_toggle` fail fast with `ScanActive` only when `_busy` (a hunt). Hex/watch/freeze/poke **wait** for each other (no 250 ms timeout — that 400'd pokes). Same-thread re-entry is allowed (scan `maps()` fallback). **ConnectionLost / socket timeout drops the client** (`transport.disconnect`); later calls are 409, not a hung 10 s read loop. Transport `write` is **`proc_write_phased`** (16-byte packet, then payload) — **not** `PS5Debug.write()` / `protocol.proc_write` (ps5dbg 0.1.1 packs payload into `datalen` and times out on the second status). No `PROC_WRITE_MULTI` opcode (ps5dbg 0.1.1 loops per patch). UI pauses hex/watch/freeze around a poke (`withPollsPaused`).

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
| POST | `/api/cheat/save` | `{filename, cheat?}` or `{filename, from_freezes:true, name, id, version, process?}` → `{ok, filename, cheat}` |
| POST | `/api/cheat/toggle` | `{name, enabled}` → `{ok, name, enabled}` |
| GET | `/api/cheat` | `{cheat: null\|dict, enabled:[str]}` |

Cheat `filename` must match `^[A-Za-z0-9._-]+\.json$` (no slashes / `..`). Traversal → `InvalidCheat` 400. Never `/data/etaHEN/cheats/`.

New 400s: `WatchLimit`, `FreezeLimit`, `NoWatch`, `NoFreeze`, `InvalidWriteSize`, `WriteTooLarge`, `InvalidWatchSize`, `InvalidFreezeSize`, `InvalidCheat`, `NoCheat`, `NoMod`. Empty / odd / non-hex `data` → `InvalidWriteSize`. `ScanActive` still 400. `NotConnected` 409.

### UI

Vanilla JS IIFE. Hex canvas fills the viewport (ImHex-style chrome). Click selects; double-click or Enter prompts then **confirm** → `POST /api/write`. Watch poll 100 ms; freeze tick ~67 ms; both pause on `scanBusy`, hex Pause, and `visibilitychange` hidden. `resetTargetUi()` still runs **before** `/api/connect` and also stops watch/freeze timers. Scan count-first unchanged (no results fetch if count > 256). Drawers: scan, maps (filter + jump), watch, hold/freeze, cheats, processes. Go-to address and a typed inspector (u8/u16/u32/i32/f32) are UI-only.

### Orchestration (what we spawned)

1. **Core first, alone** — `web/nitepr5_core/` write/watch/freeze/cheat
2. **Then parallel** — UI (`web/static`), HTTP (`web/app.py` + `web/cheats/`), tests (`web/tests/`)

### Traps for Phase 4+

- Watch/freeze timers must keep using the same `:744` `_io` lock (fail fast `ScanActive` **only while `_busy`**). Do not 400 when hex poll overlaps freeze tick. Do not open a second debugger socket for freeze.
- Do not put a 10 Hz / 15 Hz loop in the plugin’s copy of the protocol until Phase 4 moves freeze on-console on purpose.
- Keep GoldHEN JSON only. Do not write etaHEN’s cheat folder unless the user exports there.
- After code changes: **restart uvicorn** and hard-refresh `app.js`.
- **Never bitwise-AND 64-bit VAs in JS.** `addr & ~0xf` / `n >>> 0` ToInt32-truncate heap (~`0x2_0000_0000+`) and produced `GET /api/read?addr=-389259952` → `struct.error` on `Q`. Align with modulo (`alignPeephole`); `parseGoto` uses `asAddr`. Session rejects addr not in `0..2**64-1` as `InvalidAddress` 400.
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
| 5 | **B3** `code_complete`. Hardware: **0.571** injected then running (not the reverse); game stays up; pad poll. Overlay spike is not optional. |
