# Garmin watch as a BLE scoreboard remote - research summary (task #66, 2026-09-29)

Source: playground repo `squash-scoreboard-display-testing-playground`, `garmin-poc/` (commit 94de953):
`esp32/` (bare S3 GATT server + RMT load test + OLED), `watch-app/` (Connect IQ app, Monkey C),
`logs/`. The full report follows the summary unchanged.

## Summary

**Verdict: (a) possible - a valid way to use a Garmin watch as the remote.** Two iterations on a
Forerunner 970 and a bare ESP32-S3 DevKitC on the pinned `espressif32@6.13.0` + NimBLE-Arduino 1.4.3.

Works, measured:
- **Reliable link:** 0 lost / 0 duplicate / 0 out-of-order over ~350 commands, incl. 8 m on the wrist.
- **No effect on the LED driver:** FastLED RMT refill never more than 4 us late of 60 us with BLE
  advertising, connected and under traffic (1.16 M refills, 0 bails, 0 stale). BLE is not a READ FIRST #1 risk on the S3.
- **Phone stays paired** alongside; a notification mid-run delayed one press, no drop.
- **Optimistic score** on the watch (grey until the board confirms, buzz on confirm) hides the
  latency; **sync on connect** keeps the watch on the board's score through drops; **several
  watches on one board** stay in sync (board = server, watches = remotes; observed with two).
- **The app records a Padel activity** (`SPORT_RACKET` / `SUB_SPORT_PADEL`) while it is open.
- Board cost: NimBLE ~50 KB heap. Tooling free; builds from PowerShell, no VS Code.

Limits and facts to design around:
- **Latency ~0.5 s press -> board**, fixed by Connect IQ (~250 ms BLE service tick; min RTT 485-491 ms,
  median 499 ms at 50 cm and 8 m, write type irrelevant). The scoreboard shows a point ~0.5 s late.
- **Foreground only:** leaving the app drops the link (and ends the activity). ~6 s to connect per launch.
- **Pairing is required for a real product:** the POC connects to the first board it finds; two
  courts side by side need explicit one-watch <-> one-board pairing (board shows a code, watch
  stores the board, board allowlists its watches). The POC link is unbonded.
- **Watch support:** Bluetooth version is not the limit (only BT 4.0 features used). The floor is
  Connect IQ API 3.1 with the BLE module; this POC targets API 6.0 = watches from the fenix 8
  generation (Aug 2024) on - not "all 2024+" (Forerunner 165, Approach S50 are 5.x). Colour screens
  only in practice (monochrome Instinct cannot show the pending state).
- **Input differs per model:** Venu 4 / vivoactive 6 have only 2 buttons - the real app needs full
  on-screen control, swipes ignored for scoring, and a confirmed exit (swipe right = back = ends activity).
- **Distribution:** sideload needed Garmin Express before the FR970 appeared as MTP; Connect IQ beta
  apps install only on the uploader's account (no tester invites).
- **Simulator:** interactive, any watch model, but BLE only through a Nordic nRF52840 dongle / nRF52 DK,
  not the PC's Bluetooth.
- **Both boards:** V1 and V2 are ESP32-S3, so both have Bluetooth. Integration = a WiFi <-> BLE mode switch (never both),
  a BLE command source into `RemoteInputManager` beside the RF fob, NimBLE on core 0.

Not tested: battery drain (dropped), bonding, MIP / touch-only layouts in the simulator.

---

# Garmin watch as a BLE scoreboard remote - POC report (task #66)

Status: **complete** (2026-09-29), two iterations. Measured on a Forerunner 970 and a bare
ESP32-S3 DevKitC (pinned `espressif32@6.13.0`, NimBLE, FastLED load, SSD1306). Battery
drain was dropped by the user; bonding was not tested.

## Verdict

**(a) Possible - a valid way to use a Garmin watch as the remote** (after iteration 2).

Iteration 1 ended at (b): the ~0.5 s latency and "no activity recording while scoring" were
open. Iteration 2 closed both: **optimistic rendering** makes a press show instantly on the
wrist (the board confirms ~0.5 s later, buzz on confirm, user: works well), **sync on
connect** keeps the watch on the board's score through drops, and the app **records a Padel
activity** itself. A bonus the design gives for free: **several watches on one board stay in
sync** (board = server, watches = remotes; observed with two). What remains is product work, not feasibility: **explicit pairing of one
watch to one board**, so two scoreboards on neighbouring courts cannot cross-connect (the POC
takes the first board it finds - see Security), a battery check over a real match, and the
scoreboard itself still showing the point ~0.5 s after the press.

Iteration 1 analysis, kept for the record:

What works, measured:
- Reliable: **0 lost / 0 duplicate / 0 out-of-order** over ~350 commands (four 100-ping runs + manual scoring), including 8 m with the watch on the wrist.
- The watch stays paired to the phone; notifications arrive mid-run without dropping the link.
- **No effect on the LED driver**: BLE advertising, connected and under traffic, the S3's RMT refill was never more than 4 us late against 60 us of slack (0 bails, 0 stale in 1,161,689 refills with BLE on; `logs/session.log`). READ FIRST #1's hazard does not come from BLE.
- Tooling is free and works without VS Code; the NimBLE stack costs ~50 KB heap.

What decides whether it is worth building:
- **Latency ~0.5 s press -> scoreboard, fixed by Connect IQ.** Round trip min 485-491 / median 499 ms at 50 cm and 8 m, both write types; gaps quantised to 250 ms. It is not the radio (7.5 ms interval) or the write type, so firmware on either side cannot remove it. The user called it laggy. For comparison the RF fob's button C already waits ~280 ms (fires on release). Whether players accept ~0.5 s per point is the first unknown - a real match, not a bench, answers it.
- **Foreground-only.** The link drops the moment the app exits; a player recording a squash activity cannot keep the remote open in parallel. A **data field** is the only way to score *and* record, and it is untested (API allows BLE there; forums report instability).
- **Friction.** ~6 s to connect on every launch; sideloading needed Garmin Express before the FR970 appeared as MTP; distribution beyond one's own watch means store review.

At that point it was not (a): the two unknowns above could each sink it. It was never (d): the
POC took one day and nothing fought back technically.

## Setup and tooling

| Question | Answer | Source |
|---|---|---|
| Language / SDK | Monkey C, Connect IQ SDK 9.2.0 (2026-08-25) | developer.garmin.com/connect-iq/sdk |
| Install | SDK Manager zip (Windows), sign in with a Garmin account, pick SDK + device profiles | same |
| Build | `monkeyc -d <device> -f monkey.jungle -o x.prg -y key.der` (wrapped in `watch-app/build.ps1`); **verified**: SDK 9.2.0, `fr970` -> 108,044 B `.prg`. The default (gradual) type checker needs explicit casts on iterator results (`r as ScanResult`) and warns on untyped container access - warnings only | build log 2026-09-29 |
| Developer key | RSA **4096**, PKCS#8 DER (2048 fails "Signature check failed"); `gen-key.ps1` | community |
| Editor | Official: VS Code extension only. JetBrains: unofficial "Monkey C (Garmin Connect IQ)" plugin (plugins.jetbrains.com/plugin/8253), flocsy fork on GitHub, described as not actively maintained | plugin page, github.com/flocsy/IntelliJ-MonkeyC-ConnectIQ-plugin |
| Workflow verdict | **VS Code not needed to build**: SDK Manager + `monkeyc` from PowerShell scripts, source edited as plain text (IDEA plugin untried). Only loss: no IDE completion/type hints | verified 2026-09-29 |
| Simulator | Interactive: the SDK's simulator draws the chosen watch; buttons, taps and swipes by mouse; simulated battery / HR / activity data / notifications; app log in a console. Any installed profile, so touch-only or MIP models can be checked without owning them. `watch-app\sim.ps1` launches it | SDK 9.2.0 |
| Simulator BLE | **Not the PC's built-in Bluetooth.** Only a Nordic **nRF52840 USB dongle** or **nRF52 DK** flashed with Garmin's connectivity firmware (via nRF Connect for Desktop), set in *Settings -> BLE Settings* (COM port). An ESP32 cannot stand in (Nordic serial protocol). Without it the app stays on "scanning" and presses are ignored (not synced). Hardware-free alternative, not built: a "demo board" mode faking the board's confirm (~0.5 s) to try layouts, touch zones and the optimistic grey -> white | Garmin forum wiki "Getting Started with Connect IQ BLE Development"; Novel Bits nRF52840 dongle tutorial |
| Sideload | USB, MTP: copy `.prg` to `GARMIN\Apps`; shows on the watch only. FR970 first enumerated as `VID_091E&PID_0003`, vendor class FF (Garmin USB protocol, not MTP), Windows problem code 28 (no driver) and invisible in Explorer - **also with the watch's USB Mode set to MTP**, on two ports, OEM cable. To the user it looks like "only charging". **Fix: install Garmin Express** - afterwards the FR970 enumerates as `PID_51D5`, class WPD, "Forerunner 970", and Explorer copy works (quit Express first; it holds the MTP session). Scripted MTP copy (Shell.Application `CopyHere`) is unreliable: it worked for listing, then crashed PowerShell. Alternative: private **beta** upload of `ScoreRemote.iq` (`monkeyc -e`, ~12 KB), installed from the Connect IQ phone app - used successfully | observed 2026-09-29 |
| Publish | Garmin developer account (18+), per-upload review for public apps; a private beta upload worked the same day (user). Fee: none encountered; not verified for public listing | connect-iq/submit-an-app; user 2026-09-29 |
| Testers | **A beta app installs only on the uploading developer's account** - no invite/tester list (open feature request). Ways to reach testers: send them the `.prg` for their model to sideload over USB (MTP; may need Garmin Express first, as here); have each tester upload the `.iq` as a beta to *their own* developer account; or publish publicly (review). Each tester's watch model must be a `<iq:product>` in `manifest.xml` - today only `fr970` | forums.garmin.com: "Multi-user beta app", "Invite others to App Beta testing" |
| Java | Java 17.0.6 (already on this PC) runs SDK 9.2.0 | verified |

## Target devices

Target is **API level 6.0**, `minApiLevel="6.0.0"`, **Forerunner 970 only** - the one
watch available for testing (user, 2026-09-29). Other API-6 watches need their SDK
profile and a `<iq:product>` line. BLE API needs 3.1+.

| Device | SDK id | API level | Built |
|---|---|---|---|
| Forerunner 970 | `fr970` | 6.0.2, 454x454 | yes, 109,068 B `.prg` (final build with the write-type experiment) |

### Which watches API 6.0 covers

Support statement: **Garmin watches from the fēnix 8 generation (August 2024) onward, on
Connect IQ 6.0 or newer.** "All Garmin watches 2024+" would be wrong: the Forerunner 165
(Feb 2024, API 5.2) and Approach S50 (Jan 2025, API 5.1) are not API 6, and the Lily 2 family
and Bounce 2 are not on Garmin's Connect IQ device list at all.

The SDK 9.2.0 profiles at API 6.0.x (38 profiles), watches only, with announcement dates:

| Watch | Announced |
|---|---|
| fēnix 8 (incl. tactix 8, quatix 8), fēnix E, Enduro 3 | Aug 2024 |
| Instinct 3 (AMOLED / Solar), Instinct E | Jan 2025 |
| vívoactive 6 | Apr 2025 |
| Forerunner 570, Forerunner 970 | May 2025 |
| Venu X1 | Jun 2025 |
| fēnix 8 Pro (incl. MicroLED, quatix 8 Pro) | Sep 2025 (fan-wiki source) |
| Venu 4 (41 / 45 mm) | Sep 2025 |
| Instinct Crossover AMOLED | Sep 2025 |
| D2 Air X15 | Oct 2025 |
| D2 Mach 2 Pro | Apr 2026 |
| Forerunner 70, Forerunner 170 (Music) | May 2026 |
| fēnix 9, fēnix 9 Pro (Solar) | Aug 2026 |

Also API 6.0 but bike computers: Edge 540/840 (2023), 1040, 1050 (2024), MTB, 550/850 (2025).

Not covered despite being recent: Forerunner 165 (5.2), Forerunner 265/965 and Venu 3 (5.2),
fēnix 7 Pro / epix Pro (5.2), Approach S50/S70 (5.1), Instinct 2X / Crossover (3.4).

API 6.0 is this POC's choice, not a hard floor: the BLE API needs only 3.1. Lowering
`minApiLevel` would reach the Forerunner 165/265/965 and Venu 3, but whether
`SUB_SPORT_PADEL` exists below API 6 is unverified - check the older profiles before lowering.

**Bluetooth version is not the limit.** The protocol uses only Bluetooth 4.0 BLE features
(advertising, GATT write / notify / read, <= 20-byte payloads, 7.5 ms interval) - no 2M or
coded PHY, no extended advertising - and the S3 accepts 4.x centrals. Practically every
Garmin watch since ~2015 has BLE 4.x hardware. The real floors are Connect IQ's
`BluetoothLowEnergy` module (API 3.1+, and the model must expose it) and the **display**:

| Display | Typical resolution | For this app |
|---|---|---|
| AMOLED (Forerunner 970, Venu 4, fēnix 8 AMOLED) | 390-454 px | Tested; plenty of room |
| Colour MIP (Forerunner 255/955, fēnix 7/8 Solar, Enduro) | 240-280 px | Layout scales (percent-based), small text gets tight; 64-colour palette keeps the grey "unconfirmed" state. Sunlight-readable and always-on - good on court |
| Monochrome MIP (Instinct 2/3 Solar, Instinct E) | ~176 px + round sub-window | **Problematic**: no grey, so the optimistic state is invisible; small, irregular space. Needs its own layout or is out |

**Input differs per model** (from each profile's `simulator.json`):

| Model | Touch | Buttons the app sees |
|---|---|---|
| Forerunner 970, fēnix 8 | yes | 5: up, down, menu (hold up), select, back |
| Venu 4, vívoactive 6 | yes | **2: select, back** - no up/down/menu |
| Instinct 3 Solar | **no** | 5 |

The POC's input is button-first (up/down score, select undo, hold-up ping run, tap halves as
a bonus). On a Venu 4 that breaks: no button scoring, undo only on select, the ping run has
no trigger, **swipe up/down arrives as the page events that score a point**, and **swipe
right is "back", which exits the app and so stops and saves the Padel activity**. The real
app needs **full on-screen control on touch watches**: tap zones for left point, right point
and undo; swipes ignored for scoring; exit only through a confirmation, so a stray swipe
cannot end the recording. Buttons stay as the alternative input on button watches and are
the only input on the Instinct.

Practical target: API >= 3.1 with the BLE module **and a colour screen**. Not verified:
compile and run the simulator against a few MIP profiles (Forerunner 255, fēnix 7 Solar,
Instinct 3 Solar) before promising them.

Sources: SDK device profiles (API levels); dates from Garmin newsroom press releases, DC
Rainmaker and wiki.garminrumors.com, compiled by a research pass on 2026-09-29 - summarised
search results, not each page re-read; other 2024-26 models (new MARQ, quatix, tactix, Venu)
were not checked.

## BLE facts that shaped the design

- `Toybox.BluetoothLowEnergy`: watch = central only; allowed in watch app, widget, glance, **data field**, background.
- Max 3 registered profiles; one outstanding request at a time -> the app queues writes and advances in `onCharacteristicWrite` / `onDescriptorWrite`.
- Writes max **20 bytes**, no long writes -> protocol is 3-byte commands, <= 14-byte notifies.
- Notifications via CCCD write `[0x01,0x00]`; indications reported broken on some watches -> notify only.
- `pairDevice()` does not persist across app launches: the app rescans on every start.
- Bonding (`requestBond`, `isBonded`) from API 4.2.5; `CONNECTION_STRATEGY_SECURE_PAIR_BOND` crashed a Venu 3S (CIQQA-4568) -> POC is unbonded.

## ESP32 side

- `espressif32@6.13.0` (core 2.0.17) + **NimBLE-Arduino 1.4.3** builds clean alongside FastLED 3.9.16 and Adafruit SSD1306: RAM 15.5 % (50,884 B static), image 942,269 B (incl. the WiFi libs pulled by `WiFi.h`; NimBLE's own flash share not isolated). NimBLE on the pinned core: **works**, build and runtime, no Bluedroid fallback needed.
- NimBLE runtime heap: **~50 KB** (50,180 / 49,956 B measured at init); free heap 259 KB with BLE connected.
- Loop on core 1 (FastLED/RMT ISR allocated there); NimBLE host on its default core 0.
- Gotchas: a DevKitC flashed with TinyUSB firmware (303A:4001) ignores esptool's auto-reset and the 1200-baud touch - BOOT+RESET once; with `ARDUINO_USB_MODE=1` later uploads reset by themselves. Opening the native-USB serial port with DTR/RTS asserted **resets the S3** - open with both low.

## Measurements

| Test | Result |
|---|---|
| Round trips, 100 pings each, stop-and-wait (board-side log) | **50 cm**: 0 lost / 0 dup / 0 ooo, ~510 ms per exchange (50.5 s / 99), slowest 750 ms. **8 m** (watch on wrist): 0 lost / 0 dup / 0 ooo, ~574 ms per exchange (54.5 s / 95), slowest 1000 ms. Watch-side, 8 m: **100/100, lost 0, RTT min 491 / median 499 / max ~1000 ms** (max cut off on screen) |
| Write-type experiment, 50 cm | **WR** (with response): 100/100, lost 0, RTT 485 / 499 / 499 ms. **NR** (without response): 100/100, lost 0, RTT 292 / 499 / 1454 ms; CIQ still fires `onCharacteristicWrite` for NR writes (102 callbacks). Median identical -> the write response is **not** the cost. The NR max (1454 ms; board-side 1250 ms, the run's only outlier) coincides with a phone notification covering the screen mid-run (user) - no disconnect. Buzz felt equally laggy in both (user) |
| Press -> scoreboard | OLED score change and watch buzz land **at the same moment** (user; OLED redraws at 4 Hz, so +-250 ms resolution). The delay is before the board receives the press, not on the notify path back - the scoreboard would show a point ~0.5 s after the press, whatever the write type. Buzzing on press would only mask it on the wrist. Untested contributor: UP/DOWN are behaviour events and UP also carries a hold action (menu), so the firmware may wait to rule out a hold; raw `onKeyPressed` could shave that part (the no-button ping RTT stays ~500 ms) |
| Latency structure | Every inter-command gap in the session is a multiple of **250 ms** (manual presses 500/750, pings a steady 500). The radio link is 7.5 ms-interval, so the ~500 ms floor is Connect IQ's side: BLE requests/callbacks serviced on a ~250 ms tick. The NR experiment shows it is not the write-response tick: the round trip (press -> write -> board -> notify -> watch callback) costs ~500 ms either way. Distance adds retries that push a request into the next slot (8 m: +60 ms mean, 1000 ms worst). The user's "lag from 8 m" = this floor plus tail |
| Phone stays connected alongside | **Yes.** The watch stayed paired to the phone through every run; a phone notification arrived and was shown mid-run while the scoreboard link stayed up (0 lost, 0 disconnect). Cost: one command delayed to ~1.3 s while the notification took the screen |
| RMT, BLE off (baseline) | 899 frames / 33,263 refills: 0 bails, 0 stale, max late 3 us of 60 us slack, `show()` max 2.7 ms; OLED (I2C 400 kHz, 4 Hz redraw) running. Heap free 309,676 B |
| RMT, BLE advertising | 696 frames / 25,752 refills: 0 bails, 0 stale, max late 4 us of 60 us. NimBLE init cost 50,180 B heap |
| RMT, BLE connected idle | 0 bails, 0 stale, max late 2-3 us of 60 us (two sessions, ~10k refills) |
| Connection parameters | FR970 picks interval **7.5 ms** (the BLE minimum), latency 0, supervision timeout 4 s |
| Board reset -> watch reconnect | advertising 6.8 s after boot, watch reconnected **4.8 s** later with no user action |
| RMT, BLE traffic (manual scoring session, 51 commands) | ~496k refills: 0 bails, 0 stale, max late 3 us of 60 us - BLE traffic has no visible effect on the RMT refill ISR on the S3 |
| Manual scoring session (user, ~10 min) | 51 commands (A 18, B 14, U 15): 0 dup / 0 ooo / 0 gap, no disconnect. "Working without issues"; **lag noticeable from ~8 m** (user) - a squash court is 9.75 m long, so this is the real operating distance |
| Battery, 1 h app open vs 1 h baseline | **Not measured** (dropped by the user, 2026-09-29). The link runs at the 7.5 ms minimum interval with latency 0 - the most power-hungry setting - and the app keeps the AMOLED screen on; expect it to matter over a long session |
| Out of range -> reconnect | Not provoked: the link never dropped at 8 m (the longest distance tried). Reconnect paths measured instead: board reset -> 4.8 s, app relaunch -> ~6 s |
| App start -> connected | **~6 s** at 50 cm (user, stopwatch): profile register + scan + `pairDevice` + CCCD write; `pairDevice` does not persist, so every launch pays it |
| App exited (BACK) | link dropped **immediately** (board logs disconnect at exit); reopen -> link up 3.9 s after the exit incl. the user's reopen time. The remote only works while the app is in the foreground |
| Inside a recorded activity | A watch app replaces the native activity screen, so iteration 1 could not score and record at once. **Solved in iteration 2** without a data field: the app records its own Padel session (see below) |

## Iteration 2: optimistic score, sync on connect, Padel activity

Changes (2026-09-29, `ScoreRemote-fr970.prg` 112,652 B):
- **Optimistic rendering.** A press applies the board's own rules on the watch at once
  (score + one-level undo), shown grey until the board confirms; the watch buzzes on the
  confirm. With nothing pending the board's score is adopted, so a failed write or a drop
  heals itself. Presses while disconnected are ignored.
- **Sync on connect.** The board's state value carries the full `[seq, op, "a:b"]` frame
  from boot; the watch reads it after subscribing ("syncing" -> "connected") and only then
  accepts presses.
- **Padel activity.** `ActivityRecording` session (`SPORT_RACKET` / `SUB_SPORT_PADEL`,
  heart rate sensor enabled) started in `onStart`, stopped and saved in `onStop` - the app
  *is* the activity, which sidesteps "a watch app cannot run beside a native activity".
  Consequence: BACK ends the recording.
- Score writes back to with-response (no-response bought nothing).

| Test | Result |
|---|---|
| Optimistic score | **Works well** (user): the number changes on press; the ~0.5 s confirm is no longer felt as lag |
| Sync on connect / after a drop | **Works** (user): the watch shows the board's current score after reconnecting |
| Padel activity | **Works** (user): recorded as a Padel activity while the remote is open |
| **Several watches on one board** | **Works** (user): with two watches connected, a point scored on either shows on both. Client-server by construction: the board is the single source of truth, `notify()` reaches every subscribed watch, and each watch reads the board's score on connect, so a late joiner starts in sync. Not captured in the board logs (only one watch address, `64:a3:37:2a:b0:1d`, appears there), and how advertising stayed up for the second connection - NimBLE stops advertising on the first connect and this code restarts it only on a disconnect - is unverified; confirm before relying on it |
| Board log, iteration 2 (`logs/session2.log`) | 11 presses (seq 13-23, score 2:1 -> 3:10) in order, 0 dup / 0 gap. RMT over the preceding 43 min of advertising: 1.9 M refills, 0 bails, 0 stale, max 3 us |

## Security

### Requirement: explicit pairing, one watch <-> one board

The POC was checked end to end as a native Connect IQ watch app, and it deliberately takes
a shortcut a real implementation must not: the watch connects to the **first board it
finds** advertising the service UUID. With two scoreboards on neighbouring courts, a watch
could bind to the wrong one and score someone else's match. The real implementation needs
**pairing**, never "connect to whatever is in range":

- **Board side:** a pairing mode entered from the scoreboard's menu (CONFIG/PROFILE), which
  shows a short board name or code on the LEDs / e-paper and advertises it in the
  advertising data. Outside pairing mode it accepts only watches it has already paired.
- **Watch side:** a pairing screen that lists the boards currently in pairing mode (name
  plus signal strength), lets the player confirm the one matching the code on the board,
  and stores that board's identity (address or board ID) in `Application.Storage`. From
  then on it scans for and connects to **that board only**.
- **Both ends remember each other,** so a normal launch needs no user action, and a board
  in use cannot be taken over by another watch until it is re-paired from the board.
- **Several watches per board already work** (observed in iteration 2: the score syncs
  between two connected watches), so the allowlist holds a list, not one entry - e.g. one
  watch per player, both able to score and both showing the live score.

### Unbonded link

The POC's GATT server is unbonded: any BLE central in range (a phone with nRF Connect) can
write `A`/`B`/`U` and change the score. Options for a real feature, none tested here:
- **Allowlist the watch's address.** The FR970 connected from the same address every time
  (`64:a3:37:2a:b0:1d`, 4 connects incl. a board reflash and an app reinstall) - a "pair this watch" step in PROFILE/CONFIG could store it
  and NimBLE could reject everyone else. Cheapest, no watch-side change.
- **Bonding** via `Device.requestBond()` (API 4.2.5+). Encrypts the link, but the
  `SECURE_PAIR_BOND` strategy crashed a Venu 3S (CIQQA-4568) - test on the FR970 first.
- **App-level token** in each command. Weak against a sniffer, stops casual misuse.

## What an integration would need (either board)

- **Mode, not coexistence:** BLE and WiFi never run together (user, 2026-09-29). A "watch
  remote" mode would stop WiFi and start NimBLE, and the reverse - the pattern
  `PlayerSetupMode` already uses for its AP (raise in the constructor, drop in the destructor).
- **Input path:** a BLE command source feeding `RemoteInputManager` beside the 433 MHz
  receiver, so views see ordinary A/B/C presses. The RF fob stays.
- **Cores:** keep the NimBLE host on core 0 and `loop()` (FastLED/RMT ISR) on core 1, as here.
- **Budget:** ~50 KB heap at init; flash cost to be measured against V2's app partition.
- **Confirm the RMT result on real V2 LEDs** (74 slots, OLED, e-paper SPI all running) - the
  bare-board result (never >5 us late of 60 us) is strong evidence, not proof.
- **V1** is the same ESP32-S3 N16R8 on V2's pinout; the same integration applies to both boards.
