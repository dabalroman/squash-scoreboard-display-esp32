# Pre-build: patch THIS env's copy of FastLED 3.9.16 (RMT4) with refill-lateness
# counters. Rebuilds task #58's RMTDIAG instrumentation, which was never committed.
# Only env v1_ota_rmtdiag runs it; every other env keeps its own unpatched FastLED.
# The counters are extern "C" volatiles, so firmware reads them with matching externs.
#
# Lateness = (time since previous refill) - (one half-buffer time). FastLED bails
# (cuts the frame short) above 50 % of a half-buffer late, and the RMT replays the
# stale half (visible glitch) above 100 %. Histogram: 10 buckets of 12.5 % each,
# so bucket 4+ = bail and bucket 8+ = stale.
Import("env")
import os

MARK = "// RMTDIAG_PATCH"
path = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"),
                    "FastLED", "src", "platforms", "esp", "32", "rmt_4", "idf4_rmt_impl.cpp")

if not os.path.isfile(path):
    raise SystemExit("rmtdiag_patch: FastLED source not found at " + path +
                     " - run the build once more after lib_deps install")

src = open(path, encoding="utf-8").read()
if MARK not in src:
    fn = "void ESP32RMTController::fillNext(bool check_time)\n{\n    uint32_t now = __clock_cycles();\n"
    hook = "    mLastFill = now;\n\n    // -- Get the zero and one values"
    if src.count(fn) != 1 or src.count(hook) != 1:
        raise SystemExit("rmtdiag_patch: anchors not found - FastLED version changed?")

    counters = MARK + """
extern "C" {
volatile uint32_t rmtdiag_fills = 0, rmtdiag_bails = 0, rmtdiag_stale = 0;
volatile uint32_t rmtdiag_max_late_cyc = 0, rmtdiag_cyc_per_fill = 0;
volatile uint32_t rmtdiag_hist[10] = {0};
}
static inline void IRAM_ATTR rmtdiag_record(int32_t delta, uint32_t perFill)
{
    rmtdiag_fills++;
    rmtdiag_cyc_per_fill = perFill;
    int32_t late = delta - (int32_t)perFill;
    if (late < 0) late = 0;
    if ((uint32_t)late > rmtdiag_max_late_cyc) rmtdiag_max_late_cyc = late;
    uint32_t b = (uint32_t)late * 8u / perFill;
    rmtdiag_hist[b > 9 ? 9 : b]++;
    if ((uint32_t)late * 2u > perFill) rmtdiag_bails++;
    if ((uint32_t)late > perFill) rmtdiag_stale++;
}

"""
    src = src.replace(fn, counters + fn)
    src = src.replace(hook, "    if (check_time && mLastFill != 0) rmtdiag_record((int32_t)(now - mLastFill), mCyclesPerFill); " +
                      MARK + "\n" + hook)
    open(path, "w", encoding="utf-8", newline="\n").write(src)
    print("rmtdiag_patch: patched " + path)
else:
    print("rmtdiag_patch: already patched")
