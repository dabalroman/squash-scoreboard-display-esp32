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
                         (0, 3, 0xC0FFEE01, (0x0A0B0C0D, 0x11223344, 11, 7, 1)))
        # spec: results/read_2
        self.assertEqual(gc.decode_result(H("01 01 00 03 00 01 EE FF C0 44 33 22 11 88 77 66 55 0B 04 01")),
                         (1, 3, 0xC0FFEE01, (0x11223344, 0x55667788, 11, 4, 1)))
        # spec: results/read_3
        self.assertEqual(gc.decode_result(H("01 02 00 03 00 01 EE FF C0 0D 0C 0B 0A 44 33 22 11 09 0B 02")),
                         (2, 3, 0xC0FFEE01, (0x0A0B0C0D, 0x11223344, 9, 11, 2)))
        # spec: results/read_past_end
        self.assertEqual(gc.decode_result(H("01 03 00 03 00 01 EE FF C0")), (3, 3, 0xC0FFEE01, None))
        # spec: results/read_no_tournament
        self.assertEqual(gc.decode_result(H("01 03 00 00 00 00 00 00 00")), (3, 0, 0, None))

        # spec: results/unauth_read
        self.assertIsNone(gc.decode_result(H("01")))
        # spec: results/discard_short
        self.assertIsNone(gc.decode_result(H("01 02")))
        # spec: results/discard_v0
        self.assertIsNone(gc.decode_result(H("00 00 00")))


if __name__ == "__main__":
    unittest.main(verbosity=2)
