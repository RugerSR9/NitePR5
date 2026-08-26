"""HTTP Phase 4 plugin contract (HANDOFF freeze handoff).

PC FastAPI binds Session.plugin_arm / plugin_disarm / plugin_status.
The on-console plugin listens on LAN :1745 (GET /status, POST /arm, POST /disarm).
Web must not freeze via :744 while armed.
"""

from __future__ import annotations

import json
import socket
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from typing import Any

try:
    from nitepr5_core.constants import PLUGIN_PORT as PLUGIN_PORT
except ImportError:
    PLUGIN_PORT = 1745

PLUGIN_ARM_PATH = "/api/plugin/arm"
PLUGIN_DISARM_PATH = "/api/plugin/disarm"
PLUGIN_STATUS_PATH = "/api/plugin/status"

PLUGIN_HTTP_PATHS = (PLUGIN_ARM_PATH, PLUGIN_DISARM_PATH, PLUGIN_STATUS_PATH)


def plugin_port_open(host: str = "127.0.0.1", port: int = PLUGIN_PORT) -> bool:
    try:
        with socket.create_connection((host, port), timeout=0.2):
            return True
    except OSError:
        return False


def start_mock_plugin(host: str = "127.0.0.1", port: int = 0):
    """Local HTTP stand-in for the etaHEN plugin (not a real PS5)."""
    state: dict[str, Any] = {
        "armed": False,
        "pid": 0,
        "freeze_count": 0,
        "cheat_id": "",
        "enabled": [],
        "dbg": True,
        "arm_posts": 0,
        "disarm_posts": 0,
        "status_gets": 0,
    }

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt: str, *args: object) -> None:
            return

        def _read_json(self) -> dict:
            n = int(self.headers.get("Content-Length") or 0)
            if n <= 0:
                return {}
            raw = self.rfile.read(n)
            if not raw:
                return {}
            try:
                parsed = json.loads(raw.decode())
            except (UnicodeDecodeError, json.JSONDecodeError):
                return {}
            return parsed if isinstance(parsed, dict) else {}

        def _send(self, obj: dict, code: int = 200) -> None:
            body = json.dumps(obj).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:
            path = self.path.split("?", 1)[0]
            if path == "/status":
                state["status_gets"] += 1
                self._send(
                    {
                        "ok": True,
                        "armed": state["armed"],
                        "pid": state["pid"],
                        "freeze_count": state["freeze_count"],
                        "cheat_id": state["cheat_id"],
                        "enabled": list(state["enabled"]),
                        "dbg": state["dbg"],
                    }
                )
                return
            self._send({"ok": False}, 404)

        def do_POST(self) -> None:
            path = self.path.split("?", 1)[0]
            body = self._read_json()
            if path == "/arm":
                state["arm_posts"] += 1
                state["armed"] = True
                state["pid"] = int(body.get("pid") or 0)
                freezes = body.get("freezes") or []
                state["freeze_count"] = len(freezes) if isinstance(freezes, list) else 0
                cheat = body.get("cheat")
                if isinstance(cheat, dict):
                    state["cheat_id"] = str(cheat.get("id") or "")
                if "enabled" in body:
                    enabled = body.get("enabled") or []
                    state["enabled"] = list(enabled) if isinstance(enabled, list) else []
                self._send(
                    {
                        "ok": True,
                        "armed": True,
                        "freeze_count": state["freeze_count"],
                    }
                )
                return
            if path == "/disarm":
                state["disarm_posts"] += 1
                state["armed"] = False
                self._send({"ok": True, "armed": False})
                return
            self._send({"ok": False}, 404)

    httpd = HTTPServer((host, port), Handler)
    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    return httpd, state, int(httpd.server_address[1])
