#!/usr/bin/env bash
# Build the LED dump harness and compare against (or regenerate) the V1 goldens.
#   ./run.sh                    check v1_snapshot against the goldens
#   ./run.sh --golden           regenerate goldens (only ever from v1_snapshot)
#   ./run.sh --root ../../src --defines "-DBOARD_REV=1 -DLEDBAR_LAMBDA"
# Env equivalents: DUMP_ROOT, DUMP_DEFINES.
set -euo pipefail
cd "$(dirname "$0")"

ROOT="${DUMP_ROOT:-v1_snapshot}"
DEFINES="${DUMP_DEFINES:-}"
GOLDEN=0

while [ $# -gt 0 ]; do
    case "$1" in
        --golden) GOLDEN=1 ;;
        --root) ROOT="$2"; shift ;;
        --defines) DEFINES="$2"; shift ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
    shift
done

echo "include root: $ROOT"
echo "defines:      ${DEFINES:-(none)}"

# shellcheck disable=SC2086
g++ -std=gnu++11 -Wall -O0 -I shim -I "$ROOT" $DEFINES -o dump dump.cpp

./dump glyphs > out_glyphs.txt
./dump frames > out_frames.txt

if [ "$GOLDEN" = 1 ]; then
    cp out_glyphs.txt golden_v1_glyphs.txt
    cp out_frames.txt golden_v1_frames.txt
    echo "goldens written: $(wc -l < golden_v1_glyphs.txt) glyph lines, $(wc -l < golden_v1_frames.txt) frame lines"
    exit 0
fi

rc=0
diff -u golden_v1_glyphs.txt out_glyphs.txt || rc=1
diff -u golden_v1_frames.txt out_frames.txt || rc=1
if [ "$rc" = 0 ]; then
    echo "OK: output identical to V1 goldens"
else
    echo "FAIL: output differs from V1 goldens" >&2
fi
exit $rc
