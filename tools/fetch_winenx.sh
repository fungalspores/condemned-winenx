#!/bin/sh
# Wine-NX Test Build 2 (github.com/danfromtico/wine-nx/releases/tag/test-build-2):
# the runtime NRO with its Wine payload, and sphaira with the 32-bit forwarder
# options Condemned needs. Unpacked into components/wine-nx/ for assemble_sd.py.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BASE=https://github.com/danfromtico/wine-nx/releases/download/test-build-2
DEST="$ROOT/components/wine-nx"

tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
curl -fL -o "$tmp/wine.zip" "$BASE/wine-test-build-2.zip"
curl -fL -o "$tmp/sphaira-32bit-forwaders.nro" "$BASE/sphaira-32bit-forwaders.nro"
shasum -a 256 -c - <<EOF
7ab014186f7927c220b94374bd691fbf7127efd728d822e6bc97cc278e25470f  $tmp/wine.zip
ad34de3b9b71b12fd769939133db9f36e6e433f97d4093e387829acf6359a963  $tmp/sphaira-32bit-forwaders.nro
EOF
rm -rf "$DEST" && mkdir -p "$DEST/switch"
unzip -q "$tmp/wine.zip" -d "$DEST"
cp "$tmp/sphaira-32bit-forwaders.nro" "$DEST/switch/"
echo "готово: $DEST/switch/wine/wine-nx-runtime.nro"
