#!/usr/bin/env python3
# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# ES256 signing glue for the v2 update packer, shared by build.bat,
# build.sh and canary.yml. Standard library only: the actual signing
# happens in Azure Key Vault (REST sign with an az token) or, for local
# testing, in an openssl subprocess.
#
# The packer emits a signing-input file (-emit-signing-input), this
# script signs its SHA-256 and writes the raw r||s (64 bytes) signature
# that the packer embeds (-embed-signatures id:sigfile). ES256 keys sign
# SHA256(signing_input); Ed25519 keys are signed by the packer itself
# in-process (-local-key), never through this script. Azure signing goes
# through the Key Vault REST API with a token from the az session.
#
# --check runs the same CLI / session / key lookups without signing, so
# the build scripts can fail before a long build instead of after it,
# and --keys-loc with --key-id confirms the vault key is the manifest
# key of that id (the Key Vault key name and the manifest id differ).

import argparse
import base64
import hashlib
import json
import os
import shutil
import subprocess
import sys
import urllib.error
import urllib.request


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


AZ_LOGIN_HINT = (
    "Sign in once with: az login  (add --use-device-code when no browser "
    "can open, e.g. in WSL; the account needs Key Vault Crypto User on "
    "the key). The session then persists in ~/.azure.")


def az_install_hint() -> str:
    if sys.platform == 'darwin':
        return 'Install it with: brew install azure-cli'
    if sys.platform == 'win32':
        return ('Install it with: winget install --exact --id Microsoft.AzureCLI\n'
                '(then open a new Native Tools Command Prompt so PATH has az.cmd)')
    return ('Install it with: curl -sL https://aka.ms/InstallAzureCLIDeb | sudo bash\n'
            '(Debian/Ubuntu; in WSL install it inside the distro, do not rely '
            'on the Windows az reachable through the interop PATH)')


def az_access_token() -> str:
    # On Windows the CLI is az.cmd, which a plain 'az' argv misses.
    az = shutil.which('az')
    if not az:
        raise RuntimeError(
            'Azure CLI (az) not found in PATH, the Key Vault signature '
            'needs it.\n' + az_install_hint() + '\n' + AZ_LOGIN_HINT)
    result = subprocess.run(
        [
            az, 'account', 'get-access-token',
            '--resource', 'https://vault.azure.net',
            '--query', 'accessToken',
            '--output', 'tsv',
            '--only-show-errors',
        ],
        capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            'az account get-access-token failed: ' + result.stderr.strip()
            + '\n' + AZ_LOGIN_HINT)
    token = result.stdout.strip()
    if not token:
        raise RuntimeError('az account get-access-token returned no token')
    return token


def key_vault_request(token: str, url: str, body=None) -> dict:
    data = json.dumps(body).encode() if body is not None else None
    request = urllib.request.Request(url, data=data, method='POST' if data else 'GET')
    request.add_header('Authorization', 'Bearer ' + token)
    request.add_header('Content-Type', 'application/json')
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            return json.loads(response.read().decode())
    except urllib.error.HTTPError as error:
        raise RuntimeError('Key Vault %s failed: HTTP %d %s' % (
            url, error.code, error.read().decode(errors='replace')))
    except (urllib.error.URLError, OSError) as error:
        raise RuntimeError('Key Vault %s unreachable: %s' % (url, error))


def key_vault_key(token: str, args) -> dict:
    url = 'https://%s.vault.azure.net/keys/%s' % (args.az_vault, args.az_key)
    if args.az_key_version:
        url += '/' + args.az_key_version
    return key_vault_request(token, url + '?api-version=7.4')['key']


def check_manifest_key(key: dict, args):
    path = os.path.join(args.keys_loc, 'manifest.min.json')
    with open(path, 'rb') as f:
        manifest = json.load(f)
    entry = next((k for k in manifest['keys'] if k['id'] == args.key_id), None)
    if entry is None:
        raise RuntimeError('key id %r is not listed in %s' % (args.key_id, path))
    if entry.get('alg') != 'ES256':
        raise RuntimeError('key id %r is %s in the manifest, not ES256'
            % (args.key_id, entry.get('alg')))
    for coord in ('x', 'y'):
        if b64url_decode(entry[coord]) != b64url_decode(key[coord]):
            raise RuntimeError(
                'Key Vault key %s is not the manifest key %r (%s differs): '
                '--az-key names the wrong vault key'
                % (key['kid'], args.key_id, coord))


def sign_azure(token: str, key_id: str, digest: bytes) -> bytes:
    # The Key Vault REST API is called directly: "az keyvault key sign"
    # wants the digest as standard base64 but then serializes the raw
    # signature bytes through str.decode(), which is not a usable
    # transport for an ECDSA signature. The token comes from the az
    # session azure/login (OIDC) or an interactive az login set up.
    result = key_vault_request(
        token,
        key_id + '/sign?api-version=7.4',
        {'alg': 'ES256', 'value': b64url(digest)})
    signature = b64url_decode(result['value'])
    if len(signature) != 64:
        raise RuntimeError(
            'unexpected Key Vault signature size: %d (from %r)'
            % (len(signature), result['value']))
    return signature


def check_openssl_key(args):
    result = subprocess.run(
        ['openssl', 'pkey', '-in', args.openssl_key, '-noout'],
        capture_output=True)
    if result.returncode != 0:
        raise RuntimeError(
            'openssl cannot read %s: %s'
            % (args.openssl_key, result.stderr.decode().strip()))


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
    parser.add_argument('--input',
        help='signing-input file emitted by the packer')
    parser.add_argument('--output',
        help='where to write the raw r||s (64 bytes) signature')
    parser.add_argument('--check', action='store_true',
        help='verify the signing setup (CLI, session, key) without signing')
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--az-vault',
        help='Azure Key Vault name (signs through the Key Vault REST API)')
    group.add_argument('--openssl-key',
        help='local P-256 private key PEM (testing stub)')
    parser.add_argument('--az-key',
        help='Key Vault key name, required with --az-vault')
    parser.add_argument('--az-key-version', default='',
        help='optional Key Vault key version')
    parser.add_argument('--keys-loc',
        help='packer keys dir, verifies the vault key is --key-id of its manifest')
    parser.add_argument('--key-id',
        help='manifest key id the vault key must match, with --keys-loc')
    args = parser.parse_args()

    if args.az_vault and not args.az_key:
        parser.error('--az-key is required with --az-vault')
    if not args.check and not (args.input and args.output):
        parser.error('--input and --output are required without --check')
    if bool(args.keys_loc) != bool(args.key_id):
        parser.error('--keys-loc and --key-id go together')
    if args.key_id and not args.az_vault:
        parser.error('--key-id is only checked against an --az-vault key')

    try:
        if args.az_vault:
            token = az_access_token()
            key = key_vault_key(token, args)
            if args.key_id:
                check_manifest_key(key, args)
            if args.check:
                print('Key Vault key ready:', key['kid'])
                return 0
        elif args.check:
            check_openssl_key(args)
            print('OpenSSL key ready:', args.openssl_key)
            return 0

        with open(args.input, 'rb') as f:
            digest = hashlib.sha256(f.read()).digest()
        if args.az_vault:
            signature = sign_azure(token, key['kid'], digest)
        else:
            signature = sign_openssl(digest, args)
    except RuntimeError as error:
        print('sign_update.py:', error, file=sys.stderr)
        return 1

    with open(args.output, 'wb') as f:
        f.write(signature)
    print('Signature written to', args.output)
    return 0


if __name__ == '__main__':
    sys.exit(main())
