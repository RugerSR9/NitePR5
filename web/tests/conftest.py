"""pytest bootstrap: put ``web/`` on sys.path for ``nitepr5_core`` imports."""

from __future__ import annotations

import sys
from collections.abc import Iterator
from pathlib import Path

import pytest

_TESTS_DIR = Path(__file__).resolve().parent
_WEB_DIR = _TESTS_DIR.parent
for _path in (str(_WEB_DIR), str(_TESTS_DIR)):
    if _path not in sys.path:
        sys.path.insert(0, _path)

from nitepr5_core import MockTransport, Session  # noqa: E402


@pytest.fixture
def transport() -> MockTransport:
    return MockTransport()


@pytest.fixture
def session(transport: MockTransport) -> Iterator[Session]:
    sess = Session(transport)
    yield sess
    sess.disconnect()


@pytest.fixture
def connected(session: Session, transport: MockTransport) -> tuple[Session, MockTransport]:
    session.connect("127.0.0.1")
    return session, transport
