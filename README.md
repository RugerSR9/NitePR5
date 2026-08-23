# NitePR5

Homebrew memory editor for a jailbroken PS5 (firmware 9.60, etaHEN, PS5Debug). Named after PSP **NitePR**, built for PS5: the same search / hex / freeze / cheat loop, as a PC web UI first and a translucent in-game overlay later.

**Read [AGENTS.md](AGENTS.md) if you are an agent** (orchestrator + sub-agent spawn rules). Humans: start with [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). Track progress in [docs/STATUS.md](docs/STATUS.md). Completed-phase facts: [docs/HANDOFF.md](docs/HANDOFF.md).

## Status

Phase 3 is **code-complete** (mock tests pass): connect, scan, hex poke with confirm, watch, freeze, GoldHEN JSON. Hardware exits for Phase 2 (scan hunt) and Phase 3 (poke / freeze / cheat) are still outstanding — do not start Phase 4.

**Agents / next session:** start at [docs/HANDOFF.md](docs/HANDOFF.md) (“Next orchestrator — start here”). After a failed scan that timed out on 4/8 bytes, Disconnect+Connect (or restart uvicorn) before scanning again; the rest-mode hint can be a false alarm.

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

## Phase 1 — web UI

Browser → `http://127.0.0.1:1744` → FastAPI → `ps5dbg` → PS5 `:744`. Connect, pick a process, watch a 512-byte hex peephole (≤4 Hz, Pause). No scan or write.

```powershell
python -m pip install -r requirements.txt
python -m uvicorn app:app --app-dir web --host 127.0.0.1 --port 1744
```

Then open `http://127.0.0.1:1744`. Host prefills from `.ps5debug-host` / `NITEPR5_PS5_HOST`. For UI-only without a console:

```powershell
$env:NITEPR5_MOCK = "1"
python -m uvicorn app:app --app-dir web --host 127.0.0.1 --port 1744
```

Exit test (Phase 1, done): open a PS4-on-PS5 title (`CUSA*`), Connect, attach `eboot.bin`, confirm live bytes.

Phase 3 adds poke (confirm), watch ≤64 @ ~10 Hz, freeze ≤32 @ 15 Hz, and GoldHEN files in `web/cheats/`. Writes never go to `/data/etaHEN/cheats/`. `python -m pytest web/tests web/nitepr5_core/tests -q` covers the mock path.

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
