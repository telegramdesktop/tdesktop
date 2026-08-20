#!/usr/bin/env python3
# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# ES256 signing glue for the v2 update packer, shared by build.bat,
# build.sh and canary.yml. Standard library only: the actual signing
# happens in Azure Key Vault (az keyvault key sign) or, for local
# testing, in an openssl subprocess.
#
# The packer emits a signing-input file (-emit-signing-input), this
# script signs its SHA-256 and writes the raw r||s (64 bytes) signature
# that the packer embeds (-embed-signatures id:sigfile). ES256 keys sign
# SHA256(signing_input); Ed25519 keys are signed by the packer itself
# in-process (-local-key), never through this script.

import argparse
import base64
import hashlib
import json
import shutil
import subprocess
import sys


def b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b'=').decode('ascii')


def b64url_decode(text: str) -> bytes:
    padded = text + '=' * (-len(text) % 4)
    return base64.urlsafe_b64decode(padded)


def der_to_raw_rs(der: bytes) -> bytes:
    # Minimal DER parse of SEQUENCE { INTEGER r, INTEGER s }, strict
    # about lengths: the input is normally trusted openssl output, but a
    # malformed signature must fail here rather than in the packer.
    def read_len(data, offset):
        first = data[offset]
        offset += 1
        if first < 0x80:
            return first, offset
        count = first & 0x7F
        if count == 0 or count > 2 or offset + count > len(data):
            raise ValueError('bad DER length')
        value = int.from_bytes(data[offset:offset + count], 'big')
        return value, offset + count

    if not der or der[0] != 0x30:
        raise ValueError('not a DER SEQUENCE')
    total, offset = read_len(der, 1)
    if offset + total != len(der):
        raise ValueError('DER SEQUENCE length does not match the input')

    def read_int(data, offset):
        if offset >= len(data) or data[offset] != 0x02:
            raise ValueError('not a DER INTEGER')
        length, offset = read_len(data, offset + 1)
        if length == 0 or offset + length > len(data):
            raise ValueError('bad DER INTEGER length')
        value = data[offset:offset + length].lstrip(b'\x00')
        if len(value) > 32:
            raise ValueError('integer too long for P-256')
        return value.rjust(32, b'\x00'), offset + length

    r, offset = read_int(der, offset)
    s, offset = read_int(der, offset)
    if offset != len(der):
        raise ValueError('trailing bytes after the DER signature')
    return r + s


def sign_azure(digest: bytes, args) -> bytes:
    # On Windows the CLI is az.cmd, which a plain 'az' argv misses.
    command = [
        shutil.which('az') or 'az', 'keyvault', 'key', 'sign',
        '--vault-name', args.az_vault,
        '--name', args.az_key,
        '--algorithm', 'ES256',
        '--digest', b64url(digest),
        '--output', 'json',
        '--only-show-errors',
    ]
    if args.az_key_version:
        command += ['--version', args.az_key_version]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError('az keyvault key sign failed: ' + result.stderr)
    signature = b64url_decode(json.loads(result.stdout)['signature'])
    if len(signature) != 64:
        raise RuntimeError(
            'unexpected Key Vault signature size: %d' % len(signature))
    return signature


def sign_openssl(digest: bytes, args) -> bytes:
    # Local testing stub: ECDSA-sign the digest with a P-256 key file,
    # producing exactly what Key Vault would return.
    result = subprocess.run(
        ['openssl', 'pkeyutl', '-sign', '-inkey', args.openssl_key],
        input=digest,
        capture_output=True)
    if result.returncode != 0:
        raise RuntimeError(
            'openssl pkeyutl failed: ' + result.stderr.decode())
    return der_to_raw_rs(result.stdout)


def main():
    parser = argparse.ArgumentParser(
        description='Sign a packer signing-input file with an ES256 key.')
    parser.add_argument('--input', required=True,
        help='signing-input file emitted by the packer')
    parser.add_argument('--output', required=True,
        help='where to write the raw r||s (64 bytes) signature')
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--az-vault',
        help='Azure Key Vault name (signs with az keyvault key sign)')
    group.add_argument('--openssl-key',
        help='local P-256 private key PEM (testing stub)')
    parser.add_argument('--az-key',
        help='Key Vault key name, required with --az-vault')
    parser.add_argument('--az-key-version', default='',
        help='optional Key Vault key version')
    args = parser.parse_args()

    if args.az_vault and not args.az_key:
        parser.error('--az-key is required with --az-vault')

    with open(args.input, 'rb') as f:
        digest = hashlib.sha256(f.read()).digest()

    if args.az_vault:
        signature = sign_azure(digest, args)
    else:
        signature = sign_openssl(digest, args)

    with open(args.output, 'wb') as f:
        f.write(signature)
    print('Signature written to', args.output)
    return 0


if __name__ == '__main__':
    sys.exit(main())
