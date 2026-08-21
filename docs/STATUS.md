# Status

Machine-readable for orchestrators. Update this file at the end of every agent session.

```yaml
product: NitePR5
active_phase: 0
phase_state: not_started   # not_started | in_progress | code_complete | blocked_on_hardware | done
last_updated: 2026-08-20
```

| Phase | State | Notes |
|---|---|---|
| 0 Environment | not_started | Needs live PS5 + etaHEN + PS5Debug from this PC |
| 1 Web connect + peephole read | not_started | |
| 2 Scan loop | not_started | |
| 3 Hex, watch, freeze, JSON | not_started | First shippable product |
| 4 Plugin daemon | not_started | Do not create `plugin/` early |
| 5 Overlay spike | not_started | Do not create `overlay/` early; pick B2 **or** B3 |
| 6 Backlog | parked | Only if the user asks |

## Blockers

- (none yet)

## Session log

| Date | Agent | Change |
|---|---|---|
| 2026-08-20 | planning | Architecture + orchestration docs; no runtime code |
