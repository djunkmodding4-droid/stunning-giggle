#!/usr/bin/env bash
# Usage: scripts/make_runtime_zip.sh /path/to/plugin-binary [output.zip]
set -e
if [ -z "$1" ]; then
  echo "Usage: $0 /path/to/plugin-binary [output.zip]"
  exit 1
fi
BIN=$1
OUT=${2:-runtime.zip}
TMPDIR=$(mktemp -d)
cp info.txt "$TMPDIR/"
cp main.lua "$TMPDIR/"
mkdir -p "$TMPDIR/plugin"
cp "$BIN" "$TMPDIR/plugin/"
(cd "$TMPDIR" && zip -r "$OLDPWD/$OUT" .)
echo "Created $OUT"
rm -rf "$TMPDIR"