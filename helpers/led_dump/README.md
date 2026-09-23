# LED dump harness

Host-side golden dump of every LED pixel the V1 display layer writes. It proves that
refactors of `LedGlyph` / `LedDisplay` / `LedBar` stay byte-identical for V1
without flashing hardware.

- `v1_snapshot/` - `Color.h` and `Display/LedDisplay/{LedDisplay,LedGlyph,LedBar,LedBarMode}.h`
  taken with `git show` at commit 902928f. The bar renderers are not included:
  `LedDisplay.h` does not use them and they pull in Tournament/UserProfile code.
  It is frozen - its `Glyph` enum stops at id 36 (no `GLYPH_COUNT`) and its
  `setBrightness()` predates #46's `*0.8f` scaling, so it can never reproduce the
  live tree's glyphs-37-45 or brightness output. See "Two golden baselines" below.
- `shim/` - minimal `Arduino.h` / `FastLED.h` (`CRGB`, fake `millis()`, and
  FastLED 3.9.16's exact `sin8_C` and `nscale8x3` with `FASTLED_SCALE8_FIXED=1`).
  `CFastLED` also records the last `setBrightness()` value (`getBrightness()`),
  since #47 - `show()` is where real FastLED would apply it, and it never touches
  the CRGB buffer this harness inspects, so recording it separately is the only
  way to catch a brightness-path regression at all.
- `dump.cpp` - includes `Display/LedDisplay/LedDisplay.h` from the include root.
  - `golden_v1_glyphs.txt` / `v1_snapshot/golden_glyphs.txt`: glyph ids x GlyphId
    0..6 x blink {off, on-visible tick 300, on-dark tick 100}. Pixels are detected
    against two sentinel fills, so writes of any colour, black included, are
    captured. The glyph-id loop bound is `GLYPH_COUNT` (`GlyphMasks.h`, 46 today)
    on the live tree and a literal 37 (ids 0..36) against `v1_snapshot`, which has
    no such constant - see `dump.cpp`'s `GLYPH_ID_COUNT`. Ids 0..36 are frozen
    byte-identical across both; the table is append-only, so this loop only ever
    grows.
  - `golden_v1_frames.txt` / `v1_snapshot/golden_frames.txt`: a 22-step script
    over every public `LedDisplay` method. Each step clears the buffer, calls
    `d.setBrightness(127)` once up front (PrefsData's real default,
    `src/PreferencesManager.h`), renders like `display()`, then lists the
    non-black pixels. Each step header now also prints `brightness=<value>` -
    `FastLED.getBrightness()` right after `render()`.
  - Define `LEDBAR_LAMBDA` once `setLedBarState` takes a lambda.
  - **Task #45**: `startCelebration()` stopped driving a golden-diffable pixel
    table once V1 got `LedSweepAnimation` (a geometry-driven ring, not a fixed
    per-tick table) - the 3 `startCelebration(Green) t0/t1/t2` steps were dropped
    from `dump.cpp` (25 steps -> 22) and both goldens regenerated to match
    (`--golden --rebaseline` for the live pair, `--snapshot --golden` for the
    frozen pair - see "Two golden baselines" below). The celebration's own
    invariants (front-only, no leaked glyph colour, indicators survive, bar
    swept) are covered by `check_v1.sh` instead, alongside the boot sweep -
    both share `sweep_checks.h` with `check_v2.sh`.

## Two golden baselines (since 2026-09-23, task #47)

There are now two independent golden pairs, not one:

- `golden_v1_frames.txt` / `golden_v1_glyphs.txt` (top level) - the **live tree's**
  baseline. Checked by a bare `./run.sh` (root `../../src`). Brightness reads
  `101` (PrefsData's default 127, scaled by `LedDisplay::setBrightness`'s
  `*0.8f` - task #46, deliberate and permanent on V1). Glyphs cover ids 0..45.
- `v1_snapshot/golden_frames.txt` / `v1_snapshot/golden_glyphs.txt` - the frozen
  snapshot's **own** baseline. Checked by `./run.sh --snapshot`. Brightness reads
  `127` unscaled (902928f predates #46). Glyphs cover only ids 0..36 (902928f
  predates the 46-glyph table and has no `GLYPH_COUNT` to bound a wider loop).

Why split: before #47, `dump.cpp` never called `getBrightness()` and only ever
looped glyphs 0..36, so both roots happened to produce output that fit one
shared golden file - the difference between scaled and unscaled brightness, and
between 37 and 46 glyphs, was invisible. Recording brightness and the full glyph
table makes that difference real and permanent (`101` will never equal `127`),
so one file can no longer serve both checks - trying to share one would leave
whichever check runs second permanently, falsely red.

### Re-baselining the live goldens

`--golden` alone refuses to touch `golden_v1_frames.txt` / `golden_v1_glyphs.txt`
unless the root is `v1_snapshot` - that guard (task #44) exists so a routine run
against the live tree can never silently rewrite the committed baseline and turn
the check back into a tautology. To regenerate the live goldens on purpose (a
real V1 pixel or brightness behaviour change was made and verified correct),
pass both flags together: `--golden --rebaseline`. It prints a loud warning
before writing. Before running it:

1. Confirm a bare `./run.sh` currently passes (`OK: output identical to V1
   goldens`) against the *old* goldens - this proves the change you're about to
   bake in is the one you intend, not an unrelated pixel regression riding along.
2. Run `./run.sh --golden --rebaseline`.
3. Diff the old and new goldens by hand (keep a copy of the old ones first) and
   confirm only the expected lines moved.

`./run.sh --snapshot --golden` regenerates the frozen self-test goldens the same
way it always has - no `--rebaseline` needed there, since `v1_snapshot/` never
changes and regenerating from it can't hide a live-tree regression.

## Run (WSL, g++ 13, `-std=gnu++11 -Wall`)

```sh
wsl -d Ubuntu-24.04 -- bash -lc "cd /mnt/c/localhost/squash-scoreboard-display-esp32/helpers/led_dump && ./run.sh"
# harness self-test only (frozen v1_snapshot vs its own goldens - proves the harness
# works, proves NOTHING about the current src/ tree):
wsl -d Ubuntu-24.04 -- bash -lc "cd /mnt/c/localhost/squash-scoreboard-display-esp32/helpers/led_dump && ./run.sh --snapshot"
# LedSweepAnimation invariants (boot sweep + celebration), not golden-diffable -
# see check_v2.sh for the V2 equivalent, both built on sweep_checks.h:
wsl -d Ubuntu-24.04 -- bash -lc "cd /mnt/c/localhost/squash-scoreboard-display-esp32/helpers/led_dump && ./check_v1.sh"
```

The bare form checks the **live `../../src` tree** - that is the default `--root` and
`--defines` now (`-DBOARD_REV=1 -DLEDBAR_LAMBDA -DCONFIG_IDF_TARGET_ESP32S2=1
-DCURRENT_LEDDISPLAY_API`), because that combination is the one that actually compiles and
means something: a mismatch here is a real behaviour change in `src/Display/LedDisplay/`.
`v1_snapshot` predates both `LEDBAR_LAMBDA` (`setLedBarState` took an array, not a lambda)
and `CURRENT_LEDDISPLAY_API` (`resetHistoryBar()`/single-arg `startCelebration()`), so
`--snapshot` switches to `v1_snapshot` **and** drops to no defines at all - a bare
`--root v1_snapshot` on its own still carries the live `--defines` default and will not
compile; use `--snapshot`, or pass `--defines ""` alongside an explicit `--root v1_snapshot`.

With no `--golden` flag, the script exits non-zero if the output differs from the goldens for
the current root (see "Two golden baselines" above - which golden pair depends on the root).
`--golden` regenerates the pair for the current root, but against a non-`v1_snapshot` root it
additionally refuses unless `--rebaseline` is also passed - see "Re-baselining the live
goldens" above. `--root <path> --defines "..."` (or the `DUMP_ROOT` / `DUMP_DEFINES` env vars)
override either independently, e.g. to point at a different tree or add extra defines.

## V1 build fingerprint (unmodified tree, commit 902928f)

- `pio run -e lolin_s2_mini`: RAM 17.0% (55,768 B), Flash 65.1% (852,682 B of 1,310,720);
  `firmware.bin` 853,040 bytes
- `partitions.bin` sha256 `148b959cbff1c38aa8e1d5c0ba9d612c54997b945e56a63f41223eef650653a1`
