# LED dump harness

Host-side golden dump of every LED pixel the V1 display layer writes. It proves that
refactors of `LedGlyph` / `LedDisplay` / `LedBar` stay byte-identical for V1
without flashing hardware.

- `v1_snapshot/` - `Color.h` and `Display/LedDisplay/{LedDisplay,LedGlyph,LedBar,LedBarMode}.h`
  taken with `git show` at commit 902928f. The bar renderers are not included:
  `LedDisplay.h` does not use them and they pull in Tournament/UserProfile code.
- `shim/` - minimal `Arduino.h` / `FastLED.h` (`CRGB`, fake `millis()`, and
  FastLED 3.9.16's exact `sin8_C` and `nscale8x3` with `FASTLED_SCALE8_FIXED=1`).
- `dump.cpp` - includes `Display/LedDisplay/LedDisplay.h` from the include root.
  - `golden_v1_glyphs.txt`: glyph 0..36 x GlyphId 0..6 x blink {off, on-visible
    tick 300, on-dark tick 100}. Pixels are detected against two sentinel fills,
    so writes of any colour, black included, are captured.
  - `golden_v1_frames.txt`: a 25-step script over every public `LedDisplay`
    method. Each step clears the buffer and renders, like `display()`, then lists
    the non-black pixels.
  - Define `LEDBAR_LAMBDA` once `setLedBarState` takes a lambda.

## Run (WSL, g++ 13, `-std=gnu++11 -Wall`)

```sh
wsl -d Ubuntu-24.04 -- bash -lc "cd /mnt/c/localhost/squash-scoreboard-display-esp32/helpers/led_dump && ./run.sh"
# harness self-test only (frozen v1_snapshot vs its own goldens - proves the harness
# works, proves NOTHING about the current src/ tree):
wsl -d Ubuntu-24.04 -- bash -lc "cd /mnt/c/localhost/squash-scoreboard-display-esp32/helpers/led_dump && ./run.sh --snapshot"
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

With no `--golden` flag, the script exits non-zero if the output differs from the goldens.
`--golden` regenerates them and refuses to run unless the root is `v1_snapshot` (pass
`--root v1_snapshot` or `--snapshot`) - the goldens are that frozen snapshot's baseline, not
the live tree's, and writing them from `../../src` would silently turn the check back into a
tautology. `--root <path> --defines "..."` (or the `DUMP_ROOT` / `DUMP_DEFINES` env vars)
override either independently, e.g. to point at a different tree or add extra defines.

## V1 build fingerprint (unmodified tree, commit 902928f)

- `pio run -e lolin_s2_mini`: RAM 17.0% (55,768 B), Flash 65.1% (852,682 B of 1,310,720);
  `firmware.bin` 853,040 bytes
- `partitions.bin` sha256 `148b959cbff1c38aa8e1d5c0ba9d612c54997b945e56a63f41223eef650653a1`
