# Status

Machine-readable for orchestrators. Update this file at the end of every agent session.

```yaml
product: NitePR5
active_phase: 2
phase_state: code_complete   # not_started | in_progress | code_complete | blocked_on_hardware | done
last_updated: 2026-08-21
# Phase 2 code-complete: mock pytest 54 passed (overflow → snapshot+exact). Hardware exit not run.
# Next session: restart uvicorn, run the hardware exit, or start Phase 3 if the user overrides. Read docs/HANDOFF.md.
```

| Phase | State | Notes |
|---|---|---|
| 0 Environment | done | ps5debug-NG 1.3.0 on FW 9.60; procs + foreground from this PC |
| 1 Web connect + peephole read | done | CUSA13762 eboot pid 115; two 512-byte peephole reads |
| 2 Scan loop | code_complete | Turbo scan + aliasing; cheap PCD classify; exact overflow → snapshot+COUNT. Hardware hunt not run. |
| 3 Hex, watch, freeze, JSON | not_started | First shippable product |
| 4 Plugin daemon | not_started | Do not create `plugin/` early |
| 5 Overlay spike | not_started | Do not create `overlay/` early; pick B2 **or** B3 |
| 6 Backlog | parked | Only if the user asks |

## Blockers

- Hardware exit test still needs CUSA13762 in the foreground with a changing integer (score/timer). Do not mark Phase 2 `done` from mocks. Restart uvicorn so the current tree is loaded.
- Two live failures already fixed in tree (see HANDOFF Phase 2 lessons): (1) 10s recv timeout on turbo progress `u64`; (2) `TURBOSCAN_REGIONS` 64 KiB-probes of all maps (now `probe_bytes=1` + skip PCD). Re-test both plus aliasing.

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
