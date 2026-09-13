# Validate the map 902 prototype

This is a test plan for the experimental single-tile Retail probe, not evidence of a successful deployment.
Prepare a client and server with matching generated data before running these steps.
Keep installation paths, deployment inventories and actual test results in private deployment records.

## Preparation

- Confirm that the client patch and server Map.dbc both define `902` / `AzerothRetailProbe`.
- Confirm the donor build and tile match the manifest: Retail `12.1.0.69587`, tile `32_48`.
- Audit asset references and generated collision files; confirm navigation data covers the same tile.
- Check that the patch does not replace unrelated client resources or stock collision models.
- Close WoW before changing client files and stop the affected worldserver before replacing its map data.
  Follow the environment's data-preservation and recovery policy.

No game data, deployment package or extracted DBC is included in this repository.
The server installation helper is pinned to the example's original Map.dbc hash; a different baseline
must be investigated before deployment, not accepted by bypassing that check.

## Gameplay checks

With a GM character on the prepared server:

```text
.gm fly on
.go xyz -8900 -150 320 902
```

The probe contains one tile, approximately 533 by 533 yards, rather than a complete continent.
Stay within X = -9066.67 to -8533.33 and Y = -533.33 to 0. The arrival point is above the terrain;
descend while flying. Adjacent tiles are not included.

Check client loading, terrain and textures, the abbey, trees, mines, water, height queries and collision.
Verify server movement and pathfinding agree with what the client renders. NPCs and quests are not imported;
the world map, minimap and normal mount-flight rules are outside this prototype.

Return to the original Stormwind map:

```text
.go xyz -8913 554 94 0
.gm fly off
```

Record failures as well as successful checks. A ready worldserver, successful extractor exit or matching
file hash alone does not prove that the client can enter the map or that gameplay works correctly.

## Removing a test deployment

Move characters off map 902 before removing its data. With the affected server stopped and client closed,
restore the correct Map.dbc baseline and remove only the files listed in that deployment's inventory.
Preserve unrelated maps, patches, account settings and character data. Check the original maps after recovery.
