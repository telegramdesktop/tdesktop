'''
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
'''

# Converts a Wavefront .obj mesh into the compact .binobj format read at
# runtime by Ui::Premium::LoadStarModel (ui/effects/premium_star_model.cpp).
#
# Usage: obj2binobj.py <input.obj> <output.binobj>
#
# Format (big-endian, 32-bit ints and floats):
#   int32 positionFloatCount; float32[positionFloatCount]   # xyz triples
#   int32 uvFloatCount;       float32[uvFloatCount]          # uv pairs
#   int32 normalFloatCount;   float32[normalFloatCount]      # xyz triples
#   int32 cornerCount;        { int32 posIdx, uvIdx, normIdx } x cornerCount
# Indices are 0-based. UVs are stored raw; the loader flips V (1 - v).

import struct
import sys


def main():
    if len(sys.argv) != 3:
        sys.stderr.write('Usage: obj2binobj.py <input.obj> <output.binobj>\n')
        return 1

    positions = []
    uvs = []
    normals = []
    corners = []

    with open(sys.argv[1], 'r') as source:
        for line in source:
            parts = line.split()
            if not parts or parts[0].startswith('#'):
                continue
            tag = parts[0]
            if tag == 'v':
                positions += [float(parts[1]), float(parts[2]), float(parts[3])]
            elif tag == 'vt':
                uvs += [float(parts[1]), float(parts[2])]
            elif tag == 'vn':
                normals += [float(parts[1]), float(parts[2]), float(parts[3])]
            elif tag == 'f':
                face = []
                for vertex in parts[1:]:
                    fields = (vertex.split('/') + ['', ''])[:3]
                    face.append([
                        (int(fields[0]) - 1) if fields[0] else -1,
                        (int(fields[1]) - 1) if fields[1] else -1,
                        (int(fields[2]) - 1) if fields[2] else -1,
                    ])
                # Triangulate as a fan (a no-op for already-triangular faces).
                for i in range(1, len(face) - 1):
                    corners += [face[0], face[i], face[i + 1]]

    with open(sys.argv[2], 'wb') as out:
        out.write(struct.pack('>i', len(positions)))
        out.write(struct.pack('>%df' % len(positions), *positions))
        out.write(struct.pack('>i', len(uvs)))
        out.write(struct.pack('>%df' % len(uvs), *uvs))
        out.write(struct.pack('>i', len(normals)))
        out.write(struct.pack('>%df' % len(normals), *normals))
        out.write(struct.pack('>i', len(corners)))
        for corner in corners:
            out.write(struct.pack('>3i', corner[0], corner[1], corner[2]))

    return 0


if __name__ == '__main__':
    sys.exit(main())
