# NitePR5 plugin (Phase 4 + Phase 5 Wave 1)

etaHEN background daemon that owns **freeze and cheats** when armed, and **Live / Watch / Freeze / Cheats I/O** for a future B3 overlay. It is not a ShellUI overlay and does not draw on the game. Overlay UI is a later wave (`overlay/` is not this tree).

- Title ID **NTPR50001**, version **0.50**, basename **nitepr5**. etaHEN requires `^[A-Za-z]{4}\d{5}$` — **NPR500001** will not load.
- Listens **0.0.0.0:1745** HTTP/1.1 JSON (command channel from the PC web UI and from a future overlay)
- Game R/W goes through **one** **127.0.0.1:744** socket (ps5debug-NG). Two-phase `PROC_WRITE`. One-phase `PROC_READ`. Idle (not armed, overlay closed) does **not** hold `:744`.
- Persist `/data/nitepr5/state.json` and GoldHEN JSON under `/data/nitepr5/cheats/` (never `/data/etaHEN/cheats/`). Watches and `overlay_open` are RAM-only.
- Classic TV toast on start, armed, and when `:744` is missing

## Install

Do **not** send this file to elfldr **9021** (that port is for one-shot ELFs like ps5debug-NG).

1. Get `nitepr5.plugin` from GitHub Actions (see [Build](#build)).
2. Copy it to:
   - USB: `<usb>/etaHEN/plugins/` (priority), or
   - Internal: `/data/etaHEN/plugins/`
3. Enable or **kill then run** from the etaHEN Toolbox (title **NTPR50001**).

Plugins are already jailbroken; do not call IPC 9028.

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
| GET | `/status` | `{ok, armed, pid, freeze_count, cheat_id, enabled, dbg, overlay_open, watch_count}` |
| POST | `/arm` | persist freeze/cheat; plugin owns the 15 Hz tick |
| POST | `/disarm` | stop freeze tick; keep files; disconnect `:744` unless `overlay_open` |
| POST | `/cheat/toggle` | `{name, enabled}` |
| POST | `/cheat/load` | GoldHEN JSON body or `{filename}` under `/data/nitepr5/cheats/` |
| POST | `/overlay/open` | `{pid?}` else stored pid; connect `:744`; does not require armed |
| POST | `/overlay/close` | `overlay_open=false`; clear watches; disconnect if not armed |
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
