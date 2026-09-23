#!/usr/bin/env bash
# Build the LED dump harness and compare against (or regenerate) the V1 goldens.
#   ./run.sh                       check the LIVE src/ tree against the live goldens (default)
#   ./run.sh --snapshot            harness self-test: frozen v1_snapshot vs ITS OWN frozen
#                                   goldens - proves the harness works, proves NOTHING about src/
#   ./run.sh --snapshot --golden   regenerate the frozen snapshot self-test goldens
#   ./run.sh --golden --rebaseline deliberately re-baseline the LIVE goldens - loud, opt-in,
#                                   see README "Re-baselining the live goldens"
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
REBASELINE=0

while [ $# -gt 0 ]; do
    case "$1" in
        --golden) GOLDEN=1 ;;
        --rebaseline) REBASELINE=1 ;;
        --snapshot) ROOT="v1_snapshot"; DEFINES="" ;;
        --root) ROOT="$2"; shift ;;
        --defines) DEFINES="$2"; shift ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
    shift
done

# Two independent golden pairs, deliberately (see README): v1_snapshot's Glyph
# enum and setBrightness() predate both the 46-glyph table and #46's *0.8f
# scaling, so its own output can never match the live tree's once brightness
# and glyphs 37-45 are captured. Sharing one golden file across both roots would
# make one of the two checks permanently, falsely red.
if [ "$ROOT" = "v1_snapshot" ]; then
    echo "NOTE: root is v1_snapshot - this is a harness SELF-TEST (frozen copy vs its own"
    echo "      frozen goldens). It does not check the current src/ tree."
    GLYPHS_GOLDEN="v1_snapshot/golden_glyphs.txt"
    FRAMES_GOLDEN="v1_snapshot/golden_frames.txt"
else
    GLYPHS_GOLDEN="golden_v1_glyphs.txt"
    FRAMES_GOLDEN="golden_v1_frames.txt"
fi

if [ "$GOLDEN" = 1 ] && [ "$ROOT" != "v1_snapshot" ] && [ "$REBASELINE" != 1 ]; then
    echo "refusing to write goldens from root '$ROOT': $FRAMES_GOLDEN / $GLYPHS_GOLDEN are a" >&2
    echo "deliberate, committed regression baseline, not something a routine run should" >&2
    echo "overwrite (that was the tautology task #44 fixed). Pass --golden --rebaseline to" >&2
    echo "regenerate them on purpose, or --root v1_snapshot (or --snapshot) to regenerate" >&2
    echo "the frozen self-test goldens instead." >&2
    exit 2
fi

if [ "$GOLDEN" = 1 ] && [ "$ROOT" != "v1_snapshot" ] && [ "$REBASELINE" = 1 ]; then
    echo "!!! REBASELINE: about to OVERWRITE $FRAMES_GOLDEN and $GLYPHS_GOLDEN from root" >&2
    echo "!!! '$ROOT'. These are the committed V1 regression baseline. Only do this" >&2
    echo "!!! deliberately: confirm a bare ./run.sh currently passes first (proves no" >&2
    echo "!!! unexplained pixel drift), then diff old vs new goldens by hand afterwards." >&2
    echo "!!! See README 'Re-baselining the live goldens'." >&2
fi

echo "include root: $ROOT"
echo "defines:      ${DEFINES:-(none)}"

# shellcheck disable=SC2086
g++ -std=gnu++11 -Wall -O0 -I shim -I "$ROOT" $DEFINES -o dump dump.cpp

./dump glyphs > out_glyphs.txt
./dump frames > out_frames.txt

if [ "$GOLDEN" = 1 ]; then
    cp out_glyphs.txt "$GLYPHS_GOLDEN"
    cp out_frames.txt "$FRAMES_GOLDEN"
    echo "goldens written: $(wc -l < "$GLYPHS_GOLDEN") glyph lines, $(wc -l < "$FRAMES_GOLDEN") frame lines"
    exit 0
fi

rc=0
diff -u "$GLYPHS_GOLDEN" out_glyphs.txt || rc=1
diff -u "$FRAMES_GOLDEN" out_frames.txt || rc=1
if [ "$rc" = 0 ]; then
    echo "OK: output identical to V1 goldens"
else
    echo "FAIL: output differs from V1 goldens" >&2
fi
exit $rc
