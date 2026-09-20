"""Read the named 520-byte Windows 98 appearance scheme from a copied USER.DAT.

This narrowly validates a CREG REG_BINARY value; it is not a general hive parser.
Usage: python scripts/extract-win98-colors.py <copied-USER.DAT> <output.tsv>
No hive is loaded into the host registry or modified.
"""
import hashlib
from pathlib import Path
import struct
import sys

NAMES = ('Scrollbar Background ActiveTitle InactiveTitle Menu Window WindowFrame '
         'MenuText WindowText TitleText ActiveBorder InactiveBorder AppWorkSpace '
         'Hilight HilightText ButtonFace ButtonShadow GrayText ButtonText '
         'InactiveTitleText ButtonHilight ButtonDkShadow ButtonLight InfoText '
         'InfoWindow ButtonAlternateFace HotTrackingColor GradientActiveTitle '
         'GradientInactiveTitle').split()


def extract(data):
    if data[:4] != b'CREG':
        raise ValueError('Expected a Windows 9x CREG hive')
    name = b'Windows Standard'
    candidates = []
    pos = 0
    while True:
        pos = data.find(name, pos)
        if pos < 0:
            break
        if pos >= 12:
            kind, _, name_size, value_size = struct.unpack_from('<IIHH', data, pos - 12)
            if kind == 3 and name_size == len(name) and value_size == 520:
                value = data[pos + name_size:pos + name_size + value_size]
                if len(value) == 520 and struct.unpack_from('<I', value)[0] == 4:
                    # DWORD version + NONCLIENTMETRICSA (340) + LOGFONTA (60),
                    # then 29 COLORREF values. The high byte holds palette flags.
                    candidates.append(value)
        pos += len(name)
    if len(candidates) != 1:
        raise ValueError('Expected exactly one version-4 Windows Standard scheme')
    return candidates[0]


if __name__ == '__main__':
    donor = Path(sys.argv[1]).read_bytes()
    scheme = extract(donor)
    rows = ['name\trgb\tcolorref']
    for name, value in zip(NAMES, struct.unpack_from('<29I', scheme, 404)):
        rgb = f'{value & 255} {(value >> 8) & 255} {(value >> 16) & 255}'
        rows.append(f'{name}\t{rgb}\t{value:08X}')
    Path(sys.argv[2]).write_bytes(('\r\n'.join(rows) + '\r\n').encode('ascii'))
    print('USER.DAT SHA256:', hashlib.sha256(donor).hexdigest())
    print('Scheme SHA256:', hashlib.sha256(scheme).hexdigest())
