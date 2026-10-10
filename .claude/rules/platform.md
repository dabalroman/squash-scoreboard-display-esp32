---
paths:
  - "platformio.ini"
  - "helpers/rmtdiag_patch.py"
  - "src/Display/LedDisplay/*.h"
  - "src/RemoteDevelopmentService/**"
---

# Platform pin, FastLED and the RMT

Read with the root CLAUDE.md's READ FIRST section; this is its detail.

Cause: FastLED silently picks its RMT driver from the IDF version, with no way to
override it.

| | RMT4 (core 2.0.17, works) | RMT5 (core 3.x, breaks) |
|---|---|---|
| ISR owner | FastLED's own, tight, IRAM | generic IDF `rmt_tx` + encoder callback |
| Refill buffer | default `FASTLED_RMT_MEM_BLOCKS 2` = **128 symbols** (now 4, see below) | `mem_block_symbols = 0` -> default **64** |
| Refill slack (S3, 4 blocks) | half buffer; bail at **60 us** late, stale replay at **120 us** | half the buffer, heavier refill |

Half the buffer and a heavier refill path, against this board's four RF-receiver
GPIO ISRs (which fire in *bursts* from RF noise - see Input below) plus I2C.
Refills land late, the RMT starves mid-frame, and the WS2812 stream shifts, so
every downstream LED receives its neighbour's bytes.

On core 3.x, `-DFASTLED_RMT5=0` (FastLED's RMT4 path on IDF 5.x) is the first thing
#74 must try. RMT5 itself has no tuning knobs: `memory_block_symbols = with_dma ? 1024 : 0` is
  hardcoded in `rmt_5/strip_rmt.cpp`, and `.with_dma = false` is hardcoded too.

**FastLED is also pinned at 3.9.16.** Neither newer release builds here: 3.10.4
fails in `fl/gfx/crgb.h`, 3.10.3 fails on `fl::fl_map`, and installing 3.10.3 from
the registry crashes PlatformIO's library manager. The registry is stale at 3.10.3
anyway; upstream GitHub has 3.10.4.

**WiFi LED glitches (task #58; re-measure on V1 is task #72).** With WiFi on, 1-2 LEDs
can flash a wrong colour. WiFi runs on core 0, loop()/FastLED/the RMT ISR on core 1. Mechanism, from `rmt_4/idf4_rmt_impl.cpp`: the refill ISR has
one half-buffer of slack (`PULSES_PER_FILL x 1.25 us`). More than 50 % late ->
`fillNext` **bails** (frame cut short; the rest of the chain keeps the identical
previous frame, invisible). More than 100 % late -> the RMT **replays the stale half**
(8 bytes = 2.67 LEDs, channels rotate) - the visible glitch. Thresholds (bail / stale):
30 / 60 us at 2 blocks, 60 / 120 us at 4.

- The worst case is a fallback leaving **STA enabled**: the core's auto-reconnect re-runs
  `WiFi.begin()` after every NO_AP_FOUND, and each channel-hopping scan stalls the refill
  by hundreds of us. No RMT buffer covers that; only stopping STA does.
- **Fixed (task #64):** every AP path goes through `RemoteDevelopmentService::startApOnly()`
  (STA off, `WIFI_AP`, `softAP`), and `-DFASTLED_RMT_MEM_BLOCKS=4` is in both firmware
  envs' `build_flags`. It must stay a build flag - FastLED's own `.cpp` reads it.
  A fallback AP stays AP-only until reboot: no late STA connect, no periodic retry.
  Signal level (3.3 V data, no level shifter) is not the
  cause: glitches tracked the stale counter exactly.

## The `v1_ota_rmtdiag` env

- `v1_ota_rmtdiag` = `v1_ota` + `helpers/rmtdiag_patch.py`, which patches **only that env's own**
  FastLED copy (`.pio/libdeps/v1_ota_rmtdiag`) with RMT refill counters (`extern "C" rmtdiag_*`:
  fills, bails, stale replays, max lateness in cycles, a 10-bucket histogram). The instrument for
  #58/#72/#74-style measurements; the code that logs the counters is per measurement and is not
  kept. Never leave it as V1's running image - flash `v1_ota` back afterwards.

**Toolchain gotcha, only relevant if this repo is ever moved off 6.13.0:** the
`pio` on PATH runs under Python 3.10, while `~/.platformio/penv` is Python 3.14.
The pioarduino platform's builder imports `littlefs`, whose compiled extension is
`cp314`, so PATH `pio` dies with `ImportError: cannot import name 'lfs'`. Use
`& "$HOME/.platformio/penv/Scripts/pio.exe"` in that case. **This does not
affect the current 6.13.0 setup** - PATH `pio` is verified working here.

## Replacing FastLED

- **FastLED is a pinned, load-bearing dependency (3.9.16).** See *READ FIRST*.
  The project uses only `addLeds`, `show`, `clear`, `setBrightness`,
  `setMaxRefreshRate` and `CRGB` - **none** of its colour engine (no `CHSV`,
  palettes, blends). It is acting purely as a WS2812 shift-out driver, so replacing
  it is ~5 call sites plus a `CRGB` shim. Options are evaluated in the playground
  repo's CLAUDE.md; the leading candidate is NeoPixelBus with a **DMA (I2S)**
  method, because naming the peripheral explicitly avoids the silent
  driver-swap-by-IDF-version that caused the core 3.x regression. Not yet decided -
  only forced if V2's LED output misbehaves (V2 shares the pinned platform, so it
  also runs RMT4). `FASTLED_USES_ESP32S3_I2S` is **not** a fallback on the pinned
  core: the build warns that `esp_memory_utils.h` is missing and the parallel
  clockless I2S driver is unavailable.
