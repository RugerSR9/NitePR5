# NitePR5 plugin (Phase 4 + Phase 5)

etaHEN background daemon that owns **freeze and cheats** when armed, **Live / Watch / Freeze / Cheats I/O** for the B3 overlay, and **auto-inject** of `overlay.elf` into a launching CUSA/PPSA title. It is not a ShellUI overlay and does not draw on the game.

- Title ID **NTPR50001**, version **0.53**, basename **nitepr5**. etaHEN requires `^[A-Za-z]{4}\d{5}$` — **NPR500001** will not load.
- Listens **0.0.0.0:1745** HTTP/1.1 JSON (command channel from the PC web UI and from the overlay)
- Game R/W goes through **one** **127.0.0.1:744** socket (ps5debug-NG). Two-phase `PROC_WRITE`. One-phase `PROC_READ`. Overlay inject is two-phase `PROC_ELF` (`0xBDAA0007`). Idle (not armed, overlay closed) does **not** hold `:744`.
- Persist `/data/nitepr5/state.json` and GoldHEN JSON under `/data/nitepr5/cheats/` (never `/data/etaHEN/cheats/`). Watches and `overlay_open` are RAM-only.
- Classic TV toast on start, armed, `:744` missing, overlay injected / missing ELF / inject fail / overlay entry-up vs silent
- Auto-inject via `PROC_ELF` on the existing `:744` client (no `PT_ATTACH` from this plugin)

## Install

Do **not** send this file to elfldr **9021** (that port is for one-shot ELFs like ps5debug-NG).

1. Get `nitepr5.plugin` **and** `overlay.elf` from GitHub Actions (see [Build](#build)).
2. Copy `nitepr5.plugin` to:
   - USB: `<usb>/etaHEN/plugins/` (priority), or
   - Internal: `/data/etaHEN/plugins/`
3. Copy `overlay.elf` to **`/data/nitepr5/overlay.elf`** (FTP). Fallback: `/data/etaHEN/plugins/overlay.elf`.
4. Enable or **kill then run** from the etaHEN Toolbox (title **NTPR50001**).
5. Load **ps5debug-NG** on elfldr **9021** (already required for the editor).
6. Launch a CUSA/PPSA title. After ~12 s the TV should toast **overlay injected**, then **overlay entry is up**. **L1+R1+Touchpad** opens the HUD. The title must keep booting.

Plugins are already jailbroken; do not call IPC 9028. Do not send `overlay.elf` to **9021** (that starts a new process with no game pad or framebuffer).

## Build

This Windows PC does not compile the ELF. GitHub Actions does, using [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk) (`prospero-cmake`) on Ubuntu.

| How | Result |
|---|---|
| **Actions → Plugin → Run workflow** (pick a branch) | Workflow **artifact** `nitepr5.plugin` (7 days). Download the zip from that run. |
| **Publish a GitHub Release** | Same file attached as a **Release asset**. |

Merges to `main` and pull requests do not compile. The workflow file must exist on the branch you run.

`POST_BUILD` runs `scripts/make_plugin.py`: header `etaHEN_PLUGIN\0TID\0version\0` plus ELF bytes → `nitepr5.plugin`.

Local Linux (optional; SDK not installed on the Windows dev PC):

```sh
export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
bash plugin/build.sh
# output: plugin/build/nitepr5.plugin
```

## HTTP

Addresses are JSON integers (64-bit VAs as raw decimal). Bytes are hex strings. Errors are HTTP **400** with `{ok:false, error:"Name"}`.

| Method | Path | Result |
|---|---|---|
| GET | `/status` | `{ok, armed, pid, freeze_count, cheat_id, enabled, dbg, overlay_open, watch_count, overlay_injected, overlay_pid}` |
| POST | `/arm` | persist freeze/cheat; plugin owns the 15 Hz tick |
| POST | `/disarm` | stop freeze tick; keep files; disconnect `:744` unless `overlay_open` |
| POST | `/cheat/toggle` | `{name, enabled}` |
| POST | `/cheat/load` | GoldHEN JSON body or `{filename}` under `/data/nitepr5/cheats/` |
| POST | `/overlay/open` | `{pid?}` else stored pid; connect `:744`; does not require armed |
| POST | `/overlay/close` | `overlay_open=false`; clear watches; disconnect if not armed |
| POST | `/overlay/inject` | map `/data/nitepr5/overlay.elf` into `eboot.bin` now (same path as auto-inject) |
| GET | `/read` | query `addr`, `n?=512`, `pid?` → `{addr, n, data}` hex; `n` 1..4096 |
| POST | `/write` | `{addr, data, pid?}` two-phase PROC_WRITE; data 1..4096 B |
| GET | `/maps` | `{maps:[{name,start,end,offset,prot}]}` metadata only |
| GET | `/processes` | `{processes:[{pid,name}]}` |
| GET | `/foreground` | `{pid,name,titleid,contentid,app_ver}` |
| POST | `/attach` | `{pid}` logical target; persist |
| GET | `/watch` | `{watches:[{id,addr,n,label}]}` ≤64, RAM only |
| POST | `/watch` | `{addr, n?=4, label?}`; `n` in {1,2,4,8}; 65th → `WatchLimit` |
| DELETE | `/watch/{id}` | `{ok}` or `NoWatch` |
| GET | `/watch/poll` | `{values:[{id,addr,data}]}` sequential PROC_READ |
| GET | `/freeze` | `{freezes:[{id,addr,data}]}` ≤32 |
| POST | `/freeze` | `{addr, data}` 1..8 B; persist; does **not** arm; 33rd → `FreezeLimit` |
| DELETE | `/freeze/{id}` | `{ok}` persist; `NoFreeze` |
| GET | `/cheat` | `{cheat:{name,id,version,process,mods:[{name}]}, enabled}` or `cheat` null |

Freeze cap 32, data 1–8 bytes. Watch cap 64. The 33rd freeze / 65th watch is HTTP 400. No turbo scan in the plugin. No second `:744` client.
