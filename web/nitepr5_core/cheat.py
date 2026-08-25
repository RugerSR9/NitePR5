"""GoldHEN JSON cheat load/save. No extra keys; on/off stay hex in the file."""

from __future__ import annotations

import json
from pathlib import Path

from .constants import EBOOT_NAME, EXECUTABLE_MAP_NAME, WRITE_MAX
from .errors import InvalidCheat
from .types import CheatFile, CheatMod, CheatPatch, FreezeEntry, MemoryMap

_HEX_DIGITS = frozenset("0123456789abcdefABCDEF")
_REQUIRED_ROOT = ("name", "id", "version", "process", "mods")


def map_belongs_to_process(map_name: str, process: str = EBOOT_NAME) -> bool:
    """True if a VM map row belongs to GoldHEN ``process`` (usually eboot.bin).

    Live ps5debug-NG names the main ELF ``executable``, not ``eboot.bin``.
    Path-like names (``/app0/eboot.bin``) match on the basename.
    """
    name = (map_name or "").strip()
    proc = (process or EBOOT_NAME).strip() or EBOOT_NAME
    if not name:
        return False
    base = name.replace("\\", "/").rsplit("/", 1)[-1]
    if name == proc or base == proc:
        return True
    if proc.lower() == EBOOT_NAME and base.lower() == EXECUTABLE_MAP_NAME:
        return True
    return False


def module_base_from_maps(
    maps: list[MemoryMap],
    process: str = EBOOT_NAME,
) -> int | None:
    """Lowest ``start`` among maps for ``process``, or None if none match."""
    starts = [m.start for m in maps if map_belongs_to_process(m.name, process)]
    return min(starts) if starts else None


def cheat_file_from_freezes(
    freezes: list[FreezeEntry],
    *,
    base: int,
    name: str,
    title_id: str,
    version: str,
    process: str = EBOOT_NAME,
) -> CheatFile:
    """GoldHEN file whose offsets are ``addr - module_base``.

    Addresses below ``base`` are skipped (not in this module). Heap addresses
    above ``base`` are stored as large offsets so toggle writes the same VA
    this session (ASLR means they may not survive a relaunch).
    """
    if not freezes:
        raise InvalidCheat("no freezes to save")
    mods: list[CheatMod] = []
    for entry in freezes:
        if entry.addr < base:
            continue
        on = bytes(entry.data)
        mods.append(
            CheatMod(
                name=f"Freeze {entry.addr:#x}",
                description="",
                type="checkbox",
                memory=[
                    CheatPatch(
                        offset=format(entry.addr - base, "x"),
                        on=on,
                        off=bytes(len(on)),
                    )
                ],
            )
        )
    if not mods:
        raise InvalidCheat(f"no freezes with offsets inside {process}")
    return CheatFile(
        name=name or "Game",
        id=title_id or "CUSA00000",
        version=version or "00.00",
        process=process or EBOOT_NAME,
        mods=mods,
        credits=["NitePR5"],
    )


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
