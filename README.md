# NitePR5

Homebrew memory editor for a jailbroken PS5 (firmware 9.60, etaHEN, PS5Debug). Named after PSP **NitePR**, built for PS5: the same search / hex / freeze / cheat loop, as a PC web UI first and a translucent in-game overlay later.

**Read [AGENTS.md](AGENTS.md) if you are an agent** (orchestrator + sub-agent spawn rules). Humans: start with [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). Track progress in [docs/STATUS.md](docs/STATUS.md).

## Status

Phase 0 is **done**: this PC talks to ps5debug-NG v1.3.0 on firmware 9.60. Next is Phase 1 (web connect + peephole read).

## Phase 0 — how to run

On firmware **8.00+** (including **9.60**), do **not** enable PS5Debug in etaHEN Debug Settings / Toolbox. That switch loads the old Sistr0/CTN blob and is firmware-gated; the “not supported on this firmware” toast is expected.

Load [ps5debug-NG](https://github.com/OpenSourcereR-dev/ps5debug-NG/releases) instead (v1.3.0 or newer). Download `ps5debug-NG_v1.3.0.elf`, then send it to etaHEN’s ELF loader on TCP **9021**:

```powershell
python -m pip install -r requirements.txt
python scripts/send_ps5debug_elf.py --host 192.168.x.x path\to\ps5debug-NG_v1.3.0.elf
```

The TV should show `ps5debug-NG by OSR v1.3.0 loaded!`. You can also launch that ELF from Toolbox **Plugin / Payload ELF**. Do not set `PS5Debug=1` in `/data/etaHEN/config.ini` on this firmware.

Then confirm this PC can talk to it on TCP **744**. The check script lists processes and the foreground app. It does not attach, scan, or read/write game memory.

```powershell
# Host (first match wins): --host, NITEPR5_PS5_HOST / PS5_IP,
# .ps5debug-host, scripts/ps5_ip.txt. Optional: --discover (UDP 1010).
$env:NITEPR5_PS5_HOST = "192.168.x.x"
python scripts/check_ps5debug.py

python scripts/check_ps5debug.py --host 192.168.x.x
python scripts/check_ps5debug.py --discover
```

Connect timeout is 5 seconds so an offline PC fails quickly.

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
