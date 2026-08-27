# NitePR5 overlay (Phase 5 B3)

In-game ELF that draws a **translucent editor panel** over a running PS4-on-PS5 title. It is **not** an etaHEN `.plugin` (that is NTPR50001). It does **not** toast-only (fps_elf FPS path). Game RAM is never copied in-process; all reads/writes go to the plugin.

```text
DualSense → this ELF (pad + HUD)
                │  HTTP JSON 127.0.0.1:1745
                ▼
         NTPR50001 plugin
                │  127.0.0.1:744
                ▼
         ps5debug-NG
```

Do **not** open TCP **744** from this ELF. Do **not** send this file to elfldr **9021**. Do **not** wrap it as `nitepr5.plugin`.

The HUD is a **see-through dark panel** (alpha chrome, game still visible) composited onto VideoOut buffers captured from `sceVideoOutRegisterBuffers`. If inject happens after the title already registered its flip buffers, the panel may stay blank until the game re-registers (resolution/HDR toggle) — that is a hardware kill for this spike, not a toast fallback.

## Install / inject

You need **two binaries**. Plugin wrap stays **0.57**; overlay **0.572** can replace `overlay.elf` alone:

| File | What it is | How it loads |
|---|---|---|
| `nitepr5.plugin` | Background daemon (freeze + `:1745` + auto-inject) | Toolbox Plugins. **Not** elfldr. |
| `overlay.elf` | In-game HUD | NTPR50001 maps it into `eboot.bin` at game launch. **Not** Toolbox, **not** elfldr **9021**. |

Elfldr **9021** starts a **new** process (that is how `ps5debug-NG` loads). If you send `overlay.elf` there, it will not see the DualSense or the game framebuffer.

### One-time setup

1. CI: download `overlay.elf` (**0.572**). Plugin wrap stays **0.57** — keep the existing `nitepr5.plugin` and only replace `overlay.elf` unless you also rebuilt the plugin.
2. Copy the plugin to USB `<usb>/etaHEN/plugins/` or `/data/etaHEN/plugins/` (skip if NTPR50001 0.57 is already installed).
3. Copy `overlay.elf` to **`/data/nitepr5/overlay.elf`** (FTP). Fallback path: `/data/etaHEN/plugins/overlay.elf`.
4. Toolbox: kill/run **NTPR50001**.
5. Elfldr **9021**: send **ps5debug-NG** (already required for the web editor).
6. Start **CUSA13762** (or another CUSA/PPSA title). Wait ~12 s for the toast **overlay injected**, then **overlay running** from the ELF and **overlay entry is up** (plugin heartbeat). If you get **overlay silent (never started)**, CRT/`main()` never reached overlay_boot — copy the matching CI `overlay.elf`. The game must keep booting (0.52 parked the stolen thread; 0.54 CE-108255-1).
7. **L1+R1+Touchpad click** opens the HUD (click the pad, do not only rest a finger).

After inject you should see extra toasts from the overlay itself: **overlay running**, then **hooks pad=0 flip=? vo=?**, then **pad poll ok**. L1+R1+Touchpad click toasts **combo (open)** / **combo (close)**. **0.572** installs GNM flip + VideoOut buffer hooks; the pad detour stays **off**. Combo + HUD nav use pad poll. The panel should appear when open. If you get combo toasts but a blank panel, inject likely missed `sceVideoOutRegisterBuffers` — re-register (resolution/HDR toggle) or fresh-launch the title. If you only see the plugin **overlay injected** toast, an old PROC_ELF ELF is still on the console (it never reached `main()`).

The plugin waits ~12 s after a game title appears, then Johns `elfldr_inject` (int3 stager, not `pt_call`). Overlay CRT starts ~2 s later (`overlay_gate`).

If the ELF file is missing, the toast is **copy overlay.elf to /data/nitepr5**. If ps5debug-NG is not loaded, **load ps5debug-NG to inject HUD**.

Manual retry (game already running): `POST http://<ps5>:1745/overlay/inject`.

Close the title before Toolbox kill/run of NTPR50001, or a second inject can stack hooks. Do not run a web scan in the same second the game launches (brief `:744` burst).

Do **not** use etaHEN FPS **HookGame** / eboot PLT steal (that fights etaHEN’s FPS loader).

## Controls (DualSense via pad poll)

Pad poll calls `scePadReadState` directly. The in-process `scePadReadState` PLT detour is **not** installed (0.572). Combo and HUD nav therefore cannot swallow buttons from the title; that is OK for this drop.

| Combo / button | Action |
|---|---|
| **L1+R1+Touchpad** | Open / close (edge). |
| **L1 / R1** | Tabs: Live \| Watch \| Freeze \| Cheats |
| **D-pad** | Move cursor / list |
| **Cross** | Enter poke, then **Cross again** commits `POST /write`. Arm freeze. Toggle cheat. |
| **Circle** | Cancel poke / goto / maps |
| **Square** | Goto address (Live). Delete watch/freeze. |
| **Triangle** | Maps jump list (metadata only) |
| **L3** | Add watch from Live cursor |

Share / Options / Toolbox shortcut slots are left alone.

When the panel is **closed**, HTTP polls **stop**. Plugin `POST /overlay/close` clears watches.

## Views

- **Live** — hex peephole 512 B @ ~4 Hz (`GET /read?addr=&n=512`). Poke after confirm. Optional maps goto.
- **Watch** — ≤64, `GET /watch/poll` ~10 Hz while this tab is visible.
- **Freeze** — mutate list via plugin; **tick stays in the plugin**. Cross = `POST /arm` (pid + current freezes). Overlay does not write freeze ticks.
- **Cheats** — `GET /cheat`, Cross = `POST /cheat/toggle`.

No turbo scan on TV. No shared scan with the web UI.

If `:1745` connect fails, the panel shows **plugin not running / sandbox**. There is no `:744` fallback.

## Build

```sh
export PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
bash overlay/build.sh
# output: overlay/build/overlay.elf
```

Johns SDK `prospero-cmake`, same shape as etaHEN `fps_elf` (ELF, not `.plugin`). Link: kernel, SceGnmDriver, ScePad, net. HUD is CPU-composited onto the VideoOut buffer captured from `sceVideoOutRegisterBuffers`, ticked from the GNM flip hook (`sceGnmSubmitAndFlipCommandBuffersForWorkload` on NeoMode else `libSceGnmDriver`).

cJSON 1.7.17 is MIT, vendored under `third_party/cjson/`.
