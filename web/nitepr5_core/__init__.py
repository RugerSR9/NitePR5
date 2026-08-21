"""NitePR5 session core — connect, target, maps, peephole read via ps5dbg.

Put ``web/`` on ``sys.path``::

    from nitepr5_core import Session, MockTransport, discover, resolve_host
"""

from __future__ import annotations

from .constants import (
    CONNECT_TIMEOUT,
    HEX_PEEPHOLE_DEFAULT,
    PS5DEBUG_PORT,
    READ_MAX,
)
from .errors import (
    ConnectFailed,
    InvalidReadSize,
    NitePR5Error,
    NoTarget,
    NotConnected,
    ReadTooLarge,
)
from .host import discover, resolve_host
from .session import Session
from .transport import MockTransport, Ps5dbgTransport
from .types import ForegroundInfo, MemoryMap, ProcessInfo

__all__ = [
    "CONNECT_TIMEOUT",
    "HEX_PEEPHOLE_DEFAULT",
    "PS5DEBUG_PORT",
    "READ_MAX",
    "ConnectFailed",
    "InvalidReadSize",
    "NitePR5Error",
    "NoTarget",
    "NotConnected",
    "ReadTooLarge",
    "discover",
    "resolve_host",
    "Session",
    "MockTransport",
    "Ps5dbgTransport",
    "ForegroundInfo",
    "MemoryMap",
    "ProcessInfo",
]
