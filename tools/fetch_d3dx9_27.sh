#!/bin/sh
# Condemned.exe imports d3dx9_27.dll, which Wine-NX test build 2 does not ship.
# Takes Microsoft's own DLL out of the DirectX June 2010 redistributable (the same
# source winetricks uses) and puts it into components/d3dx9/ for assemble_sd.py.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
URL=https://download.microsoft.com/download/8/4/A/84A35BF1-DAFE-4AE8-82AF-AD2AE20B6B14/directx_Jun2010_redist.exe
REDIST_SHA256=053f76dcbb28802e23341b6a787e3b0791c0fa5c8d4d011b1044172dbf89c73b
DLL_SHA1=14705b638e1af81ddda5dc52f68c61ebfce5e9e3

tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
curl -fL -o "$tmp/redist.exe" "$URL"
echo "$REDIST_SHA256  $tmp/redist.exe" | shasum -a 256 -c -
(cd "$tmp" && 7z e -y redist.exe Aug2005_d3dx9_27_x86.cab >/dev/null && 7z e -y Aug2005_d3dx9_27_x86.cab d3dx9_27.dll >/dev/null)
echo "$DLL_SHA1  $tmp/d3dx9_27.dll" | shasum -a 1 -c -
mkdir -p "$ROOT/components/d3dx9"
cp "$tmp/d3dx9_27.dll" "$ROOT/components/d3dx9/"
echo "готово: components/d3dx9/d3dx9_27.dll"
