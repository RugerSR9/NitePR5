"""NitePR5 session core — connect, target, maps, peephole read, turbo scan.

Put ``web/`` on ``sys.path``::

    from nitepr5_core import Session, MockTransport, discover, resolve_host
"""

from __future__ import annotations

from .constants import (
    CONNECT_TIMEOUT,
    HEX_PEEPHOLE_DEFAULT,
    PS5DEBUG_PORT,
    READ_MAX,
    RESULTS_MAX,
    SCAN_ALIGN_U32,
    SCAN_IO_TIMEOUT,
    SCAN_REGIONS_DEFAULT,
)
from .errors import (
    ConnectFailed,
    InvalidReadSize,
    InvalidResultLimit,
    InvalidScanCompare,
    InvalidScanRegions,
    InvalidScanValue,
    NitePR5Error,
    NoScan,
    NoTarget,
    NotConnected,
    ReadTooLarge,
    ResultsTooMany,
    ScanActive,
    ScanInFlight,
    ScanUnsupported,
    UndoTooLarge,
)
from .host import discover, resolve_host
from .session import Session
from .transport import MockTransport, Ps5dbgTransport
from .types import ForegroundInfo, MemoryMap, ProcessInfo, ScanHit

__all__ = [
    "CONNECT_TIMEOUT",
    "HEX_PEEPHOLE_DEFAULT",
    "PS5DEBUG_PORT",
    "READ_MAX",
    "RESULTS_MAX",
    "SCAN_ALIGN_U32",
    "SCAN_IO_TIMEOUT",
    "SCAN_REGIONS_DEFAULT",
    "ConnectFailed",
    "InvalidReadSize",
    "InvalidResultLimit",
    "InvalidScanCompare",
    "InvalidScanRegions",
    "InvalidScanValue",
    "NitePR5Error",
    "NoScan",
    "NoTarget",
    "NotConnected",
    "ReadTooLarge",
    "ResultsTooMany",
    "ScanActive",
    "ScanHit",
    "ScanInFlight",
    "ScanUnsupported",
    "UndoTooLarge",
    "discover",
    "resolve_host",
    "Session",
    "MockTransport",
    "Ps5dbgTransport",
    "ForegroundInfo",
    "MemoryMap",
    "ProcessInfo",
]
