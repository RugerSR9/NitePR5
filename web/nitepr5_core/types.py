"""JSON-friendly session types. Addresses are int (hex only at the UI)."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True, slots=True)
class ProcessInfo:
    pid: int
    name: str
    titleid: str = ""


@dataclass(frozen=True, slots=True)
class ProcessDetail:
    pid: int
    name: str
    path: str
    titleid: str
    contentid: str


@dataclass(frozen=True, slots=True)
class ForegroundInfo:
    """Foreground app. pid == 0 is valid (home screen)."""

    pid: int
    name: str
    titleid: str
    contentid: str
    app_ver: str


@dataclass(frozen=True, slots=True)
class MemoryMap:
    """One VM map row. Metadata only — never a hex dump of the region."""

    name: str
    start: int
    end: int
    offset: int
    prot: int

    @property
    def perms(self) -> str:
        p = self.prot
        return (
            ("r" if p & 1 else "-")
            + ("w" if p & 2 else "-")
            + ("x" if p & 4 else "-")
        )

    @property
    def size(self) -> int:
        return self.end - self.start


@dataclass(frozen=True, slots=True)
class ScanHit:
    """One survivor from scan_results. current/previous are raw little-endian bytes."""

    addr: int
    current: bytes
    previous: bytes | None = None


@dataclass(frozen=True, slots=True)
class ScanRegion:
    """One classified region (turbo regions probe or maps fallback)."""

    start: int
    end: int
    prot: int
    uncached: bool = False
