#!/usr/bin/env bash
# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
#
# Round-trip harness for the v2 update packer: generates a throwaway root
# key, channel keys and manifest, then packs a dummy tree in both signing
# modes and lets the packer's built-in client-grade verification act as
# the check. Needs a Packer binary and an OpenSSL 3 (macOS LibreSSL has
# no Ed25519):
#
#   Telegram/build/canary_test_fixtures.sh <path-to-Packer> [workdir]

set -e

PACKER="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
WORKDIR="${2:-$(mktemp -d)}"
SIGN_UPDATE="$(cd "$(dirname "$0")" && pwd)/sign_update.py"

if [ ! -x "$PACKER" ]; then
  echo "Usage: $0 <path-to-Packer> [workdir]"
  exit 1
fi
if ! openssl genpkey -algorithm ed25519 2>/dev/null >/dev/null; then
  echo "This OpenSSL cannot generate Ed25519 keys, use OpenSSL 3."
  exit 1
fi

echo "Working in $WORKDIR"
mkdir -p "$WORKDIR"
cd "$WORKDIR"
mkdir -p keys app

openssl genpkey -algorithm ed25519 -out root-private.pem
openssl pkey -in root-private.pem -pubout -out keys/root-public.pem
openssl genpkey -algorithm ed25519 -out ed-private.pem
openssl ecparam -genkey -name prime256v1 -noout -out es-private.pem

python3 - << 'EOF'
import base64, json, subprocess

def b64url(b):
    return base64.urlsafe_b64encode(b).rstrip(b'=').decode()

def pub_der(pem):
    return subprocess.run(
        ['openssl', 'pkey', '-in', pem, '-pubout', '-outform', 'DER'],
        capture_output=True, check=True).stdout

edx = pub_der('ed-private.pem')[-32:]
point = pub_der('es-private.pem')[-65:]
assert point[0] == 4
manifest = {
    "format": 1,
    "manifest_version": 1,
    "issued": 1755600000,
    "expires": 1900000000,
    "keys": [
        {"id": "ed-test", "alg": "Ed25519", "x": b64url(edx)},
        {"id": "es-test", "alg": "ES256", "crv": "P-256",
            "x": b64url(point[1:33]), "y": b64url(point[33:])},
    ],
    "channels": {
        "stable": [["ed-test"]],
        "canary-public": [["ed-test"], ["es-test"]],
    },
    "revoked": [],
}
with open('keys/manifest.min.json', 'wb') as f:
    f.write(json.dumps(manifest, separators=(',', ':')).encode())
EOF
openssl pkeyutl -sign -inkey root-private.pem -rawin \
  -in keys/manifest.min.json -out keys/manifest.sig

echo "dummy binary $(date)" > app/Telegram
echo "dummy updater" > app/Updater
chmod +x app/Telegram

echo
echo "=== One-pass Ed25519 signing (stable) ==="
"$PACKER" -path app -version 5000123 -channel stable \
  -keys-loc keys -local-key ed-private.pem -local-key-id ed-test
ls -la td-update-*-5000123

echo
echo "=== Two-pass 2-of-2 signing (canary-public, ES256 external + Ed25519 local) ==="
"$PACKER" -path app -version 5000123 -channel canary-public -counter 7 \
  -keys-loc keys -emit-signing-input signing-input.bin
UNSIGNED=$(ls td-update-*-5000123-canary-7.unsigned)
python3 "$SIGN_UPDATE" --input signing-input.bin --output es.sig \
  --openssl-key es-private.pem
"$PACKER" -channel canary-public -keys-loc keys \
  -unsigned "$UNSIGNED" -embed-signatures es-test:es.sig \
  -local-key ed-private.pem -local-key-id ed-test
ls -la "${UNSIGNED%.unsigned}"

echo
echo "=== Two-pass with fully external signatures ==="
openssl pkeyutl -sign -inkey ed-private.pem -rawin \
  -in signing-input.bin -out ed.sig
rm "${UNSIGNED%.unsigned}"
"$PACKER" -channel canary-public -keys-loc keys \
  -unsigned "$UNSIGNED" -embed-signatures ed-test:ed.sig es-test:es.sig
ls -la "${UNSIGNED%.unsigned}"

echo
echo "=== Negative: a corrupt signature must be refused ==="
head -c 63 es.sig > es-bad.sig && printf 'X' >> es-bad.sig
if "$PACKER" -channel canary-public -keys-loc keys \
    -unsigned "$UNSIGNED" -embed-signatures ed-test:ed.sig es-test:es-bad.sig
then
  echo "FAILED: the packer accepted a corrupt signature!"
  exit 1
fi
echo "Refused, as it should be."

echo
echo "All fixtures round-tripped in $WORKDIR"
