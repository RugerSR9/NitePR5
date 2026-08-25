# Status

Machine-readable for orchestrators. Update this file at the end of every agent session.

```yaml
product: NitePR5
active_phase: 4
phase_state: not_started   # not_started | in_progress | code_complete | blocked_on_hardware | done
last_updated: 2026-08-25
# Phases 0–3 done on hardware (user 2026-08-25). Phase 4 plugin/ not created until this phase starts.
# Web UI: ImHex-style SPA chrome, hex canvas, drawers. Vanilla JS. Session API unchanged.
# PROC_WRITE: do not use ps5dbg 0.1.1 PS5Debug.write() — payload-in-datalen hangs :744.
# LangSmith: TRACE_TO_LANGSMITH in .env; skills in .agents/skills + .cursor/skills.
# 64-bit VA jump: JS must not `addr & ~0xf` (ToInt32 → negative /api/read). Align with modulo.
```

| Phase | State | Notes |
|---|---|---|
| 0 Environment | done | ps5debug-NG 1.3.0 on FW 9.60; procs + foreground from this PC |
| 1 Web connect + peephole read | done | CUSA13762 eboot pid 115; two 512-byte peephole reads |
| 2 Scan loop | done | Turbo scan + aliasing; cheap PCD classify; exact overflow → snapshot+COUNT. CUSA13762 hunt passed (user). |
| 3 Hex, watch, freeze, JSON | done | Poke + watch + freeze + GoldHEN JSON. Two-phase PROC_WRITE (not ps5dbg 0.1.1 `write()`). Hardware poke/freeze/cheat passed (user). |
| 4 Plugin daemon | not_started | Do not create `plugin/` until this phase starts |
| 5 Overlay spike | not_started | Do not create `overlay/` early; pick B2 **or** B3 |
| 6 Backlog | parked | Only if the user asks |

## Blockers

- None for Phases 0–3. Phase 4 has not started.
- After a failed write/scan that timed out on 4 bytes: **Disconnect and Connect again** (or restart uvicorn). The rest-mode hint is a false alarm when the socket desynced. A hung PROC_WRITE desyncs :744 the same way.

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
