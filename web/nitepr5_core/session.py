"""NitePR5 session — connect, maps, peephole R/W, turbo scan, watch, freeze, cheats.

``attach_target`` is logical (stores the pid). It does not debugger-attach.

Scan policy (ARCHITECTURE §5.3):
- One scan per Session / one ``:744`` connection. A new ``scan_start`` ends
  the previous hunt (like ``connect()`` disconnects first). ``ScanActive``
  only if a scan op is already in flight (``_busy``).
- ``attach_target`` to a different pid while a scan is active raises ``ScanActive``.
- ``disconnect()`` ends any scan (``turboscan_end``) then drops the TCP session.
- Default type UINT32, alignment 4, compare exact, regions writable+cached.
- Unknown first scan requires ``unknown=True`` or ``compare='unknown'``.
- ``scan_results`` refuses limit > 256 or <= 0. If count > 256, returns [] and
  does not GET (count-first UI).

Watch / freeze / write take ``_hold_io(block=False)`` so they fail fast with
``ScanActive`` while a hunt (``_busy``) owns the socket. They wait for a
short hex/watch/freeze peer — not 400 on every overlapping tick. No second
``:744`` connection. UI owns 10 Hz / 15 Hz timers; this layer has no
background threads.
"""

from __future__ import annotations

import json
import logging
import threading
import urllib.error
import urllib.request
from collections.abc import Iterator
from contextlib import contextmanager
from dataclasses import dataclass, field
from pathlib import Path

from .cheat import (
    cheat_file_from_freezes,
    cheat_to_dict,
    load_cheat_file,
    module_base_from_maps,
    save_cheat_file,
)
from .constants import (
    CONNECT_TIMEOUT,
    EBOOT_NAME,
    EXECUTABLE_MAP_NAME,
    FREEZE_MAX,
    FREEZE_SIZE_MAX,
    FREEZE_SIZE_MIN,
    PLUGIN_HTTP_TIMEOUT,
    PLUGIN_PORT,
    PS5DEBUG_PORT,
    READ_MAX,
    RESULTS_MAX,
    SCAN_ALIGN_U32,
    SCAN_COMPARE_EXACT,
    SCAN_COMPARE_UNKNOWN,
    SCAN_REGIONS_DEFAULT,
    SCAN_REGIONS_PROBE_BYTES,
    TSE_SERVER_RESIDENT,
    TSE_SNAPSHOT,
    WATCH_MAX,
    WATCH_SIZE_DEFAULT,
    WATCH_SIZES,
    WRITE_MAX,
)
from .errors import (
    ConnectFailed,
    FreezeLimit,
    InvalidAddress,
    InvalidCheat,
    InvalidFreezeSize,
    InvalidReadSize,
    InvalidResultLimit,
    InvalidScanRegions,
    InvalidWatchSize,
    InvalidWriteSize,
    NoCheat,
    NoFreeze,
    NoMod,
    NoScan,
    NoTarget,
    NoWatch,
    NotConnected,
    PluginError,
    PluginUnreachable,
    ReadTooLarge,
    ResultsTooMany,
    ScanActive,
    ScanUnsupported,
    UndoTooLarge,
    WatchLimit,
    WriteTooLarge,
)
from .scan import (
    TOO_MANY_MATCHES,
    alignment_or_default,
    cap_get_count,
    classify_maps,
    classify_turbo_regions,
    hits_from_resident,
    match_u32,
    parse_compare,
    parse_u32_value,
    parse_value_type,
    ranges_to_segments,
    require_first_compare,
    require_next_compare,
    snapshot_fits,
    u32_from_bytes,
    value_size,
)
from .transport import Ps5dbgTransport, Transport
from .types import (
    CheatFile,
    FreezeEntry,
    ForegroundInfo,
    MemoryMap,
    PluginStatus,
    ProcessInfo,
    ScanHit,
    WatchEntry,
    WatchValue,
)

_LOG = logging.getLogger("nitepr5")


def _is_pid(pid: object) -> bool:
    return isinstance(pid, int) and not isinstance(pid, bool) and pid >= 0


@dataclass
class _ScanState:
    pid: int
    value_type: int
    alignment: int
    engine: str  # "turbo" | "iterative" | "local"
    count: int
    segments: list[tuple[int, int]]
    hits: list[ScanHit] | None = None
    iterative_all: list[ScanHit] | None = None
    undo_stack: list[tuple[int, list[ScanHit] | None]] = field(default_factory=list)


_U64_MAX = (1 << 64) - 1


def _require_addr(addr: object) -> int:
    """PS5Debug PROC_READ/WRITE packs address as little-endian ``Q`` (u64)."""
    if not isinstance(addr, int) or isinstance(addr, bool) or addr < 0 or addr > _U64_MAX:
        raise InvalidAddress(addr)
    return addr


def _plugin_error_message(raw: bytes) -> str:
    try:
        obj = json.loads(raw.decode("utf-8") or "{}")
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError):
        return "plugin HTTP 400"
    if isinstance(obj, dict) and obj.get("error"):
        return str(obj["error"])
    return "plugin HTTP 400"


def _plugin_status_from_json(raw: bytes) -> PluginStatus:
    try:
        obj = json.loads(raw.decode("utf-8") or "{}")
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
        raise PluginUnreachable("plugin returned invalid JSON") from exc
    if not isinstance(obj, dict):
        raise PluginUnreachable("plugin returned invalid JSON")
    enabled_raw = obj.get("enabled") or []
    if not isinstance(enabled_raw, list):
        enabled_raw = []
    pid_raw = obj.get("pid") or 0
    freeze_raw = obj.get("freeze_count") or 0
    try:
        pid = int(pid_raw)
        freeze_count = int(freeze_raw)
    except (TypeError, ValueError):
        pid = 0
        freeze_count = 0
    return PluginStatus(
        ok=bool(obj.get("ok", True)),
        armed=bool(obj.get("armed", False)),
        pid=pid,
        freeze_count=freeze_count,
        cheat_id=str(obj.get("cheat_id") or ""),
        enabled=[str(x) for x in enabled_raw],
        dbg=bool(obj.get("dbg", False)),
    )


class Session:
    """One PS5Debug client session. Construct with a transport (default real)."""

    def __init__(self, transport: Transport | None = None) -> None:
        self._transport: Transport = transport if transport is not None else Ps5dbgTransport()
        self._host: str | None = None
        self._target_pid: int | None = None
        self._maps_cache: dict[int, list[MemoryMap]] = {}
        self._scan: _ScanState | None = None
        self._busy = False
        self._io = threading.RLock()
        self._busy_gate = threading.Lock()
        self._io_owner: int | None = None
        self._io_depth = 0
        self._watches: dict[int, WatchEntry] = {}
        self._freezes: dict[int, FreezeEntry] = {}
        self._next_watch_id = 1
        self._next_freeze_id = 1
        self._cheat: CheatFile | None = None
        self._cheat_enabled: set[str] = set()
        # Last successful connect() host. Survives disconnect() so we still
        # know the LAN IP; plugin HTTP is a separate socket from :744.
        self._plugin_host: str | None = None
        # True after a successful POST /arm. Disconnecting :744 must not clear
        # this — the console plugin keeps freeze (Phase 4 handoff).
        self._plugin_armed = False

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

    def _begin_busy(self) -> None:
        with self._busy_gate:
            if self._busy:
                raise ScanActive("scan operation already in flight")
            self._busy = True

    def _end_busy(self) -> None:
        with self._busy_gate:
            self._busy = False

    @contextmanager
    def _hold_io(self, *, block: bool = True) -> Iterator[None]:
        """Serialize all TCP 744 use.

        ``block=False`` fails fast only when another thread's scan owns the
        socket (``_busy``). The scan thread may re-enter (maps fallback).
        Hex / watch / freeze / poke wait for each other — a short timeout here
        turned live pokes into 400s whenever a peephole read was in flight.
        """
        me = threading.get_ident()
        already = self._io_owner == me
        if not block and self._busy and not already:
            raise ScanActive("scan in flight; peephole paused")
        self._io.acquire(blocking=True)
        self._io_owner = me
        self._io_depth += 1
        try:
            if not block and self._busy and not already:
                raise ScanActive("scan in flight; peephole paused")
            yield
        finally:
            self._io_depth -= 1
            if self._io_depth == 0:
                self._io_owner = None
            self._io.release()

    def discover(self, *, timeout: float = 2.0) -> list[str]:
        """IPs from UDP 1010 (or the mock). Does not require TCP 744."""
        return self._transport.discover(timeout=timeout)

    def connect(self, host: str) -> None:
        """Open PS5Debug on TCP 744. Reasonable timeout is CONNECT_TIMEOUT (10s)."""
        host = (host or "").strip()
        if not host:
            raise ConnectFailed("no PS5 host")
        with self._hold_io():
            self.disconnect()
            self._transport.connect(host, port=PS5DEBUG_PORT, timeout=CONNECT_TIMEOUT)
            self._host = host
            self._plugin_host = host

    def disconnect(self) -> None:
        with self._hold_io():
            self._end_scan(silent=True)
            self._transport.disconnect()
            self._host = None
            self._target_pid = None
            self._maps_cache.clear()
            self._clear_watches_and_freezes()
            # Do not auto-disarm the console plugin. Do not clear
            # _plugin_armed / _plugin_host — web UI closed is the Phase 4 exit.

    def plugin_host(self) -> str | None:
        """LAN host of the last successful ``connect()`` (PS5, not 127.0.0.1)."""
        return self._plugin_host

    def plugin_armed(self) -> bool:
        """In-memory: True after a successful ``plugin_arm`` until ``plugin_disarm``."""
        return self._plugin_armed

    def processes(self) -> list[ProcessInfo]:
        """pid, name, and titleid when cheap (foreground pid or eboot.bin proc_info)."""
        self._require_connected()
        with self._hold_io(block=False):
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
        with self._hold_io(block=False):
            return self._transport.foreground_app()

    def attach_target(self, pid: int) -> None:
        """Remember pid for maps/read/scan. Does NOT PT_ATTACH / debug_session().attach.

        Refuses a different pid while a scan is active (``ScanActive``). Same-pid
        attach is a no-op. Use ``scan_undo()`` until the scan ends, then retarget.
        """
        self._require_connected()
        if not _is_pid(pid):
            raise NoTarget(f"invalid pid: {pid!r}")
        if self._scan is not None and pid != self._scan.pid:
            raise ScanActive(
                "cannot change attach_target while a scan is active; "
                "scan_undo() until the scan ends"
            )
        if self._target_pid is not None and pid != self._target_pid:
            self._maps_cache.clear()
            self._clear_watches_and_freezes()
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
        with self._hold_io(block=False):
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
        addr = _require_addr(addr)
        with self._hold_io(block=False):
            return self._transport.read(pid, address=addr, length=n)

    def write(self, pid: int | None, addr: int, data: bytes) -> None:
        """Peephole poke. Default pid is the attached target. Rejects empty / >4 KiB.

        Confirm dialogs are a UI concern. Fail-fast if a hunt owns :744;
        otherwise wait for a short hex/watch/freeze peer so a poke is not
        refused as ScanActive.
        """
        if not isinstance(data, (bytes, bytearray)):
            raise InvalidWriteSize(data)
        n = len(data)
        if n == 0:
            raise InvalidWriteSize(n)
        if n > WRITE_MAX:
            raise WriteTooLarge(n)
        self._require_connected()
        pid = self._resolve_pid(pid)
        addr = _require_addr(addr)
        with self._hold_io(block=False):
            self._transport.write(pid, address=addr, data=bytes(data))

    def _clear_watches_and_freezes(self) -> None:
        self._watches.clear()
        self._freezes.clear()
        self._next_watch_id = 1
        self._next_freeze_id = 1

    def watches(self) -> list[WatchEntry]:
        """Pinned watches (metadata). Live bytes come from watch_poll()."""
        return list(self._watches.values())

    def freezes(self) -> list[FreezeEntry]:
        """Pinned freeze patches. Writes happen only in freeze_tick()."""
        return list(self._freezes.values())

    def loaded_cheat(self) -> CheatFile | None:
        """Currently loaded GoldHEN file, or None. Enabled flags are in-memory."""
        return self._cheat

    def cheat_enabled_names(self) -> list[str]:
        return sorted(self._cheat_enabled)

    def watch_add(
        self,
        pid: int | None = None,
        *,
        addr: int,
        n: int = WATCH_SIZE_DEFAULT,
        label: str = "",
    ) -> WatchEntry:
        """Pin an address. Cap 64; the 65th raises ``WatchLimit`` (fail closed)."""
        if not isinstance(n, int) or isinstance(n, bool) or n not in WATCH_SIZES:
            raise InvalidWatchSize(n)
        addr = _require_addr(addr)
        self._require_connected()
        pid = self._resolve_pid(pid)
        if len(self._watches) >= WATCH_MAX:
            raise WatchLimit()
        watch_id = self._next_watch_id
        self._next_watch_id += 1
        entry = WatchEntry(
            id=watch_id,
            pid=pid,
            addr=addr,
            n=n,
            label=str(label),
        )
        self._watches[watch_id] = entry
        return entry

    def watch_remove(self, watch_id: int) -> None:
        if watch_id not in self._watches:
            raise NoWatch(watch_id)
        del self._watches[watch_id]

    def watch_poll(self) -> list[WatchValue]:
        """Read each watch once. Empty list is a no-op. No thread / no 10 Hz."""
        if not self._watches:
            return []
        self._require_connected()
        out: list[WatchValue] = []
        with self._hold_io(block=False):
            for entry in self._watches.values():
                data = self._transport.read(entry.pid, address=entry.addr, length=entry.n)
                out.append(WatchValue(id=entry.id, addr=entry.addr, data=data))
        return out

    def freeze_add(
        self,
        pid: int | None = None,
        *,
        addr: int,
        data: bytes,
    ) -> FreezeEntry:
        """Pin a patch. Cap 32; the 33rd raises ``FreezeLimit``. Data length 1..8."""
        if not isinstance(data, (bytes, bytearray)):
            raise InvalidFreezeSize(data)
        n = len(data)
        if n < FREEZE_SIZE_MIN or n > FREEZE_SIZE_MAX:
            raise InvalidFreezeSize(n)
        addr = _require_addr(addr)
        self._require_connected()
        pid = self._resolve_pid(pid)
        if len(self._freezes) >= FREEZE_MAX:
            raise FreezeLimit()
        freeze_id = self._next_freeze_id
        self._next_freeze_id += 1
        entry = FreezeEntry(id=freeze_id, pid=pid, addr=addr, data=bytes(data))
        self._freezes[freeze_id] = entry
        return entry

    def freeze_remove(self, freeze_id: int) -> None:
        if freeze_id not in self._freezes:
            raise NoFreeze(freeze_id)
        del self._freezes[freeze_id]

    def freeze_tick(self) -> int:
        """Write each frozen patch once. Returns how many writes. No 15 Hz loop.

        When the console plugin is armed it owns freeze — return 0 and do
        not take :744 / do not write (Phase 4 handoff).
        """
        if self._plugin_armed:
            return 0
        if not self._freezes:
            return 0
        self._require_connected()
        written = 0
        with self._hold_io(block=False):
            for entry in self._freezes.values():
                self._transport.write(entry.pid, address=entry.addr, data=entry.data)
                written += 1
        return written

    def _resolve_plugin_host(self, host: str | None) -> str:
        if host is not None:
            chosen = host.strip()
            if chosen:
                return chosen
        if self.connected and self._host:
            return self._host
        # disconnect() clears _host but keeps _plugin_host so Disarm still
        # reaches the console after the web UI drops :744.
        if self._plugin_host:
            return self._plugin_host
        raise NotConnected("no PS5 host for plugin HTTP (connect first)")

    def _plugin_http(
        self,
        method: str,
        path: str,
        host: str | None,
        body: dict | None = None,
    ) -> PluginStatus:
        dest = self._resolve_plugin_host(host)
        url = f"http://{dest}:{PLUGIN_PORT}{path}"
        headers = {"Accept": "application/json"}
        data: bytes | None = None
        if body is not None:
            data = json.dumps(body).encode("utf-8")
            headers["Content-Type"] = "application/json"
        req = urllib.request.Request(url, data=data, method=method, headers=headers)
        try:
            with urllib.request.urlopen(req, timeout=PLUGIN_HTTP_TIMEOUT) as resp:
                raw = resp.read()
                status_code = getattr(resp, "status", None) or resp.getcode()
        except urllib.error.HTTPError as exc:
            err_body = b""
            try:
                err_body = exc.read() or b""
            except OSError:
                err_body = b""
            if exc.code == 400:
                raise PluginError(_plugin_error_message(err_body)) from exc
            raise PluginUnreachable(
                f"cannot reach plugin at {dest}:{PLUGIN_PORT} (HTTP {exc.code})"
            ) from exc
        except urllib.error.URLError as exc:
            reason = getattr(exc, "reason", exc)
            raise PluginUnreachable(
                f"cannot reach plugin at {dest}:{PLUGIN_PORT}: {reason}"
            ) from exc
        except (TimeoutError, OSError) as exc:
            raise PluginUnreachable(
                f"cannot reach plugin at {dest}:{PLUGIN_PORT}: {exc}"
            ) from exc
        if status_code != 200:
            raise PluginUnreachable(
                f"cannot reach plugin at {dest}:{PLUGIN_PORT} (HTTP {status_code})"
            )
        return _plugin_status_from_json(raw)

    def plugin_status(self, host: str | None = None) -> PluginStatus:
        """GET the plugin ``/status`` on the PS5 LAN host:1745."""
        return self._plugin_http("GET", "/status", host)

    def plugin_arm(self, host: str | None = None) -> PluginStatus:
        """POST ``/arm`` with attached pid + session freezes/cheat. No :744 write."""
        dest = self._resolve_plugin_host(host)
        pid = self._resolve_pid(None)
        payload: dict = {
            "pid": pid,
            "freezes": [
                {"addr": entry.addr, "data": entry.data.hex()}
                for entry in self._freezes.values()
            ],
            "cheat": cheat_to_dict(self._cheat) if self._cheat is not None else None,
            "enabled": self.cheat_enabled_names(),
        }
        status = self._plugin_http("POST", "/arm", dest, payload)
        self._plugin_armed = True
        return status

    def plugin_disarm(self, host: str | None = None) -> PluginStatus:
        """POST ``/disarm``. Does not drop the :744 session."""
        status = self._plugin_http("POST", "/disarm", host, {})
        self._plugin_armed = False
        return status

    def _module_base(self, pid: int, process: str) -> int:
        rows = self.maps(pid)
        base = module_base_from_maps(rows, process)
        if base is None:
            raise InvalidCheat(
                f"no maps named {process!r} (or {EXECUTABLE_MAP_NAME!r}) for pid {pid}"
            )
        return base

    def cheat_from_freezes(
        self,
        *,
        name: str,
        title_id: str,
        version: str,
        process: str = EBOOT_NAME,
    ) -> CheatFile:
        """Build a GoldHEN file from the freeze list (module-relative offsets)."""
        self._require_connected()
        pid = self._resolve_pid(None)
        proc = (process or EBOOT_NAME).strip() or EBOOT_NAME
        base = self._module_base(pid, proc)
        return cheat_file_from_freezes(
            list(self._freezes.values()),
            base=base,
            name=name,
            title_id=title_id,
            version=version,
            process=proc,
        )

    def cheat_load(self, path: str | Path) -> CheatFile:
        cheat = load_cheat_file(path)
        self._cheat = cheat
        self._cheat_enabled.clear()
        return cheat

    def cheat_save(self, path: str | Path, cheat: CheatFile | None = None) -> None:
        chosen = cheat if cheat is not None else self._cheat
        if chosen is None:
            raise NoCheat("no cheat file loaded")
        save_cheat_file(path, chosen)
        if cheat is not None:
            self._cheat = chosen
            self._cheat_enabled.clear()

    def cheat_toggle(self, name: str, enabled: bool) -> None:
        """Write GoldHEN on/off bytes at module_base + offset. Uses ``write()``."""
        if self._cheat is None:
            raise NoCheat("no cheat file loaded")
        mod = next((m for m in self._cheat.mods if m.name == name), None)
        if mod is None:
            raise NoMod(name)
        self._require_connected()
        pid = self._resolve_pid(None)
        process = self._cheat.process or EBOOT_NAME
        base = self._module_base(pid, process)
        for patch in mod.memory:
            addr = base + int(patch.offset, 16)
            payload = patch.on if enabled else patch.off
            self.write(pid, addr, payload)
        if enabled:
            self._cheat_enabled.add(name)
        else:
            self._cheat_enabled.discard(name)

    def _end_scan(self, *, silent: bool = False) -> None:
        scan, self._scan = self._scan, None
        if scan is None:
            return
        try:
            self._transport.scan_end()
        except Exception:
            if not silent:
                raise

    def _scan_segments(self, pid: int, regions: str) -> list[tuple[int, int]]:
        if regions != SCAN_REGIONS_DEFAULT:
            raise InvalidScanRegions(regions)
        # Cheap TURBOSCAN_REGIONS (probe_bytes=1) returns leaf-PTE PCD so we
        # can skip uncached GPU. probe_bytes=0 is 64 KiB × every readable map
        # and hangs. If the probe fails, fall back to maps() rw- (no PCD bit).
        probed = self._transport.scan_regions(pid, probe_bytes=SCAN_REGIONS_PROBE_BYTES)
        if probed is not None:
            classified = classify_turbo_regions(probed)
            source = "pcd"
        else:
            classified = classify_maps(self.maps(pid))
            source = "maps-fallback"
        merged_segs = ranges_to_segments(classified)
        if not merged_segs:
            raise ScanUnsupported("no writable cached regions to scan")
        total = sum(length for _addr, length in merged_segs)
        _LOG.info(
            "first scan: %s rw- segments, %.1f MiB (%s)",
            len(merged_segs),
            total / (1024 * 1024),
            source,
        )
        return merged_segs

    def _stash_if_small(self) -> None:
        """Fetch and cache hits when count ≤ 256 so next-scan undo can restore them."""
        assert self._scan is not None
        if self._scan.hits is not None:
            return
        if self._scan.count == 0:
            self._scan.hits = []
            return
        if self._scan.count > RESULTS_MAX:
            return
        if self._scan.engine == "turbo":
            n = cap_get_count(RESULTS_MAX, self._scan.count)
            rows = self._transport.scan_get_resident(
                0, n, value_size(self._scan.value_type)
            )
            self._scan.hits = hits_from_resident(rows)
        elif self._scan.iterative_all is not None:
            self._scan.hits = list(self._scan.iterative_all[:RESULTS_MAX])

    def _local_next(self, compare: int, value: int) -> int:
        assert self._scan is not None
        if self._scan.hits is None:
            raise UndoTooLarge(self._scan.count)
        size = value_size(self._scan.value_type)
        nxt: list[ScanHit] = []
        for hit in self._scan.hits:
            current = self.read(self._scan.pid, hit.addr, size)
            prev = hit.current
            if match_u32(
                compare,
                u32_from_bytes(current),
                u32_from_bytes(prev),
                value,
            ):
                nxt.append(ScanHit(addr=hit.addr, current=current, previous=prev))
        self._scan.hits = nxt
        self._scan.count = len(nxt)
        return self._scan.count

    def scan_start(
        self,
        pid: int | None = None,
        *,
        value_type: str = "u32",
        compare: str = "exact",
        value: int | None = None,
        alignment: int = SCAN_ALIGN_U32,
        unknown: bool = False,
        regions: str = SCAN_REGIONS_DEFAULT,
    ) -> int:
        """First scan. Returns the survivor count (not the hit list).

        Defaults: UINT32, alignment 4, exact compare, writable+cached regions.
        ``unknown`` must be explicit (or ``compare='unknown'``). Never defaults
        to scanning all regions. Replaces an idle previous hunt (does not open
        a second ``:744`` session). Raises ``ScanActive`` only if a scan op is
        already in flight.
        """
        self._require_connected()
        pid = self._resolve_pid(pid)
        vt = parse_value_type(value_type)
        align = alignment_or_default(alignment)
        unknown_explicit = bool(unknown) or parse_compare(compare) == SCAN_COMPARE_UNKNOWN
        if unknown_explicit:
            ct = SCAN_COMPARE_UNKNOWN
            parsed_value = parse_u32_value(value, required=False)
        else:
            ct = parse_compare(compare)
            require_first_compare(ct)
            parsed_value = parse_u32_value(value, required=True)
        # Set _busy before taking :744 so hex/watch/freeze fail fast instead of
        # waiting behind a hunt that can run for minutes.
        self._begin_busy()
        try:
            with self._hold_io():
                self._end_scan(silent=True)
                with self._transport.scan_wait():
                    caps = self._transport.scan_caps()
                    self._transport.scan_authenticate()
                    have_turbo = caps is not None
                    engines = caps[1] if caps is not None else 0
                    segments = self._scan_segments(pid, regions)
                    engine = "iterative"
                    count = 0
                    iterative_all: list[ScanHit] | None = None
                    if have_turbo and (engines & TSE_SERVER_RESIDENT):
                        if unknown_explicit and not (engines & TSE_SNAPSHOT):
                            raise ScanUnsupported(
                                "unknown first scan needs turbo snapshot; will not download RAM"
                            )
                        accepted, count = self._transport.scan_start_turbo(
                            pid,
                            segments,
                            value_type=vt,
                            compare_type=ct,
                            alignment=align,
                            value=parsed_value,
                            unknown=unknown_explicit,
                            engines=engines,
                        )
                        if accepted:
                            engine = "turbo"
                        elif unknown_explicit:
                            raise ScanUnsupported(
                                "unknown snapshot declined; will not download RAM"
                            )
                        elif engines & TSE_SNAPSHOT and snapshot_fits(
                            segments, alignment=align
                        ):
                            # Match-list cap is 256 MiB (~22M u32 hits). Common
                            # values overflow it. Snapshot keeps a bitmap on the
                            # console; exact COUNT then narrows without dumping.
                            _LOG.info(
                                "resident match-list declined; snapshot then exact count"
                            )
                            self._transport.scan_start_turbo(
                                pid,
                                segments,
                                value_type=vt,
                                compare_type=ct,
                                alignment=align,
                                value=parsed_value,
                                unknown=True,
                                engines=engines,
                            )
                            count = self._transport.scan_count_resident(
                                value_type=vt,
                                compare_type=SCAN_COMPARE_EXACT,
                                value=parsed_value,
                            )
                            engine = "turbo"
                            _LOG.info("snapshot exact count: %s", count)
                        else:
                            raise ScanUnsupported(TOO_MANY_MATCHES)
                    if engine != "turbo":
                        if unknown_explicit:
                            raise ScanUnsupported(
                                "unknown first scan is turbo-snapshot only (no iterative dump)"
                            )
                        iterative_all = self._transport.scan_start_iterative(
                            pid,
                            segments,
                            value_type=vt,
                            compare_type=ct,
                            alignment=align,
                            value=parsed_value,
                        )
                        count = len(iterative_all)
                        engine = "iterative"
                    self._scan = _ScanState(
                        pid=pid,
                        value_type=vt,
                        alignment=align,
                        engine=engine,
                        count=count,
                        segments=segments,
                        iterative_all=iterative_all,
                    )
                    return count
        finally:
            self._end_busy()

    def scan_next(self, *, compare: str, value: int | None = None) -> int:
        """Narrow the active scan. Returns the new survivor count."""
        self._require_connected()
        if self._scan is None:
            raise NoScan("no active scan")
        ct = parse_compare(compare)
        require_next_compare(ct)
        parsed_value = parse_u32_value(value, required=(ct == SCAN_COMPARE_EXACT))
        self._begin_busy()
        try:
            with self._hold_io():
                with self._transport.scan_wait():
                    self._stash_if_small()
                    self._scan.undo_stack.append((self._scan.count, self._scan.hits))
                    if self._scan.engine == "local":
                        return self._local_next(ct, parsed_value)
                    if self._scan.engine == "iterative":
                        candidates = self._scan.iterative_all or []
                        nxt = self._transport.scan_next_iterative(
                            self._scan.pid,
                            self._scan.segments,
                            candidates,
                            value_type=self._scan.value_type,
                            compare_type=ct,
                            value=parsed_value,
                        )
                        self._scan.iterative_all = nxt
                        self._scan.hits = None
                        self._scan.count = len(nxt)
                        return self._scan.count
                    count = self._transport.scan_count_resident(
                        value_type=self._scan.value_type,
                        compare_type=ct,
                        value=parsed_value,
                    )
                    self._scan.hits = None
                    self._scan.count = count
                    return count
        finally:
            self._end_busy()

    def scan_undo(self) -> int | None:
        """Undo one generation. None means the scan ended (first-scan undo)."""
        self._require_connected()
        if self._scan is None:
            raise NoScan("no active scan")
        self._begin_busy()
        try:
            with self._hold_io():
                with self._transport.scan_wait():
                    if not self._scan.undo_stack:
                        self._end_scan(silent=False)
                        return None
                    prev_count, prev_hits = self._scan.undo_stack[-1]
                    if prev_count > RESULTS_MAX:
                        raise UndoTooLarge(prev_count)
                    self._scan.undo_stack.pop()
                    self._scan.count = prev_count
                    self._scan.hits = prev_hits if prev_hits is not None else []
                    if self._scan.engine == "iterative":
                        self._scan.iterative_all = list(self._scan.hits)
                    else:
                        try:
                            self._transport.scan_end()
                        except Exception:
                            pass
                        self._scan.engine = "local"
                    return prev_count
        finally:
            self._end_busy()

    def scan_count(self) -> int:
        """Current survivor count. Does not fetch rows."""
        self._require_connected()
        if self._scan is None:
            raise NoScan("no active scan")
        return self._scan.count

    def scan_results(self, limit: int = RESULTS_MAX) -> list[ScanHit]:
        """At most ``limit`` hits. Fail closed above 256. Empty if count > 256."""
        if not isinstance(limit, int) or isinstance(limit, bool) or limit <= 0:
            raise InvalidResultLimit(limit)
        if limit > RESULTS_MAX:
            raise ResultsTooMany(limit)
        self._require_connected()
        if self._scan is None:
            raise NoScan("no active scan")
        if self._scan.count > RESULTS_MAX:
            return []
        if self._scan.count == 0:
            return []
        if self._scan.hits is not None:
            return list(self._scan.hits[:limit])
        if self._scan.engine == "iterative" and self._scan.iterative_all is not None:
            self._scan.hits = list(self._scan.iterative_all[:limit])
            return list(self._scan.hits)
        if self._scan.engine != "turbo":
            return []
        n = cap_get_count(limit, self._scan.count)
        with self._hold_io():
            with self._transport.scan_wait():
                rows = self._transport.scan_get_resident(
                    0, n, value_size(self._scan.value_type)
                )
        hits = hits_from_resident(rows)
        self._scan.hits = hits
        return list(hits[:limit])
