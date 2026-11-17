#!/usr/bin/env python3
"""Garmin App Remote test client: plays a client's side of docs/garmin-protocol.md (v2)
from a PC's Bluetooth adapter, so the board can be tested without the Connect IQ app (#81) or the
tablet. The pure record helpers (encode/decode, merge, games_crc) mirror spec 20 for the app to port.

Needs `pip install bleak`. Never imported by the build.

Keys are stored outside the repo, in ~/.scoreboard-garmin/keys.json
({board id: {slot, key, address}}); override with --keys <file>.

Usage:
  python helpers/garmin_client.py scan                 list scoreboards (company 0xFFFF + "SCBD")
  python helpers/garmin_client.py pair 4271            pair with the board advertising that code
  python helpers/garmin_client.py version              read VERSION (no auth needed)
  python helpers/garmin_client.py state                auth, print the state read + pushes for 5 s
  python helpers/garmin_client.py roster               auth, read every roster entry
  python helpers/garmin_client.py results              auth, read the tournament's finished games (spec 19)
  python helpers/garmin_client.py results 3            the same from game 3 on ("anything new?" with 3 cached)
  python helpers/garmin_client.py add-game 0x<uidA> 0x<uidB> 11 7   add an offline game (MATCH_START ... GAME_OVER)
  python helpers/garmin_client.py remove-game 0        remove the offline game at that RESULTS index
  python helpers/garmin_client.py tstatus              read the TOURNAMENT status (spec 20)
  python helpers/garmin_client.py changes [from]       pull the change log from that seq (default 1) to END
  python helpers/garmin_client.py records [from]       walk the stored records by sort index (default 0)
  python helpers/garmin_client.py put <id|new>:<rev>:<order|next>:<uidA>:<uidB>:<sA>:<sB>[:flags]
                                                       write one RECORD (flags 1 DELETED, 2 OFFLINE)
  python helpers/garmin_client.py ack <tid> <upTo>     ACK_CHANGES
  python helpers/garmin_client.py push-roster ANNA:ff0000[:uid] KRYSTIAN:0040ff[:uid]   stage the roster only
  python helpers/garmin_client.py join <tid> <sport> <name:RRGGBB[:uid]>...   stage the roster + BEGIN_TOURNAMENT
                                                       (MENU: join; sport screen, same tid: grow; alias `begin`)
  python helpers/garmin_client.py set-side <left|right|0|1> <uid>   SET_SIDE
  python helpers/garmin_client.py monitor             auth, print every state + HEART push until Ctrl+C
  python helpers/garmin_client.py heart 72             auth, send HR 72 (refreshed every 5 s), print the
                                                       others' HEART pushes until Ctrl+C, then clear it
  python helpers/garmin_client.py heart                auth, print state + HEART pushes (send nothing)
  python helpers/garmin_client.py send score 0         auth, send a command, print the ack'd state
  python helpers/garmin_client.py send set_pair 0x<leftUid> 0x<rightUid>   (decimal, or hex with 0x)
  python helpers/garmin_client.py send focus 1         MENU: move the board's cursor to squash
  python helpers/garmin_client.py send focus 0x<uid>   CHOOSE_PLAYERS: cursor to that profile (0 = START)

Commands: select_sport <id>, back, toggle_player <uid>, start_tournament, set_pair <l> <r>,
swap_sides, start_match, score <side>, undo <side>, skip, next_game, sync, focus <sport | uid | 0>,
raw <hex...>.
HEART is taken only while the board is on PLAYING (spec 18). The other watch must be a real one or
another PC: Windows shares one link per board between processes, so a second instance here would be
the same connection (and its own value is never relayed back to it).
--board <id hex> picks a stored board (default: the last one used); --seq <n> sets the first seq
(send the same seq twice to test the duplicate path).
"""

import argparse
import asyncio
import hashlib
import hmac
import json
import os
import struct
import sys
import time
import zlib
from collections import namedtuple
from pathlib import Path

sys.dont_write_bytecode = True

try:
    from bleak import BleakClient, BleakScanner
except ImportError:
    sys.exit("bleak is missing: pip install bleak")

PROTO = 0x02
COMPANY = 0xFFFF
MAGIC = b"SCBD"
UUID = "0d7800{:02x}-2f44-4f31-93b7-b78dc9e29735"
SERVICE, VERSION, COMMAND, STATE, ROSTER, AUTH, PAIRING, HEART, RESULTS, TOURNAMENT = (
    UUID.format(i) for i in range(1, 11))

COMMANDS = {
    "select_sport": (0x01, "B"), "back": (0x02, ""), "toggle_player": (0x03, "I"),
    "start_tournament": (0x04, ""), "set_pair": (0x05, "II"), "swap_sides": (0x06, ""),
    "start_match": (0x07, ""), "score": (0x08, "B"), "undo": (0x09, "B"), "skip": (0x0A, ""),
    "next_game": (0x0B, ""), "sync": (0x0D, ""), "focus": (0x0E, "I"),
    "add_offline_game": (0x0F, "IIBB"), "remove_offline_game": (0x10, "IHH"),
    "begin_tournament": (0x11, "IBBI"), "ack_changes": (0x13, "IH"), "set_side": (0x14, "BI"),
}
SCREENS = {0x00: "BOOTING", 0x01: "MENU", 0x02: "CONFIG", 0x03: "PROFILE", 0x10: "CHOOSE_PLAYERS",
           0x11: "MATCH_START", 0x12: "INTRO", 0x13: "PLAYING", 0x14: "CELEBRATION", 0x15: "GAME_OVER"}
SPORTS = {0: "none", 1: "squash", 2: "volleyball", 3: "short volleyball", 4: "padel"}
SPORT_IDS = {name: sid for sid, name in SPORTS.items()}
ACKS = {0: "APPLIED", 1: "WRONG_SCREEN", 2: "INVALID", 3: "UNKNOWN_CMD", 4: "BUSY", 5: "MALFORMED"}
PADEL_POINTS = {0: "0", 1: "15", 2: "30", 3: "40", 4: "Ad"}
# Bit 6 of the PLAYING flags and of the MATCH_START / INTRO / result flags byte (spec 11.3).
SIDES_SWAPPED = 0x40
# PLAYING flags bits 4-5: the newest point of the game and its side (spec 11.3).
LAST_KNOWN = 0x10
LAST_RIGHT = 0x20

# TOURNAMENT frame types and CURSOR targets (spec 20.1).
T_STATUS, T_ROSTER_ENTRY, T_RECORD, T_CURSOR, T_END = 0x00, 0x01, 0x02, 0x03, 0x04
CUR_STATUS, CUR_CHANGES, CUR_RECORDS = 0x00, 0x01, 0x02
FLAG_DELETED, FLAG_OFFLINE = 0x01, 0x02
RECORD_SIZE = 18
ORDER_MAX = 0xFFFF
SIDES = {"left": 0, "right": 1}

# The board drops a value 15 s after its last write (spec 18), so a live HR is re-sent well inside that.
HEART_REFRESH_S = 5.0

DEFAULT_KEYS = Path.home() / ".scoreboard-garmin" / "keys.json"


def mac(key, tag, nonce):
    return hmac.new(key, bytes([tag]) + nonce, hashlib.sha256).digest()[:8]


# --- Key store ---

def load_keys(path):
    path = Path(path)
    if not path.exists():
        return {"boards": {}, "last": None}
    try:
        return json.loads(path.read_text())
    except ValueError:
        # Never fall back to an empty store: the next save would erase every other board's key.
        raise SystemExit(f"{path} is not valid JSON - fix or move it, then retry")


def save_keys(path, store):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(".tmp")
    tmp.write_text(json.dumps(store, indent=2))
    try:
        os.chmod(tmp, 0o600)  # plaintext keys; a no-op beyond read-only on Windows
    except OSError:
        pass
    os.replace(tmp, path)


# --- Advertising ---

def parse_adv(adv):
    """(proto, board id, pairing, code) from the manufacturer data, or None."""
    payload = adv.manufacturer_data.get(COMPANY)
    if payload is None:
        return None
    # Whether a stack strips the company id is not uniform: accept the magic at 0 or 2.
    if payload[2:6] == MAGIC and payload[:2] == b"\xff\xff":
        payload = payload[2:]
    if len(payload) < 12 or payload[:4] != MAGIC:
        return None
    board_id, flags, code = struct.unpack_from("<IBH", payload, 5)
    return payload[4], board_id, bool(flags & 1), code


async def scan(seconds=5.0):
    found = {}

    def seen(device, adv):
        parsed = parse_adv(adv)
        if parsed:
            found[device.address] = (device, adv.rssi, parsed)

    async with BleakScanner(detection_callback=seen):
        await asyncio.sleep(seconds)
    return found


# --- State decoding ---

class StateAssembler:
    def __init__(self):
        self.seq = None
        self.chunks = {}

    def feed(self, frame):
        """Returns the body once every chunk of one stateSeq has arrived."""
        if len(frame) < 4 or frame[0] != PROTO:
            return None
        seq, index, count = frame[1], frame[2] >> 4, frame[2] & 0x0F
        if seq != self.seq:
            self.seq, self.chunks = seq, {}
        self.chunks[index] = frame[3:]
        if count and len(self.chunks) == count:
            return b"".join(self.chunks[i] for i in range(count))
        return None


def describe(body, names=None):
    names = names or {}

    def who(uid):
        return names.get(uid, f"{uid:08x}")

    if len(body) < 8:
        return f"short body {body.hex(' ')}"
    screen, sport, ack_seq, ack_status = body[0], body[1], body[2], body[3]
    roster_version = struct.unpack_from("<I", body, 4)[0]
    data = body[8:]
    out = (f"{SCREENS.get(screen, hex(screen))} sport={SPORTS.get(sport, sport)} "
           f"ack={ack_seq}:{ACKS.get(ack_status, ack_status)} roster={roster_version:08x}")
    try:
        if screen == 0x01:
            def entry(i):
                return SPORTS.get(i, i)
            out += f" cursor={entry(data[0])} entries={[entry(i) for i in data[2:2 + data[1]]]}"
        elif screen == 0x10:
            out += f" selected={struct.unpack_from('<I', data)[0]:#010x}"
            if len(data) >= 8:
                cursor = struct.unpack_from("<I", data, 4)[0]
                out += f" cursor={'START' if cursor == 0 else who(cursor)}"
        elif screen == 0x11:
            mask, left, right = struct.unpack_from("<III", data)
            out += f" selected={mask:#010x} left={who(left)} right={who(right)}"
            # Older firmware sends no flags byte (spec 5): not swapped.
            if len(data) >= 13 and data[12] & SIDES_SWAPPED:
                out += " SWAPPED"
        elif screen == 0x12:
            left, right = struct.unpack_from("<II", data)
            out += f" {who(left)} vs {who(right)}"
            # Older firmware sends no flags byte (spec 5): not swapped.
            if len(data) >= 9 and data[8] & SIDES_SWAPPED:
                out += " SWAPPED"
        elif screen == 0x13:
            left, right, flags, lg, rg, ls, rs = struct.unpack_from("<IIBBBBB", data)
            tags = [n for b, n in ((1, "UNCOMMITTED"), (2, "TIEBREAK"), (4, "BALL_L"), (8, "BALL_R"),
                                   (SIDES_SWAPPED, "SWAPPED")) if flags & b]
            # Newest point's side; older firmware sends 0 = unknown, no tag (spec 5).
            if flags & LAST_KNOWN:
                tags.append("LAST_R" if flags & LAST_RIGHT else "LAST_L")
            out += f" {who(left)} {lg}:{rg} {who(right)} score {ls}:{rs}"
            if len(data) >= 15:
                lp, rp = data[13], data[14]
                out += f" points {lp}:{rp}" if flags & 2 else f" points {PADEL_POINTS.get(lp, lp)}:{PADEL_POINTS.get(rp, rp)}"
            out += f" {tags}"
        elif screen in (0x14, 0x15):
            left, right, winner, ls, rs, lg, rg, variant = struct.unpack_from("<IIBBBBBB", data)
            out += (f" {who(left)} {ls}:{rs} {who(right)} games {lg}:{rg} "
                    f"winner={'left' if winner == 0 else 'right'}{' BAJGIEL' if variant else ''}")
            if len(data) >= 15 and data[14] & SIDES_SWAPPED:
                out += " SWAPPED"
    except struct.error:
        out += f" (short data {data.hex(' ')})"
    return out


def decode_heart(frame):
    """The other watches' bpm values from a HEART notify (0 = none), or None for a foreign frame."""
    if len(frame) < 4 or frame[0] != PROTO:
        return None
    return list(frame[1:4])


def decode_result(frame):
    """(index, count, tournament id, game, rev) - game is (uidA, uidB, scoreA, scoreB, turn), None past
    the end; rev only on the 11 B end frame (None on a game frame or an older board's 9 B one)."""
    if len(frame) < 9 or frame[0] != PROTO:
        return None
    index, count, tid = struct.unpack_from("<HHI", frame, 1)
    if len(frame) >= 20:
        return index, count, tid, struct.unpack_from("<IIBBB", frame, 9), None
    return index, count, tid, None, struct.unpack_from("<H", frame, 9)[0] if len(frame) >= 11 else None


def split_turn(turn):
    """(offline, turn) - bit 7 of the RESULTS turn byte is OFFLINE (spec 19)."""
    return bool(turn & 0x80), turn & 0x7F


def build_roster_entry(index, uid, r, g, b, name):
    return struct.pack("<BBBI3B9s", PROTO, T_ROSTER_ENTRY, index, uid, r, g, b, name.encode("ascii"))


def build_cursor(which, seq=0):
    return struct.pack("<BBBH", PROTO, T_CURSOR, which, seq)


def roster_wire(uid, r, g, b, name):
    return struct.pack("<I3B9s", uid, r, g, b, name.encode("ascii"))


def list_crc(entries):
    """CRC-32 over the count byte then each entry's wire form (roster_crc, games_crc; spec 20)."""
    return zlib.crc32(bytes([len(entries)]) + b"".join(entries)) & 0xFFFFFFFF


def roster_crc(entries):
    """roster_crc / rosterVersion of (uid, r, g, b, name) entries in index order."""
    return list_crc([roster_wire(*e) for e in entries])


class Record(namedtuple("Record", "id rev order uid_a uid_b score_a score_b flags")):
    """One game record (spec 20.2); its 18 B wire form is what games_crc and the merge tie compare."""
    __slots__ = ()

    @property
    def deleted(self):
        return bool(self.flags & FLAG_DELETED)

    def wire(self):
        return struct.pack("<IBHIIBBB", *self)

    @classmethod
    def from_wire(cls, data):
        return cls(*struct.unpack_from("<IBHIIBBB", data))


def build_record(record):
    """RECORD write: the same 20 bytes a RECORD read returns, so a record forwards unchanged."""
    return bytes([PROTO, T_RECORD]) + record.wire()


def sort_key(record):
    return record.order, record.id


def next_order(held):
    return min(max((r.order for r in held), default=0) + 1, ORDER_MAX)


def new_record(held, uid_a, uid_b, score_a, score_b, flags=0):
    """Create (spec 20.2): random non-zero id unused in `held`, rev 0, order above every held one."""
    ids = {r.id for r in held}
    gid = 0
    while gid == 0 or gid in ids:
        gid = int.from_bytes(os.urandom(4), "little")
    return Record(gid, 0, next_order(held), uid_a, uid_b, score_a, score_b, flags)


def edit_record(record, **changes):
    """Edit: same id and order, rev + 1 mod 256."""
    return record._replace(rev=(record.rev + 1) & 0xFF, **changes)


def delete_record(record):
    """Tombstone: rev + 1, DELETED set, everything else kept."""
    return record._replace(rev=(record.rev + 1) & 0xFF, flags=record.flags | FLAG_DELETED)


def merge(stored, new):
    """The record a peer keeps after receiving `new` over `stored` (None = unknown id), spec 20.2.
    Depends only on the two records, so peers converge whatever the arrival order."""
    if stored is None:
        return new
    if stored.deleted != new.deleted:
        return stored if stored.deleted else new
    d = (new.rev - stored.rev) & 0xFF
    if 1 <= d <= 127:
        return new
    if d > 128:
        return stored
    return new if new.wire() > stored.wire() else stored


def games_crc(records):
    """(games_count, games_crc) over the live records in (order, id) order; tombstones excluded."""
    live = sorted((r for r in records if not r.deleted), key=sort_key)
    return len(live), list_crc([r.wire() for r in live])


Status = namedtuple("Status", "tournament_id sport roster_count roster_crc changes_to games_count games_crc log_epoch")


def decode_status(frame):
    """STATUS read -> Status, or None for a foreign / short / other-type frame."""
    if len(frame) < 20 or frame[0] != PROTO or frame[1] != T_STATUS:
        return None
    return Status(*struct.unpack_from("<IBBIHBIB", frame, 2))


def decode_walk(frame):
    """A CHANGES / RECORDS read: ('record', Record), ('end', seq, lost_to) or None."""
    if len(frame) < 2 or frame[0] != PROTO:
        return None
    if frame[1] == T_RECORD and len(frame) >= 2 + RECORD_SIZE:
        return "record", Record.from_wire(frame[2:])
    if frame[1] == T_END and len(frame) >= 6:
        return ("end",) + struct.unpack_from("<HH", frame, 2)
    return None


def parse_roster_arg(text):
    """name:RRGGBB[:uid] -> (uid, r, g, b, name); a random non-zero uid when omitted."""
    parts = text.split(":")
    if len(parts) not in (2, 3) or len(parts[1]) != 6 or not 1 <= len(parts[0]) <= 9 or not parts[0].isascii():
        sys.exit("roster entry: name:RRGGBB[:uid], name 1-9 ASCII characters")
    r, g, b = bytes.fromhex(parts[1])
    uid = int(parts[2], 0) if len(parts) == 3 else int.from_bytes(os.urandom(4), "little") or 1
    return uid, r, g, b, parts[0]


def parse_record_arg(text, held=()):
    """id:rev:order:uidA:uidB:sA:sB[:flags] -> Record; id `new` = a fresh id unused in `held`,
    order `next` = 1 + the highest order in `held`."""
    parts = text.split(":")
    if len(parts) not in (7, 8):
        sys.exit("record: <id|new>:<rev>:<order|next>:<uidA>:<uidB>:<sA>:<sB>[:flags]")
    try:
        gid = new_record(held, 0, 0, 0, 0).id if parts[0] == "new" else int(parts[0], 0)
        order = next_order(held) if parts[2] == "next" else int(parts[2], 0)
        rest = [int(p, 0) for p in parts[3:]] + [0] * (8 - len(parts))
        rec = Record(gid, int(parts[1], 0), order, *rest)
        rec.wire()
    except (ValueError, struct.error) as err:
        sys.exit(f"record: {err}")
    return rec


def describe_status(st):
    if st.tournament_id == 0:
        return "TOURNAMENT status: no tournament"
    return (f"TOURNAMENT status: id {st.tournament_id:08x} {SPORTS.get(st.sport, st.sport)} "
            f"roster {st.roster_count} crc {st.roster_crc:08x} changes_to {st.changes_to} "
            f"games {st.games_count} crc {st.games_crc:08x} log_epoch {st.log_epoch:02x}")


def describe_record(rec, names=None):
    names = names or {}

    def who(uid):
        return names.get(uid, f"{uid:08x}")

    tags = "".join(t for f, t in ((FLAG_DELETED, " DELETED"), (FLAG_OFFLINE, " OFFLINE")) if rec.flags & f)
    return (f"id {rec.id:08x} rev {rec.rev:3d} order {rec.order:5d}  "
            f"{who(rec.uid_a):>9} {rec.score_a:2d}:{rec.score_b:<2d} {who(rec.uid_b)}{tags}")


def describe_heart(values):
    shown = [str(v) for v in values if v]
    return "HEART " + (" ".join(shown) + " bpm" if shown else "none")


# --- Session ---

class Session:
    def __init__(self, client, key_entry=None):
        self.client = client
        self.key_entry = key_entry
        self.assembler = StateAssembler()
        self.states = asyncio.Queue()
        self.names = {}

    async def read_version(self):
        v = await self.client.read_gatt_char(VERSION)
        return v[0], v[1:].decode("ascii", "replace")

    async def authenticate(self):
        challenge = await self.client.read_gatt_char(AUTH)
        if len(challenge) >= 3 and challenge[0] == PROTO and challenge[1] == 0x02 and challenge[2] == 0:
            # Windows keeps the BLE link up for a while after a disconnect, so a quick rerun lands
            # on the board's already-authenticated connection; it is still the same trusted link.
            print("link already authenticated (connection reused by the OS)")
            return
        if len(challenge) < 10 or challenge[0] != PROTO or challenge[1] != 0x00:
            raise RuntimeError(f"unexpected AUTH read {challenge.hex(' ')}")
        nb = bytes(challenge[2:10])
        key = bytes.fromhex(self.key_entry["key"])
        nw = os.urandom(8)
        proof = bytes([PROTO, 0x01, self.key_entry["slot"]]) + nw + mac(key, 0x57, nb)
        await self.client.write_gatt_char(AUTH, proof, response=True)
        result = await self.client.read_gatt_char(AUTH)
        if len(result) < 3 or result[2] != 0:
            raise RuntimeError(f"authentication refused: {result.hex(' ')} (pair again?)")
        if len(result) < 11 or not hmac.compare_digest(bytes(result[3:11]), mac(key, 0x42, nw)):
            raise RuntimeError("the board's MAC is wrong: untrusted board")

    async def subscribe(self):
        def on_state(_, data):
            body = self.assembler.feed(bytes(data))
            if body is not None:
                self.states.put_nowait(body)

        await self.client.start_notify(STATE, on_state)

    async def subscribe_heart(self):
        def on_heart(_, data):
            values = decode_heart(bytes(data))
            if values is not None:
                self.states.put_nowait(("heart", values))

        await self.client.start_notify(HEART, on_heart)

    async def write_heart(self, bpm):
        await self.client.write_gatt_char(HEART, bytes([PROTO, bpm]), response=True)

    async def read_roster(self):
        await self.client.write_gatt_char(ROSTER, bytes([PROTO, 0]), response=True)
        entries = []
        while True:
            e = await self.client.read_gatt_char(ROSTER)
            if len(e) < 19:
                break
            uid = struct.unpack_from("<I", e, 3)[0]
            name = e[10:19].split(b"\0")[0].decode("ascii", "replace")
            entries.append((e[1], uid, e[7], e[8], e[9], name))
            self.names[uid] = name
        return entries

    async def read_results(self, start=0):
        """(tournament id, count, [(index, game)], rev) from `start` on; None for a board without RESULTS.
        rev is None for a board without offline support (9 B end frame)."""
        if self.client.services.get_characteristic(RESULTS) is None:
            return None
        await self.client.write_gatt_char(RESULTS, struct.pack("<BH", PROTO, start), response=True)
        tid, count, games = 0, 0, []
        while True:
            decoded = decode_result(bytes(await self.client.read_gatt_char(RESULTS)))
            if decoded is None:
                sys.exit("RESULTS read refused (not authenticated?)")
            index, count, tid, game, rev = decoded
            if game is None:
                return tid, count, games, rev
            games.append((index, game))

    async def tournament_write(self, frame):
        await self.client.write_gatt_char(TOURNAMENT, frame, response=True)

    async def read_tournament(self):
        return bytes(await self.client.read_gatt_char(TOURNAMENT))

    async def command(self, frame, wait=3.0):
        await self.client.write_gatt_char(COMMAND, frame, response=True)
        seq = frame[2]
        deadline = time.monotonic() + wait
        while time.monotonic() < deadline:
            try:
                body = await asyncio.wait_for(self.states.get(), deadline - time.monotonic())
            except asyncio.TimeoutError:
                break
            if isinstance(body, tuple):
                continue
            print(describe(body, self.names))
            if len(body) >= 4 and body[2] == seq:
                return body
        print(f"no state acknowledging seq {seq}")
        return None


def build_command(name, args, seq):
    if name == "raw":
        return bytes.fromhex("".join(args))
    if name not in COMMANDS:
        sys.exit(f"unknown command {name}; one of {', '.join(COMMANDS)} or raw")
    cmd_id, fmt = COMMANDS[name]
    if len(args) != len(fmt):
        sys.exit(f"{name} takes {len(fmt)} argument(s)")
    values = [int(a, 0) for a in args]
    return bytes([PROTO, cmd_id, seq]) + struct.pack("<" + fmt, *values)


async def find_address(store, board):
    entry = store["boards"].get(board)
    if entry and entry.get("address"):
        return entry["address"]
    found = await scan()
    for address, (_, _, (_, board_id, _, _)) in found.items():
        if f"{board_id:08x}" == board:
            return address
    sys.exit(f"board {board} not found")


async def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("action", choices=["scan", "pair", "version", "state", "roster", "results", "add-game", "remove-game", "monitor", "send", "heart",
                                       *TABLET_ACTIONS])
    ap.add_argument("args", nargs="*")
    ap.add_argument("--keys", default=str(DEFAULT_KEYS))
    ap.add_argument("--board", help="board id (hex) from the key store")
    # Not a fixed 1: Windows reuses the board connection across quick reruns, and the board
    # dedupes seq per connection, so a repeated seq would be acked but not applied.
    ap.add_argument("--seq", type=int, default=int(time.time() * 10) % 255 + 1)
    opts = ap.parse_args()
    store = load_keys(opts.keys)

    if opts.action == "scan":
        found = await scan()
        if not found:
            print("no scoreboard advertising")
        for address, (device, rssi, (proto, board_id, pairing, code)) in found.items():
            paired = " paired" if f"{board_id:08x}" in store["boards"] else ""
            window = f" PAIRING code {code:04d}" if pairing else ""
            print(f"Scoreboard {board_id & 0xFFFF:04X}  id {board_id:08x}  proto {proto}  {address}  "
                  f"rssi {rssi}{window}{paired}")
        return

    if opts.action == "pair":
        if len(opts.args) != 1:
            sys.exit("pair <code>")
        code = int(opts.args[0])
        found = await scan()
        match = [(a, v) for a, v in found.items() if v[2][2] and v[2][3] == code]
        if not match:
            sys.exit(f"no board advertising PAIRING with code {code:04d}")
        address, (_, _, (proto, board_id, _, _)) = match[0]
        async with BleakClient(address) as client:
            session = Session(client)
            version = await session.read_version()
            print(f"VERSION proto {version[0]} firmware {version[1]}")
            await client.write_gatt_char(PAIRING, bytes([PROTO, 0x01]) + struct.pack("<H", code), response=True)
            result = await client.read_gatt_char(PAIRING)
            if len(result) < 20 or result[2] != 0:
                sys.exit(f"pairing refused: {result.hex(' ')}")
            entry = {"slot": result[3], "key": bytes(result[4:20]).hex(), "address": address}
            board = f"{board_id:08x}"
            store["boards"][board] = entry
            store["last"] = board
            save_keys(opts.keys, store)
            print(f"paired with board {board}, slot {entry['slot']}; key in {opts.keys}")
            session.key_entry = entry
            await session.authenticate()
            print("authenticated")
        return

    board = opts.board.lower() if opts.board else store.get("last")
    if opts.action != "version" and (not board or board not in store["boards"]):
        sys.exit("no paired board: run pair <code> first (or pass --board)")

    if opts.action == "version":
        if board and board in store["boards"]:
            address = await find_address(store, board)
        else:
            found = await scan()
            if not found:
                sys.exit("no scoreboard advertising")
            address = next(iter(found))
        async with BleakClient(address) as client:
            proto, fw = await Session(client).read_version()
            print(f"VERSION proto {proto} firmware {fw}")
        return

    address = await find_address(store, board)
    async with BleakClient(address) as client:
        session = Session(client, store["boards"][board])
        proto, fw = await session.read_version()
        print(f"VERSION proto {proto} firmware {fw}")
        await session.authenticate()
        store["last"] = board
        save_keys(opts.keys, store)

        if opts.action == "roster":
            for index, uid, r, g, b, name in await session.read_roster():
                print(f"{index:2d}  {uid:08x}  #{r:02x}{g:02x}{b:02x}  {name}")
            return

        await session.read_roster()
        if opts.action == "results":
            await run_results(session, opts.args)
            return

        if opts.action in ("add-game", "remove-game"):
            await run_offline(session, opts.action, opts.args, opts.seq)
            return

        if opts.action in TABLET_ACTIONS:
            await run_tablet(session, opts.action, opts.args, opts.seq)
            return

        first = await client.read_gatt_char(STATE)
        print(f"STATE read (chunk 0): {bytes(first).hex(' ')}")
        await session.subscribe()

        if opts.action == "send":
            if not opts.args:
                sys.exit("send <command> [args]")
            # The push that follows the subscription first.
            try:
                print(describe(await asyncio.wait_for(session.states.get(), 3.0), session.names))
            except asyncio.TimeoutError:
                print("no state push after subscribing")
            await session.command(build_command(opts.args[0], opts.args[1:], opts.seq))
            return

        if opts.action == "heart":
            await run_heart(session, opts.args)
            return

        if opts.action == "monitor":
            await session.subscribe_heart()
        seconds = None if opts.action == "monitor" else 5.0
        deadline = None if seconds is None else time.monotonic() + seconds
        while deadline is None or time.monotonic() < deadline:
            timeout = 1.0 if deadline is None else max(0.0, deadline - time.monotonic())
            try:
                body = await asyncio.wait_for(session.states.get(), timeout)
            except asyncio.TimeoutError:
                continue
            print(describe_heart(body[1]) if isinstance(body, tuple) else describe(body, session.names))


async def run_results(session, args):
    try:
        start = int(args[0], 0) if args else 0
    except ValueError:
        start = -1
    if len(args) > 1 or not 0 <= start <= 0xFFFF:
        sys.exit("results [from]")
    read = await session.read_results(start)
    if read is None:
        print("no RESULTS characteristic (firmware before #527)")
        return
    tid, count, games, rev = read
    if tid == 0:
        print("no tournament (not in a sport mode)")
        return
    print(f"tournament {tid:08x}, {count} games, " + (f"rev {rev}" if rev is not None else "rev n/a (no offline support)"))
    # Names of the active roster (the state's sport decides which, spec 12).
    def name(uid):
        return session.names.get(uid, f"{uid:08x}")

    for index, (uid_a, uid_b, score_a, score_b, turn) in games:
        offline, turn = split_turn(turn)
        print(f"{index:3d}  turn {turn:2d}{' offline' if offline else '        '}  "
              f"{name(uid_a):>9} {score_a:2d}:{score_b:<2d} {name(uid_b)}")


async def run_offline(session, action, args, seq):
    if action == "add-game":
        if len(args) != 4:
            sys.exit("add-game <uidA> <uidB> <scoreA> <scoreB>")
        frame = build_command("add_offline_game", args, seq)
    else:
        if len(args) != 1:
            sys.exit("remove-game <index>")
        read = await session.read_results(0)
        if read is None or read[0] == 0:
            sys.exit("no tournament to remove from")
        tid, _, _, rev = read
        if rev is None:
            sys.exit("board has no offline support (9 B RESULTS end frame)")
        frame = build_command("remove_offline_game", [str(tid), str(rev), args[0]], seq)
    await session.subscribe()
    # The push that follows the subscription first.
    try:
        print(describe(await asyncio.wait_for(session.states.get(), 3.0), session.names))
    except asyncio.TimeoutError:
        print("no state push after subscribing")
    body = await session.command(frame)
    if body is not None:
        print(f"result {ACKS.get(body[3], body[3])}")


TABLET_ACTIONS = ("tstatus", "changes", "records", "put", "ack", "push-roster", "join", "begin", "set-side")


async def stage(session, frames):
    """One write after the previous one's response (spec 20: staged, not acked)."""
    for frame in frames:
        await session.tournament_write(frame)


async def read_status(session):
    """CURSOR STATUS + read; the read also binds this connection to the board's tournament."""
    await session.tournament_write(build_cursor(CUR_STATUS))
    status = decode_status(await session.read_tournament())
    if status is None:
        sys.exit("TOURNAMENT read refused (not authenticated?)")
    return status


async def walk(session, which, start):
    """CURSOR then reads until END: ([Record], end seq, lost_to)."""
    await session.tournament_write(build_cursor(which, start))
    records = []
    while True:
        item = decode_walk(await session.read_tournament())
        if item is None:
            sys.exit("TOURNAMENT read refused")
        if item[0] == "end":
            return records, item[1], item[2]
        records.append(item[1])


def parse_sport(text):
    sport = SPORT_IDS.get(text.replace("_", " ").lower())
    return int(text, 0) if sport is None else sport


async def run_tablet(session, action, args, seq):
    """The tablet's side of spec 20; commands print their ack like `send`."""
    if session.client.services.get_characteristic(TOURNAMENT) is None:
        sys.exit("no TOURNAMENT characteristic (firmware before #583)")
    if action == "push-roster":
        if not args:
            sys.exit("push-roster <name:RRGGBB[:uid]>...")
        entries = [parse_roster_arg(a) for a in args]
        await stage(session, [build_roster_entry(i, *e) for i, e in enumerate(entries)])
        for i, e in enumerate(entries):
            print(f"staged {i}: {e[0]:08x} #{e[1]:02x}{e[2]:02x}{e[3]:02x} {e[4]}")
        print(f"roster crc {roster_crc(entries):08x} (stage only; join restages)")
        return
    if action == "tstatus":
        if args:
            sys.exit("tstatus")
        print(describe_status(await read_status(session)))
        return
    if action in ("changes", "records"):
        if len(args) > 1:
            sys.exit(f"{action} [from]")
        start = int(args[0], 0) if args else (1 if action == "changes" else 0)
        which = CUR_CHANGES if action == "changes" else CUR_RECORDS
        records, end, lost_to = await walk(session, which, start)
        for rec in records:
            print(describe_record(rec))
        print(f"END seq {end} lost_to {lost_to}" + ("  (gap: walk RECORDS)" if action == "changes" and lost_to >= start > 1 else ""))
        if action == "records":
            count, crc = games_crc(records)
            print(f"games {count} crc {crc:08x}")
        return
    if action == "put":
        if len(args) != 1:
            sys.exit("put <id|new>:<rev>:<order|next>:<uidA>:<uidB>:<sA>:<sB>[:flags]")
        await read_status(session)
        held = []
        if args[0].split(":")[0] == "new" or args[0].split(":")[2:3] == ["next"]:
            held, _, _ = await walk(session, CUR_RECORDS, 0)
        rec = parse_record_arg(args[0], held)
        await session.tournament_write(build_record(rec))
        print(f"wrote {describe_record(rec)}")
        print(describe_status(await read_status(session)))
        return
    if action in ("join", "begin"):
        if len(args) < 3:
            sys.exit(f"{action} <tid> <sport> <name:RRGGBB[:uid]>...")
        entries = [parse_roster_arg(a) for a in args[2:]]
        await stage(session, [build_roster_entry(i, *e) for i, e in enumerate(entries)])
        frame = build_command("begin_tournament",
                              [args[0], str(parse_sport(args[1])), str(len(entries)), str(roster_crc(entries))], seq)
    elif action == "ack":
        if len(args) != 2:
            sys.exit("ack <tid> <upTo>")
        await read_status(session)  # binds the connection to the log the ACK is for (spec 20)
        frame = build_command("ack_changes", args, seq)
    else:  # set-side
        if len(args) != 2:
            sys.exit("set-side <left|right|0|1> <uid>")
        frame = build_command("set_side", [str(SIDES.get(args[0], args[0])), args[1]], seq)
    await session.subscribe()
    # The push that follows the subscription first.
    try:
        print(describe(await asyncio.wait_for(session.states.get(), 3.0), session.names))
    except asyncio.TimeoutError:
        print("no state push after subscribing")
    body = await session.command(frame)
    if body is not None:
        print(f"result {ACKS.get(body[3], body[3])}")


async def run_heart(session, args):
    if len(args) > 1:
        sys.exit("heart [bpm]")
    try:
        bpm = int(args[0], 0) if args else None
    except ValueError:
        sys.exit("heart [bpm]")
    if bpm is not None and not 0 <= bpm <= 255:
        sys.exit("bpm is 0-255 (0 clears)")
    await session.subscribe_heart()
    if bpm == 0:
        await session.write_heart(0)
        print("HEART cleared")
        return
    sent_at = None
    try:
        while True:
            if bpm is not None and (sent_at is None or time.monotonic() - sent_at >= HEART_REFRESH_S):
                await session.write_heart(bpm)
                sent_at = time.monotonic()
            try:
                body = await asyncio.wait_for(session.states.get(), 1.0)
            except asyncio.TimeoutError:
                continue
            # A STATE push names the screen: HEART is relayed only on PLAYING.
            print(describe_heart(body[1]) if isinstance(body, tuple) else describe(body, session.names))
    finally:
        if bpm:
            try:
                await session.write_heart(0)
            except Exception:  # the link may already be gone; the board clears on disconnect anyway
                pass


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
