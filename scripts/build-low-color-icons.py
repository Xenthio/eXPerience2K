"""Losslessly select existing <=4-bpp ICO frames; never quantize or redraw.

Authoring tool only. Run after import-legacy-theme.py when source assets change.
Builds the optional 98 low-colour overlay, including inherited base icons with
authentic low-colour frames. Icons without those frames retain their normal art.
"""
import csv
import hashlib
from pathlib import Path
import struct


def low_color_icon(data):
    if len(data) < 6:
        raise ValueError('Truncated ICO')
    reserved, kind, count = struct.unpack_from('<HHH', data)
    if reserved or kind != 1 or not count or len(data) < 6 + count * 16:
        raise ValueError('Invalid ICO directory')
    kept = []
    for i in range(count):
        entry = data[6 + i * 16:22 + i * 16]
        depth, size, offset = struct.unpack_from('<HII', entry, 6)
        if offset < 6 + count * 16 or offset + size > len(data):
            raise ValueError('ICO image outside file')
        image = data[offset:offset + size]
        if depth in (1, 4):
            if len(image) < 40 or struct.unpack_from('<H', image, 14)[0] != depth:
                raise ValueError('ICO directory and DIB depth disagree')
            kept.append((entry, image))
    if not kept:
        return None
    directory = bytearray(struct.pack('<HHH', 0, 1, len(kept)))
    images = bytearray()
    for entry, image in kept:
        directory.extend(entry[:8] + struct.pack('<II', len(image), 6 + len(kept) * 16 + len(images)))
        images.extend(image)
    return bytes(directory + images)


def main():
    root = Path(__file__).resolve().parents[1]
    payload = root / 'payload'
    theme = payload / 'Themes/windows-98-nt5'
    output = theme / 'LowColor'
    rows = []
    for base in sorted((payload / 'Resources/eXPerience2K').rglob('*.ico')):
        asset = base.relative_to(payload)
        source = theme / asset if (theme / asset).exists() else base
        original = source.read_bytes()
        filtered = low_color_icon(original)
        if filtered is None:
            continue
        destination = output / asset
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(filtered)
        rows.append((asset.as_posix(), source.relative_to(payload).as_posix(),
                     hashlib.sha256(original).hexdigest(), hashlib.sha256(filtered).hexdigest()))
    with (output / 'assets.tsv').open('w', newline='', encoding='ascii') as file:
        writer = csv.writer(file, delimiter='\t', lineterminator='\r\n')
        writer.writerow(('asset', 'source', 'source_sha256', 'sha256'))
        writer.writerows(rows)
    print(f'Built {len(rows)} lossless low-colour icons.')


if __name__ == '__main__':
    main()
