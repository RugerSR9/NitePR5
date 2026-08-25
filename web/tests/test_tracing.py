"""LangSmith tracing stays off in pytest and never serializes RAM dumps."""

from __future__ import annotations

from dataclasses import dataclass

from tracing import (
    SKIP_PATHS,
    redact_value,
    should_trace_path,
    tracing_enabled,
)


def test_pytest_does_not_enable_tracing() -> None:
    assert tracing_enabled() is False


def test_poll_and_static_paths_are_skipped() -> None:
    for path in SKIP_PATHS:
        assert should_trace_path(path) is False
    assert should_trace_path("/static/app.js") is False
    assert should_trace_path("/static/style.css") is False


def test_user_session_paths_are_traced() -> None:
    assert should_trace_path("/api/connect") is True
    assert should_trace_path("/api/write") is True
    assert should_trace_path("/api/scan/start") is True
    assert should_trace_path("/api/attach_target") is True


def test_redact_omits_bytes_and_hex_payloads() -> None:
    @dataclass
    class Hit:
        addr: int
        current: bytes
        previous: bytes | None

    payload = {
        "self": object(),
        "addr": 0x20000000,
        "data": b"\x01\x02\x03\x04",
        "hex": {"data": "deadbeef"},
        "hits": [Hit(addr=1, current=b"\xff\xff", previous=None)],
    }
    redacted = redact_value(payload)
    assert "self" not in redacted
    assert redacted["addr"] == 0x20000000
    assert redacted["data"] == {"_omitted_bytes": 4}
    assert redacted["hex"]["data"] == {"_omitted_hex_chars": 8}
    assert redacted["hits"][0]["current"] == {"_omitted_bytes": 2}
    assert "\\x01" not in repr(redacted)
    assert "deadbeef" not in str(redacted)


def test_redact_truncates_long_lists() -> None:
    redacted = redact_value(list(range(40)))
    assert len(redacted) == 33
    assert redacted[-1] == {"_truncated": 8}
