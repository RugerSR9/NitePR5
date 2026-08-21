# Status

Machine-readable for orchestrators. Update this file at the end of every agent session.

```yaml
product: NitePR5
active_phase: 1
phase_state: done   # not_started | in_progress | code_complete | blocked_on_hardware | done
last_updated: 2026-08-21
# Next session: Phase 2. Read docs/HANDOFF.md "Next orchestrator" first.
# web/ and docs/HANDOFF.md are still untracked — do not lose them; commit only if the user asks.
```

| Phase | State | Notes |
|---|---|---|
| 0 Environment | done | ps5debug-NG 1.3.0 on FW 9.60; procs + foreground from this PC |
| 1 Web connect + peephole read | done | CUSA13762 eboot pid 115; two 512-byte peephole reads |
| 2 Scan loop | not_started | |
| 3 Hex, watch, freeze, JSON | not_started | First shippable product |
| 4 Plugin daemon | not_started | Do not create `plugin/` early |
| 5 Overlay spike | not_started | Do not create `overlay/` early; pick B2 **or** B3 |
| 6 Backlog | parked | Only if the user asks |

## Blockers

- (none) Phase 1 done. Next is Phase 2 (scan loop). Handoff: `docs/HANDOFF.md`. `web/` is untracked until committed.

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
