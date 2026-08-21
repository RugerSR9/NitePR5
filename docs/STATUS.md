# Status

Machine-readable for orchestrators. Update this file at the end of every agent session.

```yaml
product: NitePR5
active_phase: 0
phase_state: done   # not_started | in_progress | code_complete | blocked_on_hardware | done
last_updated: 2026-08-21
```

| Phase | State | Notes |
|---|---|---|
| 0 Environment | done | ps5debug-NG 1.3.0 on FW 9.60; procs + foreground from this PC |
| 1 Web connect + peephole read | not_started | |
| 2 Scan loop | not_started | |
| 3 Hex, watch, freeze, JSON | not_started | First shippable product |
| 4 Plugin daemon | not_started | Do not create `plugin/` early |
| 5 Overlay spike | not_started | Do not create `overlay/` early; pick B2 **or** B3 |
| 6 Backlog | parked | Only if the user asks |

## Blockers

- (none) Phase 0 passed. Next is Phase 1 (web connect + peephole read) when asked. Foreground was home (`pid=0`); open a `CUSA*` title for Phase 1.

## Session log

| Date | Agent | Change |
|---|---|---|
| 2026-08-20 | planning | Architecture + orchestration docs; no runtime code |
| 2026-08-20 | orchestrator | Phase 0: `scripts/check_ps5debug.py` + `requirements.txt`; help/no-host/discover verified; hardware exit test not run |
| 2026-08-21 | orchestrator | Documented 9.60 path: skip Toolbox PS5Debug toggle; send ps5debug-NG.elf to elfldr 9021 |
| 2026-08-21 | orchestrator | Phase 0 exit test passed: `ps5debug-NG` v1.3.0, `fw=960`, 82 processes, foreground `pid=0` (home) |
