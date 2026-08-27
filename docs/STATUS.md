# Status

Machine-readable for orchestrators. Update this file at the end of every agent session.

```yaml
product: NitePR5
active_phase: 5
phase_state: code_complete   # not_started | in_progress | code_complete | blocked_on_hardware | done
last_updated: 2026-08-26
# Phase 5 B3 overlay 0.53: hijacked thread must RET (0.52 parked it → boot freeze).
# Plugin 0.53 inject ~12 s after title, heartbeat 5 s. Overlay talks to :1745.
# Plugin injects overlay.elf at CUSA/PPSA launch via PROC_ELF.
# Phase 4 still done: NTPR50001; plugin freeze owns :744 when armed.
# GitHub Actions builds nitepr5.plugin. No ELF on this Windows PC.
# PROC_WRITE: two-phase only — ps5dbg 0.1.1 write() hangs :744.
# LangSmith: TRACE_TO_LANGSMITH in .env.
# 64-bit VA: JS must not addr & ~0xf (ToInt32). Align with modulo.
```

| Phase | State | Notes |
|---|---|---|
| 0 Environment | done | ps5debug-NG 1.3.0 on FW 9.60; procs + foreground from this PC |
| 1 Web connect + peephole read | done | CUSA13762 eboot pid 115; two 512-byte peephole reads |
| 2 Scan loop | done | Turbo scan + aliasing; cheap PCD classify; exact overflow → snapshot+COUNT. CUSA13762 hunt passed (user). |
| 3 Hex, watch, freeze, JSON | done | Poke + watch + freeze + GoldHEN JSON. Two-phase PROC_WRITE (not ps5dbg 0.1.1 `write()`). Hardware poke/freeze/cheat passed (user). |
| 4 Plugin daemon | done | NTPR50001 loads; plugin freeze overwrites web pokes (user 2026-08-26) |
| 5 Overlay spike | code_complete | **B3**. Plugin 0.53 auto-inject (~12 s) + overlay 0.53 spawn+RET. Hardware: 0.52 froze boot / silent. |
| 6 Backlog | parked | Only if the user asks |

## Blockers

- Phase 5 **code_complete**, blocked on hardware: CI `nitepr5.plugin` (**0.53**) + `overlay.elf` to `/data/nitepr5/overlay.elf`; Toolbox NTPR50001; ps5debug-NG on 9021; launch CUSA13762; wait ~12 s; toast **overlay injected**, then **overlay entry is up** (or **overlay silent**); game must keep booting; L1+R1+Touchpad; poke one Live value. If the HUD is blank (VideoOut buffers registered before inject), that is a spike kill — not a toast fallback.
- Phase 4 **done** on hardware (user 2026-08-26): plugin freeze overwrites a web poke. GitHub Actions compiles the ELF; this Windows PC does not.
- After a failed write/scan that timed out on 4 bytes: **Disconnect and Connect again** (or restart uvicorn). The rest-mode hint is a false alarm when the socket desynced. A hung PROC_WRITE desyncs `:744` the same way.

## Session log

| Date | Agent | Change |
|---|---|---|
| 2026-08-20 | planning | Architecture + orchestration docs; no runtime code |
| 2026-08-20 | orchestrator | Phase 0: `scripts/check_ps5debug.py` + `requirements.txt`; help/no-host/discover verified; hardware exit test not run |
| 2026-08-21 | orchestrator | Documented 9.60 path: skip Toolbox PS5Debug toggle; send ps5debug-NG.elf to elfldr 9021 |
| 2026-08-21 | orchestrator | Phase 0 exit test passed: `ps5debug-NG` v1.3.0, `fw=960`, 82 processes, foreground `pid=0` (home) |
| 2026-08-21 | orchestrator | Phase 1 started: spawn core only (discover/connect/processes/foreground/attach_target/maps/read) |
| 2026-08-21 | orchestrator | Phase 1 core in `web/nitepr5_core/`; spawn UI shell + mock tests |
| 2026-08-21 | orchestrator | Phase 1 UI stubs + mock tests in; spawn wire-up |
| 2026-08-21 | orchestrator | Phase 1 wired to Session; pytest 9 passed; live connect 82 procs, fg pid=0; awaiting CUSA* peephole |
| 2026-08-21 | orchestrator | Phase 1 exit passed: CUSA13762 `eboot.bin` pid 115; maps metadata; two 512-byte reads |
| 2026-08-21 | orchestrator | Compressed Phase 0+1 into `docs/HANDOFF.md` for later worker briefs |
| 2026-08-21 | orchestrator | Reconnect fix: UI `resetTargetUi()` before connect; pytest 11 passed |
| 2026-08-21 | orchestrator | Phase 2 transfer note in HANDOFF; `web/` still untracked |
| 2026-08-21 | orchestrator | Phase 2 started on main (PR #1 merged). Spawn core scan only. |
| 2026-08-21 | orchestrator | Phase 2 core: scan_* in nitepr5-core; pytest 32 passed. Spawn UI + HTTP tests. |
| 2026-08-21 | orchestrator | Phase 2 UI + HTTP tests in; First Scan replaces idle hunt; pytest 45. `code_complete`. |
| 2026-08-21 | orchestrator | Scan timeout fix: blocking recv during turbo scan; serialize :744; pause hex poll. pytest 48. |
| 2026-08-21 | orchestrator | First scan was hanging on TURBOSCAN_REGIONS 64KiB probes of all maps. Classify from maps() rw- only. |
| 2026-08-21 | orchestrator | Scan-speed research: turbo resident is on, but TS_USE_ALIASING / TS_RESCAN_ALIASING are never set; maps() classify has no PCD skip. Do not default-probe TURBOSCAN_REGIONS. |
| 2026-08-21 | orchestrator | Expanded docs/HANDOFF.md with Phase 2 API, timeout/lock/hex-pause, probe hang, uvicorn log trap, live CUSA13762 lessons. |
| 2026-08-21 | orchestrator | Scan speed on `phase_2-speedImprovements`: TS_USE_ALIASING + TS_RESCAN_ALIASING; TURBOSCAN_REGIONS probe_bytes=1 skip PCD; resident decline → ScanUnsupported. pytest 53. |
| 2026-08-21 | orchestrator | Exact first-scan overflow: snapshot then exact COUNT instead of ScanUnsupported / iterative dump. |
| 2026-08-21 | orchestrator | Phase 3 started (user override; Phase 2 stays code_complete). Spawn core write/watch/freeze/cheat first. |
| 2026-08-21 | orchestrator | Phase 3 core: write/watch/freeze/cheat in nitepr5-core; pytest 68. Spawn UI + HTTP + cap tests. |
| 2026-08-21 | orchestrator | Phase 3 HTTP + tests + UI merged. Caps refuse 257/65/33. pytest 80. `code_complete`. |
| 2026-08-21 | orchestrator | Live scan: exact overflow used 1-range dump drain + doomed snapshot (`snapshot_ok=0`), then 4-byte timeout (desync). Always segmented START; skip snapshot if bitmap > 448 MiB. pytest 81. |
| 2026-08-23 | orchestrator | Context full. Compressed pickup into `docs/HANDOFF.md` (next orchestrator). No Phase 4. Hardware still needs reconnect + less-common First Scan. |
| 2026-08-24 | orchestrator | Live 400+409 + poke fail: 250 ms :744 wait timed out behind hung reads; ConnectionLost left the socket "connected". Drop on lost; wait for peers; UI stops polls on 409. pytest 96. |
| 2026-08-24 | orchestrator | Hex poke still 409: `timed out reading 4 bytes` on `POST /api/write`. Cause: ps5dbg 0.1.1 packs PROC_WRITE payload into `datalen`; server ACKs then waits for a second payload. Two-phase `proc_write_phased` + UI pauses polls during poke. pytest 98. |
| 2026-08-24 | orchestrator | Web UI ground-up chrome: hex canvas fills the viewport; scan/maps/watch/hold/cheats/procs as drawers; inspector + goto + keyboard. Still vanilla JS (no Node). Session API unchanged. pytest 98. |
| 2026-08-25 | orchestrator | Installed LangSmith skills (`langsmith-trace`, dataset, evaluator). FastAPI tracing via `web/tracing.py`: Session spans + HTTP root traces; skip poll/hex; redact RAM. pytest 103. |
| 2026-08-25 | orchestrator | Scan-hit jump sent `GET /api/read?addr=-389259952` (JS `addr & ~0xf` ToInt32). Align with modulo; `InvalidAddress` 400; parseGoto no `>>> 0`. pytest 106. |
| 2026-08-25 | orchestrator | README rewritten as post-Phase 3 product docs (setup, editor loop, caps, cheats). No runtime change. |
| 2026-08-25 | orchestrator | User confirmed Phase 2+3 hardware exits (CUSA13762 hunt, poke/freeze/cheat). Marked `done`. Phase 4 `not_started`; no `plugin/`. |
| 2026-08-25 | orchestrator | Phase 4 started. Freeze handoff to plugin; command port 1745; title NTPR50001; source-only (no ELF cross-compile). Spawn explore, then plugin/, then web bind + tests. |
| 2026-08-25 | orchestrator | Wave 0 explore done: `.plugin` header, utility_daemon CMake, POSIX :1745, classic notify, vendor cJSON. Spawn Wave 1 `plugin/` C (no compile). |
| 2026-08-25 | orchestrator | Wave 1 `plugin/` source in (NTPR50001, :1745, two-phase PROC_WRITE). No ELF. Spawn Wave 2 web bind + tests (disjoint paths). |
| 2026-08-25 | orchestrator | Wave 2 merged: Session `plugin_arm`/`disarm`, Hold Arm/Disarm, mock HTTP :1745. `code_complete`. Hardware blocked on SDK ELF. |
| 2026-08-26 | orchestrator | Plugin CI: `.github/workflows/plugin.yml` (Run workflow artifact + Release asset). `plugin/build.sh` + Johns SDK. No toolchain on this PC. |
| 2026-08-26 | orchestrator | Title ID locked **NTPR50001** (4 letters + 5 digits). NPR500001 was invalid; etaHEN would not load it. User: plugin loads, startup toast seen. |
| 2026-08-26 | orchestrator | Phase 4 hardware exit passed: console freeze overwrites a manual web poke. Marked `done`. No Phase 5 unless asked. |
| 2026-08-26 | orchestrator | After 3–4 scans: Next Scan 200 then peephole `timed out reading 4 bytes`. Dropped `TS_RESCAN_ALIASING`; PROC_NOP after turbo COUNT. Plugin connects :744 only when armed. |
| 2026-08-26 | orchestrator | Phase 5 started (user). Wave 0: parallel explore B2 / B3 / plugin I/O. Overlay UI = Live+Watch+Freeze+Cheats views. No `overlay/` until backend pick. |
| 2026-08-26 | orchestrator | Wave 0 plugin I/O: GO — PROC_READ `0xBDAA0002` one-phase + :1745 Live/Watch/Freeze/Cheats. Overlay open/close owns :744 if not armed. No second :744. Wave 1 held until B2/B3. |
| 2026-08-26 | orchestrator | Wave 0 B2: NO-GO. Overlay Labels + GetData shortcuts are Toolbox-in-ShellUI only; no plugin PUI API. Fork required. Waiting on B3. |
| 2026-08-26 | orchestrator | Wave 0 B3: GO with caveats. Inject+flip+pad proven; translucent HUD is greenfield (fps_elf is toasts). Recommend B3. User pick before Wave 1. |
| 2026-08-26 | orchestrator | User picked **B3**. Spawn Wave 1 `plugin/` PROC_READ + :1745 Live/Watch/Freeze/Cheats. No `overlay/` yet. |
| 2026-08-26 | orchestrator | Wave 1 merged: plugin 0.50 PROC_READ + :1745 editor routes. Spawn Wave 2 `overlay/` ELF + CI. |
| 2026-08-26 | orchestrator | Wave 2 CI: sibling `overlay` job in `.github/workflows/main.yml` (skip if no CMakeLists). Waiting on overlay ELF source. |
| 2026-08-26 | orchestrator | Wave 2 overlay ELF merged (`overlay/`). Open uses getpid()+`/overlay/open` (not `/foreground` first). `code_complete`; hardware exit not run. |
| 2026-08-26 | orchestrator | Overlay 0.51: pad poll fallback + combo toast. Inject toast was plugin-only; combo was detour-only and worker slept while closed. |
| 2026-08-26 | orchestrator | User: no overlay toasts, only plugin inject toast. PROC_ELF maps ELF but Johns CRT never reached main(); PROC_ELF restores game ucred before jump. Overlay 0.52 `overlay_start` e_entry: raise caps, `/data/nitepr5/overlay.alive`, toast, then `__crt_start`. Plugin 0.52 heartbeat toast. |
| 2026-08-26 | orchestrator | User: 0.52 **silent (never started)** and game froze while booting. Hijacked thread ran kernel_init/CRT/`for(;;)` instead of returning. Overlay 0.53: pthread_create then RET. Plugin 0.53 inject ~12 s, alive wait 5 s. |
