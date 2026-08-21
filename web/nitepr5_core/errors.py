"""NitePR5-core errors. FastAPI can map these to HTTP status later."""

from __future__ import annotations

from .constants import READ_MAX

REST_MODE_HINT = (
    "Could not reach PS5Debug on TCP 744. Rest mode drops this socket — "
    "wake the console and reconnect. On FW 8.00+ (including 9.60) load "
    "ps5debug-NG via etaHEN elfldr on 9021; the Toolbox PS5Debug toggle "
    "is firmware-gated."
)


class NitePR5Error(Exception):
    """Base for nitepr5-core errors."""


class NotConnected(NitePR5Error):
    """No open PS5Debug session on TCP 744."""


class NoTarget(NitePR5Error):
    """maps/read need a pid argument or a prior attach_target()."""


class ConnectFailed(NitePR5Error):
    """TCP 744 connect/ping failed."""

    def __init__(self, message: str) -> None:
        if REST_MODE_HINT not in message:
            message = f"{message} {REST_MODE_HINT}"
        super().__init__(message)


class ReadTooLarge(NitePR5Error):
    """read() length exceeds the 4 KiB peephole cap."""

    def __init__(self, n: int, max_n: int = READ_MAX) -> None:
        self.n = n
        self.max_n = max_n
        super().__init__(
            f"read length {n} exceeds max {max_n} (hex peephole cap; "
            f"default window is 512 bytes)"
        )


class InvalidReadSize(NitePR5Error):
    """read() length is not a positive int."""

    def __init__(self, n: object) -> None:
        self.n = n
        super().__init__(f"read length must be > 0, got {n!r}")


class ResultsTooMany(NitePR5Error):
    """scan_results(limit) refused: limit exceeds RESULTS_MAX (256)."""

    def __init__(self, limit: int, max_n: int | None = None) -> None:
        from .constants import RESULTS_MAX

        self.limit = limit
        self.max_n = RESULTS_MAX if max_n is None else max_n
        super().__init__(
            f"scan_results limit {limit} exceeds max {self.max_n} "
            f"(count-first UI; narrow in-game, then Next Scan)"
        )


class InvalidResultLimit(NitePR5Error):
    """scan_results(limit) refused: limit is not a positive int."""

    def __init__(self, limit: object) -> None:
        self.limit = limit
        super().__init__(f"scan_results limit must be > 0, got {limit!r}")


class ScanActive(NitePR5Error):
    """A scan is already in flight (one scan per Session / PID)."""


ScanInFlight = ScanActive


class ScanUnsupported(NitePR5Error):
    """Turbo snapshot declined, unknown without a path, or unsafe to continue."""


class NoScan(NitePR5Error):
    """scan_next / undo / count / results called with no active scan."""


class UndoTooLarge(NitePR5Error):
    """Cannot undo: previous generation had more than RESULTS_MAX hits (never fetched)."""

    def __init__(self, count: int, max_n: int | None = None) -> None:
        from .constants import RESULTS_MAX

        self.count = count
        self.max_n = RESULTS_MAX if max_n is None else max_n
        super().__init__(
            f"cannot undo: previous scan count {count} exceeds {self.max_n} "
            f"(that generation was never fetched)"
        )


class InvalidScanRegions(NitePR5Error):
    """Region set is not allowed (default is writable_cached; never 'all')."""

    def __init__(self, regions: object) -> None:
        self.regions = regions
        super().__init__(
            f"scan regions must be 'writable_cached' (never default 'all'), got {regions!r}"
        )


class InvalidScanValue(NitePR5Error):
    """Missing or out-of-range scan value (exact u32 requires 0..0xFFFFFFFF)."""


class InvalidScanCompare(NitePR5Error):
    """Unknown compare name, or a compare that is not valid for this step."""
