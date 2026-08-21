"""NitePR5 session — Phase 1: discover, connect, processes, maps, peephole read.

``attach_target`` is logical (stores the pid). It does not debugger-attach.
write / scan / freeze / cheats are not implemented in this phase.
"""

from __future__ import annotations

from .constants import CONNECT_TIMEOUT, EBOOT_NAME, PS5DEBUG_PORT, READ_MAX
from .errors import (
    ConnectFailed,
    InvalidReadSize,
    NitePR5Error,
    NoTarget,
    NotConnected,
    ReadTooLarge,
)
from .transport import Ps5dbgTransport, Transport
from .types import ForegroundInfo, MemoryMap, ProcessInfo


def _is_pid(pid: object) -> bool:
    return isinstance(pid, int) and not isinstance(pid, bool) and pid >= 0


class Session:
    """One PS5Debug client session. Construct with a transport (default real)."""

    def __init__(self, transport: Transport | None = None) -> None:
        self._transport: Transport = transport if transport is not None else Ps5dbgTransport()
        self._host: str | None = None
        self._target_pid: int | None = None
        self._maps_cache: dict[int, list[MemoryMap]] = {}

    def __enter__(self) -> Session:
        return self

    def __exit__(self, *exc: object) -> None:
        self.disconnect()

    @property
    def connected(self) -> bool:
        return self._transport.connected

    @property
    def host(self) -> str | None:
        return self._host

    @property
    def target_pid(self) -> int | None:
        return self._target_pid

    def _require_connected(self) -> None:
        if not self._transport.connected:
            raise NotConnected("not connected to PS5Debug (TCP 744)")

    def _resolve_pid(self, pid: int | None) -> int:
        if pid is not None:
            if not _is_pid(pid):
                raise NoTarget(f"invalid pid: {pid!r}")
            return pid
        if self._target_pid is None:
            raise NoTarget("no target pid; call attach_target(pid) or pass pid")
        return self._target_pid

    def discover(self, *, timeout: float = 2.0) -> list[str]:
        """IPs from UDP 1010 (or the mock). Does not require TCP 744."""
        return self._transport.discover(timeout=timeout)

    def connect(self, host: str) -> None:
        """Open PS5Debug on TCP 744. Reasonable timeout is CONNECT_TIMEOUT (10s)."""
        host = (host or "").strip()
        if not host:
            raise ConnectFailed("no PS5 host")
        self.disconnect()
        self._transport.connect(host, port=PS5DEBUG_PORT, timeout=CONNECT_TIMEOUT)
        self._host = host

    def disconnect(self) -> None:
        self._transport.disconnect()
        self._host = None
        self._target_pid = None
        self._maps_cache.clear()

    def processes(self) -> list[ProcessInfo]:
        """pid, name, and titleid when cheap (foreground pid or eboot.bin proc_info)."""
        self._require_connected()
        procs = self._transport.procs()
        title_by_pid: dict[int, str] = {}
        try:
            fg = self._transport.foreground_app()
        except Exception:
            fg = None
        if fg is not None and fg.titleid:
            title_by_pid[fg.pid] = fg.titleid
        out: list[ProcessInfo] = []
        for p in procs:
            title = p.titleid or title_by_pid.get(p.pid, "")
            if not title and p.name == EBOOT_NAME:
                try:
                    title = self._transport.proc_info(p.pid).titleid
                except Exception:
                    title = ""
            out.append(ProcessInfo(pid=p.pid, name=p.name, titleid=title))
        return out

    def foreground(self) -> ForegroundInfo:
        """Foreground app. pid 0 is valid (home screen)."""
        self._require_connected()
        return self._transport.foreground_app()

    def attach_target(self, pid: int) -> None:
        """Remember pid for maps/read. Does NOT PT_ATTACH / debug_session().attach."""
        self._require_connected()
        if not _is_pid(pid):
            raise NoTarget(f"invalid pid: {pid!r}")
        if self._target_pid is not None and pid != self._target_pid:
            self._maps_cache.clear()
        self._target_pid = pid

    def maps(
        self,
        pid: int | None = None,
        *,
        refresh: bool = False,
    ) -> list[MemoryMap]:
        """Cached VM map rows (metadata only). Default pid is the attached target.

        Does not walk regions or return hex. refresh=True bypasses the cache.
        """
        self._require_connected()
        pid = self._resolve_pid(pid)
        if not refresh and pid in self._maps_cache:
            return list(self._maps_cache[pid])
        rows = self._transport.maps(pid)
        self._maps_cache[pid] = list(rows)
        return list(rows)

    def read(self, pid: int | None, addr: int, n: int) -> bytes:
        """Peephole read. Default pid is the attached target. Rejects n > 4096."""
        if not isinstance(n, int) or isinstance(n, bool) or n <= 0:
            raise InvalidReadSize(n)
        if n > READ_MAX:
            raise ReadTooLarge(n)
        self._require_connected()
        pid = self._resolve_pid(pid)
        if not isinstance(addr, int) or isinstance(addr, bool):
            raise NitePR5Error(f"addr must be int, got {addr!r}")
        return self._transport.read(pid, address=addr, length=n)
