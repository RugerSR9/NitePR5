"""PS5Debug transport: real ps5dbg client, plus an in-memory mock for tests.

Neither transport debugger-attaches a game (no debug_session / PT_ATTACH).
"""

from __future__ import annotations

from abc import ABC, abstractmethod

from .constants import CONNECT_TIMEOUT, PS5DEBUG_PORT, READ_MAX
from .errors import ConnectFailed, InvalidReadSize, NotConnected, ReadTooLarge
from .types import ForegroundInfo, MemoryMap, ProcessDetail, ProcessInfo


class Transport(ABC):
    """Minimal R/W-free inspection surface. write/scan/attach live elsewhere."""

    @property
    @abstractmethod
    def connected(self) -> bool:
        ...

    @abstractmethod
    def connect(
        self,
        host: str,
        *,
        port: int = PS5DEBUG_PORT,
        timeout: float = CONNECT_TIMEOUT,
    ) -> None:
        ...

    @abstractmethod
    def disconnect(self) -> None:
        ...

    @abstractmethod
    def discover(self, *, timeout: float = 2.0) -> list[str]:
        ...

    @abstractmethod
    def procs(self) -> list[ProcessInfo]:
        ...

    @abstractmethod
    def foreground_app(self) -> ForegroundInfo:
        ...

    @abstractmethod
    def proc_info(self, pid: int) -> ProcessDetail:
        ...

    @abstractmethod
    def maps(self, pid: int) -> list[MemoryMap]:
        ...

    @abstractmethod
    def read(self, pid: int, address: int, length: int) -> bytes:
        ...


class Ps5dbgTransport(Transport):
    """Wraps ``ps5dbg.PS5Debug``. Keyword for memory read is ``address=``."""

    def __init__(self) -> None:
        self._ps5 = None
        self.host: str | None = None

    @property
    def connected(self) -> bool:
        return self._ps5 is not None

    def connect(
        self,
        host: str,
        *,
        port: int = PS5DEBUG_PORT,
        timeout: float = CONNECT_TIMEOUT,
    ) -> None:
        host = (host or "").strip()
        if not host:
            raise ConnectFailed("no PS5 host")
        self.disconnect()
        from ps5dbg import PS5Debug
        from ps5dbg.errors import ConnectionLost, PS5DbgError

        ps5 = PS5Debug(host, port=port, timeout=timeout)
        try:
            ps5.connect()
            ps5.ping()
        except (ConnectionLost, OSError, TimeoutError, PS5DbgError) as exc:
            ps5.close()
            raise ConnectFailed(f"{host}:{port}: {exc}") from exc
        self._ps5 = ps5
        self.host = host

    def disconnect(self) -> None:
        ps5, self._ps5 = self._ps5, None
        self.host = None
        if ps5 is not None:
            try:
                ps5.close()
            except OSError:
                pass

    def discover(self, *, timeout: float = 2.0) -> list[str]:
        from .host import discover as lan_discover

        return lan_discover(timeout=timeout)

    def _require(self):
        if self._ps5 is None:
            raise NotConnected("not connected to PS5Debug (TCP 744)")
        return self._ps5

    def _call(self, fn, *args, **kwargs):
        from ps5dbg.errors import ConnectionLost, PS5DbgError

        try:
            return fn(*args, **kwargs)
        except ConnectionLost as exc:
            raise NotConnected(
                f"{exc}. Rest mode drops TCP 744; wake the console and reconnect."
            ) from exc
        except PS5DbgError as exc:
            from .errors import NitePR5Error

            raise NitePR5Error(str(exc)) from exc

    def procs(self) -> list[ProcessInfo]:
        raw = self._call(self._require().procs)
        return [ProcessInfo(pid=p.pid, name=p.name) for p in raw]

    def foreground_app(self) -> ForegroundInfo:
        fg = self._call(self._require().foreground_app)
        return ForegroundInfo(
            pid=fg.pid,
            name=fg.name,
            titleid=fg.titleid,
            contentid=fg.contentid,
            app_ver=fg.app_ver,
        )

    def proc_info(self, pid: int) -> ProcessDetail:
        info = self._call(self._require().proc_info, pid)
        return ProcessDetail(
            pid=info.pid,
            name=info.name,
            path=info.path,
            titleid=info.titleid,
            contentid=info.contentid,
        )

    def maps(self, pid: int) -> list[MemoryMap]:
        raw = self._call(self._require().maps, pid)
        return [
            MemoryMap(
                name=m.name,
                start=m.start,
                end=m.end,
                offset=m.offset,
                prot=m.prot,
            )
            for m in raw
        ]

    def read(self, pid: int, address: int, length: int) -> bytes:
        if length > READ_MAX:
            raise ReadTooLarge(length)
        if not isinstance(length, int) or isinstance(length, bool) or length <= 0:
            raise InvalidReadSize(length)
        ps5 = self._require()
        # TRAP: ps5dbg README says addr=; the real kwarg is address=.
        return self._call(ps5.read, pid, address=address, length=length)


class MockTransport(Transport):
    """In-memory PS5Debug stand-in. No sockets, no console.

    ``maps_fetch_count`` is incremented on every maps() call so tests can
    assert Session cache hits.
    """

    EBOOT_PID = 1234
    SHELL_PID = 1
    UI_PID = 200
    RAM_BASE = 0x200000000
    RAM_SIZE = 8192
    TITLEID = "CUSA00004"
    CONTENTID = "IV0000-CUSA00004_00-EXAMPLE000000000"
    APP_VER = "01.00"

    def __init__(self) -> None:
        self.host: str | None = None
        self._connected = False
        self.maps_fetch_count = 0
        self._ram = bytearray(i % 256 for i in range(self.RAM_SIZE))
        self._procs = [
            ProcessInfo(pid=self.SHELL_PID, name="SceSysCore"),
            ProcessInfo(pid=self.EBOOT_PID, name="eboot.bin"),
            ProcessInfo(pid=self.UI_PID, name="SceShellUI"),
        ]
        self._foreground = ForegroundInfo(
            pid=self.EBOOT_PID,
            name="eboot.bin",
            titleid=self.TITLEID,
            contentid=self.CONTENTID,
            app_ver=self.APP_VER,
        )
        self._maps: dict[int, list[MemoryMap]] = {
            self.EBOOT_PID: [
                MemoryMap(
                    name="eboot.bin",
                    start=self.RAM_BASE,
                    end=self.RAM_BASE + 0x1000,
                    offset=0,
                    prot=5,  # r-x
                ),
                MemoryMap(
                    name="eboot.bin",
                    start=self.RAM_BASE + 0x1000,
                    end=self.RAM_BASE + 0x2000,
                    offset=0x1000,
                    prot=3,  # rw-
                ),
                MemoryMap(
                    name="heap",
                    start=self.RAM_BASE + 0x2000,
                    end=self.RAM_BASE + self.RAM_SIZE,
                    offset=0,
                    prot=3,  # rw-
                ),
            ]
        }

    @property
    def connected(self) -> bool:
        return self._connected

    def connect(
        self,
        host: str,
        *,
        port: int = PS5DEBUG_PORT,
        timeout: float = CONNECT_TIMEOUT,
    ) -> None:
        host = (host or "").strip()
        if not host:
            raise ConnectFailed("no PS5 host")
        self.host = host
        self._connected = True

    def disconnect(self) -> None:
        self._connected = False
        self.host = None

    def discover(self, *, timeout: float = 2.0) -> list[str]:
        return ["127.0.0.1"]

    def _require(self) -> None:
        if not self._connected:
            raise NotConnected("not connected to PS5Debug (TCP 744)")

    def procs(self) -> list[ProcessInfo]:
        self._require()
        return list(self._procs)

    def foreground_app(self) -> ForegroundInfo:
        self._require()
        return self._foreground

    def proc_info(self, pid: int) -> ProcessDetail:
        self._require()
        for p in self._procs:
            if p.pid == pid:
                title = self.TITLEID if pid == self.EBOOT_PID else ""
                content = self.CONTENTID if pid == self.EBOOT_PID else ""
                return ProcessDetail(
                    pid=p.pid,
                    name=p.name,
                    path=f"/app0/{p.name}" if p.name == "eboot.bin" else "",
                    titleid=title,
                    contentid=content,
                )
        from .errors import NitePR5Error

        raise NitePR5Error(f"no such pid {pid}")

    def maps(self, pid: int) -> list[MemoryMap]:
        self._require()
        self.maps_fetch_count += 1
        return list(self._maps.get(pid, ()))

    def read(self, pid: int, address: int, length: int) -> bytes:
        self._require()
        if length > READ_MAX:
            raise ReadTooLarge(length)
        if not isinstance(length, int) or isinstance(length, bool) or length <= 0:
            raise InvalidReadSize(length)
        out = bytearray(length)
        for i in range(length):
            off = address + i - self.RAM_BASE
            if 0 <= off < len(self._ram):
                out[i] = self._ram[off]
        return bytes(out)
