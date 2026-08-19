'''
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
'''

# Packs a Lottie animation .json into the .tgs container read at runtime by
# Images::UnpackGzip (ui/image/image_prepare.cpp), so the repository keeps the
# plain JSON source instead of a gzip blob.
#
# Usage: json2tgs.py <input.json> <output.tgs>
#
# .tgs is a bare gzip stream, not an archive. Input that is already gzip-ed
# is copied through unchanged, so animations taken from upstream work without
# being converted first.

import gzip
import json
import sys


def main():
    if len(sys.argv) != 3:
        sys.stderr.write('Usage: json2tgs.py <input.json> <output.tgs>\n')
        return 1

    with open(sys.argv[1], 'rb') as f:
        content = f.read()

    if content[:2] != b'\x1f\x8b':
        try:
            json.loads(content)
        except ValueError as e:
            sys.stderr.write(sys.argv[1] + ': bad JSON: ' + str(e) + '\n')
            return 1
        content = gzip.compress(content, compresslevel=9, mtime=0)

    with open(sys.argv[2], 'wb') as f:
        f.write(content)

    return 0


if __name__ == '__main__':
    sys.exit(main())
