#!/usr/bin/env python3
"""Garmin App Remote test client: plays the watch's side of docs/garmin-protocol.md (v0)
from a PC's Bluetooth adapter, so the board can be tested without the Connect IQ app (#81).

Needs `pip install bleak`. Never imported by the build.

Keys are stored outside the repo, in ~/.scoreboard-garmin/keys.json
({board id: {slot, key, address}}); override with --keys <file>.

Usage:
  python helpers/garmin_client.py scan                 list scoreboards (company 0xFFFF + "SCBD")
  python helpers/garmin_client.py pair 4271            pair with the board advertising that code
  python helpers/garmin_client.py version              read VERSION (no auth needed)
  python helpers/garmin_client.py state                auth, print the state read + pushes for 5 s
  python helpers/garmin_client.py roster               auth, read every roster entry
  python helpers/garmin_client.py monitor              auth, print every state push until Ctrl+C
  python helpers/garmin_client.py send score 0         auth, send a command, print the ack'd state
  python helpers/garmin_client.py send set_pair 0x<leftUid> 0x<rightUid>   (decimal, or hex with 0x)

Commands: select_sport <id>, back, toggle_player <uid>, start_tournament, set_pair <l> <r>,
swap_sides, start_match, score <side>, undo <side>, skip, next_game, sync, raw <hex...>.
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
from pathlib import Path

sys.dont_write_bytecode = True

try:
    from bleak import BleakClient, BleakScanner
except ImportError:
    sys.exit("bleak is missing: pip install bleak")

PROTO = 0x00
COMPANY = 0xFFFF
MAGIC = b"SCBD"
UUID = "0d78000{}-2f44-4f31-93b7-b78dc9e29735"
SERVICE, VERSION, COMMAND, STATE, ROSTER, AUTH, PAIRING = (UUID.format(i) for i in range(1, 8))

COMMANDS = {
    "select_sport": (0x01, "B"), "back": (0x02, ""), "toggle_player": (0x03, "I"),
    "start_tournament": (0x04, ""), "set_pair": (0x05, "II"), "swap_sides": (0x06, ""),
    "start_match": (0x07, ""), "score": (0x08, "B"), "undo": (0x09, "B"), "skip": (0x0A, ""),
    "next_game": (0x0B, ""), "sync": (0x0D, ""),
}
SCREENS = {0x00: "BOOTING", 0x01: "MENU", 0x02: "CONFIG", 0x03: "PROFILE", 0x10: "CHOOSE_PLAYERS",
           0x11: "MATCH_START", 0x12: "INTRO", 0x13: "PLAYING", 0x14: "CELEBRATION", 0x15: "GAME_OVER"}
SPORTS = {0: "none", 1: "squash", 2: "volleyball", 3: "short volleyball", 4: "padel"}
ACKS = {0: "APPLIED", 1: "WRONG_SCREEN", 2: "INVALID", 3: "UNKNOWN_CMD", 4: "BUSY", 5: "MALFORMED"}
PADEL_POINTS = {0: "0", 1: "15", 2: "30", 3: "40", 4: "Ad"}

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
            out += f" cursor={SPORTS.get(data[0], data[0])} sports={[SPORTS.get(s, s) for s in data[2:2 + data[1]]]}"
        elif screen == 0x10:
            out += f" selected={struct.unpack_from('<I', data)[0]:#010x}"
        elif screen == 0x11:
            mask, left, right = struct.unpack_from("<III", data)
            out += f" selected={mask:#010x} left={who(left)} right={who(right)}"
        elif screen == 0x12:
            left, right = struct.unpack_from("<II", data)
            out += f" {who(left)} vs {who(right)}"
        elif screen == 0x13:
            left, right, flags, lg, rg, ls, rs = struct.unpack_from("<IIBBBBB", data)
            tags = [n for b, n in ((1, "UNCOMMITTED"), (2, "TIEBREAK"), (4, "BALL_L"), (8, "BALL_R")) if flags & b]
            out += f" {who(left)} {lg}:{rg} {who(right)} score {ls}:{rs}"
            if len(data) >= 15:
                lp, rp = data[13], data[14]
                out += f" points {lp}:{rp}" if flags & 2 else f" points {PADEL_POINTS.get(lp, lp)}:{PADEL_POINTS.get(rp, rp)}"
            out += f" {tags}"
        elif screen in (0x14, 0x15):
            left, right, winner, ls, rs, lg, rg, variant = struct.unpack_from("<IIBBBBBB", data)
            out += (f" {who(left)} {ls}:{rs} {who(right)} games {lg}:{rg} "
                    f"winner={'left' if winner == 0 else 'right'}{' BAJGIEL' if variant else ''}")
    except struct.error:
        out += f" (short data {data.hex(' ')})"
    return out


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

    async def command(self, frame, wait=3.0):
        await self.client.write_gatt_char(COMMAND, frame, response=True)
        seq = frame[2]
        deadline = time.monotonic() + wait
        while time.monotonic() < deadline:
            try:
                body = await asyncio.wait_for(self.states.get(), deadline - time.monotonic())
            except asyncio.TimeoutError:
                break
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
    ap.add_argument("action", choices=["scan", "pair", "version", "state", "roster", "monitor", "send"])
    ap.add_argument("args", nargs="*")
    ap.add_argument("--keys", default=str(DEFAULT_KEYS))
    ap.add_argument("--board", help="board id (hex) from the key store")
    ap.add_argument("--seq", type=int, default=1)
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

        seconds = None if opts.action == "monitor" else 5.0
        deadline = None if seconds is None else time.monotonic() + seconds
        while deadline is None or time.monotonic() < deadline:
            timeout = 1.0 if deadline is None else max(0.0, deadline - time.monotonic())
            try:
                body = await asyncio.wait_for(session.states.get(), timeout)
            except asyncio.TimeoutError:
                continue
            print(describe(body, session.names))


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
