# NitePR5 — Orchestration

Handoff for a **parent agent** that spawns sub-agents. Workers have **no prior chat**. Every spawn must be a self-contained brief.

Architecture and exit tests live in [ARCHITECTURE.md](ARCHITECTURE.md). Current phase lives in [STATUS.md](STATUS.md). Parent instructions live in [../AGENTS.md](../AGENTS.md). Completed-phase facts live in [HANDOFF.md](HANDOFF.md) — paste the relevant blocks into every worker prompt.

## 1. Parent vs worker

| Role | Does | Does not |
|---|---|---|
| **Orchestrator** (you) | Pick phase, spawn workers, merge, run exit test, update STATUS | Implement three features in one sitting |
| **Worker** | One job, listed files only, report back | Skip the preamble, start the next phase, add backlog items |

Use Cursor **Task** sub-agents when the work is multi-step and independent:

| Work | `subagent_type` |
|---|---|
| Implement a slice (core, UI, tests) | `generalPurpose` |
| Find APIs / repo layout / ps5dbg usage | `explore` |
| Git, env, pip, one-off shell | `shell` |

Set `run_in_background: true` and spawn **several in one message** when the table below says parallel. Do not spawn two workers that edit the same file.

## 2. Preamble (paste into every worker prompt)

```text
You are a NitePR5 worker. You have no prior conversation. Follow this brief exactly.

Product: NitePR5 (PSP NitePR is namesake only). Jailbroken PS5 FW 9.60, etaHEN only, PS5Debug TCP 744.
Read: AGENTS.md, docs/ARCHITECTURE.md §3 constraints + §5.1 session API + §5.3 performance, docs/STATUS.md.

Hard rules:
- Use Python ps5dbg. Do not reimplement ps5debug-NG. Do not PT_ATTACH / ptrace a game.
- etaHEN plugins cannot draw ShellUI overlays. Do not start plugin/ or overlay/ unless this brief says so.
- Performance: turbo scan on-console; UI gets counts; max 256 result rows; watch ≤64 @ ~10Hz; hex ≤512B default @ ~4Hz; freeze ≤32 @ 15Hz. Never dump or poll all RAM.
- Localhost web on port 1744. GoldHEN JSON cheats only.
- No online cheating, no new exploits/jailbreaks, no stacking Kylin/CheatRunner/OnionHEN.

Return to the parent: files changed, what was verified, what is blocked, anything that violated the session API.
```

Then append the **job block** for that worker (sections below).

## 3. Mock vs hardware

Workers may implement against a **fake backend** (`nitepr5-core` with an in-memory/mock transport) so they are not blocked on the console.

| Status value | Meaning |
|---|---|
| `code_complete` | Tests/mocks pass; hardware exit test not run |
| `blocked_on_hardware` | Code ready; user/console unavailable |
| `done` | ARCHITECTURE exit test passed on the real PS5 |

Never mark a phase `done` from mocks alone.

## 4. Phase playbooks

### Phase 0 — Environment

**Serial** (needs the user’s LAN/PS5). Optional parallel *prep* only.

| Agent | When | Job |
|---|---|---|
| Explore `ps5dbg` | Parallel with script | How to ping, list processes, foreground app from Python |
| Shell/generalPurpose | Parallel | Add `web/` later — **not this phase**. Phase 0 only: a short `scripts/check_ps5debug.py` (or docs note) that uses `ps5dbg` |

**Exit:** `ps5dbg` lists processes and the foreground app from this PC.

**Job block**

```text
Job: Phase 0. Confirm this PC can talk to PS5Debug.
Allowed: docs/STATUS.md, a small scripts/check_ps5debug.py if missing.
Forbidden: web UI, scans, writes, plugin, overlay.
Exit: document PS5 IP method + command that lists processes. If no console, set STATUS blocked_on_hardware and still add the check script.
```

### Phase 1 — Web connect + peephole read

**Serial first:** lock `nitepr5-core` connect/read/maps. **Then parallel.**

```text
        [Core: connect, processes, foreground, maps, read]
                         |
          +--------------+--------------+
          |                             |
   [Web static UI]              [Mock backend tests]
          |                             |
          +--------------+--------------+
                         |
              [Wire UI to core]
```

| Agent | Parallel? | Allowed paths |
|---|---|---|
| A Core | First, alone | `web/nitepr5_core/` (package name may be `web/core.py` if tiny) |
| B UI shell | After A’s API is in the tree | `web/static/`, `web/app.py` stubs |
| C Tests/mock | After A’s API | `web/tests/` |
| D Wire-up | After B+C | `web/app.py`, static JS |

**Exit:** open a PS4-on-PS5 test title, select it, see ≤512 live bytes. Refresh ≤4 Hz, pauseable. No scan/write.

**Job A**

```text
Job: Phase 1 core. Implement discover/connect/processes/foreground/attach_target/maps/read via ps5dbg.
Allowed: web/ Python package for nitepr5-core only.
Forbidden: scan, write, freeze, cheats, dumping all maps into the client.
Maps are cached. read() must reject n > 4096 and default UI will use 512.
Include a mock transport for tests.
```

**Job B**

```text
Job: Phase 1 UI shell. FastAPI + vanilla JS: connect form (PS5 IP), process list, hex peephole (16 rows), pause refresh.
Allowed: web/app.py, web/static/, requirements.txt.
Forbidden: calling PS5 yourself if core is incomplete — stub fetch('/api/...') shapes to match session API.
Port 1744 localhost only.
```

### Phase 2 — Scan loop

**Serial:** core scan, then UI. Optional parallel: region-classify helper + mock tests once start/next/count exist.

| Agent | Job |
|---|---|
| A | `scan_start/next/undo/count/results(limit)` turbo scan, fallback iterative. Writable-cached regions default. |
| B | UI: First/Next/Undo, show **count**, fetch ≤256 rows only when count ≤256. Disable buttons while in flight. |
| C | Tests: mock survivor sets; assert we never request unbounded GET |

**Exit:** find a known changing integer with a few next-scans.

**Job A extra rules:** `TURBOSCAN_*` first; region classify; skip XOM/uncached by default; unknown snapshot requires an explicit flag; one scan per PID.

### Phase 3 — Hex, watch, freeze, JSON (first product)

After core methods exist, **parallel** four workers on **different files**:

| Agent | Files | Job |
|---|---|---|
| A | core watch/freeze | `watch_*`, `freeze_*` with caps 64 / 32, bulk read/write, 10 Hz / 15 Hz owned by UI/timer not a tight loop in the protocol layer |
| B | `web/static` hex+watch | peephole poke with confirm; watch table |
| C | `web/` cheats | GoldHEN JSON load/save/toggle; `web/cheats/` |
| D | tests | cap enforcement (refuse 257th result, 65th watch, 33rd freeze) |

**Exit:** poke in-game, freeze, save JSON, reload and toggle.

### Phase 4 — Plugin daemon

**One worker.** Do not parallel with Phase 5.

```text
Job: etaHEN plugin daemon. Freeze/cheats with no overlay.
Allowed: create plugin/ now. C, ps5-payload-sdk, etaHEN plugin CMake. Title ID like NTPR50001.
Talk to 127.0.0.1:744 or a thin command channel from the PC. Notifications only (backend B1).
Forbidden: ShellUI hooks, game PRX, pad steal, overlay/.
Exit: plugin on from Toolbox; freeze/cheat still applies with the web UI closed.
```

### Phase 5 — Overlay spike

**Do not run B2 and B3 in parallel.** Orchestrator picks one after a short explore, or the user picks.

```text
Job: Phase 5 spike ONLY backend Bx (parent fills B2 or B3).
Translucent panel over a running PS4-on-PS5 game. Watch list + cheat toggles + peephole poke. DualSense as required by that backend.
Forbidden: full-process scan on TV, PiP, both backends, native-PS5-only, shared scan with web.
Exit: see-through panel; game visible; poke one watch value.
```

### Phase 6

Do not spawn unless the user asks. Backlog is in ARCHITECTURE.md.

## 5. Merge protocol

1. Prefer workers that touched disjoint paths.
2. If two diffs hit `web/app.py`, orchestrator rebases — do not tell both to “just commit.”
3. Reject PRs/diffs that: add SHN/MC4, add `PT_ATTACH`, scan All by default, fetch unbounded results, create `overlay/` before phase 5.
4. Append a row to `docs/STATUS.md` session log.
5. Tell the user what hardware step (if any) they must run.

## 6. Suggested worker return format

```text
## Done
- ...
## Files
- ...
## Verified
- mock tests / not run / hardware ...
## Blocked
- ...
## API impact
- new session methods? none / list
```
