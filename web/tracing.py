"""LangSmith tracing for the FastAPI app (langsmith-trace skill).

NitePR5 is not a LangChain app. FastAPI has no LLM client to wrap, so this
module follows the skill's non-LangChain path: ``@traceable`` / ``trace()``
on session work, plus ``TracingMiddleware`` for header propagation.

Never send game RAM (hex peepholes, watch bytes, freeze payloads) to LangSmith.
Skip high-frequency poll routes (ARCHITECTURE §5.3: hex ≤4 Hz, watch ~10 Hz,
freeze ~15 Hz). Pytest stays off unless NITEPR5_TRACE_TESTS=1.
"""

from __future__ import annotations

import logging
import os
import sys
from collections.abc import Callable
from pathlib import Path
from typing import Any

from starlette.middleware.base import BaseHTTPMiddleware
from starlette.requests import Request
from starlette.responses import Response
from starlette.types import ASGIApp

_LOG = logging.getLogger("nitepr5")

# Poll / static paths: tracing these would flood LangSmith and leak RAM dumps.
SKIP_PATHS = frozenset(
    {
        "/",
        "/api/defaults",
        "/api/read",
        "/api/watch/poll",
        "/api/freeze/tick",
    }
)
SKIP_PREFIXES = ("/static/",)

# Nested spans under the HTTP request. Omit read / watch_poll / freeze_tick.
SESSION_SPANS = (
    "discover",
    "connect",
    "disconnect",
    "processes",
    "foreground",
    "attach_target",
    "maps",
    "write",
    "scan_start",
    "scan_next",
    "scan_undo",
    "scan_count",
    "scan_results",
    "watch_add",
    "watch_remove",
    "freeze_add",
    "freeze_remove",
    "cheat_load",
    "cheat_save",
    "cheat_toggle",
    "cheat_from_freezes",
)

_ENABLED = False


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def _truthy(value: str | None) -> bool:
    return (value or "").strip().lower() in ("1", "true", "yes", "on")


def _under_pytest() -> bool:
    return "pytest" in sys.modules or bool(os.environ.get("PYTEST_CURRENT_TEST"))


def should_trace_path(path: str) -> bool:
    if path in SKIP_PATHS:
        return False
    return not path.startswith(SKIP_PREFIXES)


def redact_value(value: Any) -> Any:
    """Drop raw bytes / hex dumps; keep lengths, addresses, and counts."""
    if isinstance(value, (bytes, bytearray, memoryview)):
        return {"_omitted_bytes": len(value)}
    if isinstance(value, dict):
        out: dict[str, Any] = {}
        for key, item in value.items():
            if key == "self":
                continue
            if key in ("data", "current", "previous") and isinstance(item, str):
                out[key] = {"_omitted_hex_chars": len(item)}
            else:
                out[key] = redact_value(item)
        return out
    if isinstance(value, (list, tuple)):
        if len(value) > 32:
            head = [redact_value(item) for item in value[:32]]
            head.append({"_truncated": len(value) - 32})
            return head
        return [redact_value(item) for item in value]
    fields = getattr(value, "__dataclass_fields__", None)
    if fields is not None:
        return redact_value({name: getattr(value, name) for name in fields})
    return value


def _process_inputs(inputs: dict[str, Any]) -> dict[str, Any]:
    return redact_value(inputs)


def _process_outputs(outputs: Any) -> Any:
    return redact_value(outputs)


def tracing_enabled() -> bool:
    return _ENABLED


def configure_tracing() -> bool:
    """Load .env, honor TRACE_TO_LANGSMITH / LANGSMITH_TRACING, alias LANGCHAIN_*."""
    global _ENABLED
    _ENABLED = False

    try:
        from dotenv import load_dotenv
    except ImportError:
        load_dotenv = None
    if load_dotenv is not None and not _under_pytest():
        load_dotenv(_repo_root() / ".env", override=False)

    if not os.environ.get("LANGSMITH_API_KEY", "").strip():
        alias = os.environ.get("LANGCHAIN_API_KEY", "").strip()
        if alias:
            os.environ["LANGSMITH_API_KEY"] = alias
    if not os.environ.get("LANGSMITH_PROJECT", "").strip():
        alias = os.environ.get("LANGCHAIN_PROJECT", "").strip()
        if alias:
            os.environ["LANGSMITH_PROJECT"] = alias

    requested = (
        _truthy(os.environ.get("TRACE_TO_LANGSMITH"))
        or _truthy(os.environ.get("LANGSMITH_TRACING"))
        or _truthy(os.environ.get("LANGCHAIN_TRACING_V2"))
    )
    if _under_pytest() and not _truthy(os.environ.get("NITEPR5_TRACE_TESTS")):
        requested = False

    if not requested:
        os.environ["LANGSMITH_TRACING"] = "false"
        return False

    if not os.environ.get("LANGSMITH_API_KEY", "").strip():
        _LOG.warning("LangSmith tracing requested but LANGSMITH_API_KEY is unset")
        os.environ["LANGSMITH_TRACING"] = "false"
        return False

    os.environ["LANGSMITH_TRACING"] = "true"
    os.environ.setdefault("LANGSMITH_PROJECT", "nitepr5")
    _ENABLED = True
    _LOG.info("LangSmith tracing on (project=%s)", os.environ["LANGSMITH_PROJECT"])
    return True


def _wrap_session(session: object) -> None:
    from langsmith import traceable

    for name in SESSION_SPANS:
        method = getattr(session, name, None)
        if not callable(method):
            continue
        wrapped: Callable[..., Any] = traceable(
            name=f"session.{name}",
            run_type="tool",
            process_inputs=_process_inputs,
            process_outputs=_process_outputs,
            metadata={"layer": "nitepr5-core"},
            tags=["nitepr5", "session"],
        )(method)
        setattr(session, name, wrapped)


class RequestTraceMiddleware(BaseHTTPMiddleware):
    """Root span per user-initiated HTTP request. Polls are skipped."""

    def __init__(self, app: ASGIApp) -> None:
        super().__init__(app)

    async def dispatch(self, request: Request, call_next: Callable) -> Response:
        path = request.url.path
        if not should_trace_path(path):
            return await call_next(request)

        from langsmith import trace

        query = {key: value for key, value in request.query_params.multi_items()}
        async with trace(
            name=f"{request.method} {path}",
            run_type="chain",
            inputs={"method": request.method, "path": path, "query": query},
            tags=["nitepr5", "http"],
            metadata={"service": "nitepr5", "component": "web"},
        ) as run:
            response = await call_next(request)
            run.end(outputs={"status_code": response.status_code})
            if response.status_code >= 400:
                run.error = f"HTTP {response.status_code}"
            return response


def instrument_app(app: Any, session: object) -> None:
    """Attach LangSmith middleware and wrap Session methods. No-op if disabled."""
    if not configure_tracing():
        return
    try:
        from langsmith.middleware import TracingMiddleware
    except ImportError:
        _LOG.warning("langsmith is not installed; tracing disabled")
        return

    _wrap_session(session)
    app.add_middleware(RequestTraceMiddleware)
    app.add_middleware(TracingMiddleware)


def flush_tracing() -> None:
    if not _ENABLED:
        return
    try:
        from langsmith import Client
    except ImportError:
        return
    try:
        Client().flush()
    except Exception:
        _LOG.debug("LangSmith flush failed", exc_info=True)
