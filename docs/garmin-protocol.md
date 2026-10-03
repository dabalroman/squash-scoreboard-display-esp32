# Garmin App Remote - BLE protocol (v0)

Contract between the scoreboard firmware (task #80) and the Connect IQ watch app (task #81).
Spec for task #79. Background and measurements: the POC (see `docs/garmin-remote-poc.md`).

Status: **draft, protocol version 0 (development)**. Every value here may change until v1 is
frozen at the first public Connect IQ store release (see [Versioning](#5-versioning-and-compatibility)).
Deviations from the locked task decisions are listed in [Open questions](#17-open-questions-and-deviations).

## 1. Model

- The watch is always the BLE central, the board the GATT peripheral (Connect IQ cannot be a peripheral).
- The board is the single source of truth. The watch sends **semantic commands** (not virtual
  A/B/C/D presses); the board applies them through its normal mode/view logic and pushes the
  resulting **screen state**; the watch renders that state.
- Several watches (up to 4 connected, 8 paired) may control one board at once; each sees every
  state change.
- No BLE bonding (it crashed a Venu 3S, CIQQA-4568). Trust is an app-level, mutual
  challenge-response over a per-watch 16-byte key issued in a user-opened pairing window.
- The feature ("Garmin App Remote") is opt-in, off by default, and independent of Dev Mode.
  While enabled, the board advertises from boot.

## 2. Conventions

- **Every frame starts with the protocol version byte** (`PROTO`, currently `0x00`): every
  characteristic value read, written or notified, and the manufacturer data's version field
  (fixed offset, see 4.1). A frame whose first byte differs from the receiver's own `PROTO`
  is **dropped** by either side (the board does not apply or acknowledge it).
- Multi-byte integers are **little-endian**. Offsets and sizes are in bytes.
- **No frame exceeds 20 bytes** (Connect IQ: writes max 20 B, no long writes; reads and
  notifies are kept to the same bound so they fit the default ATT MTU of 23 without
  negotiation). Every layout below gives its total.
- Receivers ignore trailing bytes beyond the layout they know (room for compatible additions).
  A frame shorter than its layout is malformed.
- `uid` = the profile's persistent `UserProfile` uid (`uint32`). `index` = the profile's
  position in this boot's roster (= `UserProfile` id). Sides are **court sides as displayed**:
  `0` = left, `1` = right (never player A/B, which ignore the court-side swap).
- Connect IQ allows **one outstanding BLE request** and services them on a ~250 ms tick
  (POC): every read, write and CCCD write below costs the watch roughly one tick.

## 3. Constants

| Name | Value |
|---|---|
| `PROTO` | `0x00` (v0, development) |
| Company id | `0xFFFF` (Bluetooth SIG test id, until a registered one exists) |
| Product magic | `53 43 42 44` (ASCII `SCBD`), 4 bytes, frozen forever |
| Board id | `uint32`, random, generated on the first enable of the feature, stored in NVS; never `0x00000000` or `0xFFFFFFFF` |
| Pairing window | 120 s |
| Pairing code | 4 decimal digits, `0000`-`9999`, fresh random per window |
| Key | 16 random bytes per paired watch |
| Nonces | 8 random bytes, fresh per connection (board) / per attempt (watch) |
| MAC truncation | first 8 bytes of HMAC-SHA256 |
| Paired watches (board) | 8; the 9th pairing evicts the least recently authenticated slot |
| Concurrent connections | 4; the board advertises while fewer are open |
| Unauthenticated connection timeout | 10 s, then the board disconnects |

UUIDs (fresh, not the POC's):

| Item | UUID |
|---|---|
| Service | `0d780001-2f44-4f31-93b7-b78dc9e29735` |
| VERSION | `0d780002-2f44-4f31-93b7-b78dc9e29735` |
| COMMAND | `0d780003-2f44-4f31-93b7-b78dc9e29735` |
| STATE | `0d780004-2f44-4f31-93b7-b78dc9e29735` |
| ROSTER | `0d780005-2f44-4f31-93b7-b78dc9e29735` |
| AUTH | `0d780006-2f44-4f31-93b7-b78dc9e29735` |
| PAIRING | `0d780007-2f44-4f31-93b7-b78dc9e29735` |

## 4. Advertising

Legacy connectable undirected advertising (`ADV_IND`), BLE 4.0 features only, 31-byte payloads.

### 4.1 Byte budget - the service UUID does not fit beside the manufacturer data

| AD structure | Bytes |
|---|---|
| Flags (`02 01 06`) | 3 |
| Complete list of 128-bit service UUIDs (len, type `0x07`, 16) | 18 |
| Manufacturer data with the locked fields (len, type `0xFF`, company 2, magic 4, version 1, board id 4, flags 1, code 2) | 16 |
| **Total** | **37 > 31** |

Even without the Flags AD it is 34. Moving it to the scan response does not help the watch:
**Connect IQ scans passively and never sees `SCAN_RSP`** (Garmin staff in bug report CIQQA-789:
"All data would have to be sent through the ADV IND packets"). So:

- `ADV_IND` carries the **manufacturer data first**, then Flags (19 B, 12 spare). Manufacturer
  data first because one forum report says Connect IQ truncates the advertising record to 20 B
  (unverified); the 16 identifying bytes survive that.
- `SCAN_RSP` carries the 128-bit service UUID and the local name, for phones and BLE tools
  (they scan actively). The watch never uses either.
- The watch's pre-connect filter is company id + magic (+ version + board id + flags); the
  service UUID is verified **after** connecting, from the GATT table (section 7, step 2).
  This is a deviation from the locked decision - see Open questions.

### 4.2 `ADV_IND` (19 B)

| Off | Size | Field | Value |
|---|---|---|---|
| 0 | 1 | AD length | `0x0F` (15) |
| 1 | 1 | AD type | `0xFF` manufacturer specific |
| 2 | 2 | Company id | `FF FF` |
| 4 | 4 | Product magic | `53 43 42 44` |
| 8 | 1 | `PROTO` | board's protocol version |
| 9 | 4 | Board id | `uint32` |
| 13 | 1 | Flags | bit 0 `PAIRING` (window open); bits 1-7 reserved, sent 0, ignored |
| 14 | 2 | Pairing code | `uint16` 0-9999 while `PAIRING`, else `0x0000` (ignored) |
| 16 | 3 | Flags AD | `02 01 06` (LE General Discoverable, BR/EDR not supported) |

Manufacturer payload after the company id = 12 bytes (offsets 4-15). **Frozen forever, in every
protocol version:** company id, magic, and the version byte directly after the magic - so any
watch app, old or new, can identify any board and classify its version.

### 4.3 `SCAN_RSP` (30 B)

| Off | Size | Field | Value |
|---|---|---|---|
| 0 | 1 | AD length | `0x11` (17) |
| 1 | 1 | AD type | `0x07` complete list of 128-bit service UUIDs |
| 2 | 16 | Service UUID | little-endian |
| 18 | 1 | AD length | `0x0B` (11) |
| 19 | 1 | AD type | `0x09` complete local name |
| 20 | 10 | Name | `Score-XXXX`, `XXXX` = low 16 bits of the board id, uppercase hex |

The name is for humans only and never used for matching. The watch, which cannot see it, labels
a board `Scoreboard XXXX` from the same 16 bits, so phone tools and the watch agree.

NimBLE-Arduino adds `addServiceUUID` and the name to the advertising data by default; #80 must
build both payloads explicitly (`setAdvertisementData` / `setScanResponseData`).

### 4.4 Watch scan filter

For each `ScanResult`, obtain the manufacturer payload for company `0xFFFF` (parse
`getRawData()` as AD structures, or `getManufacturerSpecificData(0xFFFF)`; whether the latter
includes the two company bytes is undocumented - accept both by checking for the magic at
offset 0 or 2). Then:

1. Payload shorter than 12 B, or magic mismatch -> **ignore** (never shown, never connected).
2. Normal operation: connect only if board id is a **stored paired board**; auto-reconnect
   targets the board used last. Every other advertiser is ignored.
3. Pairing screen: list only advertisers with `PAIRING` set, showing `Scoreboard XXXX`, the
   4-digit code and RSSI.
4. Version check on the advertised `PROTO` before connecting (section 5). An incompatible board
   is listed on the pairing screen with the reason but cannot be selected; a paired board that
   became incompatible shows the message instead of being connected.

## 5. Versioning and compatibility

- One integer protocol version. The board declares only its own (`PROTO`). The watch app
  declares the range it supports, `[MIN_BOARD_PROTO, MAX_BOARD_PROTO]`.
- **v0 = development.** The wire format may change freely; the watch accepts exactly v0
  (`MIN = MAX = 0`); no compatibility matrix is kept before release.
- **v1 is frozen at the first public Connect IQ store release.** From then on every
  incompatible change bumps `PROTO`, and the range check applies.
- The watch checks the version from the advertising data before connecting, and again from the
  VERSION characteristic after connecting (both must be in range and equal).

| Board `PROTO` | Watch behaviour |
|---|---|
| `< MIN_BOARD_PROTO` | refuse to connect or pair; show "Update the scoreboard firmware" + board version + required minimum |
| `> MAX_BOARD_PROTO` | refuse to connect or pair; show "Update the watch app" (Connect IQ store) |
| in range | proceed |

The board never refuses a watch by version: a watch whose frames carry a different `PROTO`
simply gets them dropped.

**Incompatible (bump `PROTO`, from v1):** any change to an existing frame's byte layout,
field size or offset; a new meaning for an existing value (command, screen, sport, status, flag
bit); removing a command, screen, sport or characteristic; changing a UUID, the advertising
layout, the chunk header, the auth or pairing exchange, the MAC construction or truncation;
making a previously optional behaviour mandatory for the watch.

**Compatible (no bump):** appending bytes at the end of a frame or state body (receivers ignore
trailing bytes); new screen ids (an old watch shows "board busy"); new sport ids (an old watch
does not offer them); new commands (old watches never send them); new status codes (an old
watch treats any non-zero status as a rejection); new flag bits (ignored); new characteristics.

**Frozen across all versions:** company id, magic and the version byte's offset in the
advertising data; the VERSION characteristic's UUID and its byte 0.

## 6. GATT layout

One primary service, six characteristics. One Connect IQ profile (limit is 3).

| Characteristic | Properties | Auth required | Purpose |
|---|---|---|---|
| VERSION | read | no | `PROTO` + firmware version string |
| COMMAND | write (with response) | yes | semantic commands |
| STATE | read, notify | yes | screen state (notify = full, chunked; read = chunk 0) |
| ROSTER | write, read | yes | write = set read cursor; read = entry at cursor, cursor + 1 |
| AUTH | read, write | no | read = challenge / result; write = proof |
| PAIRING | write, read, notify | no | write = request; read/notify = result (this connection only) |

Before authentication the board accepts writes to COMMAND and ROSTER at the ATT level and
**discards** them, answers reads of STATE and ROSTER with the 1-byte frame `[PROTO]`, and sends
no notifications. Notify-only, never indications (unreliable on some watches).

### 6.1 VERSION (read, 2-20 B)

| Off | Size | Field |
|---|---|---|
| 0 | 1 | `PROTO` |
| 1 | 1-19 | Firmware version, ASCII, no NUL (`version.txt`, e.g. `0.6.187`) |

## 7. Connection sequence (watch)

| Step | Request | Notes |
|---|---|---|
| 1 | scan, filter (4.4), `pairDevice()` | `pairDevice` does not persist across launches: every launch rescans |
| 2 | (on connect) check service + all six characteristics exist | any missing -> disconnect, treat as not a scoreboard |
| 3 | read VERSION | out of range or `!=` advertised -> disconnect + message (section 5) |
| 4 | read AUTH | challenge (8.1) |
| 5 | write AUTH | proof |
| 6 | read AUTH | result; verify the board's MAC |
| 7 | write STATE CCCD `01 00` | the board then notifies the full current state |
| 8 | if `rosterVersion` changed: write ROSTER `[PROTO, 0]`, read `count` times | section 12 |

Pairing inserts its exchange (section 9) before step 4 on the same connection. Presses are
ignored until step 7's state has arrived. On disconnect the watch drops all pending presses and
reconnects (step 1) to the board it used last; the state read on reconnect heals any lost press.

## 8. Authentication (mutual, every connection)

Key `K` = the watch's 16-byte key in board slot `slotId`. `MAC(x)` = first 8 bytes of
HMAC-SHA256(`K`, `x`) (RFC 2104). `||` = concatenation; `"W"` = `0x57`, `"B"` = `0x42`.

1. On connect the board draws a fresh board nonce `Nb` (8 B) for this connection.
2. Watch reads AUTH -> challenge.
3. Watch draws a watch nonce `Nw` (8 B) and writes the proof: `slotId`, `Nw`, `MAC("W" || Nb)`.
4. Board checks `slotId` holds a key and the MAC matches (constant-time compare). On success the
   connection is **authenticated** (commands, state, roster unlocked) and the slot's
   last-used counter is bumped (LRU). The result carries `MAC("B" || Nw)`.
5. Watch reads AUTH -> result. Status OK and a matching board MAC -> trusted. A wrong or missing
   board MAC -> the watch disconnects, marks the board **untrusted** (no auto-reconnect until the
   user retries) and shows that the scoreboard failed authentication. An impostor advertising
   the right id cannot produce it without `K`.

One proof per connection: after a failed proof the board answers the result and disconnects
2 s later. A second proof write on the same connection is ignored.

### 8.1 AUTH frames

Challenge (read, before a proof) - 10 B:

| Off | Size | Field |
|---|---|---|
| 0 | 1 | `PROTO` |
| 1 | 1 | Type `0x00` CHALLENGE |
| 2 | 8 | `Nb` |

Proof (write) - 19 B:

| Off | Size | Field |
|---|---|---|
| 0 | 1 | `PROTO` |
| 1 | 1 | Type `0x01` PROOF |
| 2 | 1 | `slotId` (0-7) |
| 3 | 8 | `Nw` |
| 11 | 8 | `MAC("W" \|\| Nb)` |

Result (read, after a proof) - 11 B on success, 3 B on failure:

| Off | Size | Field |
|---|---|---|
| 0 | 1 | `PROTO` |
| 1 | 1 | Type `0x02` RESULT |
| 2 | 1 | Status: `0` OK, `1` BAD_PROOF, `2` UNKNOWN_SLOT |
| 3 | 8 | `MAC("B" \|\| Nw)` - present only with status 0 |

`BAD_PROOF` / `UNKNOWN_SLOT` mean the board no longer holds this watch's key (evicted, "forget
all", NVS wiped): the watch shows "pair again" rather than "untrusted".

## 9. Pairing

1. User opens CONFIG > GARMIN > pair on the board. The board opens a 120 s window, draws a
   code, shows it on the LEDs, OLED and e-paper, and advertises `PAIRING` + code.
2. Watch pairing screen lists matching advertisers (4.4 step 3) with their codes; the user picks
   the one whose code matches the board.
3. Watch connects, runs steps 2-3 of section 7, writes the PAIRING request with the code it
   picked.
4. Board, if the window is open and the code matches: draws a 16-byte key, stores it in a free
   slot (or evicts the least recently authenticated one), persists it, and returns `slotId` +
   key **to this connection only** (never notified to another connection, never readable outside
   the window).
5. Watch stores `{boardId, slotId, key}` (several boards may be stored; the last used is the
   auto-reconnect target) and continues with authentication (section 7 step 4) on the same
   connection.

One watch pairs per window: the board closes it (and leaves the pairing screen) once a watch that
paired has authenticated with its new key - not at key delivery, which would wipe the result
before the watch read it. It also closes after 120 s or when the user leaves the screen; pair
another watch by opening the window again. "Forget all" clears every slot. The code is not a secret (it is advertised);
it only makes the user pick the board in front of them. The key crosses the air in plaintext
once, inside the user-opened window - an accepted risk. Security rests on the keys, never on the
protocol being secret: this spec is public.

The 10 s unauthenticated timeout restarts when a pairing result is delivered.

### 9.1 PAIRING frames

Request (write) - 4 B:

| Off | Size | Field |
|---|---|---|
| 0 | 1 | `PROTO` |
| 1 | 1 | Type `0x01` REQUEST |
| 2 | 2 | Code `uint16` (the one shown next to the chosen board) |

Result (read; also notified if the watch subscribed) - 20 B on success, 3 B on failure; before
any request a read returns `[PROTO, 0x00]` (2 B, NONE):

| Off | Size | Field |
|---|---|---|
| 0 | 1 | `PROTO` |
| 1 | 1 | Type `0x02` RESULT |
| 2 | 1 | Status: `0` OK, `1` WINDOW_CLOSED, `2` BAD_CODE, `3` FULL_RETRY (key storage busy) |
| 3 | 1 | `slotId` - status 0 only |
| 4 | 16 | Key - status 0 only |

The board answers within the write's access callback, so the result is readable as soon as the
write response arrives.

## 10. Commands

### 10.1 COMMAND frame (write, 3-11 B)

| Off | Size | Field |
|---|---|---|
| 0 | 1 | `PROTO` |
| 1 | 1 | Command id |
| 2 | 1 | `seq` (1-255) |
| 3 | 0-8 | Arguments |

### 10.2 Command table

| Id | Name | Args | Size | Valid on | Board effect (equivalent today) |
|---|---|---|---|---|---|
| `0x01` | SELECT_SPORT | `sport` u8 | 4 | MENU | enter that sport's CHOOSE_PLAYERS (cursor to the row + D). Sport not in the menu -> INVALID |
| `0x02` | BACK | - | 3 | CHOOSE_PLAYERS, MATCH_START, INTRO, CELEBRATION, GAME_OVER, PROFILE | `DeviceMode::goBack()`: CHOOSE_PLAYERS -> MENU, MATCH_START -> CHOOSE_PLAYERS, INTRO / CELEBRATION / GAME_OVER -> MATCH_START; PROFILE -> MENU through its C/D exit, not `goBack()` (which drops the setup AP; a long C there stays a no-op). Never on PLAYING (a game must not be discarded) |
| `0x03` | TOGGLE_PLAYER | `uid` u32 | 7 | CHOOSE_PLAYERS | add / remove that profile (D on its row). Unknown uid -> INVALID |
| `0x04` | START_TOURNAMENT | - | 3 | CHOOSE_PLAYERS | -> MATCH_START (D on START). Fewer than 2 selected -> INVALID |
| `0x05` | SET_PAIR | `leftUid` u32, `rightUid` u32 | 11 | MATCH_START | make the match between them with `leftUid` on the left. Not both selected, or equal -> INVALID |
| `0x06` | SWAP_SIDES | - | 3 | MATCH_START | swap court sides (C) |
| `0x07` | START_MATCH | - | 3 | MATCH_START | -> INTRO (D) |
| `0x08` | SCORE | `side` u8 | 4 | PLAYING | point to that side (A / B); padel: a rally |
| `0x09` | UNDO | `side` u8 | 4 | PLAYING | undo that side's last point (C = left, D = right); padel: also steps back across gems. At 0:0 with nothing to step back -> MATCH_START, as the board's C/D does |
| `0x0A` | SKIP | - | 3 | INTRO, CELEBRATION | INTRO -> PLAYING; CELEBRATION -> GAME_OVER |
| `0x0B` | NEXT_GAME | - | 3 | GAME_OVER | -> MATCH_START, same pair (C/D) |
| `0x0C` | *(unused)* | - | - | - | never sent; the board answers UNKNOWN_CMD. Leaving a match is BACK, as on the fob (see Open questions 6) |
| `0x0D` | SYNC | - | 3 | every screen, incl. busy | no state change; re-push the full state |

`side` other than 0/1 -> INVALID. Scoring through BLE bypasses the RF receiver's debounce;
`seq` deduplication replaces it.

### 10.3 Sequence numbers and acknowledgement

- The watch numbers commands per connection: first `seq` = 1, then +1, wrapping 255 -> 1
  (0 is reserved for "none").
- The board keeps, per connection, the last received `seq` (0 at connect). A command whose `seq`
  equals it is a **duplicate**: not applied, ack unchanged, full state re-pushed (recovers a lost
  ack). Any other `seq` is new. One outstanding request at a time keeps commands in order.
- Commands are queued from the NimBLE host task (core 0) and applied in `loop()` (core 1)
  through the active mode. The state pushed after applying carries `ackSeq` = that `seq` and
  `ackStatus`:

| `ackStatus` | Meaning |
|---|---|
| 0 | APPLIED |
| 1 | WRONG_SCREEN (not valid on the current screen) |
| 2 | INVALID (bad argument or precondition) |
| 3 | UNKNOWN_CMD |
| 4 | BUSY (an overlay owns the displays, or the screen is being replaced: a mode change or an in-mode view swap is pending, e.g. the intro ending on its timer) |
| 5 | MALFORMED (frame too short) |

A rejected command produces a state push too, so the watch can roll back an optimistic press.
A frame with a foreign `PROTO` is dropped without any ack.

## 11. State

### 11.1 Chunks (STATE notify)

A state is a **body** (section 11.2) split into chunks of at most 17 body bytes:

| Off | Size | Field |
|---|---|---|
| 0 | 1 | `PROTO` |
| 1 | 1 | `stateSeq` (u8, per connection, +1 per push, wraps) |
| 2 | 1 | `index << 4 \| count` (index 0-14, count 1-15) |
| 3 | 1-17 | body bytes `[index * 17, index * 17 + 17)` |

Max body 255 B. The watch reassembles chunks with the same `stateSeq`; a chunk with a different
`stateSeq` discards the incomplete set. Chunks of one push are sent back to back, in order.
A READ of STATE returns chunk 0 of the current state (sync check: screen, ack, roster version);
the full state comes from notifications (pushed on CCCD enable and on every change) or SYNC.

The board pushes on every change of the body, coalesced to at most one push per `loop()` tick
(50 ms) per connection, always with the newest content. `ackSeq` / `ackStatus` are per
connection, so each connection gets its own push.

### 11.2 Body

| Off | Size | Field |
|---|---|---|
| 0 | 1 | `screen` |
| 1 | 1 | `sport` (0 outside a sport) |
| 2 | 1 | `ackSeq` (0 = none on this connection) |
| 3 | 1 | `ackStatus` |
| 4 | 4 | `rosterVersion` (section 12) |
| 8 | n | screen data |

Sports - one wire table, independent of `DeviceModeState`; the watch offers exactly the sports
the board's MENU state lists (built from `ModeSwitchingView`'s `ModeMenuEntry` table, sport rows
only, in menu order):

| `sport` | Name |
|---|---|
| `0x00` | none |
| `0x01` | squash |
| `0x02` | volleyball |
| `0x03` | short volleyball |
| `0x04` | padel |

Screens:

| `screen` | Name | Board state | Watch shows |
|---|---|---|---|
| `0x00` | BOOTING | before the first mode | board busy |
| `0x01` | MENU | `ModeSwitchingMode` | sport list (PROFILE / CONFIG never shown) |
| `0x02` | CONFIG | `ConfigMode` | board busy |
| `0x03` | PROFILE | `PlayerSetupMode` | "edit on phone" + BACK |
| `0x10` | CHOOSE_PLAYERS | `TournamentChoosePlayers` | roster with in/out, START |
| `0x11` | MATCH_START | `MatchStartGame` | left/right pick, swap, start |
| `0x12` | INTRO | `MatchIntro` | the pair, SKIP |
| `0x13` | PLAYING | `GamePlaying` | score, left/right point, undo |
| `0x14` | CELEBRATION | `GameCelebration` | result, SKIP |
| `0x15` | GAME_OVER | `GameOver` | result, NEXT_GAME / BACK |
| other | - | - | board busy |

### 11.3 Screen data

BOOTING, CONFIG, PROFILE: none (body 8 B, 1 chunk).

MENU (body `10 + n`):

| Off | Size | Field |
|---|---|---|
| 8 | 1 | cursor: `sport` of the highlighted row, 0 on a non-sport row |
| 9 | 1 | `n` sports |
| 10 | n | sport ids in menu order (today `04 01 02 03`: padel, squash, volleyball, short volleyball) |

CHOOSE_PLAYERS (body 12, 1 chunk):

| Off | Size | Field |
|---|---|---|
| 8 | 4 | selected: bit `i` = roster index `i` is in the tournament |

MATCH_START (body 20, 2 chunks):

| Off | Size | Field |
|---|---|---|
| 8 | 4 | selected bitmask (as above) |
| 12 | 4 | `leftUid` |
| 16 | 4 | `rightUid` |

INTRO (body 16, 1 chunk): `leftUid` at 8, `rightUid` at 12.

PLAYING (body 21, padel 23; 2 chunks):

| Off | Size | Field |
|---|---|---|
| 8 | 4 | `leftUid` |
| 12 | 4 | `rightUid` |
| 16 | 1 | flags: bit 0 UNCOMMITTED (scores include points inside the 4 s commit window), bit 1 TIEBREAK (padel), bit 2 GAME_BALL_LEFT, bit 3 GAME_BALL_RIGHT, bits 4-7 reserved 0 |
| 17 | 1 | `leftGames` |
| 18 | 1 | `rightGames` |
| 19 | 1 | `leftScore` |
| 20 | 1 | `rightScore` |
| 21 | 1 | padel only: `leftPoint` |
| 22 | 1 | padel only: `rightPoint` |

| Sport | `*Games` | `*Score` | `*Point` | GAME_BALL |
|---|---|---|---|---|
| squash, volleyball, short volleyball | games won in the match | points in the current game (temporary, as the LEDs show) | - | `Game::willWinOnNextPointScored` (committed) |
| padel | sets won | gems in the current set | `PadelPoint` 0 Love, 1 15, 2 30, 3 40, 4 Ad (temporary); with TIEBREAK the raw tiebreak points | gem ball, `PadelGemScorer::willWinOnNextRally` |

CELEBRATION and GAME_OVER (body 22, 2 chunks):

| Off | Size | Field |
|---|---|---|
| 8 | 4 | `leftUid` |
| 12 | 4 | `rightUid` |
| 16 | 1 | winner side (0 left, 1 right) |
| 17 | 1 | `leftScore` of the finished game (padel: gems of the set) |
| 18 | 1 | `rightScore` |
| 19 | 1 | `leftGames` after this game (padel: sets) |
| 20 | 1 | `rightGames` |
| 21 | 1 | variant: 0 normal, 1 bajgiel (`CelebrationVariant`) |

## 12. Roster sync

- `rosterVersion` = CRC-32 (ISO-HDLC, as zlib `crc32`; check value `0xCBF43926` for ASCII
  `123456789`) over `count` (1 B) followed by every entry's 16-byte wire form (uid, r, g, b,
  name) in index order. Only the board computes it; the watch compares it.
- The watch caches entries (per board id) and re-reads them only when `rosterVersion` differs
  from its cache.
- Read: write ROSTER `[PROTO, index]` (2 B) to set the per-connection cursor (0 at connect),
  then each read returns the entry at the cursor and advances it. A full re-read = 1 write +
  `count` reads (32 profiles ~ 33 requests, ~8 s at the Connect IQ tick; only after a roster
  change).

ROSTER read - 19 B, or 3 B past the end:

| Off | Size | Field |
|---|---|---|
| 0 | 1 | `PROTO` |
| 1 | 1 | `index` |
| 2 | 1 | `count` (roster size) |
| 3 | 4 | `uid` |
| 7 | 3 | `r`, `g`, `b` (raw stored colour) |
| 10 | 9 | name, printable ASCII, NUL-padded (a 9-character name has no NUL) |

## 13. Optimistic rendering (watch)

- SCORE and UNDO only: the watch applies the sport's rules locally at once, shows the result as
  pending (grey), and keeps the press's `seq`.
- A state with `ackSeq` = that `seq` (or a later one) settles it: the watch adopts the board's
  state (buzz on APPLIED). A non-zero `ackStatus` rolls back to the board's state.
- With nothing pending the watch always adopts the board's state.
- Every other command waits for the board's state; the watch shows no change until it arrives.
- Presses while disconnected, unauthenticated or before the first state are ignored.
- **Input debounce is the watch's job; the protocol stays fast.** The board applies a command per
  50 ms tick with no debounce of its own (BLE has no RF bounce). A court is a misclick
  environment, so the watch drops presses the way the board's fob does: SCORE / UNDO 750 ms per
  input, other actions 500 ms, and after a screen-changing command (SELECT_SPORT,
  START_TOURNAMENT, START_MATCH, SKIP, NEXT_GAME, BACK) every input for 1000 ms and until the
  board's state shows the new screen. Dropped presses are not queued.

## 14. Limits

| Limit | Value | Source |
|---|---|---|
| Frame size | 20 B, every frame | Connect IQ write limit |
| Outstanding requests (watch) | 1 | Connect IQ |
| Registered profiles (watch) | 1 used of 3 | Connect IQ |
| Paired watches (board) | 8, LRU eviction | locked |
| Connections (board) | 4; advertising stops at 4 | locked; NimBLE-Arduino defaults to 3 (`CONFIG_BT_NIMBLE_MAX_CONNECTIONS`), #80 sets 4; the S3 controller allows 6 activities (`CONFIG_BT_CTRL_BLE_MAX_ACT`) |
| Roster | 32 entries | `PlayerRosterLimits::MAX_PLAYERS` |
| Name | 9 chars | `PlayerRosterLimits::NAME_SIZE` - 1 |
| State body | 255 B (15 chunks) | chunk header |
| Latency press -> board | ~0.5 s | POC, Connect IQ tick |

## 15. Crypto availability

| Side | API | Status |
|---|---|---|
| Board | mbedtls 2.28.7 in the pinned Arduino core 2.0.17 (`framework-arduinoespressif32` 3.20017, IDF 4.4): `mbedtls_md_hmac()` / `mbedtls_md_hmac_starts()` (`mbedtls/md.h`), `MBEDTLS_MD_C` and `MBEDTLS_SHA256_C` enabled, `CONFIG_MBEDTLS_HARDWARE_SHA` 1 on the S3. Random: `esp_fill_random()` (true RNG while the radio is on, which BLE ensures) | **verified** by reading the installed core's headers and sdkconfig; not compiled |
| Watch | `Toybox.Cryptography.HashBasedMessageAuthenticationCode` (`:algorithm => HASH_SHA256`, `:key`; `update()`, `digest()`), API 3.0.0; `Cryptography.randomBytes(size)`, API 3.0.0. The POC's manifest targets `minApiLevel` 6.0.0, so both exist | **verified** from developer.garmin.com API docs; not run on a watch. Native HMAC, so no RFC 2104 construction from `Hash` is needed. Whether a manifest permission is required is not stated on the class page - check in #81 |

The MACs are standard HMAC-SHA256 (RFC 2104), truncated to the first 8 bytes. Test vectors in
section 16 were computed with Python's `hmac` / `hashlib`.

## 16. Worked examples (test vectors)

Synthetic values throughout: board id `0x12345678`, key `00 01 .. 0f`, slot 2,
`Nb = a0 a1 .. a7`, `Nw = b0 b1 .. b7`, pairing code 4271 (`0x10AF`). All bytes hex.

### 16.1 Advertising

Normal (`ADV_IND`, 19 B):
```
0f ff ff ff 53 43 42 44 00 78 56 34 12 00 00 00 02 01 06
```
Pairing, code 4271 (19 B):
```
0f ff ff ff 53 43 42 44 00 78 56 34 12 01 af 10 02 01 06
```
`SCAN_RSP` (30 B; service UUID little-endian, name `Score-5678`):
```
11 07 35 97 e2 c9 8d b7 b7 93 31 4f 44 2f 01 00 78 0d 0b 09 53 63 6f 72 65 2d 35 36 37 38
```
VERSION read for firmware `0.6.187` (8 B): `00 30 2e 36 2e 31 38 37`

### 16.2 Mutual authentication

```
AUTH read   (10 B): 00 00 a0 a1 a2 a3 a4 a5 a6 a7
  HMAC-SHA256(K, 57 a0 a1 a2 a3 a4 a5 a6 a7)
    = 4e470a94b924acae1ef4234f341a5c760ddebc1858661c39580b7201b2a95047
AUTH write  (19 B): 00 01 02 b0 b1 b2 b3 b4 b5 b6 b7 4e 47 0a 94 b9 24 ac ae
  HMAC-SHA256(K, 42 b0 b1 b2 b3 b4 b5 b6 b7)
    = 1857e1b4a96ebf5a8236582beab5ab29d65cef89dc6c9aab8e492a0a97ed76ce
AUTH read   (11 B): 00 02 00 18 57 e1 b4 a9 6e bf 5a
Failure     ( 3 B): 00 02 01
```

### 16.3 Pairing

```
PAIRING write (4 B):  00 01 af 10
PAIRING read  (20 B): 00 02 00 02 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f
Wrong code    (3 B):  00 02 02
```
The watch stores `{0x12345678, 2, 00 01 .. 0f}` and authenticates as in 16.2.

### 16.4 Roster read

Roster: `ANNA` uid `0x0A0B0C0D` rgb `ff 00 00`; `KRYSTIAN` uid `0x11223344` rgb `00 40 ff`;
`OLA` uid `0x55667788` rgb `00 c0 30`.

```
rosterVersion input (49 B):
  03 0d 0c 0b 0a ff 00 00 41 4e 4e 41 00 00 00 00 00 44 33 22 11 00 40 ff
  4b 52 59 53 54 49 41 4e 00 88 77 66 55 00 c0 30 4f 4c 41 00 00 00 00 00 00
rosterVersion = 0x150fe75b (on the wire: 5b e7 0f 15)

ROSTER write (2 B):  00 00
ROSTER read  (19 B): 00 00 03 0d 0c 0b 0a ff 00 00 41 4e 4e 41 00 00 00 00 00
ROSTER read  (19 B): 00 01 03 44 33 22 11 00 40 ff 4b 52 59 53 54 49 41 4e 00
ROSTER read  (19 B): 00 02 03 88 77 66 55 00 c0 30 4f 4c 41 00 00 00 00 00 00
ROSTER read  (3 B):  00 03 03                 (past the end)
```

### 16.5 MENU state

Cursor on padel, four sports. Body 14 B, one chunk, `stateSeq` 1:
```
STATE notify (17 B): 00 01 01 01 00 00 00 5b e7 0f 15 04 04 04 01 02 03
```

### 16.6 SCORE with confirm (squash)

Squash, ANNA left, KRYSTIAN right, games 1:0, score 4:3. The watch shows 5:3 pending at once:
```
COMMAND write (4 B): 00 08 07 00              SCORE left, seq 7
```
The board applies it and pushes `stateSeq` 0x2a (body 21 B: PLAYING, squash, ack 7 APPLIED,
UNCOMMITTED, games 1:0, score 5:3):
```
STATE notify (20 B): 00 2a 02 13 01 07 00 5b e7 0f 15 0d 0c 0b 0a 44 33 22 11 01
STATE notify ( 7 B): 00 2a 12 01 00 05 03
```
`ackSeq` 7 settles the pending press. 4 s later the commit pushes the same body with flags
`00`. A SCORE sent on GAME_OVER (`00 08 08 01`) comes back with `ackSeq` 8, `ackStatus` 1
WRONG_SCREEN, and the watch rolls back.

### 16.7 Padel PLAYING

Tiebreak: sets 1:0, gems 6:6, raw points 3:2, committed (flags `02`), ack 12:
```
STATE notify (20 B): 00 2b 02 13 04 0c 00 5b e7 0f 15 0d 0c 0b 0a 44 33 22 11 02
STATE notify ( 9 B): 00 2b 12 01 00 06 06 03 02
```
Ladder: sets 0:0, gems 2:1, committed deuce, then an uncommitted rally to the right -> 40:Ad
(points 3, 4; flags `01`), ack 13:
```
STATE notify (20 B): 00 2c 02 13 04 0d 00 5b e7 0f 15 0d 0c 0b 0a 44 33 22 11 01
STATE notify ( 9 B): 00 2c 12 00 00 02 01 03 04
```

### 16.8 Too-old and too-new board

Hypothetical released watch app with `MIN_BOARD_PROTO = 1`, `MAX_BOARD_PROTO = 2`
(a v0 watch has `MIN = MAX = 0`):

| Advertising (`ADV_IND`) | `PROTO` | Watch |
|---|---|---|
| `0f ff ff ff 53 43 42 44 00 78 56 34 12 00 00 00 02 01 06` | 0 | too old: no connect; "Update the scoreboard firmware (board v0, needs v1)" |
| `0f ff ff ff 53 43 42 44 03 78 56 34 12 00 00 00 02 01 06` | 3 | too new: no connect; "Update the watch app" |

Unauthenticated read of STATE or ROSTER (1 B): `00`

## 17. Open questions and deviations

1. **Service UUID is not in `ADV_IND` (deviation from the locked filter).** UUID + locked
   manufacturer data = 37 B against 31 (4.1), and Connect IQ never sees `SCAN_RSP` (passive
   scan, CIQQA-789). Chosen: manufacturer data in `ADV_IND`, UUID in `SCAN_RSP`; the watch
   filters on company id + magic before connecting and checks the service UUID in the GATT
   table after. Accidental matches are as unlikely (company + 4-byte magic); deliberate
   impostors are stopped by authentication either way. Rejected alternatives: dropping
   manufacturer fields (would lose board id, version or pairing code, all locked); a 16-bit
   UUID (needs a SIG assignment). Accepted by the user 2026-10-02.
2. **Connect IQ manufacturer-data parsing is unverified on any watch.** The POC proved only
   `getServiceUuids()` from `ADV_IND` on the FR970. A bug report (fenix 8, Aug 2026, status
   Acknowledged) has `getManufacturerSpecificData()` / `getRawData()` returning null for
   essentially every result. First spike for #81: advertise 16.1 from a bare S3 and log both
   calls on the FR970. If manufacturer data is unreadable, the fallback is service data or
   the UUID in `ADV_IND` with a shorter identity - a v0 redesign of section 4.
3. The forum claim that Connect IQ truncates the advertising record to 20 B is unverified;
   the layout keeps the identifying 16 B first so it would not matter.
4. **UNDO carries a side.** The locked list has `UNDO` without arguments, but the board's
   undo is per side (C left, D right, `losePoint(side)`); there is no "undo the last point".
5. **Additions not in the locked lists:** `ackStatus` beside `ackSeq` (the watch needs to know a
   press was rejected to roll back); SYNC (`0x0D`) for re-requesting a full state; the pairing
   request carries the chosen code; the state header carries `sport` on every screen.
6. **END_MATCH dropped (resolved 2026-10-02).** It would have been a new GAME_OVER ->
   CHOOSE_PLAYERS transition no button has. The watch mirrors the fob instead: NEXT_GAME (C/D)
   -> MATCH_START, then BACK walks MATCH_START -> CHOOSE_PLAYERS -> MENU. `0x0C` stays unused
   so SYNC keeps `0x0D`.
7. PLAYING, MATCH_START, CELEBRATION and GAME_OVER take 2 notifications because they carry
   4-byte uids (locked). If #81 measures that the second chunk costs a Connect IQ tick, v0
   can switch the state to 1-byte roster indexes (commands keep uids) and fit PLAYING in one.
8. Connect IQ's per-profile characteristic limit is undocumented; the POC used 2, this uses 6
   (+ 2 CCCDs). Check in #81's first spike.
9. Not protocol, for #80: WiFi + BLE against the RMT was **measured on V1 (2026-10-02)**: Dev
   Mode STA, advertising, 2 subscribed centrals at 20 notifies/s and fob presses in a match
   gave 0 bails, 0 stale, max 5 us late. Four connections were not measured (judged
   unrealistic by the user).
