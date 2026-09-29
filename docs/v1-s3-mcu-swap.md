# V1 MCU swap: Wemos S2 Mini -> ESP32-S3-DevKitC-1

Task #67 (scope `v1-mcu`). Research only, written 2026-09-29 against `c0825ed`.

**Premise.** V1 keeps its hardware - 112-LED 7-segment front, 24-px history bar,
rear OLED, 433 MHz receiver, buzzer, 1S2P pack - and swaps only the MCU module to
an ESP32-S3-DevKitC-1, plus a **new battery voltage divider**. S2 support is then
dropped from the tree. Both boards become S3; they differ only in the front panel,
the e-paper and the pin map.

Every item below carries an ID (`M-nn`) so follow-up tasks can reference it, a
priority, and whether it is **required** for the swap to work (`REQ`), a
**simplification** made possible by dropping the S2 (`SIMP`), or a separate
**opportunity** that the swap unblocks (`OPP`).

Decisions made during refinement are in section 12; follow-up tasks in section 13.

---

## 0. Summary

| Area | Headline |
|---|---|
| Build | `lolin_s2_mini*` envs replaced by an S3 V1 env; board/PSRAM/USB overrides shared with V2 in one `[s3]` section. |
| `Board.h` | `BOARD_REV` stops meaning "which MCU" and means "which front panel + pin map". S3 facts (`pinIsSafe`, the static_asserts) move out of the V2 branch. |
| Pins | **Unified to V2's pinout** (decided): OLED 4/5, RF 8/10/13/14, LED 18, buzzer 3, battery 6. All pin constants become one shared block in `Board.h`. |
| Battery | `BatterySensor` is gated `BOARD_REV == 2`; V1 needs it on, its own `FACTOR` calibration, and an **OLED readout in the menus** (V1 has no e-paper). |
| OTA safety | `FirmwareImageCheck` identifies the board **only by chip id**. With both boards on S3 a V2 image passes on V1. The guard needs a board marker. |
| Migration | Fresh MCU = empty NVS; start fresh (re-enter WiFi via Dev Mode + `/settings`, re-create profiles). First flash is over USB while the unit is open; OTA-only afterwards. |
| Toolchain | The S2-only reason for the platform pin disappears. Unpinning (core 3.x, GCC 13, C++17, FastLED 3.10) becomes a **separate device-gated study**, not part of the swap. |
| WiFi LED glitches | Dual core puts WiFi on core 0 and the RMT ISR on core 1; the #58 failure mechanism (a scan holding *the one* core) likely disappears. Re-measure, do not assume. |
| BLE | V1 gains Bluetooth - the Garmin remote (#66) stops being V2-only. |

---

## 1. Build environments - `platformio.ini`

| ID | Pri | Kind | Item |
|---|---|---|---|
| M-01 | high | REQ | Replace `[env:lolin_s2_mini]` (`board = lolin_s2_mini`, COM4/COM3) with an S3 env for V1. Board is `esp32-s3-devkitc-1` plus the module's overrides (see M-03). Envs become `v1` / `v1_ota` / `v2` (D5). |
| M-02 | high | REQ | `[env:lolin_s2_mini_ota]`: keep the `curl --fail -F update=@...` upload command; **the IP changes** - a new MCU has a new MAC, so the DHCP reservation for `192.168.0.129` must be moved to the new MAC (router side), or update `upload_port`. |
| M-03 | med | SIMP | Factor a shared `[s3]` section: `board = esp32-s3-devkitc-1`, `board_build.arduino.memory_type`, `flash_mode`, `partitions`, `board_upload.flash_size/maximum_size`, `-DBOARD_HAS_PSRAM`, `-DARDUINO_USB_CDC_ON_BOOT=1`, `-DARDUINO_USB_MODE=1`, `-DFASTLED_RMT_MEM_BLOCKS=4`, `-DLANG_PL`. V1 and V2 envs then differ only in `-DBOARD_REV`, `lib_deps` (GxEPD2) and ports. V1 gets the **same N16R8** as V2 (D1), so the entire board block is shared. |
| M-04 | med | REQ | Partition table. S2 Mini runs `default.csv` (4 MB, 2 x 1280 KB app). V1-S3 takes `default_16MB.csv` (2 x 6.25 MB app), same as V2 (D1). **All three keep NVS at `0x9000`, size `0x5000`** (verified in the framework's `tools/partitions/`), which is what makes M-40 possible. The CLAUDE.md "partitions.bin old vs new identical" OTA check then compares V1-S3 builds against each other, not against the S2. |
| M-05 | low | SIMP | Native test env `native_v1`: `-DCONFIG_IDF_TARGET_ESP32S2=1` -> `-DCONFIG_IDF_TARGET_ESP32S3=1`. Could move to `[native]` since both boards are S3. |
| M-06 | low | SIMP | Header comments in `[env]` (lines 16-24) explain the pin in S2 terms ("no RMT DMA on S2", "unimplemented for S2 on IDF 5.x"). Rewrite for an S3-only fleet - or delete once M-60 decides. |
| M-07 | low | REQ | `ARDUINO_USB_MODE`: the S2 Mini board file forces `USB_MODE=0` (TinyUSB) + `CDC_ON_BOOT=1` + 1200-baud touch upload. On the DevKitC-1 use V2's `USB_MODE=1` (HW CDC/JTAG): esptool auto-reset works, no 1200-baud touch, no TinyUSB quirks (see `docs/garmin-remote-poc.md` gotchas). |

## 2. `src/Board.h`

| ID | Pri | Kind | Item |
|---|---|---|---|
| M-10 | high | REQ | `BOARD_REV == 1` asserts `CONFIG_IDF_TARGET_ESP32S2` (line 15). Change to `ESP32S3`; better, hoist one `#if !CONFIG_IDF_TARGET_ESP32S3 #error` above both branches - the MCU check no longer depends on the revision. |
| M-11 | high | REQ | V1 adopts V2's pin map (section 3): OLED 4/5, RF 8/10/13/14, battery ADC 6. |
| M-12 | med | SIMP | Move `pinIsSafe()` and the pin `static_assert`s out of the V2 branch into shared code after both branches - they are S3/module facts, and V1 needs them most (M-20). E-ink asserts stay V2-only. |
| M-13 | low | SIMP | `NAME = "V1 ESP32-S2"` -> e.g. `"V1 ESP32-S3"` (appears in the boot log line, `main.cpp:332`). The `#error` text on line 13 ("S2 Mini") too. |
| M-14 | med | SIMP | `SERIAL_LOG = false` on V1 was because the S2's TinyUSB CDC was not wanted. With HW CDC (M-07) V1 can mirror logs to USB like V2 -> `true`, or drop the constant entirely (both boards true) and delete the branch in `main.cpp:252` / `LoggerHelper.h:17`. |
| M-15 | med | SIMP | Board facts that are now **capabilities**, not revisions: add `HAS_BATTERY` (both true after the divider), keep `HAS_PLAYER_SETUP` (already both true - candidate for deletion together with its `enabled` column use in `ModeSwitchingView`), consider `HAS_EINK`. Wrapper headers then test the capability, not `BOARD_REV == 2` (M-30). |
| M-16 | low | - | Unchanged per revision: `OLED_ROTATION = 2`, `LED_COUNT = 112`, `GLOBAL_BRIGHTNESS_SCALE = 0.8` (a power limit, not an MCU fact). |

## 3. Pin map for V1 on the S3 - unified to V2 (decided 2026-09-29)

**Decision: V1 is rewired to V2's pinout.** Every pin constant becomes shared; the
S3 constraints (GPIO 26-32 SPI flash, 33-37 octal PSRAM on R8/N16R8, 19/20 USB,
0/3/45/46 strapping, ADC1 = GPIO 1-10) are already satisfied by V2's map.

| Function | V1 today (S2) | V1 on S3 = V2 | Note |
|---|---|---|---|
| RF A / B / C / D | 14 / 13 / 10 / 8 | **8 / 10 / 13 / 14** | V2's order is "reversed" relative to V1's constants. With one shared table, V1's receiver outputs must be wired so that button A lands on GPIO 8 ... D on 14, exactly as V2. Verify each button on the bench (A prev, B next, C undo, D enter) - V2's order was also only confirmed on the device. |
| LED data | 18 | 18 | unchanged |
| Buzzer | 3 | 3 | unchanged. Strapping pin (JTAG source), harmless unless the efuse is burnt; V2 already runs it. V1 drives the buzzer directly, V2 through a MOSFET - no code difference. |
| OLED SDA / SCL | 33 / 34 | **4 / 5** | resolves the octal-PSRAM conflict |
| Battery ADC | - | **6** (ADC1_CH5) | the new divider |
| E-paper SCK/MOSI/CS/DC/RST/BUSY | - | 12/11/9/15/16/17 | **left unconnected on V1**; the constants can be shared, only `EInkDisplay` stays a stub on V1 |

| ID | Pri | Kind | Item |
|---|---|---|---|
| M-20 | high | SIMP | `Board.h`: move `OLED_SDA/SCL`, `RF_D0..D3`, `LED_DATA`, `BUZZER`, `BATTERY_ADC` (and optionally the `EINK_*` pins) into **one shared `namespace Board` block** outside the `BOARD_REV` branches, with `pinIsSafe()` and every pin `static_assert` (M-12). Per-revision blocks shrink to panel/power facts only: `NAME`, `LED_COUNT`, V2's `DIGIT_BASE`/`PIXELS_PER_MODULE`/`MODULE_COUNT`, `OLED_ROTATION` (V1 2, V2 0 - mounting, not pinout), `GLOBAL_BRIGHTNESS_SCALE` (0.8 / 1.0), battery `FACTOR` (M-31), `HAS_EINK`. |
| M-21 | med | SIMP | Delete the per-pin `// V1: 33` / "REVERSED vs V1" comments in V2's block - there is no V1 pin map any more. CLAUDE.md's Pinout table collapses to one column (plus "V1: e-paper pins unconnected"). |
| M-22 | med | REQ | Physical: the DevKitC-1 is ~63 x 25 mm vs the S2 Mini's 34 x 26 mm, with two USB-C ports on the short edge. Mechanical fit is outside the code; USB stays enclosed (D2). |

## 4. Battery sense on V1

Today the whole chain exists but is dark on V1: `BatterySensor` is a stub
(`BatterySensor.h:14`, `#if BOARD_REV == 2`), so `BatteryMonitor::available()` is
false and nothing downstream runs.

| ID | Pri | Kind | Item |
|---|---|---|---|
| M-30 | high | REQ | `BatterySensor.h`: gate on `Board::HAS_BATTERY` (or on `BOARD_REV` both-true) instead of `BOARD_REV == 2`; the stub branch can be **deleted** once both boards have a divider. `BATTERY_ADC` then exists on both boards. |
| M-31 | high | REQ | `BatterySensorConfig::FACTOR = 2.027` is V2's divider x ADC calibration, measured against a meter. V1's divider needs its **own measured factor** -> move `FACTOR` into `Board.h` per revision (it is a board fact). Same recommended resistor values (10k/10k) keep the 11 dB attenuation choice valid. |
| M-32 | high | REQ | **V1 shows the percent on the OLED, in the mode selector and the CONFIG menu only** (D3). Percent is shown today only on the e-paper footers (`ModeSwitchingView.h:115`, `ConfigView.h:205`). The OLED readout was deleted for V2's dead rows; the design point for the follow-up: `BackDisplay::DEAD_TOP_ROWS` (11) is **global on purpose** (no `BOARD_REV` split), so V1's healthy panel also loses its top rows and the 3-row menu has no free line. The readout must either fit the existing layout (e.g. the title line, right-aligned) or `DEAD_TOP_ROWS` becomes a per-board fact in `Board.h` - a reversal of an earlier user call, so decide it in that task's refinement. Gate on `!einkDisplay.available() && batteryMonitor.available()`, never on `BOARD_REV`, so V2 keeps its OLED clean. Low-battery overlay + brightness cap come for free (M-33). |
| M-33 | med | - | What turns on automatically, no code: `BatteryMonitor` (percent curve, 10 %/60 s low latch, 15 % hysteresis), the low-battery `Overlay` (renders on LEDs + OLED, so V1 sees it), the 5-min warning cooldown, `setBrightnessCap(31)` while low, the 10 s battery log line (`main.cpp:387`). All board-agnostic already. |
| M-34 | med | OPP | V1 draws up to 112 LEDs at 0.8 scale; V2's sag research (#42, "80 % at 2/8 brightness, 45 % at max") will hit V1 harder. #42's calibration then applies to both boards; revisit its assumptions once V1 reports. |
| M-35 | low | - | Tests: `BatteryMonitor` has host coverage independent of the board; add a `test_v1_*` assertion only if V1 gets a board-specific factor/curve. |

## 5. OTA, firmware image guard and flashing

| ID | Pri | Kind | Item |
|---|---|---|---|
| M-40 | high | REQ | **Start fresh (D4) - no NVS copy.** A blank S3 boots with `enableDevMode = 0` (WiFi, AP, OTA, telnet off), no WiFi credentials, factory roster. Before the swap: save `GET /api/roster` (reference for retyping names/colours) and note the WiFi SSID. After the first USB flash: CONFIG -> Dev Mode ON -> `[RESTART]` -> STA fails on empty credentials -> AP-only fallback -> phone on the AP -> `/settings` Wi-Fi form -> restart -> house network; then re-create the profiles in PROFILE. Move the router's DHCP reservation to the new MAC (M-02). (For the record: an esptool copy of `0x9000`/`0x5000` would have worked - same offset and IDF in every table - but was declined.) |
| M-41 | high | REQ | **`FirmwareImageCheck` loses its main case.** `src/FirmwareImageCheck.h` rejects the other board's `firmware.bin` by `chip_id` (0x0002 vs 0x0009). Both become 0x0009, so uploading `esp32s3_devkitc/firmware.bin` to V1 is accepted and boots the 9-segment layout on a 7-segment panel. Not a brick (the V2 image still has OTA), but the guard is silently void. Needs a **board marker** inside the image: e.g. a `const` struct with a magic + `BOARD_REV` placed in a fixed section (`.rodata_custom_desc` next to `esp_app_desc`, or a searchable 16-byte sentinel within the first N KB). The check runs in the *receiving* firmware, so V2 is covered once it runs marker-aware firmware - that comes with its next normal update, no separate rollout. The header comment's premise "the other board's firmware.bin is the realistic wrong file ... chip id" must be rewritten. Also `helpers/check_firmware_image.py` (`s2`/`s3` argument, chip-id table). |
| M-42 | med | SIMP | `helpers/check_firmware_image.py`: drop the `s2` choice; once M-41 lands, check the board marker instead of (or on top of) the chip id. |
| M-43 | high | REQ | **First flash is USB** (blank chip). After that, OTA stays the normal path if the unit is sealed again. Keep the two-cycle OTA test rule for the first S3 image on V1. |
| M-44 | med | - | CLAUDE.md READ FIRST #2 ("V1: OTA is the only practical way to flash. USB needs disassembly") **stays** - V1 remains sealed (D2). Only its wording changes (DevKitC-1, 16 MB table, the S3 app-size figure). The partitions.bin identity check applies from the second S3 image on; the first one goes in over USB while the unit is open. |
| M-45 | low | SIMP | App size: V1 is 913 KB in a 1280 KB slot (69.7 %). With 8/16 MB tables the size check in READ FIRST becomes a non-issue (still worth one line). V2 is 963 KB today, so a V1-S3 image lands in the same range. |
| M-46 | low | - | Comments naming the S2 env as the scripted OTA path: `RemoteDevelopmentService.cpp:77`, `PlayerSetupWebUi.cpp:88`, `web/update.html:12` - rename with M-01. |

## 6. LED output (FastLED / RMT) and the WiFi glitch work

| ID | Pri | Kind | Item |
|---|---|---|---|
| M-50 | high | REQ | **Re-measure the #58 glitch on V1-S3** before declaring anything. The S2 mechanism was: one core, a WiFi channel scan holds it 240-380 us, the RMT refill ISR misses its half-buffer slack, stale bytes replay. On the S3 the Arduino loop (and therefore `FastLED.show()` and the RMT ISR it allocates) runs on core 1 while WiFi runs on core 0, so that stall should not reach the ISR. Use the same stale/bail counters as #58 in the three WiFi states (STA connected + load, failing STA + AP, AP only). |
| M-51 | med | SIMP | `-DFASTLED_RMT_MEM_BLOCKS=4`: keep for the swap (S3 @4 blocks = 60 / 120 us slack, already V2's setting). Only drop it if M-50 shows zero bails at 2 blocks - and even then it is harmless, so low value. |
| M-52 | low | SIMP | The AP-only fallback (`RemoteDevelopmentService::startApOnly()`, #64) was motivated by the S2 glitch, but it is also the right behaviour (a retrying STA is pointless load). **Keep it**; only the CLAUDE.md justification changes. |
| M-53 | low | SIMP | Integer-only render paths justify themselves with "the S2 has no FPU": `LedBlend.h:9`, `LedBreathingAnimation.h:14`, `LedSmokeAnimation.h:18`, `LedBajgielAnimation.h:21`. The S3 has a single-precision FPU. Keep the code (integer is still faster and deterministic for the goldens) - only reword the comments to the real reason (host/device bit-identical goldens, per-pixel cost). No need to convert anything to float. |
| M-54 | - | - | Unaffected: glyph profiles, `DisplayProfile.h`, `LedDisplay.h`'s bar `#if`s, `LedSlotPositions.h`, `test_sweep`/`test_boot`/`test_v1_goldens`, `helpers/led_positions.py`, `assets/v1_led_map.svg`. All are **panel** facts under `BOARD_REV == 1` and stay exactly as they are. |

## 7. Input, ISRs, restart

| ID | Pri | Kind | Item |
|---|---|---|---|
| M-55 | low | - | RF ISRs (`main.cpp:73-76`, `IRAM_ATTR`, `volatile uint8_t`) attach from `setup()` on core 1, the same core as `loop()`, so the single-core read/modify/clear reasoning in `CR.md` still holds. No change - but `CR.md`'s "ESP32-S2 (Xtensa LX7)" wording becomes stale. |
| M-56 | low | - | `safeRestart()` / `Buzzer::init()` `gpio_hold_en`/`dis`: identical API on S3, already proven on V2. V1's buzzer is driven directly (no MOSFET) - unchanged. |

## 8. Toolchain, platform and libraries (unblocked, **not** part of the swap)

The pin to `espressif32@6.13.0` exists because FastLED on IDF 5.x picks RMT5 and its
RMT4 escape hatch "is not yet implemented for ESP32-S2". That blocker is
S2-specific. After the swap the pin is held only by unknowns about the S3.

| ID | Pri | Kind | Item |
|---|---|---|---|
| M-60 | med | OPP | **Study: both boards on core 3.x.** Questions: does `-DFASTLED_RMT5=0` (RMT4 on IDF 5) compile and run for the S3; if not, does RMT5 with the S3's larger/DMA-capable RMT survive the RF ISR bursts; is FastLED's S3 I2S/LCD parallel driver available on core 3.x (it is not on 2.0.17). Gate: V1 LED test with WiFi in all three states + two-cycle OTA. Already anticipated in memory `v1-mcu-upgrade-acceptable`. |
| M-61 | med | OPP | If M-60 passes: GCC 8.4 -> 13/14, `-std=gnu++17`. Removes `src/Utils.h`'s `std::make_unique` backfill and the "include `Utils.h` everywhere" rule, the "no `static constexpr` array members" ODR rule (inline variables), and the "language is C++11" section in CLAUDE.md. Check the libraries that broke under gnu++14. |
| M-62 | med | OPP | FastLED 3.9.16 pin: 3.10.x failed to build *on core 2.0.17*; retest on core 3.x. The CLAUDE.md NeoPixelBus/I2S replacement discussion becomes S3-only (the "I2S must exist for the S2" caveat goes). |
| M-63 | low | OPP | Core 3.x `WebServer` has route removal (`removeRoute`/`removeHandler`, verify) - 2.0.17 has none (verified: not in the pinned `WebServer.h`). That is why `PlayerSetupWebUi` lives at file scope with a gate; the gate design can stay, but the constraint note changes. |
| M-64 | low | OPP | `BatterySensor` `FACTOR` was calibrated on core 2.0.17's `analogReadMilliVolts`; core 3.x uses a different ADC calibration scheme -> recalibrate both boards under M-60. |
| M-65 | low | SIMP | CLAUDE.md "Toolchain gotcha" (PATH `pio` on Python 3.10 vs penv 3.14 for pioarduino) - still relevant only if M-60 moves to pioarduino. |
| M-66 | - | - | Libraries unchanged by the swap itself: Adafruit SSD1306 / GFX / BusIO, FastLED 3.9.16, GxEPD2 1.6.9 (V2 only). |

## 9. New capabilities on V1

| ID | Pri | Kind | Item |
|---|---|---|---|
| M-70 | low | OPP | **BLE**: the S3 has Bluetooth 5 LE. `docs/garmin-remote-poc.md` marks V1 as "no Bluetooth, would need an S3 MCU swap" (lines 40, 288) - that becomes true for both boards; #66's "V2 only" restriction lifts. NimBLE 1.4.3 already verified to build on the pinned core. |
| M-71 | low | OPP | PSRAM (if an R8 module): unused today on either board. No action. |
| M-72 | low | OPP | USB serial logs on V1 (M-14) - telnet no longer the only log path when a laptop is plugged in. |

## 10. Tests and helpers

| ID | Pri | Kind | Item |
|---|---|---|---|
| M-80 | low | SIMP | `platformio.ini` `native_v1` target define (M-05). `test/shim` and `test/common/board_config.h` contain no MCU facts (checked) - `board_config.h`'s `BOARD_REV == 1` block is panel geometry and stays. |
| M-81 | low | SIMP | `helpers/check_firmware_image.py` (M-42). `helpers/LLM starter.txt` describes the S2 Mini project setup - update or delete. |
| M-82 | low | - | `.opencodereview/rule.json` describes V1 as "Wemos S2 Mini (ESP32-S2, env lolin_s2_mini ...)" - update so reviews stop applying S2 hazards. |

## 11. Documentation

| ID | Pri | Kind | Item |
|---|---|---|---|
| M-90 | med | REQ | `CLAUDE.md`: overview table (board, "Extra" row gains battery sense on V1), READ FIRST #1 (reason for the pin now S3-only or gone - M-60), the #58 WiFi-glitch table (historical, re-measured in M-50), READ FIRST #2 (M-44), Build & Flash commands and ports, Pinout table, "Battery (V2)" section -> both boards, Garmin note, "V1's ESP32-S2 has no Bluetooth", `native_v1` define. Per memory `claude-md-only-before-commit`: one pass right before the commit. |
| M-91 | low | REQ | `README.md:68` hardware table (MCU row). |
| M-92 | low | - | `V2 Guidelines.md` is user-maintained - flag the S2 references, do not edit. |
| M-93 | low | - | `CR.md` S2 wording (M-55). |
| M-94 | low | - | Memory files: `v1-mcu-upgrade-acceptable` resolves into "done"; `v1-is-base-board` / `flash-without-probe` stay valid (V1 still flashed over OTA, new IP per M-02). |

## 12. Decisions (refined 2026-09-29)

| ID | Decision | Effect |
|---|---|---|
| P | Pinout unified to V2's | Section 3, M-20, M-21 |
| D1 | **N16R8**, same module as V2 | Whole board config shared (M-03), `default_16MB.csv` (M-04) |
| D2 | **Sealed** - USB not reachable once closed | READ FIRST #2 and the two-cycle OTA test stay (M-44) |
| D3 | **OLED readout in the mode selector + CONFIG** | M-32; `DEAD_TOP_ROWS` placement is that task's design point |
| D4 | **Start fresh**, no NVS copy | Manual re-setup procedure (M-40) |
| D5 | Envs renamed `v1` / `v1_ota` / `v2` (assumed default) | M-01, M-46, CLAUDE.md, memory `flash-without-probe` wording |
| D6 | Core 3.x is a **separate follow-up study**, not part of the swap (assumed default) | M-60..M-65 |

## 13. Follow-up tasks (filed 2026-09-29, scope `v1-mcu`)

Ordered so each builds on the previous one:

1. **#68 Board swap: build + pins** - M-01..M-07, M-10..M-16, M-20..M-21, M-80; rewire V1 to V2 pins. Host tests + both builds; bench flash over USB.
2. **#69 Battery on V1** - M-30, M-31, M-32 (OLED in menus; settle the `DEAD_TOP_ROWS` question), calibrate against a meter.
3. **#70 OTA board marker** - M-41, M-42. Must land **before** the first OTA to the swapped V1, so the guard is never void in the field.
4. **#71 First flash + re-setup** - M-40, M-43, M-02; two-cycle OTA test before closing the unit.
5. **#72 Re-measure WiFi glitches** - M-50, M-51.
6. **#73 Docs pass** - M-53, M-55, M-81, M-82, M-90..M-94.
7. *(optional)* **#74 Core 3.x study** - M-60..M-65.
8. *(optional)* **#75 BLE on V1** - M-70, folds into #66.
