import importlib.util
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).parents[1] / "map_import.py"
SPEC = importlib.util.spec_from_file_location("map_import", MODULE_PATH)
assert SPEC and SPEC.loader
map_import = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = map_import
SPEC.loader.exec_module(map_import)


def create_map_dbc(path: Path) -> None:
    fields = [0] * 66
    strings = bytearray(b"\0Azeroth\0Eastern Kingdoms\0")
    fields[0] = 0
    fields[1] = 1
    for index in range(5, 21):
        fields[index] = 9
    record = struct.pack("<66I", *fields)
    path.write_bytes(struct.pack("<4s4I", b"WDBC", 1, 66, len(record), len(strings)) + record + strings)


class MapImportTest(unittest.TestCase):
    def test_clone_map_record(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            input_path = root / "Map.dbc"
            output_path = root / "out" / "Map.dbc"
            create_map_dbc(input_path)

            dbc = map_import.DbcFile(input_path)
            dbc.append_map(map_import.MapSpec(0, "Azeroth", 900, "AzerothCata", "Cata EK"))
            dbc.write(output_path)

            result = map_import.DbcFile(output_path)
            self.assertEqual(result.map_rows(), [(0, "Azeroth"), (900, "AzerothCata")])
            custom = result.find_record(900)
            self.assertIsNotNone(custom)
            assert custom is not None
            self.assertEqual(result.get_string(result.get_uint32(custom, 5)), "Cata EK")

    def test_load_manifest_rejects_duplicate_ids(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / "manifest.json"
            manifest.write_text(
                json.dumps(
                    {
                        "schemaVersion": 1,
                        "maps": [
                            {
                                "sourceMapId": 0,
                                "sourceInternalName": "Azeroth",
                                "targetMapId": 900,
                                "targetInternalName": "AzerothCata",
                                "displayName": "A",
                            },
                            {
                                "sourceMapId": 1,
                                "sourceInternalName": "Kalimdor",
                                "targetMapId": 900,
                                "targetInternalName": "KalimdorCata",
                                "displayName": "B",
                            },
                        ],
                    }
                ),
                encoding="utf-8",
            )
            with self.assertRaises(map_import.ImportErrorWithContext):
                map_import.load_manifest(manifest)

    def test_load_manifest_rejects_path_names(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / "manifest.json"
            manifest.write_text(
                json.dumps(
                    {
                        "schemaVersion": 1,
                        "maps": [
                            {
                                "sourceMapId": 0,
                                "sourceInternalName": "Azeroth",
                                "targetMapId": 900,
                                "targetInternalName": "../escape",
                                "displayName": "A",
                            }
                        ],
                    }
                ),
                encoding="utf-8",
            )
            with self.assertRaises(map_import.ImportErrorWithContext):
                map_import.load_manifest(manifest)

    def test_stage_export_renames_map_directory_and_files(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "export"
            map_dir = source / "world" / "maps" / "azeroth"
            map_dir.mkdir(parents=True)
            (map_dir / "azeroth.wdt").write_bytes(b"wdt")
            (map_dir / "azeroth_32_48.adt").write_bytes(b"adt")
            texture = source / "world" / "textures" / "example.blp"
            texture.parent.mkdir(parents=True)
            texture.write_bytes(b"blp")

            output = root / "patch"
            map_import.copy_export_tree(source, output, "copy")
            map_import.rename_map_tree(
                output,
                map_import.MapSpec(0, "Azeroth", 900, "AzerothCata", "Cata EK"),
            )

            target = output / "world" / "maps" / "AzerothCata"
            self.assertEqual((target / "AzerothCata.wdt").read_bytes(), b"wdt")
            self.assertEqual((target / "AzerothCata_32_48.adt").read_bytes(), b"adt")
            self.assertEqual((output / "world" / "textures" / "example.blp").read_bytes(), b"blp")

    def test_stage_export_moves_wow_export_layout_to_world_maps(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            map_dir = root / "maps" / "Azeroth"
            map_dir.mkdir(parents=True)
            (map_dir / "Azeroth.wdt").write_bytes(b"wdt")

            map_import.rename_map_tree(
                root,
                map_import.MapSpec(0, "Azeroth", 900, "AzerothCata", "Cata EK"),
            )

            target = root / "world" / "maps" / "AzerothCata" / "AzerothCata.wdt"
            self.assertEqual(target.read_bytes(), b"wdt")
            self.assertFalse(map_dir.exists())

    def test_stage_export_refuses_rename_collision(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            map_dir = root / "world" / "maps" / "Azeroth"
            map_dir.mkdir(parents=True)
            (map_dir / "Azeroth.wdt").write_bytes(b"source")
            (map_dir / "AzerothCata.wdt").write_bytes(b"collision")

            with self.assertRaises(map_import.ImportErrorWithContext):
                map_import.rename_map_tree(
                    root,
                    map_import.MapSpec(0, "Azeroth", 900, "AzerothCata", "Cata EK"),
                )
            self.assertTrue(map_dir.exists())

    def test_merge_preserves_existing_files_and_rejects_collisions(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "export"
            source.mkdir()
            (source / "new.blp").write_bytes(b"new")
            output = root / "patch"
            output.mkdir()
            (output / "Map.dbc").write_bytes(b"dbc")

            map_import.copy_export_tree(source, output, "copy", merge=True)
            self.assertEqual((output / "Map.dbc").read_bytes(), b"dbc")
            self.assertEqual((output / "new.blp").read_bytes(), b"new")

            with self.assertRaises(map_import.ImportErrorWithContext):
                map_import.copy_export_tree(source, output, "copy", merge=True)


if __name__ == "__main__":
    unittest.main()
