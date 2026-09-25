#!/bin/sh
# Wine-NX Test Build 4 (github.com/danfromtico/autorun/releases/tag/test-build-4):
# the runtime NRO with its Wine payload. The 32-bit forwarder for sphaira still comes
# from the Test Build 2 release, the only one that carries it. Unpacked into
# components/wine-nx/ for assemble_sd.py.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BASE=https://github.com/danfromtico/autorun/releases/download/test-build-4
OLD=https://github.com/danfromtico/autorun/releases/download/test-build-2
DEST="$ROOT/components/wine-nx"

tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
curl -fL -o "$tmp/wine.zip" "$BASE/autorun-test-build-4.zip"
curl -fL -o "$tmp/sphaira-32bit-forwaders.nro" "$OLD/sphaira-32bit-forwaders.nro"
shasum -a 256 -c - <<EOF
183ebb2f5d4d17903e9f2e3d4d566d0921bfd568eee0567d21ead1e0f1241ad2  $tmp/wine.zip
ad34de3b9b71b12fd769939133db9f36e6e433f97d4093e387829acf6359a963  $tmp/sphaira-32bit-forwaders.nro
EOF
rm -rf "$DEST" && mkdir -p "$DEST/switch"
unzip -q "$tmp/wine.zip" -d "$DEST"
cp "$tmp/sphaira-32bit-forwaders.nro" "$DEST/switch/"
echo "готово: $DEST/switch/wine/wine-nx-runtime.nro"
