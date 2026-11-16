#!/usr/bin/env python3
"""Host tests for helpers/garmin_client.py's pure encode/decode functions.

Every vector the client touches is tagged `spec: <scenario>/<step>` next to its hex literal, so
`python helpers/check_protocol.py --repo client` (run by the native pio test pre-script) can compare
the literal against docs/protocol/scenarios.yaml and fail on drift. The tags must sit on the line
directly above the hex literal(s), one frame per step; a chunked state step lists its chunks on
consecutive lines. Run: python helpers/test_garmin_client.py
"""

import os
import random
import struct
import sys
import types
import unittest

sys.path.insert(0, os.path.dirname(__file__))

# garmin_client.py imports bleak and exits if it is missing; stub it so the pure encode/decode
# functions under test import without a Bluetooth stack.
if "bleak" not in sys.modules:
    bleak = types.ModuleType("bleak")
    bleak.BleakClient = object
    bleak.BleakScanner = object
    sys.modules["bleak"] = bleak

import garmin_client as gc  # noqa: E402


def H(s):
    return bytes.fromhex(s.replace(" ", ""))


def feed(chunks):
    """Reassemble state chunks exactly as Session.on_state does; returns the body (or None)."""
    a = gc.StateAssembler()
    body = None
    for c in chunks:
        body = a.feed(H(c))
    return body


# fixtures (docs/protocol/scenarios.yaml): key 00..0f, Nb a0..a7, Nw b0..b7, slot 2, code 4271.
KEY = H("00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F")
NB = H("A0 A1 A2 A3 A4 A5 A6 A7")
NW = H("B0 B1 B2 B3 B4 B5 B6 B7")

ANNA = (0x0A0B0C0D, 0xFF, 0x00, 0x00, "ANNA")
KRYSTIAN = (0x11223344, 0x00, 0x40, 0xFF, "KRYSTIAN")
OLA = (0x55667788, 0x00, 0xC0, 0x30, "OLA")
TID = 0x7AB1E001
ROSTER_CRC = 0x150FE75B

# sync_* records: game 1 as created, edited (11:8) and its tie twin (11:9); games 2 and 3 (OFFLINE).
G1 = gc.Record(0x5EED0001, 0, 1, ANNA[0], KRYSTIAN[0], 11, 7, 0)
G1_EDIT = gc.Record(0x5EED0001, 1, 1, ANNA[0], KRYSTIAN[0], 11, 8, 0)
G1_TIE = gc.Record(0x5EED0001, 1, 1, ANNA[0], KRYSTIAN[0], 11, 9, 0)
G2 = gc.Record(0x5EED0002, 0, 2, KRYSTIAN[0], OLA[0], 11, 5, 0)
G3 = gc.Record(0x5EED0003, 0, 3, ANNA[0], OLA[0], 11, 6, gc.FLAG_OFFLINE)
G3_DEL = gc.Record(0x5EED0003, 1, 3, ANNA[0], OLA[0], 11, 6, gc.FLAG_OFFLINE | gc.FLAG_DELETED)


def status(changes_to, games, crc, epoch):
    return gc.Status(TID, 1, 3, ROSTER_CRC, changes_to, games, crc, epoch)


def walked(frame):
    item = gc.decode_walk(frame)
    return item[1] if item and item[0] == "record" else item


class TestAuth(unittest.TestCase):
    def test_auth_exchange(self):
        # spec: auth/challenge
        challenge = H("02 00 A0 A1 A2 A3 A4 A5 A6 A7")
        self.assertEqual((challenge[0], challenge[1], challenge[2:]), (gc.PROTO, 0x00, NB))

        # spec: auth/proof
        proof = H("02 01 02 B0 B1 B2 B3 B4 B5 B6 B7 4E 47 0A 94 B9 24 AC AE")
        self.assertEqual(proof, bytes([gc.PROTO, 0x01, 2]) + NW + gc.mac(KEY, 0x57, NB))

        # spec: auth/result_ok
        result = H("02 02 00 18 57 E1 B4 A9 6E BF 5A")
        self.assertEqual(result[2], 0)  # OK
        self.assertEqual(result[3:], gc.mac(KEY, 0x42, NW))

        # spec: auth/result_bad_proof
        self.assertEqual(H("02 02 01")[2], 1)  # BAD_PROOF


class TestPairing(unittest.TestCase):
    def test_pairing(self):
        # spec: pairing/request
        self.assertEqual(H("02 01 AF 10"), bytes([gc.PROTO, 0x01]) + struct.pack("<H", 4271))

        # spec: pairing/result_ok
        res = H("02 02 00 02 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F")
        self.assertEqual((res[2], res[3], res[4:20]), (0, 2, KEY))

        # spec: pairing/result_bad_code
        self.assertEqual(H("02 02 02")[2], 2)  # BAD_CODE


class TestCommand(unittest.TestCase):
    def test_build_command(self):
        # spec: score_confirm/score
        self.assertEqual(gc.build_command("score", ["0"], 7), H("02 08 07 00"))
        # spec: focus/focus_squash
        self.assertEqual(gc.build_command("focus", ["1"], 3), H("02 0E 03 01 00 00 00"))
        # spec: focus/focus_krystian
        self.assertEqual(gc.build_command("focus", ["0x11223344"], 4), H("02 0E 04 44 33 22 11"))
        # spec: focus/focus_start
        self.assertEqual(gc.build_command("focus", ["0"], 5), H("02 0E 05 00 00 00 00"))
        # spec: focus/focus_unknown_uid
        self.assertEqual(gc.build_command("focus", ["0x12345678"], 6), H("02 0E 06 78 56 34 12"))
        # spec: last_known/score_right
        self.assertEqual(gc.build_command("score", ["1"], 1), H("02 08 01 01"))
        self.assertEqual(gc.build_command("set_side", ["1", "0x55667788"], 5), H("02 14 05 01 88 77 66 55"))

    def test_removed(self):
        self.assertNotIn("commit_mirror", gc.COMMANDS)
        self.assertNotIn("ack_outbox", gc.COMMANDS)
        self.assertNotIn(0x04, gc.SCREENS)


class TestOfflineCommands(unittest.TestCase):
    def test_build_offline(self):
        # spec: offline_games/add
        self.assertEqual(gc.build_command("add_offline_game", ["0x0A0B0C0D", "0x11223344", "0x0B", "0x07"], 5),
                         H("02 0F 05 0D 0C 0B 0A 44 33 22 11 0B 07"))
        # spec: offline_games/add_illegal
        self.assertEqual(gc.build_command("add_offline_game", ["0x0A0B0C0D", "0x11223344", "0x0F", "0x03"], 6),
                         H("02 0F 06 0D 0C 0B 0A 44 33 22 11 0F 03"))
        # spec: offline_games/remove
        self.assertEqual(gc.build_command("remove_offline_game", ["0xC0FFEE01", "0", "0"], 7),
                         H("02 10 07 01 EE FF C0 00 00 00 00"))


class TestState(unittest.TestCase):
    def test_state_decode(self):
        # spec: menu_state/menu
        self.assertIn("MENU", gc.describe(feed(["02 01 01 01 00 00 00 5B E7 0F 15 04 04 04 01 02 03"])))

        # spec: score_confirm/state
        chunks = ["02 2A 02 13 01 07 00 5B E7 0F 15 0D 0C 0B 0A 44 33 22 11 11",
                  "02 2A 12 01 00 05 03"]
        self.assertIn("PLAYING", gc.describe(feed(chunks)))

        # spec: padel_playing/tiebreak
        chunks = ["02 2B 02 13 04 0C 00 5B E7 0F 15 0D 0C 0B 0A 44 33 22 11 12",
                  "02 2B 12 01 00 06 06 03 02"]
        self.assertIn("PLAYING", gc.describe(feed(chunks)))

        # spec: padel_playing/ladder
        chunks = ["02 2C 02 13 04 0D 00 5B E7 0F 15 0D 0C 0B 0A 44 33 22 11 31",
                  "02 2C 12 00 00 02 01 03 04"]
        self.assertIn("PLAYING", gc.describe(feed(chunks)))

        # spec: focus/menu_state
        self.assertIn("MENU", gc.describe(feed(["02 02 01 01 00 03 00 5B E7 0F 15 01 04 04 01 02 03"])))

        # spec: focus/choose_players_state
        self.assertIn("CHOOSE_PLAYERS", gc.describe(feed(["02 10 01 10 01 04 00 5B E7 0F 15 05 00 00 00 44 33 22 11"])))

        # spec: sides_swapped/playing
        chunks = ["02 31 02 13 01 00 00 5B E7 0F 15 0D 0C 0B 0A 44 33 22 11 50",
                  "02 31 12 00 01 02 01"]
        self.assertIn("PLAYING", gc.describe(feed(chunks)))

        # spec: last_known/state
        chunks = ["02 40 02 13 01 01 00 5B E7 0F 15 0D 0C 0B 0A 44 33 22 11 31",
                  "02 40 12 00 00 00 01"]
        self.assertIn("PLAYING", gc.describe(feed(chunks)))


class TestHeart(unittest.TestCase):
    def test_heart(self):
        # spec: heart/write_a
        self.assertEqual(H("02 92"), bytes([gc.PROTO, 146]))
        # spec: heart/write_c
        self.assertEqual(H("02 7C"), bytes([gc.PROTO, 124]))
        # spec: heart/write_c_clear
        self.assertEqual(H("02 00"), bytes([gc.PROTO, 0]))

        # spec: heart/notify_a
        self.assertEqual(gc.decode_heart(H("02 7C 00 00")), [124, 0, 0])
        # spec: heart/notify_b
        self.assertEqual(gc.decode_heart(H("02 92 7C 00")), [146, 124, 0])
        # spec: heart/notify_c
        self.assertEqual(gc.decode_heart(H("02 92 00 00")), [146, 0, 0])
        # spec: heart/notify_a_empty
        self.assertEqual(gc.decode_heart(H("02 00 00 00")), [0, 0, 0])

        # spec: heart/discard_v0
        self.assertIsNone(gc.decode_heart(H("00 92")))
        # spec: heart/discard_short
        self.assertIsNone(gc.decode_heart(H("02")))


class TestResults(unittest.TestCase):
    def test_results(self):
        # spec: results/write
        self.assertEqual(H("02 00 00"), struct.pack("<BH", gc.PROTO, 0))

        # spec: results/read_1
        self.assertEqual(gc.decode_result(H("02 00 00 03 00 01 EE FF C0 0D 0C 0B 0A 44 33 22 11 0B 07 01")),
                         (0, 3, 0xC0FFEE01, (0x0A0B0C0D, 0x11223344, 11, 7, 1), None))
        # spec: results/read_2
        self.assertEqual(gc.decode_result(H("02 01 00 03 00 01 EE FF C0 44 33 22 11 88 77 66 55 0B 04 01")),
                         (1, 3, 0xC0FFEE01, (0x11223344, 0x55667788, 11, 4, 1), None))
        # spec: results/read_3
        self.assertEqual(gc.decode_result(H("02 02 00 03 00 01 EE FF C0 0D 0C 0B 0A 44 33 22 11 09 0B 02")),
                         (2, 3, 0xC0FFEE01, (0x0A0B0C0D, 0x11223344, 9, 11, 2), None))
        # spec: results/read_past_end
        self.assertEqual(gc.decode_result(H("02 03 00 03 00 01 EE FF C0 00 00")), (3, 3, 0xC0FFEE01, None, 0))
        # spec: results/read_no_tournament
        self.assertEqual(gc.decode_result(H("02 03 00 00 00 00 00 00 00 00 00")), (3, 0, 0, None, 0))

        # spec: offline_games/read_offline
        self.assertEqual(gc.decode_result(H("02 00 00 01 00 01 EE FF C0 0D 0C 0B 0A 44 33 22 11 0B 07 81")),
                         (0, 1, 0xC0FFEE01, (0x0A0B0C0D, 0x11223344, 11, 7, 0x81), None))
        self.assertEqual(gc.split_turn(0x81), (True, 1))
        self.assertEqual(gc.split_turn(0x02), (False, 2))
        # spec: offline_games/read_end_rev
        self.assertEqual(gc.decode_result(H("02 00 00 00 00 01 EE FF C0 01 00")), (0, 0, 0xC0FFEE01, None, 1))

        # spec: results/unauth_read
        self.assertIsNone(gc.decode_result(H("02")))
        # spec: results/discard_short
        self.assertIsNone(gc.decode_result(H("02 02")))
        # spec: results/discard_v0
        self.assertIsNone(gc.decode_result(H("00 00 00")))


class TestSyncJoin(unittest.TestCase):
    def test_join(self):
        # spec: sync_join/status
        self.assertEqual(gc.decode_status(H("02 00 01 e0 b1 7a 01 03 5b e7 0f 15 01 00 01 73 c6 15 d0 5a")),
                         status(1, 1, 0xD015C673, 0x5A))
        self.assertEqual(gc.games_crc([G1]), (1, 0xD015C673))
        # spec: sync_join/cursor_changes
        self.assertEqual(gc.build_cursor(gc.CUR_CHANGES, 1), H("02 03 01 01 00"))
        # spec: sync_join/read_1
        self.assertEqual(walked(H("02 02 01 00 ed 5e 00 01 00 0d 0c 0b 0a 44 33 22 11 0b 07 00")), G1)
        # spec: sync_join/read_end
        self.assertEqual(gc.decode_walk(H("02 04 01 00 00 00")), ("end", 1, 0))
        # spec: sync_join/ack
        self.assertEqual(gc.build_command("ack_changes", [hex(TID), "1"], 1), H("02 13 01 01 e0 b1 7a 01 00"))
        # spec: sync_join/ack_ack
        chunks = ["02 60 02 11 01 01 00 5b e7 0f 15 07 00 00 00 0d 0c 0b 0a 44",
                  "02 60 12 33 22 11 00"]
        self.assertIn("ack=1:APPLIED", gc.describe(feed(chunks)))


class TestSyncBegin(unittest.TestCase):
    def test_begin(self):
        # spec: sync_begin/status_none
        self.assertEqual(gc.decode_status(H("02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00")),
                         gc.Status(0, 0, 0, 0, 0, 0, 0, 0))
        self.assertIn("no tournament", gc.describe_status(gc.Status(0, 0, 0, 0, 0, 0, 0, 0)))
        # spec: sync_begin/roster_anna
        self.assertEqual(gc.build_roster_entry(0, *ANNA), H("02 01 00 0d 0c 0b 0a ff 00 00 41 4e 4e 41 00 00 00 00 00"))
        # spec: sync_begin/roster_krystian
        self.assertEqual(gc.build_roster_entry(1, *KRYSTIAN), H("02 01 01 44 33 22 11 00 40 ff 4b 52 59 53 54 49 41 4e 00"))
        # spec: sync_begin/roster_ola
        self.assertEqual(gc.build_roster_entry(2, *OLA), H("02 01 02 88 77 66 55 00 c0 30 4f 4c 41 00 00 00 00 00 00"))
        self.assertEqual(gc.roster_crc([ANNA, KRYSTIAN, OLA]), ROSTER_CRC)
        self.assertEqual(gc.roster_crc([ANNA, KRYSTIAN]), 0xC694F1D4)
        # spec: sync_begin/begin
        self.assertEqual(gc.build_command("begin_tournament", [hex(TID), "1", "3", str(ROSTER_CRC)], 1),
                         H("02 11 01 01 e0 b1 7a 01 03 5b e7 0f 15"))
        # spec: sync_begin/begin_ack
        chunks = ["02 70 02 11 01 01 00 5b e7 0f 15 07 00 00 00 0d 0c 0b 0a 44",
                  "02 70 12 33 22 11 00"]
        self.assertIn("ack=1:APPLIED", gc.describe(feed(chunks)))
        # spec: sync_begin/status_joined
        self.assertEqual(gc.decode_status(H("02 00 01 e0 b1 7a 01 03 5b e7 0f 15 00 00 00 8d ef 02 d2 c3")),
                         status(0, 0, 0xD202EF8D, 0xC3))
        # spec: sync_begin/push_1
        self.assertEqual(gc.build_record(G1), H("02 02 01 00 ed 5e 00 01 00 0d 0c 0b 0a 44 33 22 11 0b 07 00"))
        # spec: sync_begin/status_synced
        self.assertEqual(gc.decode_status(H("02 00 01 e0 b1 7a 01 03 5b e7 0f 15 01 00 01 73 c6 15 d0 c3")),
                         status(1, 1, 0xD015C673, 0xC3))
        # spec: sync_begin/cursor_changes
        self.assertEqual(gc.build_cursor(gc.CUR_CHANGES, 1), H("02 03 01 01 00"))
        # The pusher's own record is skipped, so its walk is empty.
        # spec: sync_begin/read_end
        self.assertEqual(gc.decode_walk(H("02 04 01 00 00 00")), ("end", 1, 0))


class TestSyncPull(unittest.TestCase):
    def test_pull(self):
        # spec: sync_pull/status
        self.assertEqual(gc.decode_status(H("02 00 01 e0 b1 7a 01 03 5b e7 0f 15 03 00 03 79 2d 75 ef 5a")),
                         status(3, 3, 0xEF752D79, 0x5A))
        self.assertEqual(gc.games_crc([G3, G1, G2]), (3, 0xEF752D79))
        # spec: sync_pull/cursor_changes
        self.assertEqual(gc.build_cursor(gc.CUR_CHANGES, 2), H("02 03 01 02 00"))
        # spec: sync_pull/read_2
        self.assertEqual(walked(H("02 02 02 00 ed 5e 00 02 00 44 33 22 11 88 77 66 55 0b 05 00")), G2)
        # spec: sync_pull/read_3
        self.assertEqual(walked(H("02 02 03 00 ed 5e 00 03 00 0d 0c 0b 0a 88 77 66 55 0b 06 02")), G3)
        # spec: sync_pull/read_end
        self.assertEqual(gc.decode_walk(H("02 04 03 00 00 00")), ("end", 3, 0))
        # spec: sync_pull/ack
        self.assertEqual(gc.build_command("ack_changes", [hex(TID), "3"], 1), H("02 13 01 01 e0 b1 7a 03 00"))
        # spec: sync_pull/ack_ack
        chunks = ["02 61 02 11 01 01 00 5b e7 0f 15 07 00 00 00 0d 0c 0b 0a 44",
                  "02 61 12 33 22 11 00"]
        self.assertIn("ack=1:APPLIED", gc.describe(feed(chunks)))


class TestSyncMerge(unittest.TestCase):
    def test_merge_frames(self):
        # spec: sync_merge/status
        self.assertEqual(gc.decode_status(H("02 00 01 e0 b1 7a 01 03 5b e7 0f 15 01 00 01 73 c6 15 d0 c3")),
                         status(1, 1, 0xD015C673, 0xC3))
        # spec: sync_merge/edit
        self.assertEqual(gc.build_record(G1_EDIT), H("02 02 01 00 ed 5e 01 01 00 0d 0c 0b 0a 44 33 22 11 0b 08 00"))
        self.assertEqual(gc.edit_record(G1, score_b=8), G1_EDIT)
        # spec: sync_merge/stale
        self.assertEqual(gc.build_record(G1), H("02 02 01 00 ed 5e 00 01 00 0d 0c 0b 0a 44 33 22 11 0b 07 00"))
        # spec: sync_merge/tie
        self.assertEqual(gc.build_record(G1_TIE), H("02 02 01 00 ed 5e 01 01 00 0d 0c 0b 0a 44 33 22 11 0b 09 00"))
        # spec: sync_merge/status_after
        self.assertEqual(gc.decode_status(H("02 00 01 e0 b1 7a 01 03 5b e7 0f 15 03 00 01 8b 0a 99 d3 c3")),
                         status(3, 1, 0xD3990A8B, 0xC3))
        self.assertEqual(gc.games_crc([G1_TIE]), (1, 0xD3990A8B))
        # spec: sync_merge/cursor_records
        self.assertEqual(gc.build_cursor(gc.CUR_RECORDS, 0), H("02 03 02 00 00"))
        # spec: sync_merge/read_0
        self.assertEqual(walked(H("02 02 01 00 ed 5e 01 01 00 0d 0c 0b 0a 44 33 22 11 0b 09 00")), G1_TIE)
        # spec: sync_merge/read_end
        self.assertEqual(gc.decode_walk(H("02 04 01 00 00 00")), ("end", 1, 0))

    def test_merge_sequence(self):
        held = gc.merge(gc.merge(None, G1), G1_EDIT)
        self.assertEqual(held, G1_EDIT)
        self.assertEqual(gc.merge(held, G1), G1_EDIT)  # stale
        self.assertEqual(gc.merge(held, G1_TIE), G1_TIE)  # tie: the larger image
        self.assertEqual(gc.merge(G1_TIE, G1_EDIT), G1_TIE)
        self.assertEqual(gc.merge(G1_TIE, G1_TIE), G1_TIE)

    def test_merge_wrap(self):
        wrapped = G1._replace(rev=0, score_b=1)
        self.assertEqual(gc.merge(G1._replace(rev=255), wrapped), wrapped)
        far = G1._replace(rev=128, score_b=1)
        self.assertEqual(gc.merge(G1, far), far)
        self.assertEqual(gc.merge(far, G1), far)
        self.assertEqual(gc.merge(G1._replace(rev=200), G1._replace(rev=100)), G1._replace(rev=200))

    def test_merge_converges(self):
        variants = [G1, G1_EDIT, G1_TIE, gc.delete_record(G1), gc.edit_record(G1_TIE, score_a=3)]
        rng = random.Random(7)
        results = set()
        for _ in range(50):
            rng.shuffle(variants)
            held = None
            for v in variants:
                held = gc.merge(held, v)
            results.add(held)
        self.assertEqual(results, {gc.delete_record(G1)})


class TestSyncTombstone(unittest.TestCase):
    def test_tombstone(self):
        # spec: sync_tombstone/remove
        self.assertEqual(gc.build_command("remove_offline_game", [hex(TID), "0x5a00", "2"], 1),
                         H("02 10 01 01 e0 b1 7a 00 5a 02 00"))
        # spec: sync_tombstone/remove_ack
        chunks = ["02 62 02 11 01 01 00 5b e7 0f 15 07 00 00 00 0d 0c 0b 0a 44",
                  "02 62 12 33 22 11 00"]
        self.assertIn("ack=1:APPLIED", gc.describe(feed(chunks)))
        # spec: sync_tombstone/results_end
        self.assertEqual(gc.decode_result(H("02 02 00 02 00 01 e0 b1 7a 01 5a")), (2, 2, TID, None, 0x5A01))
        # spec: sync_tombstone/status
        self.assertEqual(gc.decode_status(H("02 00 01 e0 b1 7a 01 03 5b e7 0f 15 04 00 02 44 60 80 a7 5a")),
                         status(4, 2, 0xA7806044, 0x5A))
        self.assertEqual(gc.games_crc([G1, G2, G3_DEL]), (2, 0xA7806044))
        # spec: sync_tombstone/cursor_changes
        self.assertEqual(gc.build_cursor(gc.CUR_CHANGES, 4), H("02 03 01 04 00"))
        # spec: sync_tombstone/read_4
        self.assertEqual(walked(H("02 02 03 00 ed 5e 01 03 00 0d 0c 0b 0a 88 77 66 55 0b 06 03")), G3_DEL)
        self.assertEqual(gc.delete_record(G3), G3_DEL)
        # spec: sync_tombstone/read_end
        self.assertEqual(gc.decode_walk(H("02 04 04 00 00 00")), ("end", 4, 0))

    def test_tombstone_sticky(self):
        self.assertEqual(gc.merge(G3_DEL, G3), G3_DEL)
        self.assertEqual(gc.merge(G3_DEL, G3._replace(rev=50)), G3_DEL)  # at any rev
        self.assertEqual(gc.merge(G3, G3_DEL), G3_DEL)
        later = gc.delete_record(G3_DEL)
        self.assertEqual(gc.merge(G3_DEL, later), later)


class TestSyncLeave(unittest.TestCase):
    def test_leave(self):
        # spec: sync_leave/back_1
        self.assertEqual(gc.build_command("back", [], 3), H("02 02 03"))
        # spec: sync_leave/back_1_ack
        self.assertIn("ack=3:APPLIED", gc.describe(feed(["02 71 01 10 01 03 00 5b e7 0f 15 07 00 00 00 00 00 00 00"])))
        # spec: sync_leave/back_2
        self.assertEqual(gc.build_command("back", [], 4), H("02 02 04"))
        # spec: sync_leave/back_2_ack
        self.assertIn("ack=4:INVALID", gc.describe(feed(["02 72 01 10 01 04 02 5b e7 0f 15 07 00 00 00 00 00 00 00"])))
        # spec: sync_leave/cursor_changes
        self.assertEqual(gc.build_cursor(gc.CUR_CHANGES, 2), H("02 03 01 02 00"))
        # spec: sync_leave/read
        self.assertEqual(walked(H("02 02 01 00 ed 5e 01 01 00 0d 0c 0b 0a 44 33 22 11 0b 09 00")), G1_TIE)
        # spec: sync_leave/read_end
        self.assertEqual(gc.decode_walk(H("02 04 03 00 00 00")), ("end", 3, 0))
        # spec: sync_leave/ack
        self.assertEqual(gc.build_command("ack_changes", [hex(TID), "3"], 5), H("02 13 05 01 e0 b1 7a 03 00"))
        # spec: sync_leave/ack_ack
        self.assertIn("ack=5:APPLIED", gc.describe(feed(["02 73 01 10 01 05 00 5b e7 0f 15 07 00 00 00 00 00 00 00"])))
        # spec: sync_leave/back_3
        self.assertEqual(gc.build_command("back", [], 6), H("02 02 06"))
        # spec: sync_leave/back_3_ack
        self.assertIn("MENU", gc.describe(feed(["02 74 01 01 00 06 00 5b e7 0f 15 04 04 04 01 02 03"])))
        # spec: sync_leave/status_none
        self.assertEqual(gc.decode_status(H("02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00")).tournament_id, 0)


class TestRecordHelpers(unittest.TestCase):
    def test_games_crc(self):
        self.assertEqual(gc.games_crc([]), (0, 0xD202EF8D))
        self.assertEqual(gc.games_crc([G3_DEL]), (0, 0xD202EF8D))
        same_order = G2._replace(order=1, id=0x00000001)
        self.assertEqual(gc.games_crc([G1, same_order]), gc.games_crc([same_order, G1]))  # (order, id) tie-break

    def test_decode_rejects(self):
        self.assertIsNone(gc.decode_status(H("02 00 01")))
        self.assertIsNone(gc.decode_status(H("01 00") + bytes(18)))
        self.assertIsNone(gc.decode_status(H("02 04 01 00 00 00") + bytes(14)))
        self.assertIsNone(gc.decode_walk(H("01 04 01 00 00 00")))
        self.assertIsNone(gc.decode_walk(H("02 02 01 00")))
        self.assertIsNone(gc.decode_walk(H("02 04 01")))
        self.assertIsNone(gc.decode_walk(H("02")))

    def test_create(self):
        held = [G1, G2, G3_DEL]
        rec = gc.new_record(held, ANNA[0], OLA[0], 11, 3, gc.FLAG_OFFLINE)
        self.assertNotIn(rec.id, {0, G1.id, G2.id, G3_DEL.id})
        self.assertEqual((rec.rev, rec.order), (0, 4))  # above the tombstone's order too
        self.assertEqual(gc.new_record([], 1, 2, 0, 0).order, 1)
        self.assertEqual(gc.new_record([G1._replace(order=gc.ORDER_MAX)], 1, 2, 0, 0).order, gc.ORDER_MAX)
        self.assertEqual(gc.edit_record(G1._replace(rev=255), score_a=5).rev, 0)
        self.assertEqual(gc.Record.from_wire(G3_DEL.wire()), G3_DEL)
        self.assertEqual(len(gc.build_record(G1)), 20)

    def test_args(self):
        self.assertEqual(gc.parse_roster_arg("ANNA:ff0000:0x0A0B0C0D"), ANNA)
        self.assertTrue(0 < gc.parse_roster_arg("OLA:00c030")[0] <= 0xFFFFFFFF)
        self.assertEqual(gc.parse_record_arg("0x5EED0001:1:1:0x0A0B0C0D:0x11223344:11:9"), G1_TIE)
        self.assertEqual(gc.parse_record_arg("0x5EED0003:1:3:0x0A0B0C0D:0x55667788:11:6:3"), G3_DEL)
        rec = gc.parse_record_arg("new:0:next:1:2:11:4", [G1, G2])
        self.assertEqual((rec.rev, rec.order), (0, 3))
        self.assertNotIn(rec.id, (0, G1.id, G2.id))
        with self.assertRaises(SystemExit):
            gc.parse_record_arg("1:256:1:1:2:11:4")
        with self.assertRaises(SystemExit):
            gc.parse_record_arg("1:0:1:1:2:11")

    def test_uuids(self):
        self.assertEqual(gc.TOURNAMENT, "0d78000a-2f44-4f31-93b7-b78dc9e29735")
        self.assertEqual(gc.RESULTS, "0d780009-2f44-4f31-93b7-b78dc9e29735")


if __name__ == "__main__":
    unittest.main(verbosity=2)
