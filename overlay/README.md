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

1. Build `overlay.elf` (CI / Linux with Johns SDK — not this Windows PC).
2. Copy it to **`/data/nitepr5/overlay.elf`** on the console.
3. Load **NTPR50001** (`nitepr5.plugin`) from the Toolbox so `:1745` is listening.
4. Load **ps5debug-NG** via elfldr **9021** as usual.
5. Start a PS4 title. First test: **CUSA13762**.
6. Inject this ELF into the foreground **`eboot.bin`** with the existing etaHEN-Plugins **Injector / NineS :9033** pattern (process name `eboot.bin`).  
   Do **not** use HookGame / eboot PLT steal (that fights etaHEN FPS).

## Controls (in-process `scePadReadState`)

| Combo / button | Action |
|---|---|
| **L1+R1+Touchpad** | Open / close (edge). Swallowed so the title does not see it. |
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
