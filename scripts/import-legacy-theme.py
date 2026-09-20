"""Reproduce the 98/NT5 overlay from user-supplied, extracted Windows files.

Authoring tool only: requires Python and pefile; neither is needed to build/run.
Example: python scripts/import-legacy-theme.py --win98 analysis/win98/WINDOWS
         --nt5 analysis/nt5/WINNT
No executable code, registry hives or VM images are copied to the payload.
"""
import argparse
import hashlib
from pathlib import Path
import re
import struct

import pefile


def resources(path):
    pe = pefile.PE(str(path))
    result = {}
    for kind in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        for name in kind.directory.entries:
            # English donor images; prefer US English when multiple languages exist.
            languages = name.directory.entries
            entry = next((x for x in languages if x.id == 1033), languages[0])
            data = entry.data.struct
            result[(str(kind.name or kind.id), str(name.name or name.id))] = pe.get_data(
                data.OffsetToData, data.Size)
    pe.close()
    return result


def icon(res, name):
    group = res.get(('14', name))
    if group is None:
        return None
    count = struct.unpack_from('<H', group, 4)[0]
    directory, images = bytearray(group[:6]), bytearray()
    for i in range(count):
        entry = group[6 + 14 * i:20 + 14 * i]
        resource_id = struct.unpack_from('<H', entry, 12)[0]
        data = res[('3', str(resource_id))]
        directory.extend(entry[:8] + struct.pack('<II', len(data), 6 + count * 16 + len(images)))
        images.extend(data)
    return bytes(directory + images)


def bitmap(dib):
    header, _, _, _, depth = struct.unpack_from('<IiiHH', dib)
    colors = struct.unpack_from('<I', dib, 32)[0] or ((1 << depth) if depth <= 8 else 0)
    masks = 12 if header == 40 and struct.unpack_from('<I', dib, 16)[0] == 3 else 0
    offset = 14 + header + colors * 4 + masks
    return b'BM' + struct.pack('<IHHI', len(dib) + 14, 0, 0, offset) + dib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--win98', type=Path, required=True, help='Extracted Windows 98 WINDOWS directory')
    parser.add_argument('--nt5', type=Path, required=True, help='Extracted NT 5.0 build 1877 WINNT directory')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    payload = root / 'payload'
    output = payload / 'Themes/windows-98-nt5'
    donors = {}
    for source, directory, system in [('Windows98', args.win98, 'SYSTEM'), ('NT5-1877', args.nt5, 'system32')]:
        for module in ['shell32.dll', 'comctl32.dll', 'explorer.exe', 'msgina.dll', 'sysdm.cpl']:
            path = directory / module if module == 'explorer.exe' else directory / system / module
            if path.exists():
                donors[(source, module)] = (resources(path), hashlib.sha256(path.read_bytes()).hexdigest())
    rows = []

    def save(relative, data, source, module, resource, donor_hash):
        dest = output / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
        rows.append((relative.as_posix(), source, module, resource, donor_hash, hashlib.sha256(data).hexdigest()))

    for script in sorted((payload / 'Resources/scripts').glob('*.txt')):
        module = script.name[:-4]
        for line in script.read_text().splitlines():
            match = re.match(r'-addoverwrite\s+(\S+),\s*(ICONGROUP|BITMAP|AVI),\s*([^, ]+)', line)
            if not match:
                continue
            asset, kind, name = match.groups()
            relative = Path(asset.replace('\\', '/'))
            # NT shell branding and 98 shell icons, with NT-only resources as fallback.
            order = ['NT5-1877', 'Windows98'] if module in ('msgina.dll', 'explorer.exe') else ['Windows98', 'NT5-1877']
            for source in order:
                donor = donors.get((source, module))
                if donor is None:
                    continue
                res, donor_hash = donor
                data = None
                donor_name = name
                if kind == 'ICONGROUP':
                    data = icon(res, name)
                elif kind == 'BITMAP':
                    # XP uses header-sized artwork, not NT's 484x280 startup panels.
                    # Reuse the authentic 413x72 workstation header for its logon slots.
                    if module == 'explorer.exe' and name in ('158', '163', '164', '166', '167'):
                        donor_name = '157'
                    if module == 'msgina.dll' and name != '103':
                        donor_name = '101'
                    dib = res.get(('2', donor_name))
                    if dib:
                        data = bitmap(dib)
                        original = (payload / relative).read_bytes()
                        if module != 'msgina.dll' and data[18:26] != original[18:26]:
                            data = None  # toolbar geometry must match XP's existing strip
                else:
                    data = res.get(('AVI', name))
                if data:
                    save(relative, data, source, module, kind + ':' + donor_name, donor_hash)
                    break

    # XP's ShellAbout/winver headers do not share NT 5.0 shell32's IDs.
    # The workstation header lives in NT's msgina; use the original DIB bytes.
    res, donor_hash = donors[('NT5-1877', 'msgina.dll')]
    for name in ('130', '131', '146', '147'):
        relative = Path('Resources/eXPerience2K/shell32') / (name + '.bmp')
        rows[:] = [row for row in rows if row[0] != relative.as_posix()]
        save(relative, bitmap(res[('2', '101')]), 'NT5-1877', 'msgina.dll',
             'BITMAP:101', donor_hash)

    for name, text in [('180', 'Microsoft Windows NT'), ('181', 'Microsoft Windows NT'),
                       ('195', '5.0 Beta style (Windows XP)')]:
        save(Path('Resources/eXPerience2K/sysdm') / (name + '.txt'),
             (text + '\r\n').encode('ascii'), 'Project', 'System Properties', 'STRING:' + name, '-')

    sound_map = {'ding.wav': 'DING.WAV', 'chord.wav': 'CHORD.WAV', 'notify.wav': 'NOTIFY.WAV',
                 'logoff.wav': 'LOGOFF.WAV', 'logon.wav': 'The Microsoft Sound.wav', 'start.wav': 'START.WAV'}
    for target, donor in sound_map.items():
        data = (args.win98 / 'MEDIA' / donor).read_bytes()
        save(Path('Sounds/Windows2000') / target, data, 'Windows98', donor, 'WAVE', hashlib.sha256(data).hexdigest())
    for target, donor in [('ringin.wav', 'ringin.wav'), ('blip.wav', 'ding.wav')]:
        data = (args.nt5 / 'Media' / donor).read_bytes()
        save(Path('Sounds/Windows2000') / target, data, 'NT5-1877', donor, 'WAVE', hashlib.sha256(data).hexdigest())
    (output / 'assets.tsv').write_text('asset\tsource\tmodule\tresource\tdonor_sha256\tsha256\n' +
        ''.join('\t'.join(row) + '\n' for row in rows), encoding='utf-8')
    print(f'Imported {len(rows)} assets; original Windows 2000 payload unchanged.')


if __name__ == '__main__':
    main()
