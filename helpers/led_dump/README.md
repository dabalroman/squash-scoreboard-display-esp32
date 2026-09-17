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
# against the live tree:
wsl -d Ubuntu-24.04 -- bash -lc "cd /mnt/c/localhost/squash-scoreboard-display-esp32/helpers/led_dump && ./run.sh --root ../../src --defines '-DBOARD_REV=1 -DLEDBAR_LAMBDA'"
```

With no `--golden` flag, the script exits non-zero if the output differs from the
goldens. `--golden` regenerates them; only use it with the default `v1_snapshot` root.
Extra defines such as `-DCONFIG_IDF_TARGET_ESP32S2=1` are passed straight through.
`DUMP_ROOT` / `DUMP_DEFINES` env vars work too.

## V1 build fingerprint (unmodified tree, commit 902928f)

- `pio run -e lolin_s2_mini`: RAM 17.0% (55,768 B), Flash 65.1% (852,682 B of 1,310,720);
  `firmware.bin` 853,040 bytes
- `partitions.bin` sha256 `148b959cbff1c38aa8e1d5c0ba9d612c54997b945e56a63f41223eef650653a1`
