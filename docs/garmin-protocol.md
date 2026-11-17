# Garmin App Remote - BLE protocol (v2)

Contract between the scoreboard firmware (task #80) and the Connect IQ watch app (task #81).
Spec for task #79. Background and measurements: the POC (task #66).

Status: **protocol version 2** (draft, task #623): v1's frames plus one tournament model shared by
the board, tablets and other boards (section 20). v1 was frozen at the first public Connect IQ store
release (watch app 1.0.0); v0 never reached the public. An incompatible change bumps `PROTO` (see
[Versioning](#5-versioning-and-compatibility)).
Deviations from the locked task decisions are listed in [Open questions](#17-open-questions-and-deviations).

## 1. Model

- The watch is always the BLE central, the board the GATT peripheral (Connect IQ cannot be a peripheral).
- The board is the single source of truth for its screen. A tournament's games are records that
  peers (tablets, later boards) merge (section 20). The watch sends **semantic commands** (not virtual
  A/B/C/D presses); the board applies them through its normal mode/view logic and pushes the
  resulting **screen state**; the watch renders that state.
- Several watches (up to 4 connected, 8 paired) may control one board at once; each sees every
  state change and, during a match, the heart rates the others share (section 18).
- No BLE bonding (it crashed a Venu 3S, CIQQA-4568). Trust is an app-level, mutual
  challenge-response over a per-watch 16-byte key issued in a user-opened pairing window.
- The feature ("Garmin App Remote") is opt-in, off by default, and independent of Dev Mode.
  While enabled, the board advertises from boot.

## 2. Conventions

- **Every frame starts with the protocol version byte** (`PROTO`, currently `0x02`): every
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

<!-- generated: constants -->
<!-- Do not edit: generated from docs/protocol/ by `python helpers/check_protocol.py build`. -->
| Name | Value | Meaning |
|---|---|---|
| `PROTO` | `0x02` | Protocol version. Byte 0 of every characteristic value and the advertised version byte. |
| `COMPANY_ID` | `0xFFFF` | Bluetooth SIG test company id carried in the manufacturer data, until a registered one exists. |
| `ADV_FLAG_PAIRING` | `0x01` | Bit 0 of the advertising flags byte: the pairing window is open and the code is present. |
| `MAX_FRAME` | 20 | Maximum frame size in bytes; every read, write and notify is at most this. |
| `MAX_ADV` | 31 | Maximum BLE advertising payload in bytes. |
| `CHUNK_HEADER` | 3 | Fixed STATE notify header: PROTO, stateSeq, index<<4 \| count. |
| `CHUNK_BODY` | 17 | Body bytes per STATE notify chunk. |
| `MAX_CHUNKS` | 15 | Maximum chunks one state body splits into. |
| `MAX_STATE_BODY` | 255 | Maximum state body length in bytes. |
| `STATE_HEADER` | 8 | Fixed state body header: screen, sport, ackSeq, ackStatus, rosterVersion (u32). |
| `KEY_SIZE` | 16 | Length of the per-watch HMAC key the board issues on pairing. |
| `NONCE_SIZE` | 8 | Length of the board nonce Nb and the watch nonce Nw. |
| `MAC_SIZE` | 8 | Truncated MAC length - the first 8 bytes of HMAC-SHA256. |
| `SLOT_COUNT` | 8 | Paired clients a board holds; the 9th pairing evicts the least recently authenticated slot. |
| `MAX_CONNECTIONS` | 4 | Concurrent BLE connections the board accepts; it stops advertising while all are open. |
| `ROSTER_ENTRY_SIZE` | 16 | Wire size of one roster entry: uid (u32), r, g, b, then a 9-byte name. |
| `ROSTER_NAME_SIZE` | 9 | Bytes of the roster entry's name field, printable ASCII, NUL-padded. |
| `VERSION_TEXT_MAX` | 19 | Maximum firmware version string bytes in the VERSION read. |
| `PAIRING_CODE_COUNT` | 10000 | Number of distinct pairing codes (0000-9999). |
| `PAIRING_WINDOW_MS` | 120000 ms | How long the pairing window stays open. |
| `UNAUTH_TIMEOUT_MS` | 10000 ms | How long an unauthenticated connection may stay open before the board disconnects it. |
| `FAILED_PROOF_DISCONNECT_MS` | 2000 ms | Delay before the board disconnects after answering a failed AUTH proof. |
| `HEART_LIST` | 3 | Heart-rate values per HEART notify (every other connection at the 4-connection limit). |
| `HEART_EXPIRY_MS` | 15000 ms | How long a shared heart-rate value lasts after its last write. |
| `HEART_MIN_INTERVAL_MS` | 1000 ms | Minimum interval between HEART notifies to one connection. |
| `RESULTS_HEADER` | 9 | RESULTS read header: PROTO, index (u16), count (u16), tournamentId (u32). |
| `RESULT_ENTRY_SIZE` | 11 | Wire size of one finished-game entry: uidA, uidB, scoreA, scoreB, turn. |
| `RESULTS_MAX` | 128 | Most live (not deleted) game records per tournament. |
| `TOMBSTONE_MAX` | 32 | Most deleted records (tombstones) a board keeps per tournament. |
| `TOURNAMENT_ROSTER_MAX` | 16 | Most roster entries a tournament holds. |
| `RECORD_SIZE` | 18 | Wire size of one game record: id (u32), rev, order (u16), uidA, uidB, scoreA, scoreB, flags. |
<!-- /generated: constants -->

UUIDs (fresh, not the POC's):

<!-- generated: uuids -->
<!-- Do not edit: generated from docs/protocol/ by `python helpers/check_protocol.py build`. -->
| Name | UUID | Item |
|---|---|---|
| `SERVICE_UUID` | `0d780001-2f44-4f31-93b7-b78dc9e29735` | The Garmin App Remote primary service. |
| `VERSION_UUID` | `0d780002-2f44-4f31-93b7-b78dc9e29735` | VERSION characteristic. |
| `COMMAND_UUID` | `0d780003-2f44-4f31-93b7-b78dc9e29735` | COMMAND characteristic. |
| `STATE_UUID` | `0d780004-2f44-4f31-93b7-b78dc9e29735` | STATE characteristic. |
| `ROSTER_UUID` | `0d780005-2f44-4f31-93b7-b78dc9e29735` | ROSTER characteristic. |
| `AUTH_UUID` | `0d780006-2f44-4f31-93b7-b78dc9e29735` | AUTH characteristic. |
| `PAIRING_UUID` | `0d780007-2f44-4f31-93b7-b78dc9e29735` | PAIRING characteristic. |
| `HEART_UUID` | `0d780008-2f44-4f31-93b7-b78dc9e29735` | HEART characteristic. |
| `RESULTS_UUID` | `0d780009-2f44-4f31-93b7-b78dc9e29735` | RESULTS characteristic. |
| `TOURNAMENT_UUID` | `0d78000a-2f44-4f31-93b7-b78dc9e29735` | TOURNAMENT characteristic. |
<!-- /generated: uuids -->

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

<!-- generated: frames:ADVERTISING -->
<!-- Do not edit: generated from docs/protocol/ by `python helpers/check_protocol.py build`. -->
**ADV_IND** (advertise, board -> watch, 19 B): The identifying manufacturer data, then the Flags AD.

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `ad_len` | `0x0F` - Length of this AD structure (15 bytes follow). |
| 1 | 1 | `ad_type` | `0xFF` - Manufacturer specific. |
| 2 | 2 | `company_id` | `COMPANY_ID` (`0xFFFF`) - Company id. |
| 4 | 4 | `magic` | `53 43 42 44` - Product magic. |
| 8 | 1 | `proto` | `PROTO` (`0x02`) - Protocol version. |
| 9 | 4 | `board_id` | Board id, drawn per board. |
| 13 | 1 | `flags` | Bit 0 PAIRING (window open); bits 1-7 reserved, sent 0, ignored. |
| 14 | 2 | `pairing_code` | Pairing code 0-9999 while PAIRING, else 0x0000 (ignored). |
| 16 | 3 | `flags_ad` | `02 01 06` - Flags AD: LE General Discoverable, BR/EDR not supported. |

Broadcast always while BLE is up. The watch's pre-connect filter matches company id + magic (+ version + board id + flags) on this payload. The manufacturer bytes after the company id are frozen across protocol versions.

**SCAN_RSP** (scan_response, board -> watch, 30 B): The 128-bit service UUID and the local name.

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `ad_len` | `0x11` - Length of this AD structure (17 bytes follow). |
| 1 | 1 | `ad_type` | `7` - Complete list of 128-bit service UUIDs. |
| 2 | 16 | `service_uuid` | `SERVICE_UUID` - The primary service UUID, little-endian. |
| 18 | 1 | `name_len` | `0x0B` - Length of the name AD structure (11 bytes follow). |
| 19 | 1 | `name_type` | `9` - Complete local name. |
| 20 | 10 | `name` | Score-XXXX, XXXX = low 16 bits of the board id, uppercase hex. |

For phones and BLE tools that scan actively; the watch never uses it. The name is for humans only and never used for matching.
<!-- /generated: frames:ADVERTISING -->

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
- **v0 was development-only.** It was frozen for store release 1.0.0, but that release was never
  published, so no public watch app speaks v0. The user moved the release to v1 (2026-11-02) so
  that HEART (section 18) is mandatory from the first public version (Open questions 10).
- **v1 is frozen at the first public Connect IQ store release** (watch app 1.0.0, accepting
  exactly v1: `MIN = MAX = 1`). v1 = v0's frames with `PROTO` `0x01`, plus HEART. A board still on
  v0 firmware is shown "Update the scoreboard firmware" (16.8).
- From then on every incompatible change bumps `PROTO`, and the range check applies.
- **v2** (#623) = v1's frames with `PROTO` `0x02`, minus tablet mode (v1's TOURNAMENT_WAIT,
  TABLET_MODE, ADV_FLAG_TOURNAMENT, the tablet slot, COMMIT_MIRROR, the outbox and mirror) and with
  the TOURNAMENT characteristic redesigned as record sync (section 20). A watch app accepting only
  v1 shows "Update the watch app" for a v2 board; a v2 watch app shows "Update the scoreboard
  firmware" for a v1 board (16.8).
- The watch checks the version from the advertising data before connecting, and again from the
  VERSION characteristic after connecting (both must be in range and equal).

| Board `PROTO` | Watch behaviour |
|---|---|
| `< MIN_BOARD_PROTO` | refuse to connect or pair; show "Update the scoreboard firmware" + board version + required minimum |
| `> MAX_BOARD_PROTO` | refuse to connect or pair; show "Update the watch app" (Connect IQ store) |
| in range | proceed |

The board never refuses a watch by version: a watch whose frames carry a different `PROTO`
simply gets them dropped.

**Incompatible (bump `PROTO`):** any change to an existing frame's byte layout,
field size or offset; a new meaning for an existing value (command, screen, sport, status, flag
bit); removing a command, screen, sport or characteristic; changing a UUID, the advertising
layout, the chunk header, the auth or pairing exchange, the MAC construction or truncation;
making a previously optional behaviour mandatory for the watch.

**Compatible (no bump):** appending bytes at the end of a frame or state body (receivers ignore
trailing bytes); new screen ids (an old watch shows "board busy"); new sport ids (an old watch
does not offer them); new commands (old watches never send them); new status codes (an old
watch treats any non-zero status as a rejection); new flag bits (ignored); new characteristics.

Compatible changes made in v1 (no bump): SIDES_SWAPPED (#510) - PLAYING flags bit 6, and a flags
byte appended to INTRO (offset 16), CELEBRATION / GAME_OVER (offset 22) and MATCH_START (offset
20, #513), still 1 and 2 chunks (section 11.3, 16.11). LAST_KNOWN / LAST_RIGHT (#509) - PLAYING
flags bits 4-5, the newest point's side (section 11.3, 16.12); a board without them sends 0 there,
which reads as "unknown". Store app 1.0.0 ignores them. RESULTS (#527) - a new characteristic,
the tournament's finished games (section 19, 16.13); optional for the watch, since a v1 board
from before it lacks the characteristic. Store app 1.0.0 never looks for it. Offline games (#563) -
ADD_OFFLINE_GAME / REMOVE_OFFLINE_GAME (0x0F / 0x10, a COMMAND frame of up to 13 B), a `rev` u16 on
RESULTS' past-the-end frame (11 B instead of 9 B), and RESULTS `turn` bit 7 = OFFLINE (section 19,
16.14). The `turn` bit is a deliberate exception to the rule above: a new meaning for an existing
field, normally a `PROTO` bump (section 17, item 12). v1's tablet mode (#583, #612) was replaced
in v2, not extended (above).

**Frozen across all versions:** company id, magic and the version byte's offset in the
advertising data; the VERSION characteristic's UUID and its byte 0.

## 6. GATT layout

One primary service, nine characteristics (RESULTS and TOURNAMENT are optional for the watch,
sections 19 and 20). One
Connect IQ profile (limit is 3).

<!-- generated: gatt -->
<!-- Do not edit: generated from docs/protocol/ by `python helpers/check_protocol.py build`. -->
| Characteristic | Properties | Auth required | Purpose |
|---|---|---|---|
| AUTH | read, write | no | Mutual challenge-response that unlocks COMMAND, STATE, ROSTER, HEART and RESULTS for one connection. Each side proves it holds the per-watch key K by MACing the other side's fresh nonce; neither the key nor anything replayable crosses the air. |
| COMMAND | write | yes | The one write path the watch uses to act on the board: semantic commands (select sport, score, swap, start, ...) rather than raw button events. |
| HEART | write, notify | yes | Heart-rate relay: the watch writes its wearer's value, the board notifies the other connected watches' values. |
| PAIRING | write, read, notify | no | The one-time pairing exchange: the watch sends the code it picked, the board answers with a fresh key on success. |
| RESULTS | write, read | yes | The finished games of the current tournament, for a watch that draws the standings. |
| ROSTER | write, read | yes | The active roster (players or teams): a write sets the per-connection read cursor, each read returns the entry at the cursor and advances it. |
| STATE | read, notify | yes | The board's current screen state: a body split into chunked notifications, plus a read of chunk 0 for a quick sync check. |
| TOURNAMENT | write, read | yes | The board's tournament as state for peers to sync: a status, a change log and the stored game records to read, records to merge and roster entries to stage. |
| VERSION | read | no | The board's protocol version and firmware version string, read once on connect to confirm compatibility. |
<!-- /generated: gatt -->

Before authentication the board accepts writes to COMMAND, ROSTER, HEART, RESULTS and TOURNAMENT
at the ATT level and **discards** them, answers reads of STATE, ROSTER, RESULTS and TOURNAMENT with
the 1-byte frame
`[PROTO]`, and sends
no notifications. Notify-only, never indications (unreliable on some watches).

### 6.1 VERSION

<!-- generated: frames:VERSION -->
<!-- Do not edit: generated from docs/protocol/ by `python helpers/check_protocol.py build`. -->
**Version** (read, board -> watch, 2-20 B): PROTO followed by the firmware version string.

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `proto` | `PROTO` (`0x02`) - Protocol version. |
| 1 | 1-19 | `version` | Firmware version, ASCII, no NUL (e.g. 0.6.187). |

Read once per connection; a shorter or foreign-PROTO value is ignored.
<!-- /generated: frames:VERSION -->

## 7. Connection sequence (watch)

| Step | Request | Notes |
|---|---|---|
| 1 | scan, filter (4.4), `pairDevice()` | `pairDevice` does not persist across launches: every launch rescans |
| 2 | (on connect) check service + the seven mandatory characteristics exist (VERSION to HEART) | any missing -> disconnect, treat as not a scoreboard; RESULTS and TOURNAMENT missing are not an error (sections 19, 20) |
| 3 | read VERSION | out of range or `!=` advertised -> disconnect + message (section 5) |
| 4 | read AUTH | challenge (8.1) |
| 5 | write AUTH | proof |
| 6 | read AUTH | result; verify the board's MAC |
| 7 | write STATE CCCD `01 00` | the board then notifies the full current state |
| 8 | if `rosterVersion` differs from the cache of the active roster's kind: write ROSTER `[PROTO, 0]`, read `count` times | section 12 |
| 9 | optional: write HEART CCCD `01 00` | only a watch that shows the other watches' heart rates; the board then notifies the current list if it is not empty (section 18) |
| 10 | optional: write RESULTS `[PROTO, n]`, read | only a watch that shows the tournament; `n` = its cached count, 0 if none (section 19.2) |

Pairing inserts its exchange (section 9) before step 4 on the same connection. Presses are
ignored until step 7's state has arrived. Sharing the watch's own heart rate needs no CCCD: it is
HEART writes while the state shows PLAYING (section 18). On disconnect the watch drops all pending presses and
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

<!-- generated: frames:AUTH -->
<!-- Do not edit: generated from docs/protocol/ by `python helpers/check_protocol.py build`. -->
**Challenge** (read, board -> watch, 10 B): The board nonce Nb for this connection.

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `proto` | `PROTO` (`0x02`) - Protocol version. |
| 1 | 1 | `type` | `0x00` CHALLENGE - Marks this read as a challenge. `0` CHALLENGE, `1` PROOF, `2` RESULT |
| 2 | 8 | `nb` | Board nonce, fresh per connection. |

The watch reads AUTH before it has written a proof (connection sequence step 4) and keeps Nb for its MAC. The board draws Nb once per connection, so every read before the proof returns the same value.

**Proof** (write, watch -> board, 19 B): Which key the watch holds, its own nonce, and a MAC proving it holds that key.

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `proto` | `PROTO` (`0x02`) - Protocol version. |
| 1 | 1 | `type` | `0x01` PROOF - Marks this write as a proof. `0` CHALLENGE, `1` PROOF, `2` RESULT |
| 2 | 1 | `slot` | Board slot the client's key was issued in (PAIRING result). Range 0-SLOT_COUNT - 1. |
| 3 | 8 | `nw` | Watch nonce, fresh per attempt. |
| 11 | 8 | `watch_mac` | First 8 bytes of HMAC-SHA256(K, 0x57 \|\| Nb). |

The watch draws Nw, computes MAC("W" \|\| Nb) with the key of the slot the board issued it, and writes this frame once (step 5). The board checks the slot holds a key and compares the MAC in constant time; on success the connection is authenticated and the slot's LRU counter is bumped. A shorter frame, a foreign PROTO or a type other than PROOF is ignored.

**Result** (read, board -> watch, 3-11 B): The board's verdict on the proof, and on success its own proof of K.

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `proto` | `PROTO` (`0x02`) - Protocol version. |
| 1 | 1 | `type` | `0x02` RESULT - Marks this read as a result. `0` CHALLENGE, `1` PROOF, `2` RESULT |
| 2 | 1 | `status` | Outcome of the proof. `0` OK, `1` BAD_PROOF, `2` UNKNOWN_SLOT |
| 3 | 8 | `board_mac` | First 8 bytes of HMAC-SHA256(K, 0x42 \|\| Nw). Present only when `status == OK`. |

The watch reads AUTH after its proof (step 6). OK with a matching board MAC: the board is trusted. OK with a wrong or missing MAC: the watch disconnects, marks the board untrusted (no auto-reconnect until the user retries) and reports a failed authentication - an impostor advertising the right board id cannot produce this MAC. BAD_PROOF or UNKNOWN_SLOT: the watch shows "pair again".
<!-- /generated: frames:AUTH -->

`BAD_PROOF` / `UNKNOWN_SLOT` mean the board no longer holds this watch's key (evicted, "forget
all", NVS wiped): the watch shows "pair again" rather than "untrusted".

## 9. Pairing

1. User opens the board's settings (OPCJE; CONFIG on an English board) > GARMIN and picks PARUJ
   (Pair); if GARMIN is off, the board turns AKTYWNY (Enabled) on and opens the window once BLE
   is up. The window is 120 s; the board draws a
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

<!-- generated: frames:PAIRING -->
<!-- Do not edit: generated from docs/protocol/ by `python helpers/check_protocol.py build`. -->
**None (no request yet)** (read, board -> watch, 2 B): What a read returns before the watch has written a request.

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `proto` | `PROTO` (`0x02`) - Protocol version. |
| 1 | 1 | `type` | `0x00` NONE - Marks this read as "no request". `0` NONE, `1` REQUEST, `2` RESULT |

The watch reads this on a fresh connection to confirm no pairing is in progress.

**Request** (write, watch -> board, 4 B): The watch asks to pair, carrying the code it picked from the advertising.

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `proto` | `PROTO` (`0x02`) - Protocol version. |
| 1 | 1 | `type` | `0x01` REQUEST - Marks this write as a pairing request. `0` NONE, `1` REQUEST, `2` RESULT |
| 2 | 2 | `code` | The 4-digit code shown next to the chosen board. |

Sent once on the pairing connection. The board answers RESULT whether the code matched or not; a request while the window is closed gets WINDOW_CLOSED.

**Result** (read, board -> watch, 3-20 B): The board's answer: status, and on success the slot id and key.

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `proto` | `PROTO` (`0x02`) - Protocol version. |
| 1 | 1 | `type` | `0x02` RESULT - Marks this read as a pairing result. `0` NONE, `1` REQUEST, `2` RESULT |
| 2 | 1 | `status` | Outcome of the request. `0` OK, `1` WINDOW_CLOSED, `2` BAD_CODE, `3` FULL_RETRY |
| 3 | 1 | `slot` | Board slot the new key was issued in. Present only when `status == OK`. |
| 4 | 16 | `key` | The watch's new 16-byte HMAC key. Present only when `status == OK`. |

Read (or notified) after the request. OK carries slotId + the 16-byte key the watch stores as {boardId, slotId, key}. Any non-zero status means no key: the watch shows why and the user may retry. The board restarts its 10 s unauthenticated timeout on delivery.
<!-- /generated: frames:PAIRING -->

The board answers within the write's access callback, so the result is readable as soon as the
write response arrives.

### 9.2 Tablets

A tablet pairs exactly like a watch, in the PARUJ window (the bench board, with no displays to
show a code on, keeps it open), and its key takes one of the
`SLOT_COUNT` slots (persisted, evicted like any other). The board cannot tell a tablet from a
watch; what makes a peer a tablet is only what it reads and writes (section 20). v1's RAM-only
tournament window and tablet slot are gone: a board that reboots keeps the tablet's key, so the
tablet re-joins without a person at the board (20.3).

## 10. Commands

### 10.1 COMMAND frame

<!-- generated: frames:COMMAND -->
<!-- Do not edit: generated from docs/protocol/ by `python helpers/check_protocol.py build`. -->
**Command** (write, watch -> board, 3-13 B): A command id, its sequence number, and the command's arguments.

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `proto` | `PROTO` (`0x02`) - Protocol version. |
| 1 | 1 | `cmd` | Command id. |
| 2 | 1 | `seq` | Sequence number, 1-255. |
| 3 | n | `args` | The command's arguments. - the command table (10.2) |

The arguments depend on the command id (commands.yaml).
<!-- /generated: frames:COMMAND -->

### 10.2 Command table

<!-- generated: commands -->
<!-- Do not edit: generated from docs/protocol/ by `python helpers/check_protocol.py build`. -->
| Id | Name | Args | Size | Valid on | Board effect |
|---|---|---|---|---|---|
| `0x01` | SELECT_SPORT | `sport` uint8 | 4 | MENU | Act on a MENU row (cursor to the row + D). |
| `0x02` | BACK | - | 3 | CHOOSE_PLAYERS, MATCH_START, INTRO, CELEBRATION, GAME_OVER, PROFILE | DeviceMode::goBack(): one step back toward the menu. |
| `0x03` | TOGGLE_PLAYER | `uid` uint32 | 7 | CHOOSE_PLAYERS | Add or remove that profile (D on its row). |
| `0x04` | START_TOURNAMENT | - | 3 | CHOOSE_PLAYERS | -> MATCH_START (D on START). |
| `0x05` | SET_PAIR | `left_uid` uint32, `right_uid` uint32 | 11 | MATCH_START | Make the match between leftUid and rightUid, leftUid on the left. |
| `0x06` | SWAP_SIDES | - | 3 | MATCH_START | Swap court sides (C). |
| `0x07` | START_MATCH | - | 3 | MATCH_START | -> INTRO (D). |
| `0x08` | SCORE | `side` uint8 | 4 | PLAYING | Point to that side (A / B); padel: a rally. |
| `0x09` | UNDO | `side` uint8 | 4 | PLAYING | Undo that side's last point (C = left, D = right). |
| `0x0A` | SKIP | - | 3 | INTRO, CELEBRATION | INTRO -> PLAYING; CELEBRATION -> GAME_OVER. |
| `0x0B` | NEXT_GAME | - | 3 | GAME_OVER | -> MATCH_START, same pair (C/D). |
| `0x0C` | UNUSED_0C | - | - | - | Reserved; never sent. |
| `0x0D` | SYNC | - | 3 | ALL | No state change; re-push the full state. |
| `0x0E` | FOCUS | `target` uint32 | 7 | MENU, CHOOSE_PLAYERS | Move the board's cursor to a row without acting on it (A / B scrolling, silent). |
| `0x0F` | ADD_OFFLINE_GAME | `uid_a` uint32, `uid_b` uint32, `score_a` uint8, `score_b` uint8 | 13 | MATCH_START, INTRO, PLAYING, CELEBRATION, GAME_OVER | Record a game played off the board in the current tournament. |
| `0x10` | REMOVE_OFFLINE_GAME | `tournament_id` uint32, `rev` uint16, `index` uint16 | 11 | MATCH_START, INTRO, PLAYING, CELEBRATION, GAME_OVER | Delete a mistaken offline game from the current tournament. |
| `0x11` | BEGIN_TOURNAMENT | `tournament_id` uint32, `sport` uint8, `count` uint8, `roster_crc` uint32 | 13 | MENU, MATCH_START, INTRO, PLAYING, CELEBRATION, GAME_OVER | Join the board to a tournament from the staged roster, or grow its roster. |
| `0x12` | UNUSED_12 | - | - | - | Reserved; never sent. |
| `0x13` | ACK_CHANGES | `tournament_id` uint32, `up_to` uint16 | 9 | CHOOSE_PLAYERS, MATCH_START, INTRO, PLAYING, CELEBRATION, GAME_OVER | The sender has stored every change up to that change seq durably. |
| `0x14` | SET_SIDE | `side` uint8, `uid` uint32 | 8 | MATCH_START, INTRO, PLAYING, CELEBRATION, GAME_OVER | Put that roster entry on that court side (hot-swap). |
<!-- /generated: commands -->

`side` other than 0/1 -> INVALID. Scoring through BLE bypasses the RF receiver's debounce;
`seq` deduplication replaces it.

FOCUS is cosmetic: it never changes the screen, the selection or any score, and the board plays
no sound for it (a fob scroll beeps; a watch scrolling through a list must not). It exists so the
board's highlight (OLED, e-paper, LEDs) follows the watch's own list while the user scrolls on
the watch. One `target` field serves both screens so the frame has one size: a CHOOSE_PLAYERS
row is named by its `uid` (uids are never 0, so 0 is free for START); a MENU row by its menu-entry
id (PROFILE and CONFIG are never listed, so no client can focus them). Each
FOCUS costs the e-paper at most one partial refresh; requests made during a refresh coalesce
into one, as for fob scrolling.

BEGIN_TOURNAMENT, ACK_CHANGES and SET_SIDE serve tournament sync (section 20); any authenticated
client may send them. 0x12 is unused (UNKNOWN_CMD).

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

<!-- generated: frames:STATE -->
<!-- Do not edit: generated from docs/protocol/ by `python helpers/check_protocol.py build`. -->
**Chunk** (notify, board -> watch, 4-20 B): One chunk of a state body, preceded by PROTO, stateSeq and index/count.

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `proto` | `PROTO` (`0x02`) - Protocol version. |
| 1 | 1 | `state_seq` | Per-connection push counter, +1 per push, wraps. |
| 2 | 1 | `index_count` | index << 4 \| count (index 0-14, count 1-15). |
| 3 | 1-17 | `body` | Body bytes for this chunk. |

The watch reassembles chunks with the same stateSeq; a chunk with a different stateSeq discards the incomplete set. Chunks of one push are sent back to back, in order.
<!-- /generated: frames:STATE -->

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
| 4 | 4 | `rosterVersion` of the active roster (section 12) |
| 8 | n | screen data |

Sports - one wire table, independent of `DeviceModeState`; the watch offers exactly the sports
the board's MENU state lists (built from `ModeSwitchingView`'s `ModeMenuEntry` table, in menu
order):

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
| `0x01` | MENU | `ModeSwitchingMode` | sport list, plus the menu entries it knows (PROFILE / CONFIG never listed) |
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
| 8 | 1 | cursor: menu-entry id of the highlighted row, 0 on a row not listed (PROFILE, CONFIG) |
| 9 | 1 | `n` menu entries |
| 10 | n | menu-entry ids in menu order (today `04 01 02 03`: padel, squash, volleyball, short volleyball) |

A menu-entry id is a `sport` id; FOCUS and SELECT_SPORT name rows by it. A client skips ids it
does not know (room for non-sport rows later).

CHOOSE_PLAYERS (body 16, 1 chunk):

| Off | Size | Field |
|---|---|---|
| 8 | 4 | selected: bit `i` = roster index `i` is in the tournament |
| 12 | 4 | cursor: `uid` of the highlighted profile row, 0 on START |

The rows are START, then one per profile in roster order (there is no exit row; BACK leaves).
Both cursors (MENU, CHOOSE_PLAYERS) follow the fob's A / B scrolling as well as FOCUS.

MATCH_START (body 21, 2 chunks):

| Off | Size | Field |
|---|---|---|
| 8 | 4 | selected bitmask (as above) |
| 12 | 4 | `leftUid` |
| 16 | 4 | `rightUid` |
| 20 | 1 | flags: bit 6 SIDES_SWAPPED, other bits reserved 0 |

INTRO (body 17, 1 chunk):

| Off | Size | Field |
|---|---|---|
| 8 | 4 | `leftUid` |
| 12 | 4 | `rightUid` |
| 16 | 1 | flags: bit 6 SIDES_SWAPPED, other bits reserved 0 |

PLAYING (body 21, padel 23; 2 chunks):

| Off | Size | Field |
|---|---|---|
| 8 | 4 | `leftUid` |
| 12 | 4 | `rightUid` |
| 16 | 1 | flags: bit 0 UNCOMMITTED (scores include points inside the 4 s commit window), bit 1 TIEBREAK (padel), bit 2 GAME_BALL_LEFT, bit 3 GAME_BALL_RIGHT, bit 4 LAST_KNOWN, bit 5 LAST_RIGHT (0 = left; 0 without LAST_KNOWN), bit 6 SIDES_SWAPPED, bit 7 reserved 0 |
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

**SIDES_SWAPPED** (MATCH_START, PLAYING, INTRO, CELEBRATION, GAME_OVER; every sport): the pair now sits on
the court sides reversed against its orientation at its first game. The board latches it per
pair (per `Match`) when that pair's first game finishes (a game left by UNDO at 0:0 does not
count), so a swap at MATCH_START before the first game is the start orientation, not a swap, and
the bit stays 0 throughout the first game. The latch lives as long as the sport mode: back to
CHOOSE_PLAYERS and round-robin pair cycling keep it, like the games tally; a mode change or a
reboot drops it. On MATCH_START the bit follows every swap live (SWAP_SIDES or the fob's C
change the state, so it is pushed); the watch shows it as a hint and never mirrors that screen.
Every watch on the board reads the same bit, so they agree on the match's own left
and right; a watch against older firmware reads the bit as 0 and a missing MATCH_START / INTRO /
result flags byte as 0 (section 5).

**LAST_KNOWN / LAST_RIGHT** (PLAYING, every sport): the court side of the newest point of the
current game that still stands, so a watch that lost its own point order (a reconnect, an app
restart) can pick the side for UNDO (Open questions 4). Only that one point is sent, never the
history. The newest point is the newest entry of the board's `GameScoreHistory` that is not
`lost` (`scored` = uncommitted, or `committed`): an uncommitted point undone is erased, a
committed one undone is `lost` until the commit erases it, so both are skipped. A side that
scores again while its own undone point is still `lost` revives that entry in its old place, so
the order is the one from before the undo.
- Squash, volleyball, short volleyball: unknown (bits 4-5 both 0) when nothing stands - at 0:0,
  or when only `lost` entries are left.
- Padel: the newest standing rally of the current gem; with none (gem 0:0), the winner of the
  previous gem of the set (the gem an UNDO there steps back into, 10.2); at the set's start
  unknown.
- A board without this addition sends both bits 0 (unknown); a watch then keeps its own fallback.

CELEBRATION and GAME_OVER (body 23, 2 chunks):

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
| 22 | 1 | flags: bit 6 SIDES_SWAPPED, other bits reserved 0 |

## 12. Roster sync

- `rosterVersion` = CRC-32 (ISO-HDLC, as zlib `crc32`; check value `0xCBF43926` for ASCII
  `123456789`) over `count` (1 B) followed by every entry's 16-byte wire form (uid, r, g, b,
  name) in index order. Only the board computes it; the watch compares it.
- **Names** on the wire are 1 to 9 printable ASCII characters (`0x20`-`0x7E`), NUL-padded, in
  every roster a board publishes: the board shows a stored profile name with any other byte as
  `?` and an empty one as `P<index>` (the web editor stores printable names only), and
  BEGIN_TOURNAMENT answers INVALID for a staged entry whose name breaks the rule. A reader keeps
  the name byte-exact, so the `rosterVersion` it computes equals the board's.
- The watch caches entries per (board id, roster kind) and re-reads them only when
  `rosterVersion` differs from the cached version of the kind the state's sport names.
- Read: write ROSTER `[PROTO, index]` (2 B) to set the per-connection cursor (0 at connect),
  then each read returns the entry at the cursor and advances it. A full re-read = 1 write +
  `count` reads (32 profiles ~ 33 requests, ~8 s at the Connect IQ tick; only after a roster
  change).

**Active roster.** The board keeps two rosters, players and teams, and publishes one at a
time: `rosterVersion` and the ROSTER characteristic always describe the **active** roster,
the one every uid in the current state belongs to (CHOOSE_PLAYERS' selection and cursor,
`leftUid` / `rightUid`). The kind is not on the wire; both sides derive it from the state's
`sport`:

| `sport` | Roster kind |
|---|---|
| `0x04` padel, `0x02` volleyball, `0x03` short volleyball | teams |
| `0x01` squash, `0x00` (BOOTING, MENU, CONFIG, PROFILE) | players |

- The board selects the roster when it changes mode and switches to it **with** the first
  state of the new mode: until that state is published, every state (pushed or read) keeps the
  old `rosterVersion` and the old roster is read, so a state's uids, its `sport` and its
  `rosterVersion` always name the same roster. A changed `rosterVersion` alone triggers a push.
- A watch retries a failed read only while the latest state still names the version it asked
  for; after a switch the old version is moot.
- A read in flight across a switch returns entries of the new roster (the cursor is per
  connection and survives it). The watch verifies the CRC of what it read against the version
  it asked for, discards a mismatch (never caches it under either kind) and re-reads; the board
  does nothing special.
- A watch that ignores the kind (one cache slot per board) still works: it re-reads on every
  switch between a team sport and the rest.
- While a tournament runs, its roster (the START selection, or a BEGIN_TOURNAMENT's staged list,
  section 20.3) is the active roster of the sport's kind, in place of the stored one; the stored
  roster returns when the board leaves the tournament. `rosterVersion` is then the tournament's
  `roster_crc`.

<!-- generated: frames:ROSTER -->
<!-- Do not edit: generated from docs/protocol/ by `python helpers/check_protocol.py build`. -->
**Write (set cursor)** (write, watch -> board, 2 B): Sets this connection's read cursor (0 at connect).

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `proto` | `PROTO` (`0x02`) - Protocol version. |
| 1 | 1 | `index` | Entry index to read next. |

One write + `count` reads re-reads the whole roster.

**Read** (read, board -> watch, 3-19 B): One roster entry, or just the header past the end.

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `proto` | `PROTO` (`0x02`) - Protocol version. |
| 1 | 1 | `index` | The cursor this read answers. |
| 2 | 1 | `count` | Roster size. |
| 3 | 4 | `uid` | The profile or team id. Present only when `index < count`. |
| 7 | 1 | `r` | Red component, raw stored colour. Present only when `index < count`. |
| 8 | 1 | `g` | Green component. Present only when `index < count`. |
| 9 | 1 | `b` | Blue component. Present only when `index < count`. |
| 10 | 9 | `name` | 1 to 9 printable ASCII characters (0x20-0x7E), NUL-padded (a 9-character name has no NUL); never any other byte (section 12). Present only when `index < count`. |

Each read returns the entry at the cursor and advances it; past the end the cursor stays and the read returns the 3-byte header alone.
<!-- /generated: frames:ROSTER -->

## 13. Optimistic rendering (watch)

- SCORE and UNDO only: the watch applies the sport's rules locally at once, shows the result as
  pending (grey), and keeps the press's `seq`.
- A state with `ackSeq` = that `seq` (or a later one) settles it: the watch adopts the board's
  state (buzz on APPLIED). A non-zero `ackStatus` rolls back to the board's state.
- With nothing pending the watch always adopts the board's state.
- Every other command waits for the board's state; the watch shows no change until it arrives.
- **FOCUS is the exception: the watch's highlight is its own.** The watch moves its highlight
  at once and sends FOCUS for the row it lands on; it never waits for or rolls back on the ack.
  While a FOCUS is unacknowledged the watch ignores the state's cursor (an older cursor would
  pull the highlight back mid-scroll); with nothing outstanding it may adopt the board's cursor,
  which is how a fob scroll reaches the watch. With one outstanding request, the watch keeps only
  the newest row to focus and sends that once the write completes: a fast scroll sends the rows
  it can, never a backlog. FOCUS is not debounced and does not start the screen-change lockout.
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
| HEART values per notify | 3 (every other connection) | `MAX_CONNECTIONS` - 1 |
| HEART notifies | 1 / s per connection, newest content | section 18 |
| Heart rate held by the board | latest per connection, RAM only, 15 s expiry | section 18 |
| Live game records | 128 per tournament; START_MATCH / ADD_OFFLINE_GAME answer INVALID at the cap, a merged record beyond it is dropped | `RESULTS_MAX`, section 20.2 |
| Tombstones | 32; the oldest change is dropped first | `TOMBSTONE_MAX`, section 20.2 |
| Tournament roster | 16 entries | `TOURNAMENT_ROSTER_MAX`, section 20 (one round robin of 16 = 120 games) |

## 15. Crypto availability

| Side | API | Status |
|---|---|---|
| Board | mbedtls 2.28.7 in the pinned Arduino core 2.0.17 (`framework-arduinoespressif32` 3.20017, IDF 4.4): `mbedtls_md_hmac()` / `mbedtls_md_hmac_starts()` (`mbedtls/md.h`), `MBEDTLS_MD_C` and `MBEDTLS_SHA256_C` enabled, `CONFIG_MBEDTLS_HARDWARE_SHA` 1 on the S3. Random: `esp_fill_random()` (true RNG while the radio is on, which BLE ensures) | **verified** by reading the installed core's headers and sdkconfig; not compiled |
| Watch | `Toybox.Cryptography.HashBasedMessageAuthenticationCode` (`:algorithm => HASH_SHA256`, `:key`; `update()`, `digest()`), API 3.0.0; `Cryptography.randomBytes(size)`, API 3.0.0. The POC's manifest targets `minApiLevel` 6.0.0, so both exist | **verified on the FR970** (#81 Gate 0, 2026-10-03): the 16.2 MACs match byte for byte, `randomBytes(8)` works. Native HMAC, so no RFC 2104 construction from `Hash` is needed. No manifest permission is needed (SDK 9.2 has no crypto permission) |

The MACs are standard HMAC-SHA256 (RFC 2104), truncated to the first 8 bytes. Test vectors in
section 16 were computed with Python's `hmac` / `hashlib`.

## 16. Worked examples (test vectors)

<!-- generated: scenarios -->
<!-- Do not edit: generated from docs/protocol/ by `python helpers/check_protocol.py build`. -->
Synthetic values shared by every scenario: board id 0x12345678, key 00..0f, slot 2, Nb a0..a7, Nw b0..b7, pairing code 4271 (0x10AF).

### 16.1 Advertising

The two advertising payloads the board broadcasts while BLE is up, and the VERSION read the watch makes once connected to confirm compatibility.

`advertising/adv_normal` - ADVERTISING advertise:
```
0f ff ff ff 53 43 42 44 02 78 56 34 12 00 00 00 02 01 06    (19 B)
```
The identifying manufacturer data first, then the Flags AD. Board id is the little-endian fixture; flags 0 = no pairing window, pairing code ignored.

`advertising/adv_pairing` - ADVERTISING advertise:
```
0f ff ff ff 53 43 42 44 02 78 56 34 12 01 af 10 02 01 06    (19 B)
```
The pairing window is open, so flags bit 0 is set and the drawn code 4271 (0x10AF) is carried little-endian. The watch lists only advertisers with bit 0 set.

`advertising/scan_rsp` - ADVERTISING scan_response:
```
11 07 35 97 e2 c9 8d b7 b7 93 31 4f 44 2f 01 00 78 0d 0b 09 53 63 6f 72 65 2d 35 36 37 38    (30 B)
```
The 128-bit service UUID little-endian, then the local name Score-XXXX where XXXX is the low 16 bits of the board id (0x5678). Phones and BLE tools see this; the watch never scans it and verifies the UUID from the GATT table instead.

`advertising/version` - VERSION read:
```
02 30 2e 36 2e 31 38 37    (8 B)
```
PROTO then the firmware version string, read once on connect. The watch compares PROTO with the advertised byte; both must be in range and equal.

### 16.2 Mutual authentication

A paired watch reconnects to its board and both sides prove they hold the same key K (slot 2) before the board unlocks state and commands for this connection.

`auth/challenge` - AUTH read:
```
02 00 a0 a1 a2 a3 a4 a5 a6 a7    (10 B)
```
First read on a fresh connection. The board drew Nb when the watch connected; the watch needs it to build a proof the board can check and that is useless on any other connection.

`auth/proof` - AUTH write:
```
02 01 02 b0 b1 b2 b3 b4 b5 b6 b7 4e 47 0a 94 b9 24 ac ae    (19 B)
  HMAC-SHA256(K, 57 a0 a1 a2 a3 a4 a5 a6 a7)
    = 4e470a94b924acae1ef4234f341a5c760ddebc1858661c39580b7201b2a95047
```
The watch names its slot, sends a fresh Nw for the board to MAC, and proves it holds K by MACing "W" || Nb. The "W" prefix keeps the watch's MAC from ever equalling a board MAC over the same nonce.

`auth/result_ok` - AUTH read:
```
02 02 00 18 57 e1 b4 a9 6e bf 5a    (11 B)
  HMAC-SHA256(K, 42 b0 b1 b2 b3 b4 b5 b6 b7)
    = 1857e1b4a96ebf5a8236582beab5ab29d65cef89dc6c9aab8e492a0a97ed76ce
```
The proof matched, so the connection is authenticated. The board answers with its own MAC over "B" || Nw, which the watch checks before trusting anything the board sends - an impostor with the right board id fails here.

`auth/result_bad_proof` (instead of `result_ok`) - AUTH read:
```
02 02 01    (3 B)
```
The same exchange when the board's slot 2 holds a different key (the watch was evicted and the slot reissued). No board MAC; the board disconnects 2 s later and the watch shows "pair again".

### 16.3 Pairing

The watch sends the code it picked from the advertising and stores the key the board issues on a match.

`pairing/request` - PAIRING write:
```
02 01 af 10    (4 B)
```
The watch writes the request carrying the code 4271 (0x10AF, little-endian) it picked next to the chosen board. The type byte marks this as REQUEST.

`pairing/result_ok` - PAIRING read:
```
02 02 00 02 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f    (20 B)
```
The code matched, so the board answers OK with the slot id and the 16-byte key the watch stores as {0x12345678, 2, 00..0f}. The key crosses the air once inside the open window.

`pairing/result_bad_code` (instead of `result_ok`) - PAIRING read:
```
02 02 02    (3 B)
```
A code that does not match the board's. Non-zero status means no key follows: the watch shows why and the user may retry.

### 16.4 Roster read (players)

The watch walks the players roster one entry per read: write sets the cursor, each read returns the entry at the cursor and advances it.

`roster_read/write` - ROSTER write:
```
02 00    (2 B)
```
The watch sets the per-connection cursor to 0 before reading the whole roster.

`roster_read/read_anna` - ROSTER read:
```
02 00 03 0d 0c 0b 0a ff 00 00 41 4e 4e 41 00 00 00 00 00    (19 B)
  CRC-32(03 0d 0c 0b 0a ff 00 00 41 4e 4e 41 00 00 00 00 00 44 33 22 11 00 40 ff 4b 52 59 53 54 49 41 4e 00 88 77 66 55 00 c0 30 4f 4c 41 00 00 00 00 00 00)
    = 0x150fe75b
```
First entry: ANNA uid 0x0A0B0C0D rgb ff 00 00. The rosterVersion every state body carries is the CRC-32 of the 1-byte count plus each entry's 16-byte wire form - here 0x150fe75b.

`roster_read/read_krystian` - ROSTER read:
```
02 01 03 44 33 22 11 00 40 ff 4b 52 59 53 54 49 41 4e 00    (19 B)
```
Second entry: KRYSTIAN uid 0x11223344 rgb 00 40 ff, name NUL-padded to 9 bytes.

`roster_read/read_ola` - ROSTER read:
```
02 02 03 88 77 66 55 00 c0 30 4f 4c 41 00 00 00 00 00 00    (19 B)
```
Third entry: OLA uid 0x55667788 rgb 00 c0 30.

`roster_read/read_past_end` - ROSTER read:
```
02 03 03    (3 B)
```
Past the end the cursor stays at the last entry and the read returns the 3-byte header alone, so the watch knows it has the whole roster.

**Roster read (teams)**

The teams roster, active in padel and volleyball: the same walk, with the teams' own rosterVersion 0xdb5f702c.

`roster_read_teams/write` - ROSTER write:
```
02 00    (2 B)
```
Same cursor write; the board serves the teams roster while a team sport is active.

`roster_read_teams/read_orly` - ROSTER read:
```
02 00 02 24 23 22 21 ff 80 00 4f 52 4c 59 00 00 00 00 00    (19 B)
  CRC-32(02 24 23 22 21 ff 80 00 4f 52 4c 59 00 00 00 00 00 34 33 32 31 00 ff 80 4c 49 53 59 00 00 00 00 00)
    = 0xdb5f702c
```
First team ORLY uid 0x21222324 rgb ff 80 00; the teams rosterVersion is 0xdb5f702c.

`roster_read_teams/read_lisy` - ROSTER read:
```
02 01 02 34 33 32 31 00 ff 80 4c 49 53 59 00 00 00 00 00    (19 B)
```
Second team LISY uid 0x31323334 rgb 00 ff 80.

`roster_read_teams/read_past_end` - ROSTER read:
```
02 02 02    (3 B)
```
Past the end: the 3-byte header alone.

### 16.5 MENU state

A MENU body in one chunk, the menu entries with the highlighted row - the first place a state body's rosterVersion appears on the wire.

`menu_state/menu` - STATE notify:
```
02 01 01 01 00 00 00 5b e7 0f 15 04 04 04 01 02 03    (17 B)
```
Cursor on padel, four sports listed. The body is 14 B, so it fits one chunk: header PROTO, stateSeq 1, index/count 0x01. rosterVersion 0x150fe75b is the players roster CRC.

### 16.6 SCORE with confirm (squash)

The watch scores a point optimistically and the board acknowledges it through the state header's ackSeq/ackStatus, then commits it 4 s later.

`score_confirm/score` - COMMAND write:
```
02 08 07 00    (4 B)
```
SCORE left with sequence number 7; the watch shows the pending point at once.

`score_confirm/state` - STATE notify:
```
02 2a 02 13 01 07 00 5b e7 0f 15 0d 0c 0b 0a 44 33 22 11 11    (20 B)
02 2a 12 01 00 05 03    (7 B)
```
The board applied it and pushed PLAYING, squash, ack 7 APPLIED, flags 0x11 (UNCOMMITTED, LAST_KNOWN - the newest point is the left's), games 1:0, score 5:3. The 21 B body is two chunks; ackSeq 7 settles the pending press.

### 16.7 Padel PLAYING

Two padel PLAYING bodies: a tiebreak and a ladder deuce, showing the extra left_point/right_point bytes and the TIEBREAK / LAST_RIGHT flags.

`padel_playing/tiebreak` - STATE notify:
```
02 2b 02 13 04 0c 00 5b e7 0f 15 0d 0c 0b 0a 44 33 22 11 12    (20 B)
02 2b 12 01 00 06 06 03 02    (9 B)
```
Tiebreak: sets 1:0, gems 6:6, raw points 3:2, committed, the last rally won by the left (flags 0x12: TIEBREAK, LAST_KNOWN), ack 12.

`padel_playing/ladder` - STATE notify:
```
02 2c 02 13 04 0d 00 5b e7 0f 15 0d 0c 0b 0a 44 33 22 11 31    (20 B)
02 2c 12 00 00 02 01 03 04    (9 B)
```
Ladder: sets 0:0, gems 2:1, committed deuce, then an uncommitted rally to the right -> 40:Ad (points 3, 4; flags 0x31: UNCOMMITTED, LAST_KNOWN, LAST_RIGHT), ack 13.

### 16.8 Too-old and too-new board

The watch accepts exactly protocol v2; a foreign PROTO in the advertising or an unauthenticated read keeps it from trusting the board.

`board_version/too_old` - ADVERTISING advertise:
```
0f ff ff ff 53 43 42 44 01 78 56 34 12 00 00 00 02 01 06    (19 B)
```
PROTO 1 in the ADV_IND (byte 8). Too old: no connect, "Update the scoreboard firmware (board v1, needs v2)".

`board_version/too_new` - ADVERTISING advertise:
```
0f ff ff ff 53 43 42 44 03 78 56 34 12 00 00 00 02 01 06    (19 B)
```
PROTO 3 in the ADV_IND. Too new: no connect, "Update the watch app". Only the version byte differs from adv_normal.

`board_version/unauth_read` - STATE read:
```
02    (1 B)
```
An unauthenticated read of STATE (or ROSTER) answers the 1-byte [PROTO] and nothing else; no state is pushed until the connection authenticates.

### 16.9 FOCUS

The watch moves the board's cursor silently with FOCUS, and the board answers with the new cursor position in the state.

`focus/focus_squash` - COMMAND write:
```
02 0e 03 01 00 00 00    (7 B)
```
On MENU, focus the squash row (sport id 0x00000001), seq 3.

`focus/menu_state` - STATE notify:
```
02 02 01 01 00 03 00 5b e7 0f 15 01 04 04 01 02 03    (17 B)
```
The board answers with the cursor on squash (body 15 B, stateSeq 2).

`focus/focus_krystian` - COMMAND write:
```
02 0e 04 44 33 22 11    (7 B)
```
On CHOOSE_PLAYERS, focus KRYSTIAN's profile row (uid 0x11223344), seq 4.

`focus/choose_players_state` - STATE notify:
```
02 10 01 10 01 04 00 5b e7 0f 15 05 00 00 00 44 33 22 11    (19 B)
```
CHOOSE_PLAYERS: ANNA (bit 0) and OLA (bit 2) selected (0x00000005), cursor on KRYSTIAN. Body 16 B, stateSeq 0x10.

`focus/focus_start` - COMMAND write:
```
02 0e 05 00 00 00 00    (7 B)
```
Focus the START row: target 0 (cursor bytes 00 00 00 00), seq 5.

`focus/focus_unknown_uid` - COMMAND write:
```
02 0e 06 78 56 34 12    (7 B)
```
A uid not in the roster (0x12345678), seq 6 - acked 6 INVALID and the cursor stays. The frame itself is well-formed, only the target is wrong.

`focus/focus_malformed` - COMMAND write:
```
02 0e 07 01    (4 B)
```
A 4-byte FOCUS is too short (missing three target bytes), acked 7 MALFORMED.

### 16.10 HEART

Three authenticated watches share heart rate while PLAYING; the board relays each watch the others' values in slot order, never its own.

`heart/write_a` - HEART write:
```
02 92    (2 B)
```
Watch A (connection slot 0) writes 146 bpm (0x92) while PLAYING.

`heart/write_c` - HEART write:
```
02 7c    (2 B)
```
Watch C (slot 2) writes 124 bpm (0x7c); both writes land before the next tick.

`heart/notify_a` - HEART notify:
```
02 7c 00 00    (4 B)
```
A's notify carries C's value (0x7c); A's own is always left out.

`heart/notify_b` - HEART notify:
```
02 92 7c 00    (4 B)
```
B's notify lists A then C in slot order, padded with 0.

`heart/notify_c` - HEART notify:
```
02 92 00 00    (4 B)
```
C's notify lists A. After C clears its value, B is re-notified the same list (A only); A is notified 01 00 00 00 once its 1 s window ends.

`heart/write_c_clear` - HEART write:
```
02 00    (2 B)
```
C clears its value with bpm 0.

`heart/notify_a_empty` - HEART notify:
```
02 00 00 00    (4 B)
```
All zero = nobody else is sharing; sent once A's 1 s window ends.

`heart/discard_v0` - HEART write:
```
00 92    (2 B)
```
A v0 (PROTO 0) write is dropped: byte 0 differs from PROTO.

`heart/discard_short` - HEART write:
```
02    (1 B)
```
A 1-byte write is too short for the 2-byte frame and is discarded.

### 16.11 SIDES_SWAPPED

The same pair played reversed (KRYSTIAN left in the first game, ANNA left now), so every state carries flags bit 6 SIDES_SWAPPED.

`sides_swapped/intro` - STATE notify:
```
02 30 01 12 01 00 00 5b e7 0f 15 0d 0c 0b 0a 44 33 22 11 40    (20 B)
```
INTRO body 17 B, stateSeq 0x30, flags 0x40 (SIDES_SWAPPED).

`sides_swapped/playing` - STATE notify:
```
02 31 02 13 01 00 00 5b e7 0f 15 0d 0c 0b 0a 44 33 22 11 50    (20 B)
02 31 12 00 01 02 01    (7 B)
```
PLAYING, games 0:1, score 2:1, committed, the newest point the left's (flags 0x50: SIDES_SWAPPED, LAST_KNOWN).

`sides_swapped/game_over` - STATE notify:
```
02 32 02 15 01 00 00 5b e7 0f 15 0d 0c 0b 0a 44 33 22 11 00    (20 B)
02 32 12 0b 07 01 01 00 40    (9 B)
```
GAME_OVER, ANNA won 11:7, games 1:1, normal variant. Body 23 B, flags 0x40 at the end of chunk 1.

`sides_swapped/match_start` - STATE notify:
```
02 33 02 11 01 00 00 5b e7 0f 15 03 00 00 00 0d 0c 0b 0a 44    (20 B)
02 33 12 33 22 11 40    (7 B)
```
MATCH_START before the next game, ANNA and KRYSTIAN selected (mask 3), still reversed (flags 0x40). Not swapped, these states carry bit 6 clear (00 in place of 40, 10 in place of 50).

### 16.12 LAST_KNOWN / LAST_RIGHT

The newest point's side and the last rally's side are named in the flags byte, so the watch can undo without guessing which side is newest.

`last_known/score_right` - COMMAND write:
```
02 08 01 01    (4 B)
```
SCORE right, seq 1, in a fresh game at 0:0 (flags were 0x00 unknown).

`last_known/state` - STATE notify:
```
02 40 02 13 01 01 00 5b e7 0f 15 0d 0c 0b 0a 44 33 22 11 31    (20 B)
02 40 12 00 00 00 01    (7 B)
```
The board pushes stateSeq 0x40: ack 1 APPLIED, score 0:1, flags 0x31 (UNCOMMITTED, LAST_KNOWN, LAST_RIGHT).

The rest of the game follows the flags byte only: the commit 4 s later keeps score 0:1 with flags 0x30 (right); SCORE left -> 1:1 flags 0x11 (left); UNDO left inside the window erases the point -> 0:1 flags 0x30 (right); UNDO right on a committed point -> 0:0 flags 0x01 (unknown); the commit -> 0:0 flags 0x00. In padel the bits follow a gem's rallies the same way.

### 16.13 RESULTS

The watch walks the finished games of a tournament one per read, ROSTER-style, to draw the standings.

`results/write` - RESULTS write:
```
02 00 00    (3 B)
```
The watch sets the read cursor to 0 (index is uint16).

`results/read_1` - RESULTS read:
```
02 00 00 03 00 01 ee ff c0 0d 0c 0b 0a 44 33 22 11 0b 07 01    (20 B)
```
ANNA - KRYSTIAN 11:7, the pair's first game.

`results/read_2` - RESULTS read:
```
02 01 00 03 00 01 ee ff c0 44 33 22 11 88 77 66 55 0b 04 01    (20 B)
```
KRYSTIAN - OLA 11:4.

`results/read_3` - RESULTS read:
```
02 02 00 03 00 01 ee ff c0 0d 0c 0b 0a 44 33 22 11 09 0b 02    (20 B)
```
ANNA - KRYSTIAN again, 9:11, the pair's second game (turn 2; KRYSTIAN won it).

`results/read_past_end` - RESULTS read:
```
02 03 00 03 00 01 ee ff c0 00 00    (11 B)
```
Past the end: the 11-byte header with rev 0 (no change but appends so far). A catch-up write of [PROTO, n] with n = the cached count then reads until this end frame.

`results/read_no_tournament` - RESULTS read:
```
02 03 00 00 00 00 00 00 00 00 00    (11 B)
```
Outside a sport mode: no tournament, count 0, id 0 - the watch drops its cache.

`results/unauth_read` - RESULTS read:
```
02    (1 B)
```
An unauthenticated read answers the 1-byte [PROTO] only.

`results/discard_short` - RESULTS write:
```
02 02    (2 B)
```
A 2-byte write is too short for the 3-byte cursor frame; the cursor stays.

`results/discard_v0` - RESULTS write:
```
00 00 00    (3 B)
```
A v0 (PROTO 0) write is dropped; the cursor stays.

### 16.14 Offline games

A client adds a game played off the board, the board refuses an illegal score, another client reads the game with its OFFLINE flag, and the client removes it again, which changes rev.

`offline_games/add` - COMMAND write:
```
02 0f 05 0d 0c 0b 0a 44 33 22 11 0b 07    (13 B)
```
ADD_OFFLINE_GAME, seq 5, ANNA - KRYSTIAN 11:7 (13 B).

`offline_games/add_ack` - STATE notify:
```
02 50 02 11 01 05 00 5b e7 0f 15 03 00 00 00 0d 0c 0b 0a 44    (20 B)
02 50 12 33 22 11 00    (7 B)
```
Legal squash score, both players selected: ack 5 APPLIED. The screen stays MATCH_START and the board plays no sound.

`offline_games/add_illegal` - COMMAND write:
```
02 0f 06 0d 0c 0b 0a 44 33 22 11 0f 03    (13 B)
```
Seq 6: 15:3 is not a legal squash game (the winner had already won at 11); the frame itself is well-formed.

`offline_games/add_illegal_ack` - STATE notify:
```
02 51 02 11 01 06 02 5b e7 0f 15 03 00 00 00 0d 0c 0b 0a 44    (20 B)
02 51 12 33 22 11 00    (7 B)
```
Ack 6 INVALID, nothing was stored.

`offline_games/read_offline` - RESULTS read:
```
02 00 00 01 00 01 ee ff c0 0d 0c 0b 0a 44 33 22 11 0b 07 81    (20 B)
```
A second client reads the game (cursor 0): turn 0x81 = OFFLINE (bit 7) and the pair's first game (bits 0-6 = 1).

`offline_games/remove` - COMMAND write:
```
02 10 07 01 ee ff c0 00 00 00 00    (11 B)
```
REMOVE_OFFLINE_GAME, seq 7: game 0 of tournament 0xC0FFEE01 as read under rev 0 (11 B).

`offline_games/remove_ack` - STATE notify:
```
02 52 02 11 01 07 00 5b e7 0f 15 03 00 00 00 0d 0c 0b 0a 44    (20 B)
02 52 12 33 22 11 00    (7 B)
```
Ack 7 APPLIED, the game is gone and rev is now 1.

`offline_games/read_end_rev` - RESULTS read:
```
02 00 00 00 00 01 ee ff c0 01 00    (11 B)
```
Reading from cursor 0 again: nothing is listed, so this is the end frame, with rev 1. A client that cached rev 0 drops its cache; the 11 B size also shows the board supports offline games.

Tournament 0xC0FFEE01 is running on a squash board at MATCH_START with ANNA and KRYSTIAN selected and no game finished yet.

### 16.15 Tournament sync - a tablet joins a board's tournament

Board 1 drew tournament 0x7AB1E001 and log_epoch 0x5A at START from ANNA, KRYSTIAN and OLA (16.4's stored players, so roster_crc = 0x150fe75b). ANNA beat KRYSTIAN 11:7: record 0x5EED0001, rev 0, order 1, change seq 1. A new connection: cursor STATUS, command seq from 1. A person started a squash tournament on board 1 and one game is recorded. A tablet with no tournament reads it, adopts it, pulls the change log and acknowledges it.

`sync_join/status` - TOURNAMENT read:
```
02 00 01 e0 b1 7a 01 03 5b e7 0f 15 01 00 01 73 c6 15 d0 5a    (20 B)
  CRC-32(01 01 00 ed 5e 00 01 00 0d 0c 0b 0a 44 33 22 11 0b 07 00)
    = 0xd015c673
```
Byte 1 0x00 STATUS. One live record; the tablet has no tournament, so it adopts 0x7AB1E001 and reads the roster through ROSTER (16.4). The read also binds this connection to the tournament.

`sync_join/cursor_changes` - TOURNAMENT write:
```
02 03 01 01 00    (5 B)
```
A joiner reads the whole log, from change seq 1.

`sync_join/read_1` - TOURNAMENT read:
```
02 02 01 00 ed 5e 00 01 00 0d 0c 0b 0a 44 33 22 11 0b 07 00    (20 B)
```
Byte 1 0x02 RECORD: id, rev 0, order 1, ANNA - KRYSTIAN 11:7, flags 0 (scored on the board). The 18 bytes after the type are what games_crc covers.

`sync_join/read_end` - TOURNAMENT read:
```
02 04 01 00 00 00    (6 B)
```
END: changes_to 1, nothing dropped. The cursor stays; the tablet resumes from seq 2 next time.

`sync_join/ack` - COMMAND write:
```
02 13 01 01 e0 b1 7a 01 00    (9 B)
```
ACK_CHANGES, seq 1, once the record is durable on the tablet. The first ACK arms the unsynced guard.

`sync_join/ack_ack` - STATE notify:
```
02 60 02 11 01 01 00 5b e7 0f 15 07 00 00 00 0d 0c 0b 0a 44    (20 B)
02 60 12 33 22 11 00    (7 B)
```
Ack 1 APPLIED; no screen change.

**A second board joins from MENU**

Board 2 on MENU holds no tournament. A new connection: cursor STATUS, command seq from 1. The tablet extends its tournament to board 2, which idles on MENU: stage the roster, BEGIN_TOURNAMENT, push the records, then pull: the board does not echo the tablet's own writes.

`sync_begin/status_none` - TOURNAMENT read:
```
02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00    (20 B)
```
tournament_id 0: no tournament, every field 0. The tablet may BEGIN it into its own.

`sync_begin/roster_anna` - TOURNAMENT write:
```
02 01 00 0d 0c 0b 0a ff 00 00 41 4e 4e 41 00 00 00 00 00    (19 B)
```
Entry 0, the 16-byte wire form of a ROSTER read (16.4). Staged, not acked.

`sync_begin/roster_krystian` - TOURNAMENT write:
```
02 01 01 44 33 22 11 00 40 ff 4b 52 59 53 54 49 41 4e 00    (19 B)
```
Entry 1.

`sync_begin/roster_ola` - TOURNAMENT write:
```
02 01 02 88 77 66 55 00 c0 30 4f 4c 41 00 00 00 00 00 00    (19 B)
```
Entry 2.

`sync_begin/begin` - COMMAND write:
```
02 11 01 01 e0 b1 7a 01 03 5b e7 0f 15    (13 B)
  CRC-32(03 0d 0c 0b 0a ff 00 00 41 4e 4e 41 00 00 00 00 00 44 33 22 11 00 40 ff 4b 52 59 53 54 49 41 4e 00 88 77 66 55 00 c0 30 4f 4c 41 00 00 00 00 00 00)
    = 0x150fe75b
```
BEGIN_TOURNAMENT, seq 1: the tablet's id and sport, 3 entries, roster_crc over the count byte and the staged entries (the rosterVersion formula of section 12).

`sync_begin/begin_ack` - STATE notify:
```
02 70 02 11 01 01 00 5b e7 0f 15 07 00 00 00 0d 0c 0b 0a 44    (20 B)
02 70 12 33 22 11 00    (7 B)
```
Ack 1 APPLIED on the sport's MATCH_START, every entry selected.

`sync_begin/status_joined` - TOURNAMENT read:
```
02 00 01 e0 b1 7a 01 03 5b e7 0f 15 00 00 00 8d ef 02 d2 c3    (20 B)
  CRC-32(00)
    = 0xd202ef8d
```
Joined with no record yet: changes_to 0, games_count 0, and games_crc of the empty list = CRC-32 of the count byte 0. Binds this connection.

`sync_begin/push_1` - TOURNAMENT write:
```
02 02 01 00 ed 5e 00 01 00 0d 0c 0b 0a 44 33 22 11 0b 07 00    (20 B)
```
Board 1's record forwarded unchanged (sync_join/read_1 with the same bytes): unknown id, so board 2 stores it and gives it change seq 1. Never acked.

`sync_begin/status_synced` - TOURNAMENT read:
```
02 00 01 e0 b1 7a 01 03 5b e7 0f 15 01 00 01 73 c6 15 d0 c3    (20 B)
  CRC-32(01 01 00 ed 5e 00 01 00 0d 0c 0b 0a 44 33 22 11 0b 07 00)
    = 0xd015c673
```
games_count and games_crc now equal board 1's (sync_join/status): both boards hold the same live records.

`sync_begin/cursor_changes` - TOURNAMENT write:
```
02 03 01 01 00    (5 B)
```
The tablet pulls board 2's log from seq 1.

`sync_begin/read_end` - TOURNAMENT read:
```
02 04 01 00 00 00    (6 B)
```
Change seq 1 is the tablet's own push (its pairing slot wrote it), so it is skipped: END at once, seq 1. ACKing 1 covers it.

**Resume after a link drop**

Since sync_join: KRYSTIAN beat OLA 11:5 on board 1 (0x5EED0002, order 2, seq 2), then a watch added the offline game ANNA - OLA 11:6 (0x5EED0003, order 3, seq 3). The tablet read seq 2, then lost the link before END. A new connection. Board 1 recorded two more games; the tablet's link dropped mid-pull. It reconnects and resumes from its last END, re-reading at most what it already had.

`sync_pull/status` - TOURNAMENT read:
```
02 00 01 e0 b1 7a 01 03 5b e7 0f 15 03 00 03 79 2d 75 ef 5a    (20 B)
  CRC-32(03 01 00 ed 5e 00 01 00 0d 0c 0b 0a 44 33 22 11 0b 07 00 02 00 ed 5e 00 02 00 44 33 22 11 88 77 66 55 0b 05 00 03 00 ed 5e 00 03 00 0d 0c 0b 0a 88 77 66 55 0b 06 02)
    = 0xef752d79
```
Same id: the tablet continues. changes_to 3 > its last END 1.

`sync_pull/cursor_changes` - TOURNAMENT write:
```
02 03 01 02 00    (5 B)
```
Resume from the last END seq + 1; the read of seq 2 before the drop does not count.

`sync_pull/read_2` - TOURNAMENT read:
```
02 02 02 00 ed 5e 00 02 00 44 33 22 11 88 77 66 55 0b 05 00    (20 B)
```
Seq 2 again: merging it a second time is a no-op on the tablet.

`sync_pull/read_3` - TOURNAMENT read:
```
02 02 03 00 ed 5e 00 03 00 0d 0c 0b 0a 88 77 66 55 0b 06 02    (20 B)
```
The watch's offline game: flags 0x02 OFFLINE.

`sync_pull/read_end` - TOURNAMENT read:
```
02 04 03 00 00 00    (6 B)
```
END: changes_to 3.

`sync_pull/ack` - COMMAND write:
```
02 13 01 01 e0 b1 7a 03 00    (9 B)
```
ACK_CHANGES up to 3, seq 1 on this connection.

`sync_pull/ack_ack` - STATE notify:
```
02 61 02 11 01 01 00 5b e7 0f 15 07 00 00 00 0d 0c 0b 0a 44    (20 B)
02 61 12 33 22 11 00    (7 B)
```
Ack 1 APPLIED.

**Edits, a stale write and a tie**

Board 2 after sync_begin. Tablet T1 corrected 0x5EED0001 to 11:8; tablet T2 independently corrected it to 11:9; both edits are rev 1. A late relay still carries rev 0. Every write is on a bound connection. The merge rule on board 2: a newer rev replaces, an older one is a no-op, two different edits with the same rev resolve to the larger byte image on every peer.

`sync_merge/status` - TOURNAMENT read:
```
02 00 01 e0 b1 7a 01 03 5b e7 0f 15 01 00 01 73 c6 15 d0 c3    (20 B)
  CRC-32(01 01 00 ed 5e 00 01 00 0d 0c 0b 0a 44 33 22 11 0b 07 00)
    = 0xd015c673
```
One live record, changes_to 1.

`sync_merge/edit` - TOURNAMENT write:
```
02 02 01 00 ed 5e 01 01 00 0d 0c 0b 0a 44 33 22 11 0b 08 00    (20 B)
```
T1's edit: rev 0 -> 1, order kept. Newer, so it replaces the stored record and takes change seq 2.

`sync_merge/stale` - TOURNAMENT write:
```
02 02 01 00 ed 5e 00 01 00 0d 0c 0b 0a 44 33 22 11 0b 07 00    (20 B)
```
The rev 0 original again: older than the stored rev 1, a no-op (no change seq).

`sync_merge/tie` - TOURNAMENT write:
```
02 02 01 00 ed 5e 01 01 00 0d 0c 0b 0a 44 33 22 11 0b 09 00    (20 B)
```
T2's edit, also rev 1. Same rev, different bytes: the larger 18-byte image wins - the first differing byte is score_b, 0x09 > 0x08 - so 11:9 replaces 11:8 (change seq 3). T1 gets 11:9 on its next pull; had it arrived first, 11:8 would have lost the same way.

`sync_merge/status_after` - TOURNAMENT read:
```
02 00 01 e0 b1 7a 01 03 5b e7 0f 15 03 00 01 8b 0a 99 d3 c3    (20 B)
  CRC-32(01 01 00 ed 5e 01 01 00 0d 0c 0b 0a 44 33 22 11 0b 09 00)
    = 0xd3990a8b
```
changes_to 3, still one live record; games_crc over the 11:9 form.

`sync_merge/cursor_records` - TOURNAMENT write:
```
02 03 02 00 00    (5 B)
```
A full walk by sort index, the fallback when games_crc differs from the reader's.

`sync_merge/read_0` - TOURNAMENT read:
```
02 02 01 00 ed 5e 01 01 00 0d 0c 0b 0a 44 33 22 11 0b 09 00    (20 B)
```
Index 0: the stored record in its latest form.

`sync_merge/read_end` - TOURNAMENT read:
```
02 04 01 00 00 00    (6 B)
```
END: seq = 1 stored record (live + tombstones).

**Delete**

Board 1 after sync_pull: three live records, RESULTS rev 0x5A00 (log_epoch 0x5A, only appends so far). The watch's connection has command seq 1 next; the tablet's cursor is STATUS on its own connection. A watch deletes the offline game on board 1; the board keeps a tombstone, RESULTS shrinks, and the tablet pulls the tombstone like any change.

`sync_tombstone/remove` - COMMAND write:
```
02 10 01 01 e0 b1 7a 00 5a 02 00    (11 B)
```
REMOVE_OFFLINE_GAME: RESULTS index 2 (0x5EED0003, the last by order) under RESULTS rev 0x5A00.

`sync_tombstone/remove_ack` - STATE notify:
```
02 62 02 11 01 01 00 5b e7 0f 15 07 00 00 00 0d 0c 0b 0a 44    (20 B)
02 62 12 33 22 11 00    (7 B)
```
Ack 1 APPLIED; no screen change.

`sync_tombstone/results_end` - RESULTS read:
```
02 02 00 02 00 01 e0 b1 7a 01 5a    (11 B)
```
The watch's RESULTS from index 2: count 2 now and rev 0x5A01 (log_epoch 0x5A, one change), so it drops its cache.

`sync_tombstone/status` - TOURNAMENT read:
```
02 00 01 e0 b1 7a 01 03 5b e7 0f 15 04 00 02 44 60 80 a7 5a    (20 B)
  CRC-32(02 01 00 ed 5e 00 01 00 0d 0c 0b 0a 44 33 22 11 0b 07 00 02 00 ed 5e 00 02 00 44 33 22 11 88 77 66 55 0b 05 00)
    = 0xa7806044
```
The tablet: changes_to 4, two live records (a tombstone is not in games_crc).

`sync_tombstone/cursor_changes` - TOURNAMENT write:
```
02 03 01 04 00    (5 B)
```
From its last END + 1.

`sync_tombstone/read_4` - TOURNAMENT read:
```
02 02 03 00 ed 5e 01 03 00 0d 0c 0b 0a 88 77 66 55 0b 06 03    (20 B)
```
The tombstone: rev 0 -> 1, flags 0x03 DELETED + OFFLINE, everything else kept. The tablet merges it like an edit and forwards it to board 2 unchanged.

`sync_tombstone/read_end` - TOURNAMENT read:
```
02 04 04 00 00 00    (6 B)
```
END: changes_to 4; no tombstone dropped yet.

**End releases a board**

Board 2 after sync_merge (changes_to 3), on the tablet's connection of sync_begin: after BEGIN (seq 1) it ACKed up to 1 (seq 2), which armed the unsynced guard. The tablet's End on board 2: BACK toward MENU is refused while the board holds un-ACKed changes, so the tablet pulls, ACKs, and the board leaves.

`sync_leave/back_1` - COMMAND write:
```
02 02 03    (3 B)
```
BACK on MATCH_START.

`sync_leave/back_1_ack` - STATE notify:
```
02 71 01 10 01 03 00 5b e7 0f 15 07 00 00 00 00 00 00 00    (19 B)
```
Ack 3 APPLIED on CHOOSE_PLAYERS, cursor on START; the tournament still runs.

`sync_leave/back_2` - COMMAND write:
```
02 02 04    (3 B)
```
BACK on CHOOSE_PLAYERS would leave the tournament.

`sync_leave/back_2_ack` - STATE notify:
```
02 72 01 10 01 04 02 5b e7 0f 15 07 00 00 00 00 00 00 00    (19 B)
```
Ack 4 INVALID: changes_to 3 > acked_to 1. Nothing changes.

`sync_leave/cursor_changes` - TOURNAMENT write:
```
02 03 01 02 00    (5 B)
```
Pull from the last END + 1.

`sync_leave/read` - TOURNAMENT read:
```
02 02 01 00 ed 5e 01 01 00 0d 0c 0b 0a 44 33 22 11 0b 09 00    (20 B)
```
Seqs 2 and 3 both changed 0x5EED0001; the walk returns it once, in its latest form (change seq 3, written by T2, so not skipped for T1).

`sync_leave/read_end` - TOURNAMENT read:
```
02 04 03 00 00 00    (6 B)
```
END: changes_to 3.

`sync_leave/ack` - COMMAND write:
```
02 13 05 01 e0 b1 7a 03 00    (9 B)
```
ACK_CHANGES up to 3.

`sync_leave/ack_ack` - STATE notify:
```
02 73 01 10 01 05 00 5b e7 0f 15 07 00 00 00 00 00 00 00    (19 B)
```
Ack 5 APPLIED.

`sync_leave/back_3` - COMMAND write:
```
02 02 06    (3 B)
```
BACK again.

`sync_leave/back_3_ack` - STATE notify:
```
02 74 01 01 00 06 00 5b e7 0f 15 04 04 04 01 02 03    (17 B)
```
Ack 6 APPLIED on MENU: the board left and dropped the tournament; its stored roster is active again.

`sync_leave/status_none` - TOURNAMENT read:
```
02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00    (20 B)
```
No tournament. The tablet keeps the board's link for a later BEGIN.
<!-- /generated: scenarios -->
## 17. Open questions and deviations

1. **Service UUID is not in `ADV_IND` (deviation from the locked filter).** UUID + locked
   manufacturer data = 37 B against 31 (4.1), and Connect IQ never sees `SCAN_RSP` (passive
   scan, CIQQA-789). Chosen: manufacturer data in `ADV_IND`, UUID in `SCAN_RSP`; the watch
   filters on company id + magic before connecting and checks the service UUID in the GATT
   table after. Accidental matches are as unlikely (company + 4-byte magic); deliberate
   impostors are stopped by authentication either way. Rejected alternatives: dropping
   manufacturer fields (would lose board id, version or pairing code, all locked); a 16-bit
   UUID (needs a SIG assignment). Accepted by the user 2026-10-02.
2. **Manufacturer data is readable (FR970, #81 Gate 0, 2026-10-03).** `getManufacturerSpecificData(0xFFFF)`
   returned V1's payload **without** the company bytes (magic at offset 0); `getRawData()` was non-null for
   every result. The pairing flag and code were read from the advertising and the watch paired with them.
   Other devices' manufacturer data (another company id) reads null for `0xFFFF`, as expected. The
   watch's parser still accepts the magic at offset 0 or 2. Other watch models are unverified (the bug
   report was a fenix 8).
3. The forum claim that Connect IQ truncates the advertising record to 20 B: the FR970 returned the
   whole record (no truncation seen); the layout keeps the identifying 16 B first anyway.
4. **UNDO carries a side.** The locked list has `UNDO` without arguments, but the board's
   undo is per side (C left, D right, `losePoint(side)`); there is no "undo the last point".
   A watch without its own point order (after a reconnect) takes the side from the PLAYING flags'
   LAST_KNOWN / LAST_RIGHT (11.3, #509).
5. **Additions not in the locked lists:** `ackStatus` beside `ackSeq` (the watch needs to know a
   press was rejected to roll back); SYNC (`0x0D`) for re-requesting a full state; the pairing
   request carries the chosen code; the state header carries `sport` on every screen; FOCUS
   (`0x0E`) and the CHOOSE_PLAYERS cursor, so the board's highlight follows the watch's list.
6. **END_MATCH dropped (resolved 2026-10-02).** It would have been a new GAME_OVER ->
   CHOOSE_PLAYERS transition no button has. The watch mirrors the fob instead: NEXT_GAME (C/D)
   -> MATCH_START, then BACK walks MATCH_START -> CHOOSE_PLAYERS -> MENU. `0x0C` stays unused
   so SYNC keeps `0x0D`.
7. PLAYING, MATCH_START, CELEBRATION and GAME_OVER take 2 notifications because they carry
   4-byte uids (locked). **Measured on the FR970 (#81 Gate 0):** 132 two-chunk states, gap chunk 0 ->
   chunk 1 min 6 / median 22 / max 247 ms, typically 6-9 ms. The second chunk does not cost a tick,
   so the state stays as specified.
8. Connect IQ's per-profile characteristic limit is undocumented; the v0 profile (6 characteristics
   + 2 CCCDs) registered and every entry resolved on the FR970 (#81 Gate 0). v1 adds HEART (7 + 3)
   and RESULTS (8 + 3); not yet verified on a watch.
9. Not protocol, for #80: WiFi + BLE against the RMT was **measured on V1 (2026-10-02)**: Dev
   Mode STA, advertising, 2 subscribed centrals at 20 notifies/s and fob presses in a match
   gave 0 bails, 0 stale, max 5 us late. Four connections were not measured (judged
   unrealistic by the user).
10. **v0 -> v1 before the first public release (user, 2026-11-02).** Store release 1.0.0 was
    frozen on v0 but never published. Section 5 would allow HEART as a new characteristic without a
    bump, but then a v0 board would lack it and every watch would have to treat it as optional.
    Chosen: bump `PROTO` to 1 before publishing, so HEART is mandatory (the watch's step 2 checks
    seven characteristics) and v1 is the first public version; v0 boards are told to update. No
    frame changed beyond byte 0: the MACs (over `"W" || Nb` / `"B" || Nw`) and the roster CRC do
    not cover `PROTO`, so 16.2 and 16.4 keep their MACs and versions.

11. **SIDES_SWAPPED is latched on the board, not on the watch (#508 / #510, 2026-11-05).** A
    watch-only latch would anchor to whatever layout the watch first saw, so a late joiner or an
    app restart after a swap would disagree with the other watch. The board latches per `Match` at
    its first finished game and publishes the bit compatibly (section 5): no `PROTO` bump, no extra chunk.

12. **RESULTS `turn` bit 7 = OFFLINE is a new meaning for an existing field (#563, 2026-11-12).**
    Normally that is a `PROTO` bump (section 5); it ships in v1 because the gain (any client can
    add games played off the board) outweighs the cost: store app 1.0.0 never reads RESULTS, and
    only beta watches 1.3.x do, which show turn 129+ on offline games until updated. Everything
    else of #563 is a plain compatible change.

13. **One tournament model, v2 (#623, 2026-11-16).** v1's tablet mode made the tablet the owner
    and the board a terminal behind a separate mode, a RAM-only key and a mirror; a board reboot
    lost the board's link and un-ACKed games. v2 drops it: every board plays its own tournament,
    and peers merge per-game records (rev + tombstones, section 20.2), so a tablet can join any
    time, extend the tournament to other boards and recover it after a power-off by re-joining.
    The redesigned TOURNAMENT layout, the removed screen, menu entry, flag and slot are why
    `PROTO` is bumped rather than extended. RESULTS stays a read-only view and is no longer
    append-only (an edit or delete bumps `rev`).

## 18. Heart rate relay (HEART)

During a match each watch may share its wearer's current heart rate, and the board relays it to
the other connected watches (HeartSync in the watch app). The board only relays: it never shows,
logs or stores a heart rate (`docs/privacy-garmin.md`).

- **Characteristic** HEART `0d780008-2f44-4f31-93b7-b78dc9e29735`, write (with response) +
  notify, authentication required: before authentication writes are discarded and nothing is
  notified. Mandatory on a v1 board; optional for the watch (one that neither shares nor shows
  heart rates never writes it or enables its CCCD).
- **PLAYING only.** A write is accepted only while the board's last published screen is PLAYING
  (`0x13`), else discarded. When a published state leaves PLAYING, every value is cleared. An
  overlay over PLAYING does not leave it (its pushes carry no new state).
- **Outside the command path.** No `seq`, no ack, no STATE push, no sound; never queued, never
  deduplicated. A lost write is healed by the next one.
- **Held in RAM, per connection:** the latest value and its write time. A value **expires 15 s**
  after its last write, and is cleared by a write of 0, at disconnect and on leaving PLAYING; a
  connection that loses its trust ("forget all") stops counting at once. Nothing is persisted.

### 18.1 HEART frames

<!-- generated: frames:HEART -->
<!-- Do not edit: generated from docs/protocol/ by `python helpers/check_protocol.py build`. -->
**Write** (write, watch -> board, 2 B): This watch's current heart rate, or a clear.

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `proto` | `PROTO` (`0x02`) - Protocol version. |
| 1 | 1 | `bpm` | 1-255 = this watch's heart rate; 0 = clear mine. |

Shared while PLAYING, at most once per second, at least every 5 s while a reading exists so it never expires; 0 clears mine.

**Notify** (notify, board -> watch, 4 B): The other connections' current values.

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `proto` | `PROTO` (`0x02`) - Protocol version. |
| 1 | 3 | `values` | The other connections' non-zero values in connection-slot order, padded with 0. |

Sent to every authenticated connection that enabled the HEART CCCD, at most once per second per connection with the newest content, always. The receiver's own value is never in its list; all zero = nobody else is sharing.
<!-- /generated: frames:HEART -->

The receiver's own value is never in its list. All zero = nobody else is sharing. A connection
slot is the board's slot (0-3) a connection takes when it connects and keeps until it drops, so the
order is stable while the links stay up; it carries no identity (the watch cannot tell whose value
is whose). Three values fit every other connection at the 4-connection limit: a two-watch match uses
the first, and a later "show everyone" needs no protocol change.

### 18.2 When the board notifies

- To every authenticated connection that enabled the HEART CCCD, when its list changes: a write
  with a new value, a clear, an expiry, a disconnect, leaving PLAYING.
- **At most one notify per second per connection, always the newest content.** A change inside the
  1 s window after the last notify goes out when the window ends, with the list current then; if by
  then it equals what was last sent, nothing is sent. A failed notify is sent again when the window
  ends.
- **CCCD enable:** the connection counts as having been sent the empty list, so a non-empty current
  list goes out at once (within the rate limit) and an empty one is not sent. A watch that
  subscribes mid-match sees the others without waiting for a change.
- The board checks on its 50 ms tick: write -> the other watches within ~1 s plus the Connect IQ
  ticks.

### 18.3 Watch (recommended)

- Sharing: write HEART while the state shows PLAYING, when the reading changes, at most once per
  second, and at least every 5 s while a reading exists so it never expires; write 0 when sharing
  stops (setting off, no reading) or let it expire. Each write costs one request tick (section 2),
  so a press waiting to be sent goes first.
- Showing: enable the HEART CCCD after the first state (section 7 step 9) and show the first
  non-zero value (a two-watch match has one). Drop it at disconnect, when the state leaves PLAYING,
  or when the list is all zero; frames with a foreign `PROTO` are ignored.

## 19. Tournament results (RESULTS)

The finished games of the current tournament, for a watch that draws the standings (#525). The
board is the source of truth: a watch never derives results from the states it observed.

- **Characteristic** RESULTS `0d780009-2f44-4f31-93b7-b78dc9e29735`, write (with response) + read,
  authentication required: before authentication writes are discarded and reads answer `[PROTO]`.
  **Optional for the watch**: a v1 board from before #527 lacks it (section 5), so a watch that
  does not find it hides the table and still connects.
- **Tournament** = the board's running tournament (section 20): from START on CHOOSE_PLAYERS or
  a BEGIN_TOURNAMENT until the board leaves it to MENU or reboots. RAM only on the board;
  `tournamentId` is the tournament's, the same on every board in it.
- **A read-only view of the live game records, sorted by `(order, id)`** (section 20.2). A game
  finished on this board appends (it takes the highest order). An edit, a delete
  (REMOVE_OFFLINE_GAME, or a tombstone merged from a peer) or a merged record that sorts before
  the end changes `rev` and may renumber turns. A game appears once its winning point is
  committed (an UNDO at 0:0 leaves an unfinished game, never a finished one); an offline game
  (ADD_OFFLINE_GAME, section 10.2) appears at once. Changes are not pushed: STATE is unchanged and
  a new game triggers no notify.
- **Players A and B are the pair's own order** (the record's `uid_a` / `uid_b`: the match's own
  A/B on the board that created it), not court sides: the exception to section 2, since a table
  needs the pair, not who stands where. A later swap of sides does not change A and B.
- **Winner = the higher score.** A game only finishes on a winning point, so the scores never tie
  (padel: the set's gems, including 7-6 after a tiebreak).
- **Turn** = the pair's k-th listed game, 1-based (ANNA - KRYSTIAN's second game is turn 2,
  whatever other pairs played in between), in bits 0-6 of the `turn` byte; bit 7 = OFFLINE (the
  record's OFFLINE flag: added by a client, not tracked by a board). Every peer sorts the same
  way, so every peer numbers the same turns.
- **`rev`** (past-the-end frame only) starts at `log_epoch` * 256 with every change log (so a
  re-joined board never repeats an old rev) and changes on every change to the
  listed games other than an append. A client caches it beside
  `tournamentId` and drops its cache on a different one; a remove followed by an add keeps `count`,
  so `count` alone cannot show it. An 11 B end frame also tells the client the board supports
  offline games; a 9 B one is older firmware: hide the feature.
- uids belong to the active roster (section 12): players in squash, teams in padel and volleyball.
- At most `RESULTS_MAX` (128) live records exist per tournament (section 14), so RESULTS lists
  every one.

### 19.1 RESULTS frames

<!-- generated: frames:RESULTS -->
<!-- Do not edit: generated from docs/protocol/ by `python helpers/check_protocol.py build`. -->
**Write (set cursor)** (write, watch -> board, 3 B): Sets this connection's read cursor (0 at connect).

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `proto` | `PROTO` (`0x02`) - Protocol version. |
| 1 | 2 | `index` | Game index to read next. |

To catch up, the watch writes [PROTO, n] with n = its cached count and reads until an end frame (index >= count).

**Read** (read, board -> watch, 11-20 B): One finished game, or the 11-byte header with rev alone past the end.

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `proto` | `PROTO` (`0x02`) - Protocol version. |
| 1 | 2 | `index` | The cursor this read answers. |
| 3 | 2 | `count` | Games listed now. |
| 5 | 4 | `tournament_id` | Tournament id; 0 = no tournament (then count is 0). |
| 9 | 2 | `rev` | Revision of the listed games; changes on every change other than an append. Present only when `index >= count`. |
| 9 | 4 | `uid_a` | The pair's own A uid (not court side). Present only when `index < count`. |
| 13 | 4 | `uid_b` | The pair's own B uid (not court side). Present only when `index < count`. |
| 17 | 1 | `score_a` | A's score of the finished game. Present only when `index < count`. |
| 18 | 1 | `score_b` | B's score of the finished game. Present only when `index < count`. |
| 19 | 1 | `turn` | Bit 7 OFFLINE (game added by a client, not tracked by the board); bits 0-6 the pair's k-th listed game, 1-based. Present only when `index < count`. |

Each read returns the game at the cursor and advances it; past the end it stays. A different tournamentId, a different rev or a count below the cached one drops the client's cache. rev starts at log_epoch * 256 (section 20.1) with every change log and changes on every change other than an append. An 11-byte end frame also tells the client the board supports offline games (a 9-byte one = older firmware: hide the feature).
<!-- /generated: frames:RESULTS -->

An empty tournament reads its own id with `count` 0.

### 19.2 Watch (recommended)

- **Incremental read.** Cache `(tournamentId, games)` per board. To catch up, write
  `[PROTO, n]` with `n` = the cached count and read until the end frame (index >= count): 1 write + 1 read when
  nothing is new.
- **A different `tournamentId` or `rev`, or a `count` below the cached one, drops the cache**: write
  `[PROTO, 0]` and read all `count` games. `0` = no tournament: show nothing.
- When to read: on entering the table screen and when the state shows a game just finished
  (CELEBRATION, GAME_OVER); no polling during PLAYING. Each read costs one request tick (section
  2), so a press waiting to be sent goes first.
- Results stay in the watch's RAM, like the board's; nothing is persisted.

## 20. Tournament sync (TOURNAMENT)

Every sport session on a board is a tournament (#623): an id, a sport, a roster and game records.
Every peer that holds it - the board, tablets, later other boards - keeps the same records and
merges what the others write. No frame or rule names a tablet: what a peer does follows only from
what it reads and writes, so board-to-board sync can use the same frames later.

- **Characteristic** TOURNAMENT `0d78000a-2f44-4f31-93b7-b78dc9e29735`, write (with response) +
  read, authentication required: before authentication writes are discarded and reads answer
  `[PROTO]`. Optional for a watch.
- **On the board** a tournament starts at START on CHOOSE_PLAYERS (a fresh random non-zero
  `tournament_id`; roster = the selected stored profiles, uids kept, at most
  `TOURNAMENT_ROSTER_MAX`) or by BEGIN_TOURNAMENT on MENU (join, 20.3). It ends when the board
  leaves to MENU (20.6) or reboots. RAM only: never NVS; the stored roster is untouched and active
  again afterwards. A dropped link changes nothing. One tournament per board: a board in one never
  joins another, and two tournaments are never merged.
- **Peers.** Any authenticated client may read and write; a tablet pairs like a watch (9.2).
- **CRC** = CRC-32/ISO-HDLC (zlib `crc32`, as `rosterVersion`, section 12), a little-endian
  `uint32` on the wire. `roster_crc` is over the count (1 B) and every entry's 16 B wire form
  (uid, r, g, b, name) in index order, so it equals the `rosterVersion` the board publishes for
  that roster. `games_crc` is over `games_count` (1 B) and every live record's 18 B form in
  `(order, id)` order; no live record gives `0xD202EF8D` (16.15 `sync_begin`).
- **Binding.** A STATUS read binds the connection to the tournament and change log it reports
  (`tournament_id`, `log_epoch`; 0 = none). RECORD writes on a connection not bound to the
  running change log are discarded and its ACK_CHANGES answers INVALID, so a write or an ACK meant
  for a tournament the board has left never lands in the next one - not even when the board
  re-joined the same id (a new `log_epoch`). Read STATUS first after every connect; after
  INVALID read it again, pull and ACK the new log.
- **Roster stage.** One per board, shared by all connections. ROSTER_ENTRY writes are byte copies,
  not acked, not checked until BEGIN_TOURNAMENT; a rewritten index overwrites. BEGIN clears it
  when it answers APPLIED or INVALID (BUSY and WRONG_SCREEN leave it), and leaving the tournament
  clears it. So a peer writes the whole roster before every BEGIN, each write after the previous
  one's response.

### 20.1 TOURNAMENT frames

<!-- generated: frames:TOURNAMENT -->
<!-- Do not edit: generated from docs/protocol/ by `python helpers/check_protocol.py build`. -->
**Write ROSTER_ENTRY** (write, watch -> board, 19 B): Stages one roster entry for the next BEGIN_TOURNAMENT.

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `proto` | `PROTO` (`0x02`) - Protocol version. |
| 1 | 1 | `type` | `0x01` ROSTER_ENTRY - Frame type. |
| 2 | 1 | `index` | Roster index, 0 to TOURNAMENT_ROSTER_MAX - 1. |
| 3 | 4 | `uid` | The entry's uid, non-zero, unique in the roster. |
| 7 | 1 | `r` | Red component. |
| 8 | 1 | `g` | Green component. |
| 9 | 1 | `b` | Blue component. |
| 10 | 9 | `name` | Printable ASCII, NUL-padded (a 9-character name has no NUL). |

The peer writes entries 0 to count - 1, then sends BEGIN_TOURNAMENT. A rewritten index overwrites; index >= TOURNAMENT_ROSTER_MAX is discarded.

**Write RECORD** (write, watch -> board, 20 B): Merges one game record into the bound tournament.

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `proto` | `PROTO` (`0x02`) - Protocol version. |
| 1 | 1 | `type` | `0x02` RECORD - Frame type. |
| 2 | 4 | `id` | The game's id, random, non-zero, unique in the tournament; never reused. |
| 6 | 1 | `rev` | Edit count mod 256; 0 at creation, + 1 on every edit or delete. |
| 7 | 2 | `order` | Lamport stamp, 1 + the highest order the creator holds; kept by edits. Sort by (order, id). |
| 9 | 4 | `uid_a` | Side A's roster uid (the match's own A). |
| 13 | 4 | `uid_b` | Side B's roster uid. |
| 17 | 1 | `score_a` | A's score. |
| 18 | 1 | `score_b` | B's score. |
| 19 | 1 | `flags` | Record flags. bit 0 DELETED; bit 1 OFFLINE |

Applied at once by the merge rule (section 20.2): a winning record replaces the stored one and takes the next change seq; a losing or identical one is a no-op. Dropped without a trace when the connection is not bound to the running tournament, id or order is 0, a reserved flag is set, a replace carries another order than the stored record, or (live records) the uids are equal or not both in the roster, the score is not a legal finished game of the sport, or a new id would exceed the live cap (RESULTS_MAX, one less while a match runs). The writer learns of a drop by comparing games_crc (section 20.5).

**Write CURSOR** (write, watch -> board, 5 B): Selects what this connection's next reads return.

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `proto` | `PROTO` (`0x02`) - Protocol version. |
| 1 | 1 | `type` | `0x03` CURSOR - Frame type. |
| 2 | 1 | `which` | STATUS, CHANGES or RECORDS; any other value is discarded. `0` STATUS, `1` CHANGES, `2` RECORDS |
| 3 | 2 | `seq` | First change seq (CHANGES) or sort index (RECORDS) to read. |

STATUS: every read answers STATUS (seq ignored, send 0). CHANGES: reads walk the records whose last change seq is >= seq, skipping those whose last change this peer wrote (section 20.4). RECORDS: reads walk the stored records from sort index seq on. At connect the cursor is STATUS.

**Read STATUS** (read, board -> watch, 20 B): The board's tournament, read first after every (re)connect; binds the connection to it.

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `proto` | `PROTO` (`0x02`) - Protocol version. |
| 1 | 1 | `type` | `0x00` STATUS - Frame type. |
| 2 | 4 | `tournament_id` | The running tournament's id; 0 = none. |
| 6 | 1 | `sport` | The tournament's sport. `0` none, `1` squash, `2` volleyball, `3` short_volleyball, `4` padel |
| 7 | 1 | `roster_count` | Entries in the tournament roster (read them through ROSTER). |
| 8 | 4 | `roster_crc` | CRC-32 of the roster, as BEGIN_TOURNAMENT's roster_crc and the state's rosterVersion. |
| 12 | 2 | `changes_to` | The last change seq given out; 0 = no change yet. Restarts with every tournament on this board. |
| 14 | 1 | `games_count` | Live (not deleted) records stored. |
| 15 | 4 | `games_crc` | CRC-32 over games_count (1 B) and the live records' 18-byte forms in (order, id) order. |
| 19 | 1 | `log_epoch` | Random non-zero, drawn when this board's change log starts (START, or BEGIN_TOURNAMENT on MENU); 0 = no tournament. Tells a restarted log from the old one. |

tournament_id 0 = no tournament (MENU, CHOOSE_PLAYERS before the first START, CONFIG, PROFILE), then every field is 0. A peer with no tournament may adopt this one (join); a peer with the same id pulls changes and pushes its records; a peer with another id leaves the board alone. A log_epoch other than the one of the peer's last pull from this board means a new change log: pull from 1.

**Read RECORD** (read, board -> watch, 20 B): The next record of a CHANGES or RECORDS walk; the cursor moves past it.

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `proto` | `PROTO` (`0x02`) - Protocol version. |
| 1 | 1 | `type` | `0x02` RECORD - Frame type. |
| 2 | 4 | `id` | The game's id, random, non-zero, unique in the tournament; never reused. |
| 6 | 1 | `rev` | Edit count mod 256; 0 at creation, + 1 on every edit or delete. |
| 7 | 2 | `order` | Lamport stamp, 1 + the highest order the creator holds; kept by edits. Sort by (order, id). |
| 9 | 4 | `uid_a` | Side A's roster uid (the match's own A). |
| 13 | 4 | `uid_b` | Side B's roster uid. |
| 17 | 1 | `score_a` | A's score. |
| 18 | 1 | `score_b` | B's score. |
| 19 | 1 | `flags` | Record flags. bit 0 DELETED; bit 1 OFFLINE |

CHANGES returns the record with the smallest last change seq >= the cursor, then moves the cursor past that seq; each record is returned once per walk, in its latest form. A record whose last change came from the reading connection's pairing slot is skipped. RECORDS returns the record at the sort index (live and tombstones, by (order, id)), then index + 1. Byte-identical to the write, so it can be forwarded to another peer as is.

**Read END** (read, board -> watch, 6 B): A CHANGES or RECORDS walk has nothing more at the cursor; the cursor stays.

| Off | Size | Field | Meaning |
|---|---|---|---|
| 0 | 1 | `proto` | `PROTO` (`0x02`) - Protocol version. |
| 1 | 1 | `type` | `0x04` END - Frame type. |
| 2 | 2 | `seq` | CHANGES - changes_to; RECORDS - stored records (live + tombstones). |
| 4 | 2 | `lost_to` | Highest change seq of a tombstone dropped past TOMBSTONE_MAX; 0 = none. A puller whose last END is below it may have missed a delete. |

CHANGES: seq = changes_to, the value to ACK and to resume from (seq + 1). RECORDS: seq = the stored record count. Also answered when no tournament runs (seq 0, lost_to 0).
<!-- /generated: frames:TOURNAMENT -->

The read cursor is per connection: STATUS at connect, then whatever the last CURSOR write chose.

### 20.2 Records and merge

- **Record** (`RECORD_SIZE` 18 B, bytes 2-19 of the RECORD frame): `id`, `rev`, `order`,
  `uid_a`, `uid_b`, `score_a`, `score_b`, `flags`. The same bytes are read and written, so a peer
  forwards a record from one board to another unchanged.
- **Create.** A game finished on the board (its winning point committed) or a watch's
  ADD_OFFLINE_GAME (flag OFFLINE) creates a record: random non-zero `id` never used in the
  tournament, `rev` 0, `order` = 1 + the highest order stored (live or tombstone), scores in the
  match's own A/B order. A peer creating a record does the same over the records it holds. Board
  games are final when played: there is no pending state.
- **Edit** = same `id` and `order`, `rev` + 1, players or scores changed. **Delete** = `rev` + 1
  and DELETED set, everything else kept: a **tombstone**, which stays so that a stale peer cannot
  bring the game back.
- **Merge** (every peer, every record, either direction). No stored record with that id: store it
  (tombstones too). **Delete is sticky:** if exactly one of the two is a tombstone, the tombstone
  wins at any rev, so a same-rev edit or a stale peer's edit never revives a deleted game.
  Otherwise `d = (new.rev - stored.rev) mod 256`: 1-127 the new one replaces the stored one;
  129-255 it is older, a no-op; 0 or 128 the larger 18 B image wins (the wire bytes compared
  first byte first), an identical one is a no-op. The result depends only on the two records,
  never on arrival order, so peers that saw the same records converge (16.15 `sync_merge`).
- **Change seq.** Every store or replace takes the board's next change seq (`uint16` from 1, per
  change log); a no-op takes none. `changes_to` = the last one given. With it the board notes the
  change's **writer** (RAM): the pairing slot (9.2) of the authenticated connection whose RECORD
  write or command made it, or "board" for a game played on the board and the fob. Pairing a
  slot again resets that slot's notes to "board", so a new device in the slot never inherits
  another one's writes. At 65535 the log is full:
  the board answers START_MATCH and ADD_OFFLINE_GAME INVALID and drops RECORD writes.
- **Change log:** starts empty with a fresh random `log_epoch` (STATUS) at START on
  CHOOSE_PLAYERS and at BEGIN_TOURNAMENT on MENU, so a board that rebooted or left and re-joined
  never looks like its old log.
- **Order** is a Lamport stamp: a new record's `order` is above everything its creator holds
  (capped at 65535), so a game played after another one was seen sorts after it. Two peers
  creating at once may pick the same order; `id` breaks the tie. Every peer sorts by
  `(order, id)`, both compared as unsigned numbers, so turns (section 19) agree everywhere. A
  record's `order` never changes: a write with the id of a stored record and another order is
  dropped.
- **Dropped writes** (no trace, no change seq): id or order 0, a reserved flag bit, a changed
  order; and for a live record: equal uids, a uid not in the roster, a score that is not a legal
  finished game of the sport (as ADD_OFFLINE_GAME), or a new id while `RESULTS_MAX` live records
  exist (`RESULTS_MAX` - 1 from START_MATCH until the match's record exists, so a game played on
  the board always finds a slot). A replace of an existing id is never dropped by the cap. The
  writer sees the drop as a `games_crc` mismatch (20.5).
- **Tombstones** are kept up to `TOMBSTONE_MAX`; one more drops the tombstone with the oldest
  change seq and raises END's `lost_to` to that seq. A puller whose last END is below `lost_to`
  may have missed a delete (20.4). Only boards evict: a peer keeps every tombstone for the
  tournament's life.
- **Limits accepted:** a record edited 128 or more times between two peers' syncs may resolve to
  the older side and peers may not converge on it (serial arithmetic is not transitive); a
  tombstone a board dropped can let a peer that missed it, and holds no tombstone of its own,
  push the game back.

### 20.3 Starting and joining

- **On the board:** START on CHOOSE_PLAYERS starts the tournament (fresh id and `log_epoch`, the
  selection as the roster). The picker stops at 16 selected. BACK from MATCH_START to
  CHOOSE_PLAYERS keeps the tournament; a roster entry cannot be un-selected (TOGGLE_PLAYER
  INVALID); START again appends the newly selected players to the roster.
- **A peer joins** (16.15 `sync_join`): STATUS shows a non-zero id and the peer holds no
  tournament, so it adopts it - id, sport, the roster through ROSTER (section 12, checked against
  `roster_crc`) - and pulls CHANGES from 1 (20.4). No question to the user.
- **A peer extends its tournament to a board on MENU** (16.15 `sync_begin`; also a second board
  joining): STATUS shows 0. Stage the roster (ROSTER_ENTRY 0 to count - 1), BEGIN_TOURNAMENT with
  the id, sport and `roster_crc`: the board takes the tournament with no records, every entry
  selected, on MATCH_START. Read STATUS (binds), push every record the peer holds, tombstones
  included (20.5). Two peers racing: the second BEGIN finds MATCH_START and acts as roster
  growth (same id) or answers INVALID (another id); either way it re-reads STATUS.
- **STATUS 0 off MENU** (CHOOSE_PLAYERS before START, CONFIG, PROFILE): BEGIN answers
  WRONG_SCREEN. A peer may send BACK on CHOOSE_PLAYERS (no tournament, no guard) to reach MENU, or
  wait.
- **Another id:** the board runs another tournament; leave it alone. A person ends it (long C,
  20.6) before the board can join.
- **Roster growth** (a late player, a name fix): BEGIN_TOURNAMENT on a sport screen with the same
  id and sport and every current uid staged; the staged list becomes the roster (order, names,
  colours as staged; new entries selected). A current uid missing is INVALID. The roster only
  grows. `rosterVersion` changes, so watches re-read it.
- **Roster conflicts.** Entries carry no revision; on a board the last BEGIN wins. A peer
  remembers per board the `roster_crc` it last agreed on with it (equal STATUS after a join, a
  BEGIN or an adoption). When STATUS shows another `roster_crc`:
  - the peer's roster still equals that agreed one (only the board changed): it takes the
    board's roster as read (order, names, colours) and appends the uids only it holds;
  - the peer changed its roster since (a rename, a new entry): it keeps its own entries and
    appends the uids only the board holds.
  If the result differs from the board's roster, the peer grows the board with it (BEGIN, same
  id). A rename so spreads from the peer that made it, and two peers renaming at once converge on
  the last BEGIN instead of renaming each other's entries back and forth. Limit accepted: two
  peers changing different entries of the same board at once keep only the last BEGIN's version
  of the other's entry.
- **Re-join after a reboot:** the board boots on MENU with no tournament; the peer's key is
  persisted (9.2), so it reconnects unasked, sees STATUS 0, and extends its tournament to the
  board again as above, with a new `log_epoch` (20.4). A peer re-extends only a board whose link
  dropped while it was in the tournament; a board the peer saw go to MENU over a live link was
  left by a person (20.6) and stays out until the user adds it again. A game that finished on the
  board and was not pulled before the reboot is lost: pull right after every finished game.

### 20.4 Pulling changes

- **Walk:** CURSOR (CHANGES, `s`), read RECORD frames until END. Each record comes once, in its
  latest form, however often it changed since `s` (16.15 `sync_leave`). END's `seq` =
  `changes_to`.
- **No echo:** a record whose last change was written by the reading connection's pairing slot
  is skipped - the peer holds it already (16.15 `sync_begin`). Skipped entries still count: END's
  `seq` and the ACK cover them, so the unsynced guard clears on the writer's own ACK. A peer that
  re-paired into another slot reads its old writes back once; merging them is a no-op. A board
  pulling from another board (later, board-to-board) authenticates into a slot like any peer.
- **Store, then ACK.** Merge every record durably, then ACK_CHANGES with END's `seq`; the next
  pull starts at that `seq` + 1. A lost link mid-pull resumes from the last END + 1: re-read
  records merge as no-ops (16.15 `sync_pull`). A lost ACK costs one re-read.
- **acked_to** is board-wide: the highest `up_to` any peer ACKed. The first ACK of a tournament
  arms the unsynced guard (20.6); a board nobody ever ACKed plays and leaves as a lone board. So
  one peer's ACK clears the guard for all: ACK only what is durable, and only with an END read on
  this connection after its STATUS. An ACK on a connection bound to an older `log_epoch` answers
  INVALID (Binding, section 20); a STATUS read rebinds, so the peer compares the re-read
  `log_epoch` (20.5) with its pull's before it ACKs.
- **Log restarted:** a `log_epoch` other than the one of the peer's last pull from this board
  means a new log (it rebooted, or left and re-joined): pull from 1. Every sync ends with the
  STATUS check of 20.5, which catches anything a cursor rule missed.
- **Missed deletes:** END's `lost_to` above the peer's last END means a tombstone it never read
  was dropped. Walk RECORDS (16.15 `sync_merge`); a live record the peer pulled from this board,
  or pushed to it and saw confirmed by a matching `games_crc`, in this `log_epoch`, that the walk
  does not list was deleted there: the peer stores it as a
  tombstone with `rev` + 1. Records it got elsewhere are pushed as usual.
- **When:** after every connect, and when STATE shows a finished game (CELEBRATION) or acks a
  command that changes records (ADD_OFFLINE_GAME, REMOVE_OFFLINE_GAME). Each read costs one request.

### 20.5 Pushing records

- After STATUS binds the connection and the pull (20.4, including `lost_to`), the peer writes the
  records the board may lack or hold in an older form: those it changed or received since its last push to that board (forwarded unchanged
  from other boards), one write after the previous one's response.
- Then STATUS again: `games_count` and `games_crc` equal to the peer's own live set = in sync.
  Otherwise walk RECORDS (CURSOR RECORDS 0, read until END; no writes during the walk, as each
  write may shift the sort index): then write every record the board lacks or holds older, merge
  every one the board holds newer. Tombstones are not in `games_crc`, so a tombstone the board
  evicts during a push is no mismatch. A mismatch that survives a full walk means the board
  dropped a write (20.2) or another peer wrote meanwhile: check once more, then show it, do not
  loop.
- Two peers writing the same board (two tablets) need nothing extra: their records merge by the
  rule and each pulls the other's from the board.

### 20.6 Leaving and End

- **Leaving** drops the board's tournament: records, roster, change log, `acked_to`, the guard and
  the roster stage. The board lands on MENU with its stored roster active; keys stay.
- **Unsynced guard:** armed by the first ACK_CHANGES, it holds while `changes_to > acked_to`. Then
  BACK on CHOOSE_PLAYERS answers INVALID, and the fob's long C first shows a warning (games not
  synced, hold C again) and leaves on a second long C while it shows; while the warning shows
  every command answers BUSY. Unarmed or clear, both leave at once.
- **Fob long C** on a board that joined leaves only that board; the other peers keep the
  tournament.
- **A peer's End** releases every board (16.15 `sync_leave`): pull, ACK, then BACK until MENU
  (MATCH_START -> CHOOSE_PLAYERS -> MENU; BACK never acts on PLAYING, so End waits for the game).
  Removing one board from a tournament is End for that board only. A board out of reach keeps
  the tournament until a person leaves it.

### 20.7 SET_SIDE

A peer's hot-swap of one side, in every session. A `side` other than 0/1, a uid not in the roster
or the other side's uid is INVALID; the side's current uid is APPLIED and changes nothing. On
MATCH_START, INTRO and PLAYING the current game moves to the new pair: its score is kept, the
other side's player stays, the side's colour follows the entry. On CELEBRATION and GAME_OVER it
sets the next game's pair only; the finished game's record stays as created and a peer edits it to
fix a wrong pick.

### 20.8 Watches and the fob

- Watches work as always on the tournament roster and never need TOURNAMENT. ADD_OFFLINE_GAME
  creates an OFFLINE record, REMOVE_OFFLINE_GAME tombstones an OFFLINE one; RESULTS shows every
  peer's live records, so a game edited on a tablet shows up on the watch after its next read.
- The fob's pair cycling uses the tournament roster.
