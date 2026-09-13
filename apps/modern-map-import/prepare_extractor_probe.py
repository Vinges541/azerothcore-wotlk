"""Create a disposable extractor-only fixture; never deploy its reduced DBCs."""

import argparse
from pathlib import Path
import shutil
import struct
import tempfile

from audit_probe import chunks
from map_import import DbcFile, load_manifest


def prepare(client, converted, dbc_root, manifest):
    specs = load_manifest(manifest)
    if len(specs) != 1:
        raise ValueError("Extractor probe requires exactly one map")
    spec = specs[0]
    for directory in (client / "Data" / "ruRU", converted, dbc_root):
        if not directory.is_dir():
            raise ValueError(f"Missing directory: {directory}")
    root = Path(tempfile.mkdtemp(prefix="wxl-extractor-probe-"))
    data = root / "input" / "Data"
    locale = data / "ruRU"
    locale.mkdir(parents=True)
    for source, target in ((client / "Data", data), (client / "Data" / "ruRU", locale)):
        for entry in source.glob("*.MPQ"):
            if entry.is_file():
                (target / entry.name).symlink_to(entry.resolve())
    patch = data / "patch-4.MPQ"
    shutil.copytree(converted, patch)
    # Locale patches mount after common patches in the vmap extractor.
    (locale / "patch-ruRU-4.MPQ").symlink_to(patch)
    dbc = DbcFile(dbc_root / "Map.dbc")
    dbc.append_map(spec)
    dbc.records = [dbc.find_record(spec.target_map_id)]
    dbc.write(patch / "DBFilesClient" / "Map.dbc")
    objects = DbcFile(dbc_root / "GameObjectDisplayInfo.dbc")
    objects.records = []  # No world-wide gameobject extraction in a one-tile test.
    objects.write(patch / "DBFilesClient" / "GameObjectDisplayInfo.dbc")
    for wdt in patch.rglob("*.wdt"):
        output = bytearray()
        for tag, payload in chunks(wdt.read_bytes()):
            if tag == b"MAIN":
                if len(payload) != 4096 * 8:
                    raise ValueError("Unexpected MAIN size")
                payload = bytearray(payload)
                for index in range(4096):
                    tile = wdt.with_name(f"{wdt.stem}_{index % 64}_{index // 64}.adt")
                    if not tile.is_file():
                        payload[index * 8:index * 8 + 8] = bytes(8)
            # Modern FDID tables are deliberately retained: these standalone
            # extractors only consume MAIN. This is not a client-ready WDT edit.
            output.extend(tag[::-1] + struct.pack("<I", len(payload)) + payload)
        wdt.write_bytes(output)
    (root / "output").mkdir()
    return root


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("client", "converted", "dbc-root", "manifest"):
        parser.add_argument(f"--{name}", required=True, type=Path)
    args = parser.parse_args()
    print(prepare(args.client, args.converted, args.dbc_root, args.manifest))
