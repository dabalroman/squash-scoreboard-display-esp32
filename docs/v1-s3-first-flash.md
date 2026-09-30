# V1 on ESP32-S3: first flash, seal, re-setup

Runbook for tasks #71 (first flash + re-setup) and #72 (WiFi glitch re-measure).
Branch `v1-mcu`. Background: `docs/v1-s3-mcu-swap.md`.

The swapped V1 gets USB exactly once, as a **naked DevKitC-1** (nothing soldered).
After that it is sealed and OTA is the only way in. Everything that proves the
board can take updates must therefore happen on the naked board.

**Status 2026-09-30:** steps 1 and 2 done on the naked board. Bootstrap seeded Dev Mode + WiFi, the V2 image was refused
with HTTP 400, and two OTA cycles landed (0.6.141, 0.6.142). Next: step 3.

Serial gotcha on the naked board: the S3's USB-Serial/JTAG maps DTR/RTS to BOOT/EN, so
opening or closing a terminal can leave the chip in `waiting for download`. Reset it with
`esptool.py --chip esp32s3 -p COMx --after hard_reset read_mac`. The log only appears
while the terminal holds DTR (HW CDC's `Serial` is false otherwise).

## 0. Before the swap (old S2 unit, still running)

- Save the roster: `curl http://<old-v1-ip>/api/roster > logs/roster-before-swap.json`
  (gated: open PROFILE on the device first, or have Dev Mode on and STA connected).
- Note the house WiFi SSID and password.

## 1. Naked board: bootstrap flash over USB

A blank chip boots with Dev Mode off (no WiFi, no OTA), and with no remote wired
there is no way to switch it on. The `v1_bootstrap` env bakes the house WiFi
credentials into this one image; they are used only when NVS holds no valid
settings, and they are seeded with Dev Mode ON.

```powershell
$env:SCOREBOARD_WIFI_SSID = "<house ssid>"
$env:SCOREBOARD_WIFI_PASSWORD = "<house password>"
pio run -t upload -e v1_bootstrap --upload-port COMx     # either USB-C port
Remove-Item Env:SCOREBOARD_WIFI_SSID, Env:SCOREBOARD_WIFI_PASSWORD
pio device monitor -p COMx -b 115200                     # the log prints the IP
```

- The build fails if either variable is missing. The credentials never reach git,
  `platformio.ini`, or the normal `v1` / `v1_ota` images.
- Floating inputs are harmless: the RF pins have internal pull-downs, a missing
  OLED only logs, the LED data pin drives nothing.
- The battery ADC floats on a naked board, so a low-battery overlay may fire and
  cap brightness. Ignore it until the divider is soldered.

## 2. Naked board: prove it can take updates (two-cycle OTA)

The new MAC gets a new address from DHCP (the USB log prints it). Put it in the gitignored
`platformio.local.ini` (`[env:v1_ota] upload_port`, see `platformio.local.ini.example`) or
pass it explicitly:

```powershell
curl http://<ip>/api/device                                  # {"fw":..., "ssid":...}
pio run -t upload -e v1_ota --upload-port <ip>               # cycle 1: normal v1 image
curl http://<ip>/api/device                                  # fw == version.txt
pio run -t upload -e v1_ota --upload-port <ip>               # cycle 2: proves it still receives
curl http://<ip>/api/device
```

- Poll `/api/device` for ~20-40 s after each upload; do not assume failure early.
- Before every V1 upload: `python helpers/check_firmware_image.py .pio/build/v1_ota/firmware.bin v1`
  must print `verdict Ok` with the board marker (V1 refuses unmarked images).
- Check the wrong-board guard once: uploading `.pio/build/v2/firmware.bin` to V1
  must be refused (HTTP 400, V1 keeps running) - the host helper predicts `WrongBoard`:
  `curl --fail -F "update=@.pio/build/v2/firmware.bin" http://<ip>/update` (never add `?force=1`).
- NVS survives OTA, so the bootstrapped WiFi settings stay; the normal `v1` image
  contains no credentials of its own.

**Do not solder until both cycles pass.** Cycle 2 is the one that matters.

## 3. Solder in and seal

Wire to the shared pinout (`src/Board.h`):

| Function | GPIO |
|---|---|
| RF A / B / C / D (prev / next / undo / enter) | 8 / 10 / 13 / 14 |
| WS2812B data | 18 |
| Buzzer | 3 |
| OLED SDA / SCL | 4 / 5 |
| Battery ADC (10k/10k divider) | 6 |
| E-paper pins 9, 11, 12, 15, 16, 17 | unconnected on V1 |

## 4. Sealed unit: bench checks (OTA only from here)

- Buttons: A prev, B next, C undo (long C = back), D enter.
- Boot sweep, a squash game, the history bar, a celebration.
- OLED: top rows usable again (V1 `DEAD_TOP_ROWS` 0); battery percent in the mode
  selector and CONFIG.
- Battery: compare the logged volts (every 10 s on telnet / port 23) with a meter
  across the pack; if off, retune V1's `FACTOR` in `Board.h` (starts at V2's 2.027).
- Re-create the profiles in PROFILE from `logs/roster-before-swap.json`.
- One more OTA of a normal build to confirm the sealed unit still updates.

## 5. #72: WiFi LED glitch re-measure

The S2 glitch came from a WiFi scan holding its single core. On the S3, WiFi runs
on core 0 and `loop()`/FastLED/the RMT ISR on core 1, so it is expected to go away.
Measure, don't assume: rebuild #58's temporary probe (FastLED `fillNext()` counters:
fills / bails / stale / max lateness / histogram, reported every 30 s on telnet;
method in task #58's plan) and soak in the three states:

1. STA connected + AP, HTTP load on `/api/device` and `/settings`.
2. Failing STA + AP (the fallback that glitched on the S2).
3. AP only.

Pass: zero stale replays in every state. S3 @4 blocks thresholds: bail 60 us,
stale 120 us. Keep `FASTLED_RMT_MEM_BLOCKS=4` and the AP-only fallback whatever
the result; only CLAUDE.md's justification changes. Remove the probe afterwards
and two-cycle OTA back to production.
