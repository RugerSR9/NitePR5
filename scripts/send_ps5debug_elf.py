#!/usr/bin/env python3
"""Send ps5debug-NG.elf to etaHEN elfldr (TCP 9021).

On FW 8.00+ (including 9.60) the etaHEN Toolbox "PS5Debug" toggle is the old
Sistr0/CTN blob and is firmware-gated. Load OpenSourcereR ps5debug-NG instead.
"""
from __future__ import annotations

import argparse
import os
import socket
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
ELFLDR_PORT = 9021
SEND_TIMEOUT = 15.0


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


def resolve_host(cli_host: str | None) -> str:
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
    print(
        "No PS5 host. Pass --host / -H, set NITEPR5_PS5_HOST or PS5_IP, "
        "or write the IP to .ps5debug-host.",
        file=sys.stderr,
    )
    raise SystemExit(2)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Send a ps5debug-NG ELF to etaHEN elfldr on TCP 9021. "
            "Do not use the Toolbox PS5Debug toggle on FW 8.00+."
        )
    )
    parser.add_argument("elf", type=Path, help="Path to ps5debug-NG_v1.3.0.elf (or newer)")
    parser.add_argument("-H", "--host", help="PS5 LAN IP or hostname")
    parser.add_argument(
        "-p",
        "--port",
        type=int,
        default=ELFLDR_PORT,
        help=f"elfldr port (default {ELFLDR_PORT})",
    )
    args = parser.parse_args()

    elf_path = args.elf.expanduser().resolve()
    if not elf_path.is_file():
        print(f"ELF not found: {elf_path}", file=sys.stderr)
        return 2
    blob = elf_path.read_bytes()
    if len(blob) < 64 or blob[:4] != b"\x7fELF":
        print(f"Not an ELF file: {elf_path}", file=sys.stderr)
        return 2

    host = resolve_host(args.host)
    print(f"Sending {elf_path.name} ({len(blob)} bytes) to {host}:{args.port} ...")
    try:
        with socket.create_connection((host, args.port), timeout=SEND_TIMEOUT) as sock:
            sock.settimeout(SEND_TIMEOUT)
            sock.sendall(blob)
            try:
                sock.shutdown(socket.SHUT_WR)
            except OSError:
                pass
    except OSError as exc:
        print(f"elfldr send failed: {exc}", file=sys.stderr)
        print(
            "Check: etaHEN is running, elfldr is listening on 9021, "
            "and this PC is on the same LAN.",
            file=sys.stderr,
        )
        return 1

    print("Sent. Watch the PS5 for: ps5debug-NG by OSR ... loaded!")
    print("Then run: python scripts/check_ps5debug.py --host", host)
    return 0


if __name__ == "__main__":
    sys.exit(main())
