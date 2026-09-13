"""Package the single-tile map 902 experiment without overriding stock assets."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct

from audit_probe import audit, chunks
from map_import import DbcFile, load_manifest


def pack(tag, payload):
    return tag[::-1] + struct.pack('<I', len(payload)) + payload


def package(source, extraction, dbc_source, output):
    if output.exists():
        raise ValueError('Output must not exist')
    mapping = {}
    for path in source.rglob('*'):
        if not path.is_file():
            continue
        name = path.relative_to(source).as_posix().lower()
        if name.startswith('world/maps/'):
            mapping[name] = name
        else:
            head, tail = name.split('/', 1)
            prefix = ('z902' + head)[:len(head)]
            if len(head) < 4:
                raise ValueError('Cannot isolate short root')
            mapping[name] = prefix + '/' + tail
    if len(set(mapping.values())) != len(mapping):
        raise ValueError('Isolated path collision')
    aliases = dict(mapping)
    for name, dest in mapping.items():
        if name.endswith('.m2'):
            aliases[name[:-3] + '.mdx'] = dest[:-3] + '.mdx'
            aliases[name[:-3] + '.mdl'] = dest[:-3] + '.mdl'
    pattern = re.compile(b'|'.join(re.escape(s.encode()).replace(b'/', b'[/\\\\]')
                                  for s in sorted(aliases, key=len, reverse=True)), re.I)
    patch = output / 'client' / 'patch-5.MPQ'
    for name, dest in mapping.items():
        data = (source / name).read_bytes()
        if name.endswith(('.adt', '.wdt', '.wdl', '.wmo', '.m2')):
            # Equal-width replacements preserve every binary offset and chunk size.
            data = pattern.sub(lambda m: aliases[m[0].decode().replace('\\', '/').lower()].replace('/', '\\').encode(), data)
        if name.endswith('/nsabbey.wmo'):
            parts = list(chunks(data))
            table = dict(parts)
            bell = b'world/azeroth/elwynn/activedoodads/abbeybell/nsabbeybell.m2'
            start = table[b'MODN'].find(bell)
            if start >= 0:
                if any((struct.unpack_from('<I', table[b'MODD'], i)[0] & 0xffffff) == start
                       for i in range(0, len(table[b'MODD']), 40)):
                    raise ValueError('Missing bell is used by a placement')
                data = b''.join(pack(tag, payload.replace(bell, bytes(len(bell))))
                                if tag == b'MODN' else pack(tag, payload) for tag, payload in parts)
        if name.endswith('.wdt'):
            parts = []
            for tag, payload in chunks(data):
                if tag in (b'MAID', b'MAI2'):
                    continue  # The converted ADT is loaded by its new path, not donor FDID.
                if tag == b'MPHD':
                    payload = payload[:4] + bytes(28)
                if tag == b'MAIN':
                    payload = bytearray(32768)
                    struct.pack_into('<I', payload, (48 * 64 + 32) * 8, 1)
                parts.append(pack(tag, payload))
            data = b''.join(parts)
        if name.endswith('.wdl'):
            parts = []
            for tag, payload in chunks(data):
                if tag == b'MAOF':
                    offsets = bytearray(16384)
                    index = (48 * 64 + 32) * 4
                    offsets[index:index + 4] = payload[index:index + 4]
                    payload = offsets
                parts.append(pack(tag, payload))
            data = b''.join(parts)
        target = patch / dest
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    report = audit(patch)
    if report['missingPaths'] or any(t['missingTiles'] for t in report['terrain'].values()):
        raise ValueError(f'Isolated dependency audit failed: {report["missingPaths"]}')
    dbc = DbcFile(dbc_source)
    dbc.append_map(load_manifest(Path(__file__).parent / 'examples/retail-elwynn-probe.json')[0])
    dbc.write(patch / 'DBFilesClient/Map.dbc')
    server = output / 'server'
    dbc.write(server / 'dbc/Map.dbc')
    (server / 'maps').mkdir()
    shutil.copy2(extraction / 'output/maps/9024832.map', server / 'maps/9024832.map')
    vmaps = extraction / 'output-retry/vmaps'
    out_vmaps = server / 'vmaps'
    out_vmaps.mkdir()
    models = {}
    for path in vmaps.glob('*.vmo'):
        old = path.name[:-4]
        stem, ext = old.rsplit('.', 1)
        digest = hashlib.sha256(('probe902/' + old).encode()).hexdigest()
        models[old] = ('z' + digest * 4)[:len(stem)] + '.' + ext
    if len(set(models.values())) != len(models):
        raise ValueError('Collision model namespace conflict')
    for old, new in models.items():
        shutil.copy2(vmaps / (old + '.vmo'), out_vmaps / (new + '.vmo'))
    # ModelSpawn names have explicit lengths. Equal-length remapping leaves their
    # serialized structure intact and keeps stock collision models untouched.
    regex = re.compile(b'|'.join(re.escape(s.encode()) for s in sorted(models, key=len, reverse=True)))
    counts = {}
    for name in ('902.vmtree', '902_32_48.vmtile'):
        data, count = regex.subn(lambda m: models[m[0].decode()].encode(), (vmaps / name).read_bytes())
        (out_vmaps / name).write_bytes(data)
        counts[name] = count
    report['collisionNames'] = models
    report['collisionReferenceReplacements'] = counts
    report['clientPaths'] = mapping
    (output / 'audit.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for field in ('source', 'extraction', 'dbc-source', 'output'):
        parser.add_argument('--' + field, required=True, type=Path)
    args = parser.parse_args()
    result = package(args.source, args.extraction, args.dbc_source, args.output)
    print(json.dumps({key: result[key] for key in ('fileCount', 'missingPaths', 'collisionReferenceReplacements')}))
