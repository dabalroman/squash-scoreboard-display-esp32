#!/usr/bin/env python3
"""Host tests for helpers/garmin_client.py's pure encode/decode functions.

Every vector the client touches is tagged `spec: <scenario>/<step>` next to its hex literal, so
`python helpers/check_protocol.py --repo client` (run by the native pio test pre-script) can compare
the literal against docs/protocol/scenarios.yaml and fail on drift. The tags must sit on the line
directly above the hex literal(s), one frame per step; a chunked state step lists its chunks on one
line. Run: python helpers/test_garmin_client.py
"""

import os
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


class TestAuth(unittest.TestCase):
    def test_auth_exchange(self):
        # spec: auth/challenge
        challenge = H("01 00 A0 A1 A2 A3 A4 A5 A6 A7")
        self.assertEqual((challenge[0], challenge[1], challenge[2:]), (gc.PROTO, 0x00, NB))

        # spec: auth/proof
        proof = H("01 01 02 B0 B1 B2 B3 B4 B5 B6 B7 4E 47 0A 94 B9 24 AC AE")
        self.assertEqual(proof, bytes([gc.PROTO, 0x01, 2]) + NW + gc.mac(KEY, 0x57, NB))

        # spec: auth/result_ok
        result = H("01 02 00 18 57 E1 B4 A9 6E BF 5A")
        self.assertEqual(result[2], 0)  # OK
        self.assertEqual(result[3:], gc.mac(KEY, 0x42, NW))

        # spec: auth/result_bad_proof
        self.assertEqual(H("01 02 01")[2], 1)  # BAD_PROOF


class TestPairing(unittest.TestCase):
    def test_pairing(self):
        # spec: pairing/request
        self.assertEqual(H("01 01 AF 10"), bytes([gc.PROTO, 0x01]) + struct.pack("<H", 4271))

        # spec: pairing/result_ok
        res = H("01 02 00 02 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F")
        self.assertEqual((res[2], res[3], res[4:20]), (0, 2, KEY))

        # spec: pairing/result_bad_code
        self.assertEqual(H("01 02 02")[2], 2)  # BAD_CODE


class TestCommand(unittest.TestCase):
    def test_build_command(self):
        # spec: score_confirm/score
        self.assertEqual(gc.build_command("score", ["0"], 7), H("01 08 07 00"))
        # spec: focus/focus_squash
        self.assertEqual(gc.build_command("focus", ["1"], 3), H("01 0E 03 01 00 00 00"))
        # spec: focus/focus_krystian
        self.assertEqual(gc.build_command("focus", ["0x11223344"], 4), H("01 0E 04 44 33 22 11"))
        # spec: focus/focus_start
        self.assertEqual(gc.build_command("focus", ["0"], 5), H("01 0E 05 00 00 00 00"))
        # spec: focus/focus_unknown_uid
        self.assertEqual(gc.build_command("focus", ["0x12345678"], 6), H("01 0E 06 78 56 34 12"))
        # spec: last_known/score_right
        self.assertEqual(gc.build_command("score", ["1"], 1), H("01 08 01 01"))


class TestOfflineCommands(unittest.TestCase):
    def test_build_offline(self):
        # spec: offline_games/add
        self.assertEqual(gc.build_command("add_offline_game", ["0x0A0B0C0D", "0x11223344", "0x0B", "0x07"], 5),
                         H("01 0F 05 0D 0C 0B 0A 44 33 22 11 0B 07"))
        # spec: offline_games/add_illegal
        self.assertEqual(gc.build_command("add_offline_game", ["0x0A0B0C0D", "0x11223344", "0x0F", "0x03"], 6),
                         H("01 0F 06 0D 0C 0B 0A 44 33 22 11 0F 03"))
        # spec: offline_games/remove
        self.assertEqual(gc.build_command("remove_offline_game", ["0xC0FFEE01", "0", "0"], 7),
                         H("01 10 07 01 EE FF C0 00 00 00 00"))


class TestState(unittest.TestCase):
    def test_state_decode(self):
        # spec: menu_state/menu
        self.assertIn("MENU", gc.describe(feed(["01 01 01 01 00 00 00 5B E7 0F 15 04 04 04 01 02 03"])))

        # spec: score_confirm/state
        chunks = ["01 2A 02 13 01 07 00 5B E7 0F 15 0D 0C 0B 0A 44 33 22 11 11",
                  "01 2A 12 01 00 05 03"]
        self.assertIn("PLAYING", gc.describe(feed(chunks)))

        # spec: padel_playing/tiebreak
        chunks = ["01 2B 02 13 04 0C 00 5B E7 0F 15 0D 0C 0B 0A 44 33 22 11 12",
                  "01 2B 12 01 00 06 06 03 02"]
        self.assertIn("PLAYING", gc.describe(feed(chunks)))

        # spec: padel_playing/ladder
        chunks = ["01 2C 02 13 04 0D 00 5B E7 0F 15 0D 0C 0B 0A 44 33 22 11 31",
                  "01 2C 12 00 00 02 01 03 04"]
        self.assertIn("PLAYING", gc.describe(feed(chunks)))

        # spec: focus/menu_state
        self.assertIn("MENU", gc.describe(feed(["01 02 01 01 00 03 00 5B E7 0F 15 01 04 04 01 02 03"])))

        # spec: focus/choose_players_state
        self.assertIn("CHOOSE_PLAYERS", gc.describe(feed(["01 10 01 10 01 04 00 5B E7 0F 15 05 00 00 00 44 33 22 11"])))

        # spec: sides_swapped/playing
        chunks = ["01 31 02 13 01 00 00 5B E7 0F 15 0D 0C 0B 0A 44 33 22 11 50",
                  "01 31 12 00 01 02 01"]
        self.assertIn("PLAYING", gc.describe(feed(chunks)))

        # spec: last_known/state
        chunks = ["01 40 02 13 01 01 00 5B E7 0F 15 0D 0C 0B 0A 44 33 22 11 31",
                  "01 40 12 00 00 00 01"]
        self.assertIn("PLAYING", gc.describe(feed(chunks)))


class TestHeart(unittest.TestCase):
    def test_heart(self):
        # spec: heart/write_a
        self.assertEqual(H("01 92"), bytes([gc.PROTO, 146]))
        # spec: heart/write_c
        self.assertEqual(H("01 7C"), bytes([gc.PROTO, 124]))
        # spec: heart/write_c_clear
        self.assertEqual(H("01 00"), bytes([gc.PROTO, 0]))

        # spec: heart/notify_a
        self.assertEqual(gc.decode_heart(H("01 7C 00 00")), [124, 0, 0])
        # spec: heart/notify_b
        self.assertEqual(gc.decode_heart(H("01 92 7C 00")), [146, 124, 0])
        # spec: heart/notify_c
        self.assertEqual(gc.decode_heart(H("01 92 00 00")), [146, 0, 0])
        # spec: heart/notify_a_empty
        self.assertEqual(gc.decode_heart(H("01 00 00 00")), [0, 0, 0])

        # spec: heart/discard_v0
        self.assertIsNone(gc.decode_heart(H("00 92")))
        # spec: heart/discard_short
        self.assertIsNone(gc.decode_heart(H("01")))


class TestResults(unittest.TestCase):
    def test_results(self):
        # spec: results/write
        self.assertEqual(H("01 00 00"), struct.pack("<BH", gc.PROTO, 0))

        # spec: results/read_1
        self.assertEqual(gc.decode_result(H("01 00 00 03 00 01 EE FF C0 0D 0C 0B 0A 44 33 22 11 0B 07 01")),
                         (0, 3, 0xC0FFEE01, (0x0A0B0C0D, 0x11223344, 11, 7, 1), None))
        # spec: results/read_2
        self.assertEqual(gc.decode_result(H("01 01 00 03 00 01 EE FF C0 44 33 22 11 88 77 66 55 0B 04 01")),
                         (1, 3, 0xC0FFEE01, (0x11223344, 0x55667788, 11, 4, 1), None))
        # spec: results/read_3
        self.assertEqual(gc.decode_result(H("01 02 00 03 00 01 EE FF C0 0D 0C 0B 0A 44 33 22 11 09 0B 02")),
                         (2, 3, 0xC0FFEE01, (0x0A0B0C0D, 0x11223344, 9, 11, 2), None))
        # spec: results/read_past_end
        self.assertEqual(gc.decode_result(H("01 03 00 03 00 01 EE FF C0 00 00")), (3, 3, 0xC0FFEE01, None, 0))
        # older firmware: 9 B end frame, no rev
        self.assertEqual(gc.decode_result(H("01 03 00 03 00 01 EE FF C0")), (3, 3, 0xC0FFEE01, None, None))
        # spec: results/read_no_tournament
        self.assertEqual(gc.decode_result(H("01 03 00 00 00 00 00 00 00 00 00")), (3, 0, 0, None, 0))

        # spec: offline_games/read_offline
        self.assertEqual(gc.decode_result(H("01 00 00 01 00 01 EE FF C0 0D 0C 0B 0A 44 33 22 11 0B 07 81")),
                         (0, 1, 0xC0FFEE01, (0x0A0B0C0D, 0x11223344, 11, 7, 0x81), None))
        self.assertEqual(gc.split_turn(0x81), (True, 1))
        self.assertEqual(gc.split_turn(0x02), (False, 2))
        # spec: offline_games/read_end_rev
        self.assertEqual(gc.decode_result(H("01 00 00 00 00 01 EE FF C0 01 00")), (0, 0, 0xC0FFEE01, None, 1))

        # spec: results/unauth_read
        self.assertIsNone(gc.decode_result(H("01")))
        # spec: results/discard_short
        self.assertIsNone(gc.decode_result(H("01 02")))
        # spec: results/discard_v0
        self.assertIsNone(gc.decode_result(H("00 00 00")))


ANNA = (0x0A0B0C0D, 0xFF, 0x00, 0x00, "ANNA")
KRYSTIAN = (0x11223344, 0x00, 0x40, 0xFF, "KRYSTIAN")
OLA = (0x55667788, 0x00, 0xC0, 0x30, "OLA")
TID = "0x7AB1E001"


class TestTabletFrames(unittest.TestCase):
    def test_roster_entries(self):
        # spec: tablet_begin/roster_anna
        self.assertEqual(gc.build_roster_entry(0, *ANNA), H("01 01 00 0d 0c 0b 0a ff 00 00 41 4e 4e 41 00 00 00 00 00"))
        # spec: tablet_begin/roster_krystian
        self.assertEqual(gc.build_roster_entry(1, *KRYSTIAN), H("01 01 01 44 33 22 11 00 40 ff 4b 52 59 53 54 49 41 4e 00"))
        # spec: tablet_repush/roster_anna
        self.assertEqual(gc.build_roster_entry(0, *ANNA), H("01 01 00 0d 0c 0b 0a ff 00 00 41 4e 4e 41 00 00 00 00 00"))
        # spec: tablet_repush/roster_krystian
        self.assertEqual(gc.build_roster_entry(1, *KRYSTIAN), H("01 01 01 44 33 22 11 00 40 ff 4b 52 59 53 54 49 41 4e 00"))
        # spec: tablet_repush/roster_ola
        self.assertEqual(gc.build_roster_entry(2, *OLA), H("01 01 02 88 77 66 55 00 c0 30 4f 4c 41 00 00 00 00 00 00"))

    def test_mirror_entries(self):
        # spec: tablet_mirror/mirror_0
        self.assertEqual(gc.build_mirror_entry(0, 0x5EED0001, 0x0A0B0C0D, 0x11223344, 11, 7, 1),
                         H("01 02 00 01 00 ed 5e 0d 0c 0b 0a 44 33 22 11 0b 07 01"))
        # spec: tablet_mirror/mirror_1
        self.assertEqual(gc.build_mirror_entry(1, 0x5EED0002, 0x11223344, 0x55667788, 11, 4, 2),
                         H("01 02 01 02 00 ed 5e 44 33 22 11 88 77 66 55 0b 04 02"))
        # spec: tablet_mirror/mirror_2
        self.assertEqual(gc.build_mirror_entry(2, 0x7AB10003, 0x0A0B0C0D, 0x55667788, 11, 9),
                         H("01 02 02 03 00 b1 7a 0d 0c 0b 0a 88 77 66 55 0b 09 00"))

    def test_cursor(self):
        # spec: tablet_outbox/cursor_outbox
        self.assertEqual(gc.build_cursor(gc.T_OUTBOX, 1), H("01 03 01 01 00"))
        # spec: tablet_outbox/cursor_status
        self.assertEqual(gc.build_cursor(gc.T_STATUS), H("01 03 00 00 00"))

    def test_crc(self):
        roster2 = [gc.roster_wire(*ANNA), gc.roster_wire(*KRYSTIAN)]
        self.assertEqual(gc.tablet_crc(roster2), 0xC694F1D4)
        self.assertEqual(gc.tablet_crc(roster2 + [gc.roster_wire(*OLA)]), 0x150FE75B)
        games = [gc.mirror_wire(0x5EED0001, 0x0A0B0C0D, 0x11223344, 11, 7, 1),
                 gc.mirror_wire(0x5EED0002, 0x11223344, 0x55667788, 11, 4, 2),
                 gc.mirror_wire(0x7AB10003, 0x0A0B0C0D, 0x55667788, 11, 9)]
        self.assertEqual(gc.tablet_crc(games), 0x8050D643)
        self.assertEqual(gc.tablet_crc([]), 0xD202EF8D)

    def test_args(self):
        self.assertEqual(gc.parse_roster_arg("ANNA:ff0000:0x0A0B0C0D"), ANNA)
        self.assertTrue(0 < gc.parse_roster_arg("OLA:00c030")[0] <= 0xFFFFFFFF)
        self.assertEqual(gc.parse_game_arg("0x5EED0001:1:2:11:7:1"), (0x5EED0001, 1, 2, 11, 7, 1))
        self.assertEqual(gc.parse_game_arg("5:1:2:11:7")[5], 0)


class TestTabletCommands(unittest.TestCase):
    def test_commands(self):
        # spec: tablet_begin/begin
        self.assertEqual(gc.build_command("begin_tournament", [TID, "1", "2", str(0xC694F1D4)], 1),
                         H("01 11 01 01 e0 b1 7a 01 02 d4 f1 94 c6"))
        # spec: tablet_repush/repush
        self.assertEqual(gc.build_command("begin_tournament", [TID, "1", "3", str(0x150FE75B)], 2),
                         H("01 11 02 01 e0 b1 7a 01 03 5b e7 0f 15"))
        # spec: tablet_outbox/ack
        self.assertEqual(gc.build_command("ack_outbox", [TID, "2"], 3), H("01 13 03 01 e0 b1 7a 02 00"))
        # spec: tablet_mirror/commit
        self.assertEqual(gc.build_command("commit_mirror", [TID, "3", str(0x8050D643)], 4),
                         H("01 12 04 01 e0 b1 7a 03 43 d6 50 80"))
        # spec: tablet_set_side/set_side
        self.assertEqual(gc.build_command("set_side", ["1", "0x55667788"], 5), H("01 14 05 01 88 77 66 55"))

    def test_refusal_commands(self):
        # spec: tablet_refusals/begin_other_tid
        self.assertEqual(gc.build_command("begin_tournament", ["0x7AB1E002", "1", "3", str(0x150FE75B)], 6),
                         H("01 11 06 02 e0 b1 7a 01 03 5b e7 0f 15"))
        # spec: tablet_refusals/commit_bad_crc
        self.assertEqual(gc.build_command("commit_mirror", [TID, "0", "0"], 7), H("01 12 07 01 e0 b1 7a 00 00 00 00 00"))
        # spec: tablet_refusals/remove
        self.assertEqual(gc.build_command("remove_offline_game", [TID, "1", "0"], 8),
                         H("01 10 08 01 e0 b1 7a 01 00 00 00"))
        # spec: tablet_refusals/back
        self.assertEqual(gc.build_command("back", [], 9), H("01 02 09"))
        # spec: tablet_refusals/begin_on_menu
        self.assertEqual(gc.build_command("begin_tournament", [TID, "1", "2", str(0xC694F1D4)], 1),
                         H("01 11 01 01 e0 b1 7a 01 02 d4 f1 94 c6"))


class TestTabletReads(unittest.TestCase):
    def test_status(self):
        # spec: tablet_begin/status_none
        self.assertEqual(gc.decode_status(H("01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00")),
                         (0, 0, 0, 0, 0, 0, 0))
        # spec: tablet_begin/status_begun
        self.assertEqual(gc.decode_status(H("01 00 01 e0 b1 7a 01 02 d4 f1 94 c6 01 00 00 00 00 00 00 00")),
                         (0x7AB1E001, 1, 2, 0xC694F1D4, 1, 0, 0))
        # spec: tablet_outbox/status
        self.assertEqual(gc.decode_status(H("01 00 01 e0 b1 7a 01 03 5b e7 0f 15 01 00 02 00 00 00 00 00")),
                         (0x7AB1E001, 1, 3, 0x150FE75B, 1, 2, 0))
        # spec: tablet_outbox/status_acked
        self.assertEqual(gc.decode_status(H("01 00 01 e0 b1 7a 01 03 5b e7 0f 15 03 00 02 00 00 00 00 00")),
                         (0x7AB1E001, 1, 3, 0x150FE75B, 3, 2, 0))
        # spec: tablet_mirror/status_mirrored
        self.assertEqual(gc.decode_status(H("01 00 01 e0 b1 7a 01 03 5b e7 0f 15 03 00 02 00 43 d6 50 80")),
                         (0x7AB1E001, 1, 3, 0x150FE75B, 3, 2, 0x8050D643))
        self.assertIn("no tablet", gc.describe_status((0,) * 7))
        self.assertIsNone(gc.decode_status(H("01 00 01")))
        self.assertIsNone(gc.decode_status(H("00 00") + bytes(18)))

    def test_outbox(self):
        # spec: tablet_outbox/read_1
        self.assertEqual(gc.decode_outbox(H("01 01 01 00 01 00 ed 5e 0d 0c 0b 0a 44 33 22 11 0b 07 00")),
                         ("game", 1, 0x5EED0001, 0x0A0B0C0D, 0x11223344, 11, 7, 0))
        # spec: tablet_outbox/read_2
        self.assertEqual(gc.decode_outbox(H("01 01 02 00 02 00 ed 5e 44 33 22 11 88 77 66 55 0b 04 02")),
                         ("game", 2, 0x5EED0002, 0x11223344, 0x55667788, 11, 4, gc.GAME_OFFLINE))
        # spec: tablet_outbox/read_end
        self.assertEqual(gc.decode_outbox(H("01 02 03 00")), ("end", 3))
        self.assertIsNone(gc.decode_outbox(H("00 02 03 00")))
        self.assertIsNone(gc.decode_outbox(H("01 01 01")))

    def test_results_after_mirror(self):
        # spec: tablet_mirror/results_2
        self.assertEqual(gc.decode_result(H("01 01 00 02 00 01 e0 b1 7a 0d 0c 0b 0a 88 77 66 55 0b 09 01")),
                         (1, 2, 0x7AB1E001, (0x0A0B0C0D, 0x55667788, 11, 9, 1), None))
        # spec: tablet_mirror/results_end
        self.assertEqual(gc.decode_result(H("01 02 00 02 00 01 e0 b1 7a 01 00")), (2, 2, 0x7AB1E001, None, 1))


class TestTabletStates(unittest.TestCase):
    def test_states(self):
        # spec: tablet_begin/wait_state
        self.assertIn("TOURNAMENT_WAIT", gc.describe(feed(["01 60 01 04 00 00 00 5b e7 0f 15"])))

        # spec: tablet_begin/begin_ack
        chunks = ["01 61 02 11 01 01 00 d4 f1 94 c6 03 00 00 00 0d 0c 0b 0a 44",
                  "01 61 12 33 22 11 00"]
        self.assertIn("MATCH_START", gc.describe(feed(chunks)))

        # spec: tablet_repush/repush_ack
        chunks = ["01 62 02 11 01 02 00 5b e7 0f 15 07 00 00 00 0d 0c 0b 0a 44",
                  "01 62 12 33 22 11 00"]
        self.assertIn("ack=2:APPLIED", gc.describe(feed(chunks)))

        # spec: tablet_outbox/ack_ack
        chunks = ["01 63 02 11 01 03 00 5b e7 0f 15 07 00 00 00 0d 0c 0b 0a 44",
                  "01 63 12 33 22 11 00"]
        self.assertIn("ack=3:APPLIED", gc.describe(feed(chunks)))

        # spec: tablet_mirror/commit_ack
        chunks = ["01 64 02 11 01 04 00 5b e7 0f 15 07 00 00 00 0d 0c 0b 0a 44",
                  "01 64 12 33 22 11 00"]
        self.assertIn("ack=4:APPLIED", gc.describe(feed(chunks)))

        # spec: tablet_set_side/set_side_ack
        chunks = ["01 65 02 11 01 05 00 5b e7 0f 15 07 00 00 00 0d 0c 0b 0a 88",
                  "01 65 12 77 66 55 00"]
        self.assertIn("ack=5:APPLIED", gc.describe(feed(chunks)))

        # spec: tablet_refusals/begin_other_tid_ack
        chunks = ["01 66 02 11 01 06 02 5b e7 0f 15 07 00 00 00 0d 0c 0b 0a 88",
                  "01 66 12 77 66 55 00"]
        self.assertIn("ack=6:INVALID", gc.describe(feed(chunks)))

        # spec: tablet_refusals/commit_bad_crc_ack
        chunks = ["01 67 02 11 01 07 02 5b e7 0f 15 07 00 00 00 0d 0c 0b 0a 88",
                  "01 67 12 77 66 55 00"]
        self.assertIn("ack=7:INVALID", gc.describe(feed(chunks)))

        # spec: tablet_refusals/remove_ack
        chunks = ["01 68 02 11 01 08 01 5b e7 0f 15 07 00 00 00 0d 0c 0b 0a 88",
                  "01 68 12 77 66 55 00"]
        self.assertIn("ack=8:WRONG_SCREEN", gc.describe(feed(chunks)))

        # spec: tablet_refusals/back_ack
        chunks = ["01 69 02 11 01 09 01 5b e7 0f 15 07 00 00 00 0d 0c 0b 0a 88",
                  "01 69 12 77 66 55 00"]
        self.assertIn("ack=9:WRONG_SCREEN", gc.describe(feed(chunks)))

        # spec: tablet_refusals/begin_on_menu_ack
        self.assertIn("ack=1:WRONG_SCREEN", gc.describe(feed(["01 02 01 01 00 01 01 5b e7 0f 15 04 04 04 01 02 03"])))

    def test_uuids(self):
        self.assertEqual(gc.TOURNAMENT, "0d78000a-2f44-4f31-93b7-b78dc9e29735")
        self.assertEqual(gc.RESULTS, "0d780009-2f44-4f31-93b7-b78dc9e29735")


if __name__ == "__main__":
    unittest.main(verbosity=2)
