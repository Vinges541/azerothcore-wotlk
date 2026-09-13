"""Read-only dependency/terrain audit of a cold-converted map export.

This checks explicit legacy path tables, not every modern FileDataID extension,
animation dependency, or runtime rendering rule. Missing paths may exist in stock
archives; they are reported rather than silently treated as present.
"""

import argparse
import collections
import json
from pathlib import Path
import struct


def chunks(data):
    offset = 0
    while offset < len(data):
        if offset + 8 > len(data):
            raise ValueError(f"Truncated chunk header at {offset}")
        size = struct.unpack_from("<I", data, offset + 4)[0]
        end = offset + 8 + size
        if end > len(data):
            raise ValueError(f"Truncated chunk payload at {offset}")
        yield data[offset:offset + 4][::-1], data[offset + 8:end]
        offset = end


def audit(root):
    files = {}
    for path in root.rglob("*"):
        if path.is_file():
            name = path.relative_to(root).as_posix().lower()
            if name in files:
                raise ValueError(f"Case-insensitive collision: {name}")
            files[name] = path
    missing = collections.defaultdict(set)
    areas, versions, terrain = set(), collections.Counter(), {}
    references = 0

    def check(raw, source):
        nonlocal references
        name = raw.decode("utf-8").replace("\\", "/").lower().strip("\0")
        if not name:
            return
        if name.endswith((".mdx", ".mdl")):
            name = name[:-4] + ".m2"
        references += 1
        if name not in files:
            missing[name].add(source)

    for name, path in files.items():
        data = path.read_bytes()
        if path.suffix.lower() in (".adt", ".wdt", ".wmo"):
            for tag, payload in chunks(data):
                if tag in (b"MTEX", b"MMDX", b"MWMO", b"MOTX", b"MODN"):
                    for raw in payload.split(b"\0"):
                        check(raw, name)
                if tag == b"MCNK":
                    areas.add(struct.unpack_from("<I", payload, 52)[0])
                if tag == b"MAIN" and path.suffix.lower() == ".wdt":
                    if len(payload) != 64 * 64 * 8:
                        raise ValueError("Unexpected WDT MAIN length")
                    active = [i for i in range(4096)
                              if struct.unpack_from("<I", payload, i * 8)[0] & 1]
                    absent = []
                    for i in active:
                        tile = f"{name[:-4]}_{i % 64}_{i // 64}.adt"
                        if tile not in files:
                            absent.append([i % 64, i // 64])
                    terrain[name] = {"activeTiles": len(active), "missingTiles": absent}
                if tag == b"MOHD":
                    for index in range(struct.unpack_from("<I", payload, 4)[0]):
                        check(f"{name[:-4]}_{index:03}.wmo".encode(), name)
        if path.suffix.lower() == ".m2":
            if data[:4] != b"MD20":
                raise ValueError(f"Not a legacy-layout M2: {name}")
            versions[hex(struct.unpack_from("<I", data, 4)[0])] += 1
            count, offset = struct.unpack_from("<II", data, 80)
            if offset + count * 16 > len(data):
                raise ValueError(f"M2 texture table out of bounds: {name}")
            for index in range(count):
                kind, _, length, start = struct.unpack_from("<4I", data, offset + index * 16)
                if kind == 0 and length:
                    if start + length > len(data):
                        raise ValueError(f"M2 texture path out of bounds: {name}")
                    check(data[start:start + length], name)
            for index in range(struct.unpack_from("<I", data, 68)[0]):
                check(f"{name[:-3]}{index:02}.skin".encode(), name)
    return {
        "fileCount": len(files), "explicitReferencesChecked": references,
        "missingPaths": {key: sorted(value) for key, value in sorted(missing.items())},
        "areaIds": sorted(areas), "m2Versions": dict(versions), "terrain": terrain,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    args = parser.parse_args()
    if not args.root.is_dir():
        parser.error("root must be an existing directory")
    report = audit(args.root)
    print(json.dumps(report, indent=2))
    raise SystemExit(1 if report["missingPaths"] or any(
        item["missingTiles"] for item in report["terrain"].values()) else 0)
