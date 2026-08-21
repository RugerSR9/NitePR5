"""Find a PS5Debug host (Phase 0 order) or LAN-discover via UDP 1010."""

from __future__ import annotations

import os
from pathlib import Path


def _repo_root() -> Path:
    # web/nitepr5_core/host.py → repo root
    return Path(__file__).resolve().parents[2]


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


def resolve_host(explicit: str | None = None, *, repo_root: Path | None = None) -> str | None:
    """Host order: explicit, NITEPR5_PS5_HOST, PS5_IP, .ps5debug-host, scripts/ps5_ip.txt.

    Returns None if nothing is set. Does not run UDP discover — that is extra.
    """
    if explicit and explicit.strip():
        return explicit.strip()
    for key in ("NITEPR5_PS5_HOST", "PS5_IP"):
        val = os.environ.get(key, "").strip()
        if val:
            return val
    root = repo_root if repo_root is not None else _repo_root()
    for path in (root / ".ps5debug-host", root / "scripts" / "ps5_ip.txt"):
        val = _read_host_file(path)
        if val:
            return val
    return None


def discover(*, timeout: float = 2.0) -> list[str]:
    """UDP LAN discover (port 1010) via ps5dbg. Empty list if nothing replies."""
    from ps5dbg.discover import discover as lan_discover

    return list(lan_discover(timeout=timeout))
