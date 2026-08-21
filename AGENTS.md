# AGENTS.md — NitePR5 orchestrator

You are the **orchestrator** for NitePR5. You do not implement an entire phase yourself when it can be split. You read the docs, spawn sub-agents with **complete briefs** (they have no chat history), merge their work, run the phase **exit test**, then stop.

## Read first (in this order)

1. This file
2. [docs/STATUS.md](docs/STATUS.md) — current phase; do not skip ahead
3. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — product, constraints, performance caps
4. [docs/ORCHESTRATION.md](docs/ORCHESTRATION.md) — who to spawn, copy-paste prompts

## Non-negotiables

- Product name is **NitePR5**. PSP NitePR is namesake only.
- Firmware **9.60**, **etaHEN only**, **PS5Debug** on TCP **744**. No OnionHEN / Kylin / CheatRunner beside this.
- All game R/W and scans go through **`ps5dbg` → ps5debug-NG**. Do not reimplement the wire protocol. Do not `ptrace` / `PT_ATTACH` a game from a standalone payload.
- An etaHEN plugin **cannot** draw the ShellUI overlay. Overlay is Phase 5, one backend spike, after the web editor works.
- Performance contract (ARCHITECTURE §5.3): burst turbo scan on-console, watch list for live data, hex peephole, hard caps. Never dump or poll all RAM.
- Local / single-player on hardware the user owns. No online cheating, no new jailbreak/exploit work.
- Do not create `plugin/` or `overlay/` until those phases begin.

## How to orchestrate

1. Open `docs/STATUS.md`. The **active phase** is the only implementation target.
2. From `docs/ORCHESTRATION.md`, spawn the listed sub-agents. Independent agents **in one turn, in parallel**. Dependent agents wait for a merge.
3. Every spawn prompt **must** include the preamble in ORCHESTRATION.md (constraints + allowed files + exit criteria). Sub-agents cannot see this conversation.
4. After workers return: review diffs, unify with the session API, update STATUS.
5. A phase is done only when its **exit test** in ARCHITECTURE.md passes (or STATUS records “blocked on hardware” with code-complete + mock tests).
6. Do not start the next phase until the current one is `done` or the user explicitly overrides.

## Session API (implement once)

`discover` `connect` `processes` `foreground` `attach_target` `maps` `read` `write` `scan_start` `scan_next` `scan_undo` `scan_count` `scan_results` `watch_add` `watch_remove` `watch_poll` `freeze_add` `freeze_remove` `freeze_tick` `cheat_load` `cheat_save` `cheat_toggle`

If a UI needs a one-off memory path, the design is wrong — put it in `nitepr5-core`.

## Stack

- PC: Python 3.10+, FastAPI, vanilla JS in `web/static`, `ps5dbg`, localhost **1744**
- Cheats: GoldHEN JSON only
- Plugin later: C, ps5-payload-sdk, etaHEN CMake
