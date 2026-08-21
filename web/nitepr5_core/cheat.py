"""GoldHEN JSON cheat load/save. No extra keys; on/off stay hex in the file."""

from __future__ import annotations

import json
from pathlib import Path

from .constants import EBOOT_NAME, WRITE_MAX
from .errors import InvalidCheat
from .types import CheatFile, CheatMod, CheatPatch

_HEX_DIGITS = frozenset("0123456789abcdefABCDEF")
_REQUIRED_ROOT = ("name", "id", "version", "process", "mods")


def _as_str(value: object, *, field: str) -> str:
    if not isinstance(value, str):
        raise InvalidCheat(f"{field} must be a string, got {type(value).__name__}")
    return value


def _parse_hex_bytes(value: object, *, field: str) -> bytes:
    if not isinstance(value, str) or not value.strip():
        raise InvalidCheat(f"{field} must be a non-empty hex string")
    s = value.strip()
    if len(s) % 2 != 0 or any(c not in _HEX_DIGITS for c in s):
        raise InvalidCheat(f"{field} is not valid hex: {value!r}")
    data = bytes.fromhex(s)
    if not data or len(data) > WRITE_MAX:
        raise InvalidCheat(
            f"{field} length {len(data)} is not 1..{WRITE_MAX}"
        )
    return data


def _parse_offset(value: object) -> str:
    if not isinstance(value, str) or not value.strip():
        raise InvalidCheat("offset must be a non-empty hex string")
    s = value.strip()
    try:
        int(s, 16)
    except ValueError as exc:
        raise InvalidCheat(f"offset is not valid hex: {value!r}") from exc
    return s


def _parse_patch(raw: object) -> CheatPatch:
    if not isinstance(raw, dict):
        raise InvalidCheat("memory patch must be an object")
    return CheatPatch(
        offset=_parse_offset(raw.get("offset")),
        on=_parse_hex_bytes(raw.get("on"), field="on"),
        off=_parse_hex_bytes(raw.get("off"), field="off"),
    )


def _parse_mod(raw: object) -> CheatMod:
    if not isinstance(raw, dict):
        raise InvalidCheat("mod must be an object")
    name = _as_str(raw.get("name", ""), field="mods.name")
    if not name:
        raise InvalidCheat("mod name is required")
    memory_raw = raw.get("memory")
    if not isinstance(memory_raw, list):
        raise InvalidCheat(f"mod {name!r} memory must be a list")
    description = raw.get("description", "")
    if not isinstance(description, str):
        raise InvalidCheat(f"mod {name!r} description must be a string")
    type_name = raw.get("type", "checkbox")
    if not isinstance(type_name, str):
        raise InvalidCheat(f"mod {name!r} type must be a string")
    return CheatMod(
        name=name,
        description=description,
        type=type_name,
        memory=[_parse_patch(p) for p in memory_raw],
    )


def parse_cheat_dict(raw: object) -> CheatFile:
    if not isinstance(raw, dict):
        raise InvalidCheat("cheat JSON must be an object")
    missing = [k for k in _REQUIRED_ROOT if k not in raw]
    if missing:
        raise InvalidCheat(f"missing {missing[0]}")
    mods_raw = raw["mods"]
    if not isinstance(mods_raw, list):
        raise InvalidCheat("mods must be a list")
    credits_raw = raw.get("credits", [])
    if not isinstance(credits_raw, list) or any(
        not isinstance(c, str) for c in credits_raw
    ):
        raise InvalidCheat("credits must be a list of strings")
    process = _as_str(raw["process"], field="process").strip() or EBOOT_NAME
    return CheatFile(
        name=_as_str(raw["name"], field="name"),
        id=_as_str(raw["id"], field="id"),
        version=_as_str(raw["version"], field="version"),
        process=process,
        mods=[_parse_mod(m) for m in mods_raw],
        credits=list(credits_raw),
    )


def cheat_to_dict(cheat: CheatFile) -> dict:
    """Stable GoldHEN key order. No enabled flags (those stay in-memory)."""
    return {
        "name": cheat.name,
        "id": cheat.id,
        "version": cheat.version,
        "process": cheat.process,
        "mods": [
            {
                "name": mod.name,
                "description": mod.description,
                "type": mod.type,
                "memory": [
                    {
                        "offset": patch.offset,
                        "on": patch.on.hex(),
                        "off": patch.off.hex(),
                    }
                    for patch in mod.memory
                ],
            }
            for mod in cheat.mods
        ],
        "credits": list(cheat.credits),
    }


def load_cheat_file(path: str | Path) -> CheatFile:
    try:
        text = Path(path).read_text(encoding="utf-8")
    except OSError as exc:
        raise InvalidCheat(f"cannot read cheat file: {exc}") from exc
    try:
        raw = json.loads(text)
    except json.JSONDecodeError as exc:
        raise InvalidCheat(f"invalid JSON: {exc}") from exc
    return parse_cheat_dict(raw)


def save_cheat_file(path: str | Path, cheat: CheatFile) -> None:
    dest = Path(path)
    dest.write_text(
        json.dumps(cheat_to_dict(cheat), indent=2) + "\n",
        encoding="utf-8",
    )
