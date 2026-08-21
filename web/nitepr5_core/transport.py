"""PS5Debug transport: real ps5dbg client, plus an in-memory mock for tests.

Neither transport debugger-attaches a game (no debug_session / PT_ATTACH).
Scans use ps5dbg.turboscan (resident) or iterative SCAN_START/COUNT — never
PS5Debug.scan() / scan_aob_find_all().
"""

from __future__ import annotations

import socket
import struct
from abc import ABC, abstractmethod
from contextlib import contextmanager
from collections.abc import Iterator

from .constants import (
    CONNECT_TIMEOUT,
    PS5DEBUG_PORT,
    READ_MAX,
    RESULTS_MAX,
    SCAN_IO_TIMEOUT,
    SCAN_ALIGN_U32,
    SCAN_COMPARE_UNKNOWN,
    SCAN_REGIONS_PROBE_BYTES,
    SCAN_VALUE_UINT32,
    TSE_ALIASING,
    TSE_RESCAN_ALIASING,
    TSE_SERVER_RESIDENT,
    TSE_SNAPSHOT,
    TSE_SNAPSHOT_SEGMENTS,
    WRITE_MAX,
)
from .errors import (
    ConnectFailed,
    InvalidReadSize,
    InvalidWriteSize,
    NotConnected,
    ReadTooLarge,
    ScanUnsupported,
    WriteTooLarge,
)
from .scan import match_u32, u32_from_bytes, value_size
from .types import ForegroundInfo, MemoryMap, ProcessDetail, ProcessInfo, ScanHit, ScanRegion


class Transport(ABC):
    """Inspection, peephole R/W, turbo/iterative scan. One :744 connection."""

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

    @abstractmethod
    def write(self, pid: int, address: int, data: bytes) -> None:
        """Write process memory. Keyword on the real client is ``address=``."""
        ...

    @contextmanager
    def scan_wait(self) -> Iterator[None]:
        """Hold the command socket without the 10s connect timeout.

        Mock is a no-op. Real transport sets ``SCAN_IO_TIMEOUT`` (blocking).
        """
        yield

    @abstractmethod
    def scan_caps(self) -> tuple[int, int, int] | None:
        """TURBOSCAN_CAPS. None if the server has no turbo (BadStatus). No auth."""
        ...

    @abstractmethod
    def scan_authenticate(self) -> None:
        """ps5.authenticate(flags=2) before stateful scan. Caps probe does not need this."""
        ...

    @abstractmethod
    def scan_regions(self, pid: int, *, probe_bytes: int = SCAN_REGIONS_PROBE_BYTES) -> list[ScanRegion] | None:
        """TURBOSCAN_REGIONS after auth. None if the probe fails (use maps()).

        ``probe_bytes`` must stay at 1 (PCD bit). Never pass 0 — that is 64 KiB
        per readable map and hangs first scan.
        """
        ...

    @abstractmethod
    def scan_start_turbo(
        self,
        pid: int,
        segments: list[tuple[int, int]],
        *,
        value_type: int,
        compare_type: int,
        alignment: int,
        value: int,
        unknown: bool,
        engines: int,
    ) -> tuple[bool, int]:
        """Start a server-resident turbo scan. Returns (accepted, count).

        accepted=False means the match-list buffer declined (overflow / alloc).
        Caller should retry via snapshot + exact COUNT, not stream hits to the PC.
        """
        ...

    @abstractmethod
    def scan_count_resident(
        self,
        *,
        value_type: int,
        compare_type: int,
        value: int,
    ) -> int:
        ...

    @abstractmethod
    def scan_get_resident(
        self,
        start_index: int,
        count: int,
        value_length: int,
    ) -> list[tuple[int, bytes, bytes, bytes | None]]:
        ...

    @abstractmethod
    def scan_end(self) -> None:
        """Free the server-resident turbo session. Safe no-op if none."""
        ...

    @abstractmethod
    def scan_start_iterative(
        self,
        pid: int,
        segments: list[tuple[int, int]],
        *,
        value_type: int,
        compare_type: int,
        alignment: int,
        value: int,
    ) -> list[ScanHit]:
        ...

    @abstractmethod
    def scan_next_iterative(
        self,
        pid: int,
        segments: list[tuple[int, int]],
        candidates: list[ScanHit],
        *,
        value_type: int,
        compare_type: int,
        value: int,
    ) -> list[ScanHit]:
        ...


class Ps5dbgTransport(Transport):
    """Wraps ``ps5dbg.PS5Debug``. Keyword for memory read is ``address=``."""

    def __init__(self) -> None:
        self._ps5 = None
        self.host: str | None = None
        self._scan_authed = False
        self._turbo_open = False
        self._rescan_aliasing = False

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
        self._configure_cmd_socket()

    def disconnect(self) -> None:
        self._scan_authed = False
        self._turbo_open = False
        self._rescan_aliasing = False
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

    def _cmd_sock(self):
        ps5 = self._ps5
        if ps5 is None:
            return None
        return getattr(ps5.connection, "_sock", None)

    def _configure_cmd_socket(self) -> None:
        """TCP keepalive so a dropped 744 is still detected during a long scan."""
        sock = self._cmd_sock()
        if sock is None:
            return
        try:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
        except OSError:
            return
        if hasattr(socket, "SIO_KEEPALIVE_VALS"):
            try:
                sock.ioctl(socket.SIO_KEEPALIVE_VALS, (1, 30_000, 5_000))
            except OSError:
                pass
        for opt, value in (
            ("TCP_KEEPIDLE", 30),
            ("TCP_KEEPINTVL", 5),
            ("TCP_KEEPCNT", 6),
        ):
            name = getattr(socket, opt, None)
            if name is None:
                continue
            try:
                sock.setsockopt(socket.IPPROTO_TCP, name, value)
            except OSError:
                pass

    @contextmanager
    def scan_wait(self) -> Iterator[None]:
        """Block on recv for turbo scan progress (u64 / summary), not 10s."""
        sock = self._cmd_sock()
        if sock is None:
            yield
            return
        previous = sock.gettimeout()
        try:
            sock.settimeout(SCAN_IO_TIMEOUT)
            yield
        finally:
            try:
                sock.settimeout(previous)
            except OSError:
                pass

    def _call(self, fn, *args, **kwargs):
        from ps5dbg.errors import ConnectionLost, PS5DbgError

        try:
            return fn(*args, **kwargs)
        except ConnectionLost as exc:
            raise NotConnected(
                f"{exc}. Reconnect — a failed scan can desync TCP 744. "
                "Rest mode also drops this socket."
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

    def write(self, pid: int, address: int, data: bytes) -> None:
        if not isinstance(data, (bytes, bytearray)) or len(data) == 0:
            raise InvalidWriteSize(0 if isinstance(data, (bytes, bytearray)) else data)
        if len(data) > WRITE_MAX:
            raise WriteTooLarge(len(data))
        ps5 = self._require()
        # Same trap as read: keyword is address=, not addr=.
        # Do not reimplement PROC_WRITE_MULTI; ps5dbg 0.1.1 loops proc_write.
        self._call(ps5.write, pid, address=address, data=bytes(data))

    def scan_caps(self) -> tuple[int, int, int] | None:
        import ps5dbg.turboscan as ts
        from ps5dbg.errors import BadStatus, ConnectionLost, PS5DbgError

        ps5 = self._require()
        try:
            with self.scan_wait():
                return ts.turboscan_caps(ps5.connection)
        except BadStatus:
            return None
        except ConnectionLost as exc:
            raise NotConnected(
                f"{exc}. Reconnect — a failed scan can desync TCP 744. "
                "Rest mode also drops this socket."
            ) from exc
        except PS5DbgError as exc:
            from .errors import NitePR5Error

            raise NitePR5Error(str(exc)) from exc

    def scan_authenticate(self) -> None:
        if self._scan_authed:
            return
        self._call(self._require().authenticate, 2)
        self._scan_authed = True

    def scan_regions(
        self, pid: int, *, probe_bytes: int = SCAN_REGIONS_PROBE_BYTES
    ) -> list[ScanRegion] | None:
        import ps5dbg.turboscan as ts
        from ps5dbg.errors import BadStatus, ConnectionLost, PS5DbgError

        if probe_bytes <= 0:
            probe_bytes = SCAN_REGIONS_PROBE_BYTES
        ps5 = self._require()
        try:
            with self.scan_wait():
                raw = ts.turboscan_regions(
                    ps5.connection, pid, max_regions=0, probe_bytes=probe_bytes
                )
        except (BadStatus, PS5DbgError, ConnectionLost, OSError):
            return None
        return [
            ScanRegion(start=r.start, end=r.end, prot=r.prot, uncached=r.uncached)
            for r in raw
        ]

    def _scan_conn(self):
        return self._require().connection

    def scan_start_turbo(
        self,
        pid: int,
        segments: list[tuple[int, int]],
        *,
        value_type: int,
        compare_type: int,
        alignment: int,
        value: int,
        unknown: bool,
        engines: int,
    ) -> tuple[bool, int]:
        import ps5dbg.turboscan as ts
        from ps5dbg.errors import ConnectionLost, PS5DbgError

        from .scan import turbo_start_resident_segments, turbo_start_snapshot_segments

        if not segments:
            raise ScanUnsupported("no segments to scan")
        conn = self._scan_conn()
        use_aliasing = bool(engines & TSE_ALIASING)
        self._rescan_aliasing = bool(engines & TSE_RESCAN_ALIASING)
        try:
            with self.scan_wait():
                if unknown:
                    if not (engines & TSE_SNAPSHOT):
                        raise ScanUnsupported(
                            "unknown first scan needs turbo snapshot (TSE_SNAPSHOT)"
                        )
                    count = turbo_start_snapshot_segments(
                        conn,
                        pid,
                        segments,
                        value_type,
                        alignment,
                        use_aliasing=use_aliasing,
                    )
                    self._turbo_open = True
                    return True, count
                if not (engines & TSE_SERVER_RESIDENT):
                    return False, 0
                # Always segmented, even for one range. Single-range resident
                # overflow streams the full hit list (desync if drained as
                # u64s). Segmented overflow is an empty sentinel only.
                if not (engines & TSE_SNAPSHOT_SEGMENTS) and len(segments) != 1:
                    return False, 0
                if engines & TSE_SNAPSHOT_SEGMENTS:
                    stored, count = turbo_start_resident_segments(
                        conn,
                        pid,
                        segments,
                        value_type,
                        compare_type,
                        alignment,
                        value,
                        use_aliasing=use_aliasing,
                    )
                else:
                    addr, length = segments[0]
                    flags = ts.TS_SERVER_RESIDENT
                    if use_aliasing:
                        flags |= ts.TS_USE_ALIASING
                    stored, count = ts.turboscan_start_resident(
                        conn,
                        pid,
                        addr,
                        length,
                        value_type,
                        compare_type,
                        alignment,
                        value,
                        flags=flags,
                    )
                if stored == 0:
                    return False, count
                self._turbo_open = True
                return True, count
        except ScanUnsupported:
            try:
                self.scan_end()
            except Exception:
                pass
            raise
        except ConnectionLost as exc:
            raise NotConnected(
                f"{exc}. Reconnect — a failed scan can desync TCP 744. "
                "Rest mode also drops this socket."
            ) from exc
        except PS5DbgError as exc:
            from .errors import NitePR5Error

            raise NitePR5Error(str(exc)) from exc

    def scan_count_resident(
        self,
        *,
        value_type: int,
        compare_type: int,
        value: int,
    ) -> int:
        import ps5dbg.turboscan as ts

        conn = self._scan_conn()
        flags = ts.TS_SERVER_RESIDENT
        if self._rescan_aliasing:
            flags |= ts.TS_RESCAN_ALIASING
        with self.scan_wait():
            return self._call(
                ts.turboscan_count_resident,
                conn,
                value_type,
                compare_type,
                value,
                flags=flags,
            )

    def scan_get_resident(
        self,
        start_index: int,
        count: int,
        value_length: int,
    ) -> list[tuple[int, bytes, bytes, bytes | None]]:
        import ps5dbg.turboscan as ts

        if count <= 0 or count > RESULTS_MAX:
            raise ScanUnsupported(
                f"refusing unbounded turbo GET (count={count}, max={RESULTS_MAX})"
            )
        conn = self._scan_conn()
        with self.scan_wait():
            return self._call(
                ts.turboscan_get_resident,
                conn,
                start_index,
                count,
                value_length,
                0,
            )

    def scan_end(self) -> None:
        if not self._turbo_open:
            return
        import ps5dbg.turboscan as ts
        from ps5dbg.errors import ConnectionLost, PS5DbgError

        try:
            with self.scan_wait():
                ts.turboscan_end(self._scan_conn())
        except (ConnectionLost, PS5DbgError, OSError, NotConnected):
            pass
        self._turbo_open = False

    def scan_start_iterative(
        self,
        pid: int,
        segments: list[tuple[int, int]],
        *,
        value_type: int,
        compare_type: int,
        alignment: int,
        value: int,
    ) -> list[ScanHit]:
        from ps5dbg.protocol import proc_scan_start

        conn = self._scan_conn()
        hits: list[ScanHit] = []
        with self.scan_wait():
            for addr, length in segments:
                rel = self._call(
                    proc_scan_start,
                    conn,
                    pid,
                    addr,
                    length,
                    value_type,
                    compare_type,
                    alignment,
                    value,
                )
                for off, val in rel:
                    hits.append(ScanHit(addr=addr + off, current=val, previous=val))
        return hits

    def scan_next_iterative(
        self,
        pid: int,
        segments: list[tuple[int, int]],
        candidates: list[ScanHit],
        *,
        value_type: int,
        compare_type: int,
        value: int,
    ) -> list[ScanHit]:
        from ps5dbg.protocol import proc_scan_count

        conn = self._scan_conn()
        survivors: list[ScanHit] = []
        with self.scan_wait():
            for addr, length in segments:
                group = [
                    (h.addr - addr, h.current)
                    for h in candidates
                    if addr <= h.addr < addr + length
                ]
                if not group:
                    continue
                got = self._call(
                    proc_scan_count,
                    conn,
                    pid,
                    addr,
                    value_type,
                    compare_type,
                    value,
                    group,
                )
                for off, val in got:
                    prev = None
                    abs_addr = addr + off
                    for h in candidates:
                        if h.addr == abs_addr:
                            prev = h.current
                            break
                    survivors.append(ScanHit(addr=abs_addr, current=val, previous=prev))
        return survivors


class MockTransport(Transport):
    """In-memory PS5Debug stand-in. No sockets, no console.

    ``maps_fetch_count`` is incremented on every maps() call so tests can
    assert Session cache hits.
    """

    EBOOT_PID = 1234
    SHELL_PID = 1
    UI_PID = 200
    RAM_BASE = 0x200000000
    HEAP_END = RAM_BASE + 0x3000
    GPU_START = RAM_BASE + 0x4000
    GPU_END = RAM_BASE + 0x5000
    STORE_SIZE = 0x5000
    TITLEID = "CUSA00004"
    CONTENTID = "IV0000-CUSA00004_00-EXAMPLE000000000"
    APP_VER = "01.00"

    # Planted UINT32s. 100 in r-x is a decoy (must not match default regions).
    # GPU/SceGnm is rw- but uncached — cheap TURBOSCAN_REGIONS must skip it.
    PLANTED_U32 = 100
    DECOY_RX_ADDR = RAM_BASE + 0x0FFC
    WRITABLE_EBOOT_ADDR = RAM_BASE + 0x1000
    HEAP_A = RAM_BASE + 0x2000
    HEAP_B = RAM_BASE + 0x2004
    HEAP_C = RAM_BASE + 0x2008
    HEAP_D = RAM_BASE + 0x2010
    GPU_ADDR = GPU_START

    def __init__(self) -> None:
        self.host: str | None = None
        self._connected = False
        self.maps_fetch_count = 0
        self.regions_probe_count = 0
        self.last_regions_probe_bytes = 0
        self.get_resident_calls = 0
        self.unbounded_get_attempted = False
        self.scan_auth_count = 0
        self.iterative_start_calls = 0
        self.decline_resident = False
        self.decline_snapshot = False
        self.fail_regions_probe = False
        self.last_use_aliasing = False
        self.last_rescan_aliasing = False
        self.write_calls: list[tuple[int, int, bytes]] = []
        self._scan_authed = False
        self._turbo_open = False
        self._rescan_aliasing = False
        self._survivors: list[ScanHit] = []
        self._value_type = SCAN_VALUE_UINT32
        self._ram = bytearray(i % 256 for i in range(self.STORE_SIZE))
        self.poke_u32(self.DECOY_RX_ADDR, self.PLANTED_U32)
        self.poke_u32(self.WRITABLE_EBOOT_ADDR, self.PLANTED_U32)
        self.poke_u32(self.HEAP_A, self.PLANTED_U32)
        self.poke_u32(self.HEAP_B, 200)
        self.poke_u32(self.HEAP_C, self.PLANTED_U32)
        self.poke_u32(self.HEAP_D, 50)
        self.poke_u32(self.GPU_ADDR, self.PLANTED_U32)
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
                    end=self.HEAP_END,
                    offset=0,
                    prot=3,  # rw-
                ),
                MemoryMap(
                    name="SceGnm",
                    start=self.GPU_START,
                    end=self.GPU_END,
                    offset=0,
                    prot=3,  # rw- uncached (Garlic)
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
        self._scan_authed = False
        self._turbo_open = False
        self._rescan_aliasing = False
        self._survivors = []

    def poke_u32(self, address: int, value: int) -> None:
        """Test helper: plant a little-endian UINT32 in mock RAM. Not a session write."""
        off = address - self.RAM_BASE
        if off < 0 or off + 4 > len(self._ram):
            raise ValueError(f"poke_u32 address {address:#x} out of mock RAM")
        struct.pack_into("<I", self._ram, off, value & 0xFFFFFFFF)

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

    def write(self, pid: int, address: int, data: bytes) -> None:
        self._require()
        if not isinstance(data, (bytes, bytearray)) or len(data) == 0:
            raise InvalidWriteSize(0 if isinstance(data, (bytes, bytearray)) else data)
        if len(data) > WRITE_MAX:
            raise WriteTooLarge(len(data))
        payload = bytes(data)
        self.write_calls.append((pid, address, payload))
        for i, byte in enumerate(payload):
            off = address + i - self.RAM_BASE
            if 0 <= off < len(self._ram):
                self._ram[off] = byte

    def scan_caps(self) -> tuple[int, int, int] | None:
        self._require()
        engines = (
            TSE_ALIASING
            | TSE_SERVER_RESIDENT
            | TSE_SNAPSHOT
            | TSE_SNAPSHOT_SEGMENTS
            | TSE_RESCAN_ALIASING
        )
        return (1, engines, 1)

    def scan_authenticate(self) -> None:
        self._require()
        self.scan_auth_count += 1
        self._scan_authed = True

    def scan_regions(
        self, pid: int, *, probe_bytes: int = SCAN_REGIONS_PROBE_BYTES
    ) -> list[ScanRegion] | None:
        self._require()
        self.regions_probe_count += 1
        self.last_regions_probe_bytes = (
            SCAN_REGIONS_PROBE_BYTES if probe_bytes <= 0 else probe_bytes
        )
        if self.fail_regions_probe:
            return None
        return [
            ScanRegion(
                start=m.start,
                end=m.end,
                prot=m.prot,
                uncached=(m.name == "SceGnm"),
            )
            for m in self._maps.get(pid, ())
        ]

    def _walk_segments(
        self,
        segments: list[tuple[int, int]],
        *,
        compare_type: int,
        alignment: int,
        value: int,
        previous: dict[int, bytes] | None = None,
    ) -> list[ScanHit]:
        size = value_size(self._value_type)
        hits: list[ScanHit] = []
        for addr, length in segments:
            end = addr + length
            cur = ((addr + alignment - 1) // alignment) * alignment
            while cur + size <= end:
                off = cur - self.RAM_BASE
                if 0 <= off and off + size <= len(self._ram):
                    current = bytes(self._ram[off : off + size])
                    prev_b = previous.get(cur) if previous else current
                    if prev_b is None:
                        prev_b = current
                    cur_i = u32_from_bytes(current)
                    prev_i = u32_from_bytes(prev_b)
                    if match_u32(compare_type, cur_i, prev_i, value):
                        hits.append(ScanHit(addr=cur, current=current, previous=prev_b))
                cur += alignment
        return hits

    def scan_start_turbo(
        self,
        pid: int,
        segments: list[tuple[int, int]],
        *,
        value_type: int,
        compare_type: int,
        alignment: int,
        value: int,
        unknown: bool,
        engines: int,
    ) -> tuple[bool, int]:
        self._require()
        if not self._scan_authed:
            raise ScanUnsupported("scan authenticate(flags=2) required before stateful scan")
        self.last_use_aliasing = bool(engines & TSE_ALIASING)
        self._rescan_aliasing = bool(engines & TSE_RESCAN_ALIASING)
        if self.decline_resident and not unknown:
            return False, 0
        if self.decline_snapshot and unknown:
            raise ScanUnsupported("server declined unknown snapshot (snapshot_ok=0)")
        self._value_type = value_type
        ct = SCAN_COMPARE_UNKNOWN if unknown else compare_type
        align = alignment if alignment > 0 else SCAN_ALIGN_U32
        # Default regions: only walk caller-selected segments (writable+cached).
        self._survivors = self._walk_segments(
            segments, compare_type=ct, alignment=align, value=value
        )
        self._turbo_open = True
        return True, len(self._survivors)

    def scan_count_resident(
        self,
        *,
        value_type: int,
        compare_type: int,
        value: int,
    ) -> int:
        self._require()
        if not self._turbo_open:
            raise ScanUnsupported("no resident scan")
        self.last_rescan_aliasing = self._rescan_aliasing
        size = value_size(value_type)
        nxt: list[ScanHit] = []
        for hit in self._survivors:
            off = hit.addr - self.RAM_BASE
            if 0 <= off and off + size <= len(self._ram):
                current = bytes(self._ram[off : off + size])
            else:
                current = hit.current
            prev = hit.current
            if match_u32(
                compare_type,
                u32_from_bytes(current),
                u32_from_bytes(prev),
                value,
            ):
                nxt.append(ScanHit(addr=hit.addr, current=current, previous=prev))
        self._survivors = nxt
        return len(self._survivors)

    def scan_get_resident(
        self,
        start_index: int,
        count: int,
        value_length: int,
    ) -> list[tuple[int, bytes, bytes, bytes | None]]:
        self._require()
        if count <= 0 or count > RESULTS_MAX:
            self.unbounded_get_attempted = True
        self.get_resident_calls += 1
        window = self._survivors[start_index : start_index + max(0, count)]
        out: list[tuple[int, bytes, bytes, bytes | None]] = []
        for h in window:
            cur = h.current[:value_length] if value_length else h.current
            prev = h.previous[:value_length] if h.previous and value_length else h.previous
            out.append((h.addr, cur, prev if prev is not None else cur, None))
        return out

    def scan_end(self) -> None:
        self._turbo_open = False
        self._survivors = []

    def scan_start_iterative(
        self,
        pid: int,
        segments: list[tuple[int, int]],
        *,
        value_type: int,
        compare_type: int,
        alignment: int,
        value: int,
    ) -> list[ScanHit]:
        self.iterative_start_calls += 1
        accepted, _count = self.scan_start_turbo(
            pid,
            segments,
            value_type=value_type,
            compare_type=compare_type,
            alignment=alignment,
            value=value,
            unknown=compare_type == SCAN_COMPARE_UNKNOWN,
            engines=TSE_SERVER_RESIDENT,
        )
        if not accepted:
            return []
        return list(self._survivors)

    def scan_next_iterative(
        self,
        pid: int,
        segments: list[tuple[int, int]],
        candidates: list[ScanHit],
        *,
        value_type: int,
        compare_type: int,
        value: int,
    ) -> list[ScanHit]:
        self._require()
        self._survivors = list(candidates)
        self._turbo_open = True
        self.scan_count_resident(
            value_type=value_type, compare_type=compare_type, value=value
        )
        return list(self._survivors)
