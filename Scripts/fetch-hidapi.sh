#!/usr/bin/env bash
# Copies the hidapi sources the plugin compiles into Source/ThirdParty/hidapi.
#
# Only the public header, the Windows/macOS/Linux (hidraw) backends and the license files are kept.
# The plugin uses hidapi under its BSD-3-Clause option (see LICENSE).
#
# Usage, from the repo root (Git Bash on Windows):
#   Scripts/fetch-hidapi.sh [tag]     default tag: hidapi-0.15.0
set -euo pipefail

TAG="${1:-hidapi-0.15.0}"
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$REPO_ROOT/Source/ThirdParty/hidapi"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

git clone --quiet --depth 1 --branch "$TAG" https://github.com/libusb/hidapi.git "$WORK/hidapi"
SRC="$WORK/hidapi"

rm -rf "$DEST/hidapi" "$DEST/windows" "$DEST/mac" "$DEST/linux"
mkdir -p "$DEST/hidapi" "$DEST/windows" "$DEST/mac" "$DEST/linux"

cp "$SRC/hidapi/hidapi.h" "$DEST/hidapi/"
for DIR in windows mac linux; do
	find "$SRC/$DIR" -maxdepth 1 -type f \( -name '*.c' -o -name '*.h' \) -exec cp {} "$DEST/$DIR/" \;
done
cp "$SRC"/LICENSE*.txt "$DEST/"
cp "$SRC/VERSION" "$DEST/VERSION"
echo "$TAG" > "$DEST/TAG"

echo "hidapi $(cat "$DEST/VERSION") ($TAG) copied to $DEST"
