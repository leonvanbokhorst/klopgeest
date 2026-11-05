#!/usr/bin/env bash
# Fetches JUCE 9.0.5 into .deps/JUCE (idempotent). Builds never fetch at compile time.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="9.0.5"
DEST="$ROOT/.deps/JUCE"

if [ -f "$DEST/CMakeLists.txt" ]; then
  echo "JUCE already present at $DEST"
  exit 0
fi

mkdir -p "$ROOT/.deps"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
echo "Downloading JUCE $VERSION..."
curl -sSL "https://github.com/juce-framework/JUCE/archive/refs/tags/${VERSION}.tar.gz" -o "$TMP/juce.tar.gz"
tar xzf "$TMP/juce.tar.gz" -C "$TMP"
mv "$TMP/JUCE-${VERSION}" "$DEST"
echo "JUCE $VERSION installed at $DEST"
