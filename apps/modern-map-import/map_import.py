#!/usr/bin/env python3

"""Prepare modern WoW map exports for a side-by-side WotLK client patch."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


DBC_HEADER = struct.Struct("<4s4I")
UINT32 = struct.Struct("<I")
MAP_ID_FIELD = 0
MAP_INTERNAL_NAME_FIELD = 1
MAP_LOCALIZED_NAME_FIELDS = range(5, 21)
UINT32_MAX = (1 << 32) - 1


class ImportErrorWithContext(RuntimeError):
    pass


@dataclass(frozen=True)
class MapSpec:
    source_map_id: int
    source_internal_name: str
    target_map_id: int
    target_internal_name: str
    display_name: str

    @classmethod
    def from_json(cls, value: dict[str, Any]) -> "MapSpec":
        return cls(
            source_map_id=int(value["sourceMapId"]),
            source_internal_name=str(value["sourceInternalName"]),
            target_map_id=int(value["targetMapId"]),
            target_internal_name=str(value["targetInternalName"]),
            display_name=str(value["displayName"]),
        )


class DbcFile:
    def __init__(self, path: Path) -> None:
        payload = path.read_bytes()
        if len(payload) < DBC_HEADER.size:
            raise ImportErrorWithContext(f"{path}: file is smaller than a DBC header")

        magic, record_count, field_count, record_size, string_size = DBC_HEADER.unpack_from(payload)
        if magic != b"WDBC":
            raise ImportErrorWithContext(f"{path}: expected WDBC, found {magic!r}")
        if record_size < field_count * UINT32.size:
            raise ImportErrorWithContext(f"{path}: record size is smaller than its fields")

        records_end = DBC_HEADER.size + record_count * record_size
        expected_size = records_end + string_size
        if len(payload) != expected_size:
            raise ImportErrorWithContext(
                f"{path}: header describes {expected_size} bytes, file contains {len(payload)}"
            )

        self.field_count = field_count
        self.record_size = record_size
        self.records = [
            bytearray(payload[offset : offset + record_size])
            for offset in range(DBC_HEADER.size, records_end, record_size)
        ]
        self.strings = bytearray(payload[records_end:])

    def get_uint32(self, record: bytearray, field: int) -> int:
        self._check_field(field)
        return UINT32.unpack_from(record, field * UINT32.size)[0]

    def set_uint32(self, record: bytearray, field: int, value: int) -> None:
        self._check_field(field)
        UINT32.pack_into(record, field * UINT32.size, value)

    def get_string(self, offset: int) -> str:
        if offset >= len(self.strings):
            raise ImportErrorWithContext(f"DBC string offset {offset} is out of bounds")
        end = self.strings.find(b"\0", offset)
        if end < 0:
            raise ImportErrorWithContext(f"DBC string at offset {offset} is not terminated")
        return self.strings[offset:end].decode("utf-8", errors="replace")

    def add_string(self, value: str) -> int:
        encoded = value.encode("utf-8") + b"\0"
        existing = self.strings.find(encoded)
        if existing >= 0:
            return existing
        offset = len(self.strings)
        self.strings.extend(encoded)
        return offset

    def find_record(self, record_id: int) -> bytearray | None:
        return next(
            (record for record in self.records if self.get_uint32(record, MAP_ID_FIELD) == record_id),
            None,
        )

    def append_map(self, spec: MapSpec) -> None:
        if self.field_count < 66:
            raise ImportErrorWithContext(
                f"Map.dbc must have at least 66 fields, found {self.field_count}"
            )
        if self.find_record(spec.target_map_id) is not None:
            raise ImportErrorWithContext(f"target map ID {spec.target_map_id} already exists")

        source = self.find_record(spec.source_map_id)
        if source is None:
            raise ImportErrorWithContext(f"source map ID {spec.source_map_id} does not exist")
        source_name_offset = self.get_uint32(source, MAP_INTERNAL_NAME_FIELD)
        source_name = self.get_string(source_name_offset)
        if source_name.casefold() != spec.source_internal_name.casefold():
            raise ImportErrorWithContext(
                f"source map ID {spec.source_map_id} is {source_name!r}, not "
                f"{spec.source_internal_name!r}"
            )

        record = bytearray(source)
        self.set_uint32(record, MAP_ID_FIELD, spec.target_map_id)
        self.set_uint32(record, MAP_INTERNAL_NAME_FIELD, self.add_string(spec.target_internal_name))
        display_name_offset = self.add_string(spec.display_name)
        for field in MAP_LOCALIZED_NAME_FIELDS:
            self.set_uint32(record, field, display_name_offset)
        self.records.append(record)

    def write(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        header = DBC_HEADER.pack(
            b"WDBC",
            len(self.records),
            self.field_count,
            self.record_size,
            len(self.strings),
        )
        path.write_bytes(header + b"".join(self.records) + self.strings)

    def map_rows(self) -> list[tuple[int, str]]:
        rows = []
        for record in self.records:
            map_id = self.get_uint32(record, MAP_ID_FIELD)
            name_offset = self.get_uint32(record, MAP_INTERNAL_NAME_FIELD)
            rows.append((map_id, self.get_string(name_offset)))
        return rows

    def _check_field(self, field: int) -> None:
        if field < 0 or field >= self.field_count:
            raise ImportErrorWithContext(f"DBC field {field} is out of bounds")


def load_manifest(path: Path) -> list[MapSpec]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if value.get("schemaVersion") != 1:
        raise ImportErrorWithContext(f"{path}: unsupported schemaVersion")
    specs = [MapSpec.from_json(item) for item in value.get("maps", [])]
    if not specs:
        raise ImportErrorWithContext(f"{path}: manifest has no maps")

    for spec in specs:
        if not 0 <= spec.source_map_id <= UINT32_MAX:
            raise ImportErrorWithContext(f"{path}: source map ID is outside uint32")
        if not 0 <= spec.target_map_id <= UINT32_MAX:
            raise ImportErrorWithContext(f"{path}: target map ID is outside uint32")
        for label, name in (
            ("sourceInternalName", spec.source_internal_name),
            ("targetInternalName", spec.target_internal_name),
        ):
            if not name or name in (".", "..") or "/" in name or "\\" in name or "\0" in name:
                raise ImportErrorWithContext(f"{path}: invalid {label} {name!r}")

    target_ids = [spec.target_map_id for spec in specs]
    target_names = [spec.target_internal_name.casefold() for spec in specs]
    if len(target_ids) != len(set(target_ids)):
        raise ImportErrorWithContext(f"{path}: duplicate target map IDs")
    if len(target_names) != len(set(target_names)):
        raise ImportErrorWithContext(f"{path}: duplicate target internal names")
    return specs


def copy_or_link(source: Path, target: Path, mode: str) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    if mode == "hardlink":
        try:
            os.link(source, target)
            return
        except OSError:
            pass
    shutil.copy2(source, target)


def copy_export_tree(source: Path, target: Path, mode: str, merge: bool = False) -> None:
    items = list(source.rglob("*"))
    if target.exists() and not merge:
        raise ImportErrorWithContext(f"{target}: output already exists")
    if target.exists() and not target.is_dir():
        raise ImportErrorWithContext(f"{target}: output is not a directory")
    if merge:
        collisions = [
            item.relative_to(source)
            for item in items
            if item.is_file() and (target / item.relative_to(source)).exists()
        ]
        if collisions:
            raise ImportErrorWithContext(f"{target}: refusing to overwrite {collisions[0]}")

    target.mkdir(parents=True, exist_ok=merge)
    for item in items:
        relative = item.relative_to(source)
        destination = target / relative
        if item.is_dir():
            destination.mkdir(exist_ok=True)
        elif item.is_file():
            copy_or_link(item, destination, mode)


def find_child_case_insensitive(parent: Path, name: str) -> Path:
    wanted = name.casefold()
    matches = [child for child in parent.iterdir() if child.name.casefold() == wanted]
    if len(matches) != 1:
        raise ImportErrorWithContext(f"{parent}: expected exactly one child named {name!r}")
    return matches[0]


def find_optional_child_case_insensitive(parent: Path, name: str) -> Path | None:
    if not parent.is_dir():
        return None
    wanted = name.casefold()
    matches = [child for child in parent.iterdir() if child.name.casefold() == wanted]
    if len(matches) > 1:
        raise ImportErrorWithContext(f"{parent}: found multiple children named {name!r}")
    return matches[0] if matches else None


def find_map_directory(export_root: Path, internal_name: str) -> Path:
    candidates: list[Path] = []
    top_level_maps = find_optional_child_case_insensitive(export_root, "maps")
    if top_level_maps:
        map_directory = find_optional_child_case_insensitive(top_level_maps, internal_name)
        if map_directory:
            candidates.append(map_directory)

    world = find_optional_child_case_insensitive(export_root, "world")
    world_maps = find_optional_child_case_insensitive(world, "maps") if world else None
    if world_maps:
        map_directory = find_optional_child_case_insensitive(world_maps, internal_name)
        if map_directory:
            candidates.append(map_directory)

    if len(candidates) != 1:
        raise ImportErrorWithContext(
            f"{export_root}: expected exactly one maps/{internal_name} or "
            f"world/maps/{internal_name} directory"
        )
    return candidates[0]


def canonical_maps_directory(export_root: Path) -> Path:
    world = find_optional_child_case_insensitive(export_root, "world")
    if world is None:
        world = export_root / "world"
    maps = find_optional_child_case_insensitive(world, "maps")
    return maps if maps else world / "maps"


def plan_map_renames(source_dir: Path, spec: MapSpec) -> list[tuple[Path, Path]]:
    source_prefix = spec.source_internal_name.casefold()
    renames: list[tuple[Path, Path]] = []
    destinations: set[str] = set()
    for path in source_dir.rglob("*"):
        if not path.is_file() or not path.name.casefold().startswith(source_prefix):
            continue
        suffix = path.name[len(spec.source_internal_name) :]
        destination = path.with_name(spec.target_internal_name + suffix)
        destination_key = destination.relative_to(source_dir).as_posix().casefold()
        if destination_key in destinations or (destination.exists() and destination != path):
            raise ImportErrorWithContext(f"{source_dir}: rename would overwrite {destination.name}")
        destinations.add(destination_key)
        renames.append((path.relative_to(source_dir), destination.relative_to(source_dir)))
    return renames


def rename_map_tree(export_root: Path, spec: MapSpec) -> None:
    source_dir = find_map_directory(export_root, spec.source_internal_name)
    maps = canonical_maps_directory(export_root)
    target_dir = maps / spec.target_internal_name
    if target_dir.exists():
        raise ImportErrorWithContext(f"{target_dir}: target map directory already exists")

    renames = plan_map_renames(source_dir, spec)
    maps.mkdir(parents=True, exist_ok=True)
    source_dir.rename(target_dir)
    for source, destination in renames:
        (target_dir / source).rename(target_dir / destination)


def command_inspect_dbc(args: argparse.Namespace) -> None:
    dbc = DbcFile(args.input)
    print(f"records={len(dbc.records)} fields={dbc.field_count} record_size={dbc.record_size}")
    for map_id, internal_name in dbc.map_rows():
        print(f"{map_id:4d} {internal_name}")


def command_clone_dbc(args: argparse.Namespace) -> None:
    dbc = DbcFile(args.input)
    for spec in load_manifest(args.manifest):
        dbc.append_map(spec)
    dbc.write(args.output)
    print(f"wrote {args.output} with {len(dbc.records)} records")


def command_stage_export(args: argparse.Namespace) -> None:
    specs = load_manifest(args.manifest)
    for spec in specs:
        source_dir = find_map_directory(args.input, spec.source_internal_name)
        plan_map_renames(source_dir, spec)
    copy_export_tree(args.input, args.output, args.mode, args.merge)
    for spec in specs:
        rename_map_tree(args.output, spec)
    print(f"staged {args.output}")


def path_argument(value: str) -> Path:
    return Path(value).expanduser().resolve()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    inspect_dbc = commands.add_parser("inspect-dbc", help="list map IDs and internal names")
    inspect_dbc.add_argument("--input", type=path_argument, required=True)
    inspect_dbc.set_defaults(handler=command_inspect_dbc)

    clone_dbc = commands.add_parser("clone-dbc", help="append side-by-side maps to Map.dbc")
    clone_dbc.add_argument("--input", type=path_argument, required=True)
    clone_dbc.add_argument("--manifest", type=path_argument, required=True)
    clone_dbc.add_argument("--output", type=path_argument, required=True)
    clone_dbc.set_defaults(handler=command_clone_dbc)

    stage_export = commands.add_parser("stage-export", help="stage and rename a raw wow.export tree")
    stage_export.add_argument("--input", type=path_argument, required=True)
    stage_export.add_argument("--manifest", type=path_argument, required=True)
    stage_export.add_argument("--output", type=path_argument, required=True)
    stage_export.add_argument("--mode", choices=("hardlink", "copy"), default="hardlink")
    stage_export.add_argument("--merge", action="store_true", help="merge into a directory without overwriting files")
    stage_export.set_defaults(handler=command_stage_export)
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        args.handler(args)
    except (ImportErrorWithContext, KeyError, OSError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
