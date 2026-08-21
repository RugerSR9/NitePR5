# NitePR5

Homebrew memory editor for a jailbroken PS5 (firmware 9.60, etaHEN, PS5Debug). Named after PSP **NitePR**, built for PS5: the same search / hex / freeze / cheat loop, as a PC web UI first and a translucent in-game overlay later.

**Read [AGENTS.md](AGENTS.md) if you are an agent** (orchestrator + sub-agent spawn rules). Humans: start with [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). Track progress in [docs/STATUS.md](docs/STATUS.md).

## Status

Architecture only. No runtime code yet.

## Phases (short)

| Phase | What ships |
|---|---|
| 0 | Confirm etaHEN + PS5Debug from this PC |
| 1 | Web UI: connect and read memory |
| 2 | Web UI: iterative scan |
| 3 | Web UI: hex, poke, freeze, GoldHEN JSON — first usable product |
| 4 | etaHEN plugin daemon (freezes/cheats with no overlay) |
| 5 | Overlay spike (see-through TV panel) |
| 6 | Shared sessions and extras, only when asked |

## Do not

- Load OnionHEN, Kylin, CheatRunner, or other hotkey injectors beside this
- Reimplement PS5Debug’s protocol (`ps5dbg` already speaks it)
- Assume an etaHEN plugin can draw the system overlay — it cannot
- Dump or poll all of game RAM; follow the performance contract in the architecture doc
