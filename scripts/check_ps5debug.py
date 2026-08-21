#!/usr/bin/env python3
"""NitePR5 Phase 0: confirm this PC can talk to PS5Debug (TCP 744) via ps5dbg.

Lists processes and the foreground app. Does not attach, scan, read, or write
game memory.
"""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PS5DEBUG_PORT = 744
CONNECT_TIMEOUT = 5.0
HINT = (
    "Could not reach PS5Debug on TCP 744. On FW 8.00+ (including 9.60) the "
    "etaHEN Toolbox 'PS5Debug' toggle is the old Sistr0/CTN blob and is "
    "firmware-gated — send ps5debug-NG.elf to etaHEN elfldr on 9021 instead "
    "(see README). Also check: etaHEN is running, this PC is on the same LAN, "
    "and the console is not in rest mode (rest mode drops TCP 744)."
)


def _read_host_file(path: Path) -> str | None:
    if not path.is_file():
        return None
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return None
    for line in text.splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            return line
    return None


def resolve_host(cli_host: str | None, *, discover: bool) -> str:
    """Host order: --host, NITEPR5_PS5_HOST / PS5_IP, .ps5debug-host, scripts/ps5_ip.txt.

    ``--discover`` is extra (UDP 1010) and is not required when a host is already set.
    """
    if cli_host and cli_host.strip():
        return cli_host.strip()
    for key in ("NITEPR5_PS5_HOST", "PS5_IP"):
        val = os.environ.get(key, "").strip()
        if val:
            return val
    for path in (REPO_ROOT / ".ps5debug-host", REPO_ROOT / "scripts" / "ps5_ip.txt"):
        val = _read_host_file(path)
        if val:
            return val
    if discover:
        from ps5dbg.discover import discover as lan_discover

        hits = lan_discover(timeout=2.0)
        if not hits:
            print("LAN discover found no PS5Debug consoles (UDP 1010).", file=sys.stderr)
            print(HINT, file=sys.stderr)
            raise SystemExit(1)
        if len(hits) > 1:
            print(
                f"LAN discover found {len(hits)} consoles: {', '.join(hits)}",
                file=sys.stderr,
            )
            print("Using the first. Pass --host to pick one.", file=sys.stderr)
        return hits[0]
    print(
        "No PS5 host. Pass --host / -H, set NITEPR5_PS5_HOST or PS5_IP, "
        "write the IP to .ps5debug-host or scripts/ps5_ip.txt, or use --discover.",
        file=sys.stderr,
    )
    raise SystemExit(2)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "NitePR5 Phase 0: connect to PS5Debug on TCP 744 and list processes. "
            "Does not attach, scan, or read/write game memory."
        )
    )
    parser.add_argument("-H", "--host", help="PS5 LAN IP or hostname")
    parser.add_argument(
        "--discover",
        action="store_true",
        help="UDP LAN discover (port 1010) if no host was given",
    )
    args = parser.parse_args()

    try:
        from ps5dbg import PS5Debug
        from ps5dbg.errors import ConnectionLost, PS5DbgError
    except ImportError:
        print("ps5dbg is required: pip install -r requirements.txt", file=sys.stderr)
        return 2

    host = resolve_host(args.host, discover=args.discover)
    print(f"Connecting to {host}:{PS5DEBUG_PORT} (timeout {CONNECT_TIMEOUT:.0f}s) ...")

    try:
        with PS5Debug(host, port=PS5DEBUG_PORT, timeout=CONNECT_TIMEOUT) as ps5:
            try:
                brand = ps5.branding()
                fw = ps5.fw_version()
                print(f"Connected  branding={brand.brand}  fw={fw}")
            except PS5DbgError as exc:
                print(f"Connected (info probe failed: {exc})")

            procs = ps5.procs()
            title_by_pid: dict[int, str] = {}
            try:
                fg = ps5.foreground_app()
            except PS5DbgError:
                fg = None
            if fg is not None and fg.titleid:
                title_by_pid[fg.pid] = fg.titleid

            print(f"{'PID':>8}  {'NAME':<32}  TITLEID")
            for p in procs:
                title = title_by_pid.get(p.pid, "")
                if not title and p.name == "eboot.bin":
                    try:
                        title = ps5.proc_info(p.pid).titleid
                    except PS5DbgError:
                        title = ""
                print(f"{p.pid:8d}  {p.name:<32}  {title}")
            print(f"{len(procs)} process(es)")

            if fg is None:
                print("Foreground app: (API returned no foreground app)")
            else:
                print(
                    f"Foreground app: pid={fg.pid}  name={fg.name}  "
                    f"titleid={fg.titleid}  contentid={fg.contentid}  "
                    f"app_ver={fg.app_ver}"
                )
    except (ConnectionLost, OSError, TimeoutError) as exc:
        print(f"Connection failed: {exc}", file=sys.stderr)
        print(HINT, file=sys.stderr)
        return 1
    except PS5DbgError as exc:
        print(f"PS5Debug error: {exc}", file=sys.stderr)
        print(HINT, file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
