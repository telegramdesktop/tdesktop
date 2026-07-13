'''
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
'''

# Decodes a compact .binobj mesh (the format used by Telegram for Android, see
# obj2binobj.py for the layout) back into a human-readable Wavefront .obj.
#
# This is a developer tool, not part of the build: it is handy when porting a
# new 3D model from the Telegram for Android assets. Convert the .binobj to a
# text .obj, commit the .obj, and let generate_models.cmake bake it back at
# build time. The round-trip is byte-identical (obj2binobj.py <-> binobj2obj.py).
#
# Usage: binobj2obj.py <input.binobj> <output.obj>
#
# UVs are written raw (the runtime loader flips V); float values use %.9g, the
# shortest decimal that round-trips a 32-bit float exactly.

import struct
import sys


def main():
    if len(sys.argv) != 3:
        sys.stderr.write('Usage: binobj2obj.py <input.binobj> <output.obj>\n')
        return 1

    data = open(sys.argv[1], 'rb').read()
    offset = 0

    def read_int():
        nonlocal offset
        value = struct.unpack_from('>i', data, offset)[0]
        offset += 4
        return value

    def read_floats(count):
        nonlocal offset
        values = struct.unpack_from('>%df' % count, data, offset)
        offset += 4 * count
        return values

    positions = read_floats(read_int())
    uvs = read_floats(read_int())
    normals = read_floats(read_int())
    corner_count = read_int()
    corners = [struct.unpack_from('>3i', data, offset + 12 * i)
        for i in range(corner_count)]
    offset += 12 * corner_count
    if offset != len(data):
        sys.stderr.write('binobj2obj.py: trailing bytes, format mismatch\n')
        return 1

    g = lambda x: '%.9g' % x
    with open(sys.argv[2], 'w') as out:
        out.write('# 3D mesh decoded from .binobj (source of truth; baked back'
            ' to .binobj at build time).\n')
        out.write('# UVs are stored raw here; the V coordinate is flipped (1-v)'
            ' at load time.\n')
        for i in range(len(positions) // 3):
            out.write('v %s %s %s\n' % (
                g(positions[3 * i]),
                g(positions[3 * i + 1]),
                g(positions[3 * i + 2])))
        for i in range(len(uvs) // 2):
            out.write('vt %s %s\n' % (g(uvs[2 * i]), g(uvs[2 * i + 1])))
        for i in range(len(normals) // 3):
            out.write('vn %s %s %s\n' % (
                g(normals[3 * i]),
                g(normals[3 * i + 1]),
                g(normals[3 * i + 2])))
        for t in range(corner_count // 3):
            a, b, c = corners[3 * t], corners[3 * t + 1], corners[3 * t + 2]
            out.write('f %d/%d/%d %d/%d/%d %d/%d/%d\n' % (
                a[0] + 1, a[1] + 1, a[2] + 1,
                b[0] + 1, b[1] + 1, b[2] + 1,
                c[0] + 1, c[1] + 1, c[2] + 1))

    return 0


if __name__ == '__main__':
    sys.exit(main())
