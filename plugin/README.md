# NitePR5 plugin (Phase 4)

etaHEN background daemon that owns **freeze and cheats** when armed. It is not a ShellUI overlay and does not draw on the game.

- Title ID **NPR500001**, version **0.40**, basename **nitepr5**
- Listens **0.0.0.0:1745** HTTP/1.1 JSON (command channel from the PC web UI)
- Game writes go through **127.0.0.1:744** (ps5debug-NG). Two-phase `PROC_WRITE` only.
- Persist `/data/nitepr5/state.json` and GoldHEN JSON under `/data/nitepr5/cheats/` (never `/data/etaHEN/cheats/`)
- Classic TV toast on start, armed, and when `:744` is missing

## Install

Copy the built `.plugin` (produced later on an SDK machine) to:

- USB: `<usb>/etaHEN/plugins/` (priority), or
- Internal: `/data/etaHEN/plugins/`

Enable or kill from the etaHEN Toolbox. Plugins are already jailbroken; do not call IPC 9028.

## Build

Build **only** with [etaHEN-Plugins](https://github.com/etaHEN/etaHEN-Plugins) / [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) clang. Set `PS5SDK` or `PS5_PAYLOAD_SDK`.

Do **not** configure or compile this tree on the Windows dev PC. There is no in-tree ELF or `.plugin` (those artifacts are gitignored).

```sh
cmake -S plugin -B /tmp/nitepr5-plugin-build
cmake --build /tmp/nitepr5-plugin-build
```

`POST_BUILD` runs `scripts/make_plugin.py` (vendored from etaHEN-Plugins): header `etaHEN_PLUGIN\0TID\0version\0` plus ELF bytes → `nitepr5.plugin`.

## HTTP

| Method | Path | Result |
|---|---|---|
| GET | `/status` | `{ok, armed, pid, freeze_count, cheat_id, enabled, dbg}` |
| POST | `/arm` | persist freeze/cheat; plugin owns the 15 Hz tick |
| POST | `/disarm` | stop freeze tick; keep files |
| POST | `/cheat/toggle` | `{name, enabled}` |
| POST | `/cheat/load` | GoldHEN JSON body or `{filename}` under `/data/nitepr5/cheats/` |

Freeze cap 32, data 1–8 bytes. The 33rd freeze is HTTP 400 `FreezeLimit`. No turbo scan in the plugin.
