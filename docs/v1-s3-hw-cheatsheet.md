# V1 on ESP32-S3-DevKitC-1 N16R8 - soldering cheatsheet

Source of truth for pins: `src/Board.h` (shared pin block). Procedure and background:
`docs/v1-s3-first-flash.md`, `docs/v1-s3-mcu-swap.md`.

## 1. Pinout

| Wire from | To DevKitC-1 | Old S2 Mini pin | Notes |
|---|---|---|---|
| RF receiver out **A** (prev) | **GPIO 8** | 14 | order is **reversed** vs the S2 wiring |
| RF receiver out **B** (next) | **GPIO 10** | 13 | |
| RF receiver out **C** (undo / long = back) | **GPIO 13** | 10 | |
| RF receiver out **D** (enter) | **GPIO 14** | 8 | |
| WS2812B data in (first LED) | **GPIO 18** | 18 | unchanged, 3.3 V data as before |
| Buzzer + (active high) | **GPIO 3** | 3 | unchanged, driven directly |
| OLED SDA | **GPIO 4** | 33 | 33-37 are PSRAM on the S3 - never reuse the old pins |
| OLED SCL | **GPIO 5** | 34 | |
| Battery divider midpoint | **GPIO 6** | - (new) | BAT+ -10k- GPIO 6 -10k- GND |
| OLED VCC, RF receiver VCC (if 3.3 V) | **3V3** | 3V3 | |
| 5 V rail from the boost converter | **5V** pin | VBUS/5V | powers the board's LDO |
| All grounds | **GND** | GND | one common ground: board, LEDs, receiver, OLED, divider, buzzer |

**Leave unconnected:** GPIO 9, 11, 12, 15, 16, 17 (V2's e-paper), GPIO 0, 45, 46
(strapping), GPIO 19/20 (USB), GPIO 26-37 (flash / octal PSRAM).

### Where the pins are (DevKitC-1, header J1)

Every pin above is on **J1**, the header on the same side as the `3V3` / `RST` pins.
Order from the USB end, per Espressif's DevKitC-1 pin layout. **Check it against the
silkscreen before soldering**; board revisions differ in details.

| J1 pos | Pin | Use |
|---|---|---|
| 1 | 3V3 | OLED / receiver VCC |
| 2 | 3V3 | |
| 3 | RST | - |
| 4 | GPIO 4 | **OLED SDA** |
| 5 | GPIO 5 | **OLED SCL** |
| 6 | GPIO 6 | **Battery divider** |
| 7 | GPIO 7 | - |
| 8-10 | GPIO 15, 16, 17 | - (e-paper on V2) |
| 11 | GPIO 18 | **LED data** |
| 12 | GPIO 8 | **RF A** |
| 13 | GPIO 3 | **Buzzer** |
| 14 | GPIO 46 | - (strapping) |
| 15 | GPIO 9 | - (e-paper on V2) |
| 16 | GPIO 10 | **RF B** |
| 17-18 | GPIO 11, 12 | - (e-paper on V2) |
| 19 | GPIO 13 | **RF C** |
| 20 | GPIO 14 | **RF D** |
| 21 | 5V | **5 V rail in** |
| 22 | GND | **GND** |

## 2. Checklist

### Before soldering (bench, USB still reachable)
- [x] Bootstrap flash over USB, WiFi + Dev Mode seeded (2026-09-30)
- [x] V2 image refused over OTA (HTTP 400)
- [x] Two-cycle OTA passed (0.6.141, 0.6.142)
- [x] OTA address: the board's IP is in the gitignored `platformio.local.ini`
      (`[env:v1_ota] upload_port`); if DHCP moves it, update it there or pass `--upload-port`
- [ ] Save the old unit's roster if it is still running: `curl http://<old-v1-ip>/api/roster`

### Electrical, before power
- [ ] RF receiver data outputs are **<= 3.3 V** HIGH. S3 GPIOs are not 5 V tolerant:
      if the receiver runs on 5 V, keep the level shifting/divider the S2 wiring had
- [ ] Battery divider: two 10k, midpoint to GPIO 6; with a full pack the pin reads ~2.1 V
- [ ] No short 5V-GND / 3V3-GND (meter, board unpowered)
- [ ] Common ground between the boost converter, the LED strip and the board
- [ ] Nothing soldered to GPIO 26-37 or 0/45/46
- [ ] USB-C ports stay reachable only if you want a recovery path; otherwise sealed

### First power-up (battery, sealed or not)
- [ ] `curl http://<ip>/api/device` answers `{"fw":...,"ssid":...}` within ~40 s
- [ ] Telnet log (port 23) shows `Battery: ... V` in the 3.0-4.2 V range, **not**
      `(implausible, ignored)` - if implausible, the divider is not reaching GPIO 6
- [ ] Measure the pack with a meter; if the logged volts differ, retune
      `Board::BATTERY_FACTOR` for V1 in `src/Board.h` (starts at 2.027)
- [ ] Boot sweep plays over all 112 LEDs (a stuck or wrong-colour first LED = data wire)
- [ ] OLED shows the mode menu, with battery % top right

### Function
- [ ] Remote: **A = prev, B = next, C = undo, D = enter**, long C (2 s) = back.
      A swapped pair means two RF wires are swapped - fix the wiring, not the code
- [ ] Buzzer beeps on a press; silent through a restart (CONFIG -> RESTART)
- [ ] A squash game: score, colon, history bar, celebration
- [ ] PROFILE: phone joins the setup AP, re-create the profiles
- [ ] One more OTA of a normal build (`pio run -t upload -e v1_ota`) - the sealed unit
      still updates. Before every V1 upload:
      `python helpers/check_firmware_image.py .pio/build/v1_ota/firmware.bin v1` -> `verdict Ok`

### Later (task #72)
- [ ] Re-measure the WiFi LED glitch counters in the three WiFi states
      (`docs/v1-s3-first-flash.md`, section 5)
