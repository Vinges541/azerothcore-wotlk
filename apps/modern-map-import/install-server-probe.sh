#!/bin/sh
# Run in an ephemeral container with /package read-only and the data volume at /data.
set -eu
expected=261bd7c2c9dbdba1f2966ca21106673ed722c2e3681d227f1eed261b6ce870d5
actual=$(sha256sum /data/dbc/Map.dbc | cut -d ' ' -f 1)
[ "$actual" = "$expected" ] || { echo 'Live Map.dbc changed; refusing deployment'; exit 1; }
[ ! -e /data/wxl-backups/probe902-20260906 ] || { echo 'Backup already exists'; exit 1; }
for directory in maps vmaps mmaps; do
    for source in /package/server/"$directory"/*; do
        name=${source##*/}
        [ ! -e /data/"$directory"/"$name" ] || { echo "Target collision: $directory/$name"; exit 1; }
    done
done
if [ "${1:-}" = '--check' ]; then
    echo 'Deployment preflight passed'
    exit 0
fi
mkdir -p /data/wxl-backups/probe902-20260906
cp -p /data/dbc/Map.dbc /data/wxl-backups/probe902-20260906/Map.dbc
cp /package/audit.json /data/wxl-backups/probe902-20260906/audit.json
for directory in maps vmaps mmaps; do
    mkdir -p /data/"$directory"
    for source in /package/server/"$directory"/*; do
        name=${source##*/}
        cp "$source" /data/"$directory"/"$name"
        cmp "$source" /data/"$directory"/"$name"
    done
done
cp /package/server/dbc/Map.dbc /data/dbc/Map.dbc.probe902-new
mv /data/dbc/Map.dbc.probe902-new /data/dbc/Map.dbc
cmp /package/server/dbc/Map.dbc /data/dbc/Map.dbc
echo 'Map 902 installed; original Map.dbc backed up'
