# NitePR5 — Architecture

A memory editor for a jailbroken PS5. The name reserves original PSP **NitePR** and marks this as the PS5 project. Two UIs share one memory backend: a PC web editor, and later a semi-transparent in-game overlay. This document is the build order. Do not start a later phase until the current phase’s exit criteria pass.

## 1. Environment (locked)

| Item | Value |
|---|---|
| Console | PS5, firmware **9.60** |
| HEN | **etaHEN only** (do not stack OnionHEN, Kylin Core, CheatRunner, PHU Tools) |
| Debugger | **ps5debug-NG** (OpenSourcereR), sent to etaHEN **elfldr :9021** — not the Toolbox toggle |
| First test titles | PS4 games running on PS5 (`CUSA*`). Native PS5 (`PPSA*`) after overlay exists. |
| Dev PC | Windows, same LAN as the console |

ps5debug-NG listens on **TCP 744** (commands), **755** (debug interrupts), **UDP 1010** (LAN discovery). Rest mode drops 744; clients must reconnect after wake.

On **FW 8.00+** (including **9.60**), etaHEN’s Toolbox / Debug Settings **PS5Debug** switch is the old bundled Sistr0/CTN blob. The daemon sets `no_ps5debug` when `fw_ver >= 0x800` and the UI reports that firmware is unsupported. That is expected. Load **ps5debug-NG** `ps5debug-NG_v1.3.0.elf` (or newer) through etaHEN elfldr on **TCP 9021**, or via Toolbox **Plugin / Payload ELF**. Do not enable `PS5Debug=1` in `config.ini` on this firmware — that autoloads the same old blob.

## 2. What this product is

Original NitePR on PSP was an in-process plugin: search, hex-edit, freeze, and save cheats from an overlay while the game ran. NitePR5 is the same loop on PS5, not a trainer that only toggles downloaded files.

**In scope (full product, not all at once)**

- Connect to the foreground game
- Iterative value scan (exact / unknown / next-scan / undo)
- Hex view and poke
- Freeze addresses
- Save and load GoldHEN/etaHEN-compatible JSON cheats
- Semi-transparent on-TV overlay driven by DualSense
- PC web editor on the LAN

**Out of scope until explicitly pulled from the backlog**

- New jailbreak or debugger
- Online / multiplayer use
- SHN/MC4 import, pointer scanner, AOB, disassembler, watchpoints
- Picture-in-picture (scale the game framebuffer)
- In-process GPU present hooks for every native PS5 title

## 3. Critical constraints (do not ignore)

These are the gaps that would otherwise turn the project into a puzzle.

### 3.1 An etaHEN plugin cannot draw the system overlay

etaHEN **plugins** are background daemons loaded by the util daemon. They get filesystem, sockets, and (after jailbreak IPC) libhijacker. They do **not** get ShellUI drawing.

etaHEN’s CPU/GPU/RAM overlay is **hardcoded inside the Toolbox**, which is injected into `SceShellUI` and creates Mono PUI widgets on the game scene. Third-party plugins have no API to add those widgets.

etaHEN’s DualSense shortcuts are the same story: they live in a `GamePad.GetData` hook inside ShellUI. A daemon cannot assume it sees pad input while a game has the controller.

etaHEN’s FPS counter is a **third** path: libhijacker injects `/data/etaHEN/fps.prx` into the **game** and hooks `scePadReadState` plus GNM flip. That is in-process, per-game, and closer to original NitePR, but it is also the riskiest overlay backend.

**Consequence:** overlay is a separate display backend with a ladder of implementations. The memory editor must work fully from the web UI before any overlay work starts.

### 3.2 Do not `ptrace` a game from a random process

ps5debug-NG runs inside `SceShellCore` so `PT_ATTACH` looks like an SCE debug attach. A standalone payload that attaches itself will flag the game (AppContext) and the title stops progressing.

**Consequence:** all process R/W and scanning go through PS5Debug. Do not attach for v1. Pause-the-game (thread suspend) is a later feature that uses the debugger namespace on purpose.

### 3.3 Scan state is per TCP connection

ps5debug-NG threads each client. Turbo-scan survivor sets are **per connection**. The web UI and an overlay that each open `:744` will not share a scan list.

**Consequence:** v1 accepts that. Shared sessions need a broker (Phase 6). Until then, pick one UI as the scanner for a given hunt.

### 3.4 RAM is huge

PSP had tens of MB. PS5 games have gigabytes, and some regions are uncached (~40 MB/s). Never dump all RAM to the PC, never poll the whole process, and never list unbounded scan hits. See **§5.3**.

## 4. Technology (locked)

| Layer | Choice | Why |
|---|---|---|
| Memory backend | **ps5debug-NG** loaded via etaHEN elfldr | Firmware-ported scan, R/W, maps, freeze-friendly bulk write |
| PC protocol client | Python **`ps5dbg`** | Full v1.3.0 coverage; do not reimplement the wire protocol |
| Web app | **Python 3.10+**, FastAPI (HTTP + WebSocket), vanilla JS in `web/static` | Fast to iterate on Windows; no Node toolchain required for v1 |
| Console plugin (later) | **C**, [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk), etaHEN plugin CMake | Matches how plugins are actually loaded |
| Cheat files | **GoldHEN JSON** only at first | Same schema etaHEN already loads |
| Overlay | Interface + backends; **not** assumed to be the plugin itself | See §6 |

Do not add libhijacker, ShellUI hooks, or an in-game PRX until the overlay spike says that backend won.

## 5. Layered design

Each box is a process or library with a single job. Upper layers may be missing; lower layers must still work.

```text
┌──────────────────────────────────────────────────────────────┐
│  UI clients (optional, independent)                          │
│  ┌─────────────────────┐    ┌─────────────────────────────┐  │
│  │  web (PC browser)   │    │  overlay (TV, later)        │  │
│  │  Phase 1–3          │    │  Phase 5+                   │  │
│  └──────────┬──────────┘    └──────────────┬──────────────┘  │
└─────────────┼──────────────────────────────┼─────────────────┘
              │  NitePR5 session API         │
              ▼                              ▼
┌──────────────────────────────────────────────────────────────┐
│  nitepr5-core                                                │
│  Connect, target process, scan session, hex window,          │
│  freeze list, cheat file I/O                                 │
│  Phase 1 lives on the PC. Phase 6 may move this to a daemon. │
└────────────────────────────┬─────────────────────────────────┘
                             │  ps5dbg / TCP 744
                             ▼
┌──────────────────────────────────────────────────────────────┐
│  PS5Debug (existing, not our code)                           │
│  inside SceShellCore                                         │
└────────────────────────────┬─────────────────────────────────┘
                             ▼
                      Foreground game
```

**Rule:** a feature is implemented once in `nitepr5-core`, then bound to a UI. If a UI needs a one-off code path, the design is wrong.

### 5.1 NitePR5 session API (stable surface)

Keep this small. Both UIs call the same operations.

- `discover()` / `connect(host)`
- `processes()` / `foreground()` / `attach_target(pid)` (logical target, not `PT_ATTACH`)
- `maps(pid)` (cached; see §5.3)
- `read(pid, addr, n)` / `write(pid, addr, bytes)`
- `scan_start(...)` / `scan_next(...)` / `scan_undo()` / `scan_count()` / `scan_results(limit)`
- `watch_add` / `watch_remove` / `watch_poll`
- `freeze_add` / `freeze_remove` / `freeze_tick`
- `cheat_load` / `cheat_save` / `cheat_toggle`
- `plugin_status` / `plugin_arm` / `plugin_disarm` (Phase 4; HTTP to PS5:1745, not a second :744)

Anything not in this list is either a UI concern or backlog.

### 5.2 Repository layout (create folders when the phase starts)

```text
NitePR5/
  AGENTS.md                 ← orchestrator entry (spawn sub-agents)
  docs/ARCHITECTURE.md      ← product + exit tests
  docs/ORCHESTRATION.md     ← worker briefs
  docs/STATUS.md            ← active phase
  web/                      ← Phase 1 (Python + static JS)
  plugin/                   ← Phase 4 (etaHEN daemon)
  overlay/                  ← Phase 5 (display backends)
```

Do not create `plugin/` or `overlay/` until those phases begin.

### 5.3 Performance contract

NitePR5 must feel instant and must not hitch the game. That means **never treating the address space as a live stream**. PSP NitePR could scrape 32 MB. A PS5 title has gigabytes, plus slow uncached GPU (“Garlic”) regions that read around 40 MB/s. Reading “everything, all the time” will stall the title and flood the LAN.

The model is Cheat Engine’s, not a debugger memory dump:

| Surface | When it touches RAM | How much |
|---|---|---|
| **Scanner** | Only when you press First / Next Scan | Selected regions, **on the console**. PC gets a **count**, then at most a page of addresses |
| **Results list** | After a scan, and only if the count is small | Cap fetch at **256** rows. Above that: “narrow in-game, then Next Scan” |
| **Watch list** | Continuous, but tiny | User-pinned addresses only (**max 64**), polled at **~10 Hz** |
| **Hex peephole** | Only while that panel is visible | **512 bytes** default (max 4 KiB), refresh **~4 Hz**, with a Pause button |
| **Freeze / cheats** | On toggle, or a slow tick | **Max 32** frozen addresses, **15 Hz** writes via bulk write — NitePR’s “Cheat Hz” |

**Three rules**

1. **Burst search, live watch.** Scanning is a button. Live updating is the watch list. The hex view is a peephole, not a viewport onto all of RAM.
2. **Work stays on the PS5.** Use PS5Debug **turbo scan** (`TURBOSCAN_*`): AVX2 compare, **server-resident survivors**, multi-segment regions. Next-scan re-reads only survivors. Do not download the first-scan hit list. Do not dump the process to the PC.
3. **Skip memory that is not game state.** Default regions: **writable + cached** heaps and `eboot`/module data. Exclude execute-only (XOM), RX code, and uncached/slow regions from `TURBOSCAN` region classify. Unknown-initial snapshot **drops all-zero slots** (server default). Exact `u32` + 4-byte alignment is the default type (ammo, money, health).

**Defaults (user can widen; never the reverse as default)**

- Scan type: 4-byte unsigned, aligned
- Regions: writable cached only (not “All”)
- Unknown first scan: extra confirm — it snapshots selected RAM on the console
- One scan in flight; First/Next disabled until it finishes
- Overlay (Phase 5): **watch list + cheat toggles only**. Full scans stay on the PC web UI
- No auto-refresh of maps; refresh on process change or a Refresh button
- Hex and watch polling **stop** when the tab/overlay is hidden
- Never open a second `:744` scan session against the same PID while one is running

**Hard caps** (fail closed — refuse, do not silently crawl)

| Limit | Value |
|---|---|
| Results fetched to a UI | 256 |
| Watch list | 64 |
| Freeze list | 32 |
| Hex window | 512 B default / 4 KiB max |
| Watch poll | 10 Hz |
| Hex refresh | 4 Hz |
| Freeze tick | 15 Hz |
| Concurrent scans per PID | 1 |

If turbo scan is missing, fall back to the iterative `SCAN_START` / `COUNT` / `GET` trio — still server-side, still capped. Never fall back to “read all maps in a PC loop.”

This contract starts in **Phase 1** (peephole read, not a dump) and **Phase 2** (count-first scanner). Do not build a naive “view all memory” screen and optimize later.

## 6. Overlay strategy (ladder)

Goal: semi-transparent panel over the running game so the title stays visible. PiP is a fallback, not the plan.

Backends are tried in order. Each has an explicit kill condition. The memory editor does not wait on this ladder.

| ID | Backend | What you see | Input | Risk | When |
|---|---|---|---|---|---|
| B0 | None (web only) | Game untouched | Mouse/keyboard on PC | None | Phase 1–3 |
| B1 | Notifications | Toast on TV | None | Trivial | Phase 4 |
| B2 | ShellUI PUI widgets | Translucent HUD over game (etaHEN stats path) | Need a ShellUI pad hook or IPC we do not have | Conflicts with etaHEN Toolbox hooks; FW-sensitive | Spike A |
| B3 | In-game PRX on GNM flip | True overlay inside the game (etaHEN FPS path) | Hook `scePadReadState` in the game | Per-game; PS4 wrapper first; APR titles | Spike B |
| B4 | Video takeover / PiP | Game scaled or paused under a full UI | Own pad loop | Hides or distorts the game | Last resort |

**Phase 5 is a spike, not a feature dump.** Spike A and Spike B are time-boxed. Ship whichever produces a translucent, non-covering panel on a PS4-on-PS5 title. Do not implement both.

Until a backend exists, “open NitePR5” means the web UI. The plugin (Phase 4) can still apply freezes and cheats with no on-TV menu.

## 7. Input / hotkey

| Phase | How you open it |
|---|---|
| 1–3 | Browser on the PC |
| 4 | Toolbox → enable plugin; status via notification |
| 5+ | Combo chosen after the overlay backend is known |

Do not steal etaHEN’s existing shortcut slots (Cheats, Toolbox, webMAN, Kstuff). When a combo is needed, pick one that is **off** in the Toolbox, or consume pad inside an injected game PRX (B3) so ShellUI is left alone.

## 8. Web editor

The PC hosts the UI. Browser → `http://127.0.0.1:1744` → FastAPI → `ps5dbg` → `PS5_IP:744`.

Bind the HTTP server to **localhost** in v1 (not `0.0.0.0`). The PS5 is the only machine that should accept unauthenticated memory writes, and even that is LAN-only by nature of PS5Debug.

Optional later: plugin serves the same static files at `http://PS5_IP:1744` (CheatRunner pattern). Same frontend, different host. Not Phase 1.

## 9. Cheat files

GoldHEN JSON, one file per title version:

```text
{titleId}_{version}.json
```

Example: `CUSA00004_01.07.json`

```json
{
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
        { "offset": "1234ab", "on": "01000000", "off": "00000000" }
      ]
    }
  ],
  "credits": ["NitePR5"]
}
```

`offset` is hex relative to the target module base (`eboot.bin` unless specified). `on` / `off` are raw hex bytes with no separators.

**Storage**

- Phase 1–3: `web/cheats/` on the PC
- Phase 4+: also `/data/nitepr5/cheats/`
- Never write into `/data/etaHEN/cheats/` unless the user exports there. Coexist; do not hijack etaHEN’s menu.

## 10. Ports and coexistence

| Port | Owner | NitePR5 uses it? |
|---|---|---|
| 744 / 755 / 1010 / 3232 | PS5Debug | Yes (client) |
| 9021 | etaHEN elfldr | Send `ps5debug-NG.elf` (Phase 0); deploy plugin later |
| 9028 | etaHEN jailbreak IPC | Plugin privilege, Phase 4 |
| 1337 / 9081 / 9090 / 12800 | etaHEN optional services | No |
| **1744** | NitePR5 web (PC localhost, later optional console) | Yes |

If 1744 is taken on the PC, config overrides it. Do not pick 9999 (CheatRunner).

## 11. Build order

Each phase has **one job**, **allowed work**, and an **exit test**. If the exit test fails, stop. Do not start the next folder.

```text
Phase 0  Environment
    │
    ▼
Phase 1  Web: connect and read
    │
    ▼
Phase 2  Web: scan loop
    │
    ▼
Phase 3  Web: hex, poke, freeze, JSON cheats     ← usable product
    │
    ▼
Phase 4  Plugin daemon: freeze/cheats without overlay
    │
    ▼
Phase 5  Overlay spike (B2 or B3, not both)
    │
    ▼
Phase 6  Broker + optional console-hosted web     ← backlog unless needed
```

### Phase 0 — Environment

**Job:** prove the console is the stack we designed for.

- etaHEN running; **ps5debug-NG** loaded via elfldr **9021** (not the Toolbox PS5Debug toggle); PC can ping the PS5
- No second cheat injector loaded
- Note the PS5 LAN IP and a PS4-on-PS5 test title you own

**Exit:** from the PC, `ps5dbg` (or equivalent) lists processes and identifies the foreground app.

### Phase 1 — Web connect and read

**Job:** a browser talks to PS5Debug through our app. No scanner yet.

Allowed: discover/connect, process list, foreground, read a **peephole** (≤ 512 bytes), display hex. Optional 4 Hz refresh of that window only, with pause.

Forbidden: scan, write, overlay, plugin, cheat files, dumping maps as hex, auto-walking all regions.

**Exit:** open the test title, select it in the web UI, see live bytes from a mapped region.

### Phase 2 — Scan loop

**Job:** the classic NitePR search loop, on the PC.

Allowed: exact and unknown first scan; next-scan (equal / changed / increased / decreased); undo; **count-first UI**; fetch at most 256 results; writable-cached regions by default; turbo scan with iterative fallback.

Forbidden: freeze, overlay, plugin, pulling the full survivor set, scanning “All” as the default, a live updating result table of thousands of rows.

**Exit:** find a known changing integer (ammo, money, timer) in the test title in under a few next-scans, matching how you used NitePR.

### Phase 3 — Hex, poke, freeze, cheats

**Job:** a complete editor without any TV UI. This is the first shippable product.

Allowed: hex peephole, write with confirm, **watch list** (≤ 64, ~10 Hz), freeze list (≤ 32, 15 Hz bulk write), save/load GoldHEN JSON, toggle mods.

Forbidden: overlay, plugin, pointer/AOB/disasm, unbounded polling.

**Exit:** change a value in-game from the hex view; freeze it; save a cheat; reload the file and toggle it.

### Phase 4 — etaHEN plugin daemon

**Job:** a console process that can apply freezes and cheats while you play, **with no overlay**.

Allowed: plugin skeleton, title id, jailbreak IPC, localhost client to `:744` **or** a thin command channel from the PC web app, freeze tick on-console, notifications.

Forbidden: ShellUI hooks, game PRX injection, pad stealing.

**Exit:** enable the plugin from the Toolbox; with the web UI closed, a previously saved cheat or freeze still applies in-game.

Why this exists before overlay: freeze loops want to live on the console. Overlay can come later and talk to this daemon.

### Phase 5 — Overlay spike

**Job:** answer one question: can we draw a translucent panel over a running PS4-on-PS5 game and drive a simplified scan/hex UI with the DualSense?

Allowed: **one** backend (B2 or B3), a stripped UI (**watch list + cheat list + peephole poke**), input only as required by that backend. No first-scan-of-all-RAM on the TV.

Forbidden: PiP, native-PS5-only paths, disasm, sharing scan state with the web UI, overlay-side turbo scan of the whole process.

**Exit:** combo (or injected pad hook) opens a see-through panel; game remains visible; you can poke one value without covering the screen.

If both spikes fail, keep Phase 3+4 as the product and park overlay.

### Phase 6 — Improvements (only when asked)

- Session broker so TV and browser share one scan
- Console-hosted copy of the web UI
- Pointer / AOB / dump / disasm / watchpoints
- Optional pause via debug thread suspend
- Native PS5 overlay
- JSON export into etaHEN’s cheat folder
- PiP as overlay fallback

## 12. Safety

- Writes require an explicit confirm in the web UI. Overlay pokes are smaller and still require X to commit.
- Default bind: web on localhost; PS5Debug already LAN-only.
- Do not enable `PT_ATTACH` until a dedicated pause feature exists.
- Wrong writes crash games and can corrupt saves. Test on titles you can reinstall.
- Local / single-player / research on hardware you own. Not for online play.

## 13. Decision log

Record new decisions here so phases do not silently fork.

| Date | Decision |
|---|---|
| 2026-08-20 | FW 9.60, etaHEN, PS5Debug. Web first. Overlay is a later spike. GoldHEN JSON only. Python `ps5dbg` + FastAPI. Port 1744. |
| 2026-08-20 | Product name is **NitePR5**. Original PSP NitePR stays the namesake only. Paths and module names use `nitepr5`. |
| 2026-08-20 | Performance contract: burst turbo scan on-console, watch list for live data, hex peephole, skip uncached/XOM, hard caps in §5.3. |
| 2026-08-20 | Orchestration handoff: `AGENTS.md`, `docs/ORCHESTRATION.md`, `docs/STATUS.md`, `.cursor/rules/nitepr5.mdc`. |
| 2026-08-21 | FW 8.00+: do not use etaHEN Toolbox “PS5Debug” (old Sistr0/CTN, firmware-gated). Load OpenSourcereR **ps5debug-NG** via elfldr 9021. |
| 2026-08-21 | Phase 1: `web/nitepr5_core.Session` is the only memory path. FastAPI on 127.0.0.1:1744. `attach_target` is logical. `NITEPR5_MOCK=1` for UI without a console. |
| 2026-08-21 | First live title: **CUSA13762** (The Golf Club 2019). Compressed facts: `docs/HANDOFF.md`. |
| 2026-08-21 | Connect/reconnect must reset UI attach + hex poll (`resetTargetUi`) to match `Session.connect()` → `disconnect()`. |
| 2026-08-21 | Phase 2: turbo resident + `TS_SNAPSHOT_SEGMENTS` gap-fill in `Ps5dbgTransport` (ps5dbg 0.1.1 helper omits the trailing list). Unknown is turbo-snapshot only — no iterative RAM dump. `scan_start` replaces an idle hunt; `ScanActive` only while `_busy`. If count > 256, `scan_results` returns `[]` (no GET). |
| 2026-08-21 | Scan waits on :744 without the 10s connect timeout (`SCAN_IO_TIMEOUT is None`). Hex poll pauses during scan; one lock serializes command-socket I/O. TCP keepalive detects a dropped 744. |
| 2026-08-21 | Scan speed: OR `TS_USE_ALIASING` on START and `TS_RESCAN_ALIASING` on COUNT when advertised. Classify via `TURBOSCAN_REGIONS` `probe_bytes=1` (never 0) and skip PCD. Exact first-scan match-list overflow → snapshot + exact COUNT (not iterative dump). |
| 2026-08-21 | Phase 3: `write` / `watch_*` / `freeze_*` / `cheat_*` in nitepr5-core. UI owns 10 Hz watch and 15 Hz freeze timers (same `:744` lock as scan). GoldHEN JSON in `web/cheats/` only. Writes require confirm. Caps fail closed (256 / 64 / 32 / 4096). |
| 2026-08-21 | Exact first-scan overflow: always segmented resident START (never single-range dump). Snapshot+COUNT only if membership bitmap ≤ 448 MiB (`SNAP_BITMAP_MAX`). Else `TOO_MANY_MATCHES`. Drain overflow as length-prefixed blocks. `snapshot_ok=0` is ENOSPC, not rest mode; reconnect after desync. |
| 2026-08-24 | Web editor chrome is an ImHex-style SPA: hex grid is the canvas; other tools live in drawers. Stay **vanilla JS** in `web/static` (no React/Vue/Node). Hex libraries aimed at whole-file dumps do not fit the 512-byte peephole. Fonts: IBM Plex Sans + JetBrains Mono with system fallbacks. |
| 2026-08-25 | Phase 2+3 hardware exits passed on CUSA13762 (hunt, poke, freeze, GoldHEN save/reload/toggle). Web editor is the shippable product. Phase 4 next. |
| 2026-08-25 | Phase 4 code_complete: `plugin/` source (NTPR50001, :1745) + Session `plugin_arm`/`plugin_disarm`. `freeze_tick` no-ops while armed. No ELF on the Windows PC. |
| 2026-08-26 | Do not set `TS_RESCAN_ALIASING` on COUNT. A second :744 client (etaHEN plugin) over-subscribes aliasing; Next Scan then returns 200 and the next PROC_READ times out. PROC_NOP after turbo START/COUNT before hex resumes. |
| 2026-08-25 | Phase 4: plugin owns freeze when armed (web stops `freeze_tick`). Command port **1745**. Title **NTPR50001**. Source-only `plugin/` — no PS5 ELF cross-compile on the Windows dev PC. Notifications (B1) only. |
| 2026-08-26 | Plugin ELF is compiled in GitHub Actions only (`workflow_dispatch` artifact or Release asset). No SDK/WSL/Docker on the Windows PC. Install `.plugin` via Toolbox, never elfldr 9021. |
| 2026-08-26 | Plugin title ID is **NTPR50001** (`^[A-Za-z]{4}\d{5}$`). **NPR500001** (3 letters + 6 digits) is invalid; etaHEN will not load it. User confirmed load + startup toast. |
| 2026-08-26 | Phase 4 hardware exit passed: plugin freeze overwrites a manual web poke. Phase 4 `done`. Overlay (Phase 5) stays parked until asked. |
