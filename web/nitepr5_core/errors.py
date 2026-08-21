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
