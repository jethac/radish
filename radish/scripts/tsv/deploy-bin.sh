#!/usr/bin/env bash
# Install a built binary into the private prefix as bin/<name>-<sha8>; prints that name.
set -euo pipefail
. "$(dirname "$0")/common.sh"
FILE=$1 NAME=${2:-$(basename "$1")}
SHA=$(sha256sum "$FILE" | cut -c1-8)
DEST=$PREFIX/bin/$NAME-$SHA
flock -w 1800 "$LOCK" bash -c "adb -s $SERIAL shell 'test -f $DEST' 2>/dev/null || {
    adb -s $SERIAL push -q '$FILE' '$DEST.tmp' >&2 && adb -s $SERIAL shell 'chmod 755 $DEST.tmp && mv $DEST.tmp $DEST'; }"
echo "$NAME-$SHA"
