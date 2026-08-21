"""Phase 1 session tests against MockTransport (no PS5 required)."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

_WEB = Path(__file__).resolve().parents[2]
if str(_WEB) not in sys.path:
    sys.path.insert(0, str(_WEB))

from nitepr5_core import (  # noqa: E402
    HEX_PEEPHOLE_DEFAULT,
    READ_MAX,
    ConnectFailed,
    InvalidReadSize,
    MockTransport,
    NoTarget,
    NotConnected,
    ReadTooLarge,
    Session,
)


class SessionMockTests(unittest.TestCase):
    def setUp(self) -> None:
        self.transport = MockTransport()
        self.session = Session(self.transport)

    def tearDown(self) -> None:
        self.session.disconnect()

    def test_connect_is_noop(self) -> None:
        self.session.connect("192.168.4.42")
        self.assertTrue(self.session.connected)
        self.assertEqual(self.session.host, "192.168.4.42")

    def test_connect_empty_host_fails(self) -> None:
        with self.assertRaises(ConnectFailed) as ctx:
            self.session.connect("")
        self.assertIn("744", str(ctx.exception))
        self.assertIn("rest mode", str(ctx.exception).lower())

    def test_processes_and_foreground(self) -> None:
        self.session.connect("mock")
        procs = self.session.processes()
        names = {p.name: p for p in procs}
        self.assertIn("eboot.bin", names)
        self.assertEqual(names["eboot.bin"].pid, MockTransport.EBOOT_PID)
        self.assertEqual(names["eboot.bin"].titleid, MockTransport.TITLEID)
        fg = self.session.foreground()
        self.assertEqual(fg.pid, MockTransport.EBOOT_PID)
        self.assertEqual(fg.titleid, MockTransport.TITLEID)

    def test_attach_target_is_logical(self) -> None:
        self.session.connect("mock")
        self.session.attach_target(MockTransport.EBOOT_PID)
        self.assertEqual(self.session.target_pid, MockTransport.EBOOT_PID)
        self.assertEqual(self.transport.maps_fetch_count, 0)

    def test_maps_cached_until_refresh_or_retarget(self) -> None:
        self.session.connect("mock")
        self.session.attach_target(MockTransport.EBOOT_PID)
        first = self.session.maps()
        self.assertEqual(len(first), 4)
        self.assertEqual(self.transport.maps_fetch_count, 1)
        second = self.session.maps()
        self.assertEqual(self.transport.maps_fetch_count, 1)
        self.assertEqual([m.start for m in first], [m.start for m in second])
        self.session.maps(refresh=True)
        self.assertEqual(self.transport.maps_fetch_count, 2)
        self.session.attach_target(MockTransport.SHELL_PID)
        self.session.attach_target(MockTransport.EBOOT_PID)
        self.session.maps()
        self.assertEqual(self.transport.maps_fetch_count, 3)

    def test_reconnect_clears_target_and_maps_cache(self) -> None:
        self.session.connect("mock")
        self.session.attach_target(MockTransport.EBOOT_PID)
        self.session.maps()
        self.assertEqual(self.session.target_pid, MockTransport.EBOOT_PID)
        self.assertEqual(self.transport.maps_fetch_count, 1)
        self.session.connect("mock")
        self.assertIsNone(self.session.target_pid)
        with self.assertRaises(NoTarget):
            self.session.maps()
        self.assertEqual(self.transport.maps_fetch_count, 1)

    def test_maps_without_target_raises(self) -> None:
        self.session.connect("mock")
        with self.assertRaises(NoTarget):
            self.session.maps()

    def test_read_512(self) -> None:
        self.session.connect("mock")
        self.session.attach_target(MockTransport.EBOOT_PID)
        data = self.session.read(None, MockTransport.RAM_BASE, HEX_PEEPHOLE_DEFAULT)
        self.assertEqual(len(data), 512)
        self.assertEqual(data[0], 0)
        self.assertEqual(data[1], 1)

    def test_read_4097_raises(self) -> None:
        self.session.connect("mock")
        self.session.attach_target(MockTransport.EBOOT_PID)
        with self.assertRaises(ReadTooLarge):
            self.session.read(None, MockTransport.RAM_BASE, READ_MAX + 1)
        with self.assertRaises(ReadTooLarge):
            self.session.read(None, MockTransport.RAM_BASE, 4097)

    def test_read_0_raises(self) -> None:
        self.session.connect("mock")
        self.session.attach_target(MockTransport.EBOOT_PID)
        with self.assertRaises(InvalidReadSize):
            self.session.read(None, MockTransport.RAM_BASE, 0)

    def test_read_before_connect_raises(self) -> None:
        with self.assertRaises(NotConnected):
            self.session.read(MockTransport.EBOOT_PID, MockTransport.RAM_BASE, 16)

    def test_discover_without_console(self) -> None:
        self.assertEqual(self.session.discover(), ["127.0.0.1"])

    def test_home_pid_zero_is_valid_target(self) -> None:
        self.session.connect("mock")
        self.session.attach_target(0)
        self.assertEqual(self.session.target_pid, 0)
        rows = self.session.maps()
        self.assertEqual(rows, [])
        self.assertEqual(self.transport.maps_fetch_count, 1)


if __name__ == "__main__":
    unittest.main()
