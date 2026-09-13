# Modern map import lab

This directory contains the reproducible part of importing post-WotLK maps as new maps instead of replacing
the stock WotLK continents.

The first reserved IDs are:

- `900` / `AzerothCata` — Cataclysm Eastern Kingdoms, cloned from map `0` / `Azeroth`.
- `901` / `KalimdorCata` — Cataclysm Kalimdor, cloned from map `1` / `Kalimdor`.

The runtime client remains WotLK 3.3.5a build 12340 with WarcraftXL. The donor build only supplies terrain and
assets. A current Retail CDN source is useful for current revisions; use build 15595 when the exact Cataclysm
4.3.4 world is required.

## Runtime prerequisites

Supply your own compatible client, donor export, converter bundle and extractor binaries.
These tools do not assume a client installation or generated data beside the source checkout.
Keep exports, converted assets, deployment inventories and test reports outside public source commits.

The PowerShell utilities are Windows-side helpers and require explicit paths;
they do not configure Wine. `setup-converter.ps1` requires `-Archive` and `-Directory`.
`install-client-probe.ps1` requires `-Source` and `-Client` and refuses to overwrite an existing patch.

`patch-client.ps1` accepts explicit `-ClientDirectory` and `-PatcherPath`, verifies
pinned hashes, and refuses an already modified executable. It is not a preparation step
for a client that already has other executable modifications.

The FileDataID resolver also requires `TextureFilePath.db2` and `ModelFilePath.db2`; material-resource lookups
use `TextureFileData.db2`. Raw terrain and dependency files alone do not supply this mapping. Match the mappings
to the exported donor files before testing tiles that reference assets by ID.

## 1. Export raw files

Use wow.export's map view and select a donor source. In the CASC map viewer, select all required tiles, enable
`Export WMO (Raw)` and `Export M2 (Raw)`, then choose `Export Raw` from the export button. Keep shared children and
shared textures enabled so dependencies retain their client paths. Do not export OBJ or a baked height map: the
pipeline needs ADT/WDT/WDL files and their referenced M2, WMO and BLP assets.

wow.export 0.2.19 writes the terrain itself under `maps/Azeroth` and `maps/Kalimdor`; older or hand-made exports may
already use `world/maps/...`. The staging command accepts either layout and always emits the client layout under
`world/maps/...`.

Keep one manifest per donor build. Never combine files from different builds without recording that explicitly.

## 2. Stage maps under new internal names

```sh
python3 apps/modern-map-import/map_import.py stage-export \
  --input /path/to/raw-export \
  --manifest apps/modern-map-import/examples/cataclysm-4.3.4.json \
  --output /private/work/map-import/patch-4.MPQ \
  --merge
```

The default mode hard-links unchanged assets and copies only when hard links are unavailable. `--merge` permits the
existing DBC directory in the staging patch but still refuses to overwrite any file.

Hard-linked files share their contents with the export source. Use `--mode copy` for a staging tree that will be
modified in place by a converter. Renaming map directories does not isolate shared model or texture paths:
assets in the patch can override stock assets of the same name. Close WoW before replacing active files
and follow the deployment environment's data-preservation policy.

## 3. Add custom Map.dbc rows

Create a Map.dbc containing the stock records plus the two custom records.
Supply an explicit original DBC input from your own client data. The example paths below
must be replaced with your chosen private staging paths:

```sh
python3 apps/modern-map-import/map_import.py clone-dbc \
  --input /private/work/original-data/dbc/Map.dbc \
  --manifest apps/modern-map-import/examples/cataclysm-4.3.4.json \
  --output /private/work/map-import/patch-4.MPQ/DBFilesClient/Map.dbc
```

The development patch is a loose directory named `patch-4.MPQ`. WarcraftXL and the modified AzerothCore extractors
mount this directory without an MPQ packing step. The resulting DBC must also replace the server runtime's `Map.dbc`
when the custom map data is deployed. Do not replace the source checkout's DBC.

## Experimental single-tile Retail probe

`examples/retail-elwynn-probe.json` reserves map `902` / `AzerothRetailProbe`, separate from both stock continents
and the Cataclysm IDs. The example targets donor tile `32_48` from Retail `12.1.0.69587`.
Use a donor-specific export profile and stage with `--mode copy`.
`export-retail-probe.cjs` records donor build keys and fetch failures in the chosen output directory.

`convert-probe.ps1` calls the official cold converter's native ABI from x86 Windows PowerShell. It requires the
x86 Visual C++ runtime and an extracted converter bundle.

Run the read-only audit with `python3 apps/modern-map-import/audit_probe.py /path/to/converted`.
Validate referenced assets and restrict WDT tile flags to terrain that is actually present.
`package_probe.py` namespaces client resources and collision models and retains only the selected tile.
It rejects missing dependencies and removes the known unused abbey-bell reference only after checking
that no placement uses it. This packaging logic is specific to the example probe.

See [READY-TO-TEST.md](READY-TO-TEST.md) for a gameplay validation plan. The prototype is experimental;
successful export, conversion or extraction does not establish client loading, rendering or physics correctness.
The modified extractors accept both slash separators and normalize archive lookups to backslashes.

`prepare_extractor_probe.py` creates a disposable fixture using read-only archive symlinks, a copied terrain tree,
one Map.dbc record, an empty GameObjectDisplayInfo.dbc, and MAIN flags restricted to existing ADTs. These reduced
DBCs and the extractor-only WDT are **not deployment files**.

Prepare the map, vmap and mmap outputs for the same donor tile and keep their provenance with the package.
The expected terrain outputs include `server/maps/9024832.map`, `server/vmaps/902.vmtree` and
`server/vmaps/902_32_48.vmtile`. `package_probe.py` writes an `audit.json` inventory alongside its output.
The server installation helper is pinned to the original Map.dbc hash of this example; it intentionally
refuses a different baseline. Review the helper and the target data layout before using it for a deployment.

## Remaining conversion boundary

WarcraftXL can hot-convert modern assets for the WotLK client. AzerothCore's standalone map and vmap extractors do
not run inside WarcraftXL, so the same export must also be cold-converted to the WotLK byte layout before producing
server `maps`, `vmaps` and `mmaps`. The extractors in this checkout can read the loose `patch-4.MPQ` directory, but
they still require cold-converted bytes. Client rendering alone is not sufficient for collision, height or
pathfinding.

Cold Converter's documentation still requires WarcraftXL on the client. Its output must be tested against the
standalone extractors; downloading the converter does not establish end-to-end format compatibility.

Cataclysm and later ADTs also carry area IDs absent from the WotLK AreaTable.dbc. Until those rows are imported or
remapped, a prototype can permit flight by custom map ID, but area names, zone rules and exploration will be
incomplete.
