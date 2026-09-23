#!/usr/bin/env bash
# Build the LED dump harness and compare against (or regenerate) the V1 goldens.
#   ./run.sh                    check the LIVE src/ tree against the goldens (default)
#   ./run.sh --snapshot         harness self-test: frozen v1_snapshot vs its own goldens -
#                                proves the harness works, proves NOTHING about src/
#   ./run.sh --golden --root v1_snapshot   regenerate goldens (only ever from v1_snapshot)
#   ./run.sh --root <path> --defines "..."  override root/defines explicitly
# Env equivalents: DUMP_ROOT, DUMP_DEFINES.
set -euo pipefail
cd "$(dirname "$0")"

# The set that actually compiles against the live tree: see CLAUDE.md's "READ FIRST" /
# helpers/led_dump/README.md. LEDBAR_LAMBDA/CURRENT_LEDDISPLAY_API select the post-refactor
# LedDisplay API (setLedBarState(lambda), resetAnimations()/startCelebration(c,bool));
# v1_snapshot predates both, so --snapshot below passes no defines at all, on purpose.
DEFAULT_DEFINES="-DBOARD_REV=1 -DLEDBAR_LAMBDA -DCONFIG_IDF_TARGET_ESP32S2=1 -DCURRENT_LEDDISPLAY_API"

ROOT="${DUMP_ROOT:-../../src}"
DEFINES="${DUMP_DEFINES:-$DEFAULT_DEFINES}"
GOLDEN=0

while [ $# -gt 0 ]; do
    case "$1" in
        --golden) GOLDEN=1 ;;
        --snapshot) ROOT="v1_snapshot"; DEFINES="" ;;
        --root) ROOT="$2"; shift ;;
        --defines) DEFINES="$2"; shift ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
    shift
done

if [ "$ROOT" = "v1_snapshot" ]; then
    echo "NOTE: root is v1_snapshot - this is a harness SELF-TEST (frozen copy vs its own"
    echo "      goldens). It does not check the current src/ tree."
fi

if [ "$GOLDEN" = 1 ] && [ "$ROOT" != "v1_snapshot" ]; then
    echo "refusing to write goldens from root '$ROOT': the goldens are v1_snapshot's" >&2
    echo "baseline, not the live tree's. Pass --root v1_snapshot (or --snapshot) to regenerate." >&2
    exit 2
fi

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
