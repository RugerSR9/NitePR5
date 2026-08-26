# NitePR5 plugin (Phase 4)

etaHEN background daemon that owns **freeze and cheats** when armed. It is not a ShellUI overlay and does not draw on the game.

- Title ID **NPR500001**, version **0.40**, basename **nitepr5**
- Listens **0.0.0.0:1745** HTTP/1.1 JSON (command channel from the PC web UI)
- Game writes go through **127.0.0.1:744** (ps5debug-NG). Two-phase `PROC_WRITE` only.
- Persist `/data/nitepr5/state.json` and GoldHEN JSON under `/data/nitepr5/cheats/` (never `/data/etaHEN/cheats/`)
- Classic TV toast on start, armed, and when `:744` is missing

## Install

Do **not** send this file to elfldr **9021** (that port is for one-shot ELFs like ps5debug-NG).

1. Get `nitepr5.plugin` from GitHub Actions (see [Build](#build)).
2. Copy it to:
   - USB: `<usb>/etaHEN/plugins/` (priority), or
   - Internal: `/data/etaHEN/plugins/`
3. Enable or **kill then run** from the etaHEN Toolbox (title **NPR500001**).

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

| Method | Path | Result |
|---|---|---|
| GET | `/status` | `{ok, armed, pid, freeze_count, cheat_id, enabled, dbg}` |
| POST | `/arm` | persist freeze/cheat; plugin owns the 15 Hz tick |
| POST | `/disarm` | stop freeze tick; keep files |
| POST | `/cheat/toggle` | `{name, enabled}` |
| POST | `/cheat/load` | GoldHEN JSON body or `{filename}` under `/data/nitepr5/cheats/` |

Freeze cap 32, data 1–8 bytes. The 33rd freeze is HTTP 400 `FreezeLimit`. No turbo scan in the plugin.
