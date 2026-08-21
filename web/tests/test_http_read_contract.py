"""HTTP clients must never GET unbounded read (n omitted / n='all' / dump RAM).

Default peephole is 512 bytes, not "all". FastAPI/app.py is owned by the UI
worker and may be missing; HTTP tests skip if the app cannot be imported.
"""

from __future__ import annotations

import pytest

from http_read_contract import UNBOUNDED_READ, http_read_n
from nitepr5_core import HEX_PEEPHOLE_DEFAULT, READ_MAX, InvalidReadSize, ReadTooLarge


def test_default_read_n_is_512_not_all() -> None:
    assert HEX_PEEPHOLE_DEFAULT == 512
    assert http_read_n() == 512
    assert http_read_n(None) == 512
    assert http_read_n() != UNBOUNDED_READ
    assert http_read_n() != "all"


def test_http_read_refuses_unbounded_and_oversize() -> None:
    with pytest.raises(ReadTooLarge):
        http_read_n("all")
    with pytest.raises(ReadTooLarge):
        http_read_n(READ_MAX + 1)
    with pytest.raises(InvalidReadSize):
        http_read_n(0)
    assert http_read_n(512) == 512
    assert http_read_n(4096) == 4096


def test_http_app_read_default_is_peephole_if_present() -> None:
    """Skip when UI app is not here yet. Never treat missing n as dump-all."""
    app_mod = pytest.importorskip("app")
    pytest.importorskip("fastapi")
    if not hasattr(app_mod, "app"):
        pytest.skip("app.app not defined yet")

    app = app_mod.app
    schema = app.openapi()
    read_paths = [
        (path, spec)
        for path, methods in schema.get("paths", {}).items()
        if "read" in path.lower()
        for method, spec in methods.items()
        if method.lower() == "get"
    ]
    if not read_paths:
        pytest.skip("no GET read route in OpenAPI yet")

    for _path, spec in read_paths:
        params = spec.get("parameters") or []
        n_params = [p for p in params if p.get("name") in ("n", "length", "size")]
        if not n_params:
            continue
        for param in n_params:
            default = param.get("schema", {}).get("default", param.get("default"))
            assert default != "all"
            assert default != UNBOUNDED_READ
            if default is not None:
                assert default == HEX_PEEPHOLE_DEFAULT
