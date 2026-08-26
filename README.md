# NitePR5

Homebrew memory editor for a jailbroken PS5 (firmware **9.60**, **etaHEN**, **ps5debug-NG**). Named after PSP **NitePR**: search, hex-edit, freeze, and save cheats while a game runs.

The Phase 3 product is a **PC web editor**. Open `http://127.0.0.1:1744`, connect to the console, and edit the foreground title. Phase 4 plugin **source** is in `plugin/` (no ELF on this PC). Overlay is still later.

**Humans:** this file, then [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). Progress: [docs/STATUS.md](docs/STATUS.md). **Agents:** start at [AGENTS.md](AGENTS.md) and [docs/HANDOFF.md](docs/HANDOFF.md).

## Status

Phases 0–3 are **done**. The web UI is the shippable PC editor: connect, scan, poke, watch, freeze, and load/save GoldHEN JSON on hardware (**CUSA13762**).

Phase 4 is **code_complete** (source + web bind + mocks). Hardware exit waits on an SDK-built `.plugin`. After a hung scan or write (`timed out reading 4 bytes`), **Disconnect then Connect** (or restart uvicorn). The rest-mode hint is often a false alarm from a desynced `:744` socket.

## What you get

Browser → FastAPI on **localhost 1744** → Python **`ps5dbg`** → PS5Debug on TCP **744**. All game R/W and scans go through that path. The UI is vanilla JS: an ImHex-style hex canvas with tool drawers.

| Surface | What it does |
|---|---|
| **Hex** | 512-byte live peephole (~4 Hz). Click to select, double-click or Enter to poke (always confirms). Inspector shows u8 / u16 / u32 / i32 / f32. Go-to address in the title bar. |
| **Scan** | Exact first scan (optional unknown). Next Scan: still this value / increased / decreased / changed / unchanged. Undo. Count first; the hit table appears only if there are ≤256 matches. Writable cached regions only. |
| **Watch** | Pin up to **64** addresses, polled at ~10 Hz. |
| **Hold** | Freeze up to **32** addresses at ~15 Hz. |
| **Cheats** | GoldHEN JSON in `web/cheats/` on this PC. Load, toggle mods, save, or export holds. Never writes `/data/etaHEN/cheats/`. |
| **Maps / Procs** | Jump the peephole to a mapped region (names and ranges, not a dump). Pick `eboot.bin`; Open attaches logically — no `PT_ATTACH`. |

Caps **refuse** rather than silently crawl: 256 scan rows, 64 watches, 32 freezes, 4 KiB max read/write. The scanner runs **on the console** (turbo scan). The PC never dumps all RAM.

**One browser tab.** Two tabs share one Session and fight over `:744`.

## Requirements

| Item | Value |
|---|---|
| Console | PS5, firmware **9.60**, **etaHEN only** |
| Debugger | **[ps5debug-NG](https://github.com/OpenSourcereR-dev/ps5debug-NG/releases)** v1.3.0+ via elfldr **9021** — not the Toolbox PS5Debug toggle |
| Titles | PS4 games on PS5 (`CUSA*`) first. Live title used so far: **CUSA13762** (The Golf Club 2019). |
| PC | Windows, same LAN, **Python 3.10+** |
| Bind | Web UI on `127.0.0.1:1744` only (not `0.0.0.0`) |

Do not stack OnionHEN, Kylin Core, CheatRunner, or other injectors beside this.

On firmware **8.00+** (including **9.60**), etaHEN’s Toolbox / Debug Settings **PS5Debug** switch is the old Sistr0/CTN blob and is firmware-gated. The “not supported on this firmware” toast is expected. Load **ps5debug-NG** instead. Do not set `PS5Debug=1` in `/data/etaHEN/config.ini`.

## Setup

```powershell
python -m pip install -r requirements.txt
```

### 1. Load ps5debug-NG

Download `ps5debug-NG_v1.3.0.elf`, then send it to etaHEN’s ELF loader:

```powershell
python scripts/send_ps5debug_elf.py --host 192.168.x.x path\to\ps5debug-NG_v1.3.0.elf
```

The TV should show `ps5debug-NG by OSR v1.3.0 loaded!`. You can also launch that ELF from Toolbox **Plugin / Payload ELF**.

Host order for the scripts: `--host`, then `NITEPR5_PS5_HOST` / `PS5_IP`, then `.ps5debug-host`, then `scripts/ps5_ip.txt`.

### 2. Confirm TCP 744

Lists processes and the foreground app. Does not attach, scan, or read/write game memory.

```powershell
$env:NITEPR5_PS5_HOST = "192.168.x.x"
python scripts/check_ps5debug.py

python scripts/check_ps5debug.py --host 192.168.x.x
python scripts/check_ps5debug.py --discover
```

`--discover` is extra (UDP **1010**). Connect timeout is 5 seconds so an offline PC fails quickly. Rest mode **does** drop TCP 744; reconnect after wake.

### 3. Run the editor

```powershell
python -m uvicorn app:app --app-dir web --host 127.0.0.1 --port 1744
```

Open `http://127.0.0.1:1744`. The host field prefills from `.ps5debug-host` / `NITEPR5_PS5_HOST`. After changing Python or `web/static/app.js`, restart uvicorn and hard-refresh the browser.

Without a console (UI + mock process):

```powershell
$env:NITEPR5_MOCK = "1"
python -m uvicorn app:app --app-dir web --host 127.0.0.1 --port 1744
```

```powershell
python -m pytest web/tests web/nitepr5_core/tests -q
```

## Using the editor

1. **Connect** (or **Find** on UDP 1010). Status in the footer should leave Offline.
2. Open **Procs**, pick the game (`eboot.bin`, foreground is marked), **Open**. The hex canvas fills with a 512-byte window on the first writable map.
3. **Scan** a value you can change in-game (ammo, money, a timer — not `0` or `1` on a first pass). Wait for the count. Change it in-game, **Next**. Repeat until the table appears (≤256 hits). Click a hit to jump the hex window.
4. Select a byte. **Write** (u32 in the toolbar, or Enter / double-click) — confirm the dialog. **Pin** to watch; **Hold** to freeze.
5. **Cheats:** **Export holds** to GoldHEN JSON in `web/cheats/`, or **Load** an existing `{titleId}_{version}.json` and toggle mods.

Shortcuts (`?` in the title bar): **G** go to address, arrows move the selection, PgUp/PgDn slide the window, **Enter** poke, **Esc** close a drawer. Pause stops hex / watch / freeze polls (they also stop when the tab is hidden, and during a scan).

If a First Scan reports too many matches, pick a **less common** in-game value and scan again. Unknown-initial on the whole writable set can be declined if the on-console snapshot is huge.

## Cheat files

GoldHEN JSON only, one file per title version: `{titleId}_{version}.json` (example `CUSA00004_01.07.json`). `offset` is hex relative to the module base (`eboot.bin` / `executable`). `on` / `off` are raw hex bytes. Schema: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) §9.

Files live in `web/cheats/` on the PC (`web/cheats/**` is gitignored except `.gitkeep`). Filename must be `Name.json` with no path.

## Optional tracing

[LangSmith](https://smith.langchain.com/): set `TRACE_TO_LANGSMITH=true` and `LANGSMITH_API_KEY` (or `LANGCHAIN_API_KEY`) in `.env`. Project `nitepr5` unless you set `LANGSMITH_PROJECT`. Hex peepholes, watch polls, and freeze ticks are not traced. RAM bytes are never sent.

## Roadmap

| Phase | State | What |
|---|---|---|
| 0 Environment | done | etaHEN + ps5debug-NG from this PC |
| 1 Connect + peephole | done | Web UI reads 512 live bytes |
| 2 Scan loop | done | Turbo scan on-console; CUSA13762 hunt passed |
| 3 Hex, poke, freeze, JSON | done | First usable product; poke/freeze/cheat passed on hardware |
| 4 Plugin daemon | code_complete | Freezes/cheats on-console with no overlay; ELF not built here |
| 5 Overlay spike | not started | See-through TV panel (one backend) |
| 6 Backlog | parked | Shared sessions, pointer/AOB, etc. — only if asked |

## Do not

- Load OnionHEN, Kylin, CheatRunner, or other hotkey injectors beside this
- Reimplement PS5Debug’s protocol (`ps5dbg` already speaks it)
- Call `PS5Debug.write()` / `protocol.proc_write` (ps5dbg 0.1.1 packs the payload into `datalen` and hangs `:744`). Core uses a two-phase `PROC_WRITE`.
- Assume an etaHEN plugin can draw the system overlay — it cannot
- Dump or poll all of game RAM
- Enable `PT_ATTACH` / pause-the-game from this product
- Create `overlay/` before Phase 5
- Use this online or on hardware you do not own
