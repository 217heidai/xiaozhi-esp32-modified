"""Terrier idle-rotation durations must match the EFC clips (round down).

The idle rotation table in main/application.cc (kIdleEmotions) and the
one-shot welcome/done durations are hand-written seconds, while the clips
are EFC files whose real length is frames * frame delay. Rounding down
(cutting the tail) is the rule: a 7.25s clip gets 7s, an 8.5s clip gets 8s.
This test re-derives every duration from the actual .efc files, so a
regenerated clip or a typo in the table fails here instead of showing up
as a cut or replayed animation on the device.

It also pins the EFC encoding to zlib+legacy (flags byte 0x04): the
zlib+PRED encoding (0x0c) zeroes the TLS global mutex on frame-0 decode
and crashes the device on the first MQTT publish (see TERRIER_SYNC.md).
"""

import math
import pathlib
import re
import struct
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TERRIER_DIR = ROOT / "main" / "assets" / "emoji" / "terrier"
APPLICATION_CC = ROOT / "main" / "application.cc"
LCD_DISPLAY_CC = ROOT / "main" / "display" / "lcd_display.cc"

# EFC header: magic "EFC1" (4 bytes), version (1), flags (1), then the
# frame count as a little-endian uint16 at offset 16. Verified against
# all 16 clips (decode_frames counts match).
EFC_FRAME_COUNT_OFFSET = 16
EFC_FLAGS_OFFSET = 5
EFC_FLAGS_ZLIB_LEGACY = 0x04


def efc_frame_count(path):
    with open(path, "rb") as f:
        header = f.read(32)
    if len(header) < EFC_FRAME_COUNT_OFFSET + 2 or header[:4] != b"EFC1":
        raise ValueError(f"{path} is not an EFC file")
    return struct.unpack_from("<H", header, EFC_FRAME_COUNT_OFFSET)[0]


def efc_flags(path):
    with open(path, "rb") as f:
        header = f.read(8)
    return header[EFC_FLAGS_OFFSET]


class TerrierEmotionDurationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.frame_delay_ms = cls._frame_delay_ms()
        cls.table = cls._idle_table()
        cls.one_shot = cls._one_shot_durations()

    @staticmethod
    def _frame_delay_ms():
        delays = set(re.findall(r"lv_efc_set_frame_delay\(\w+, (\d+)\)",
                                LCD_DISPLAY_CC.read_text()))
        assert len(delays) == 1, f"expected one frame delay, got {delays}"
        return int(delays.pop())

    @staticmethod
    def _idle_table():
        text = APPLICATION_CC.read_text()
        match = re.search(r"kIdleEmotions\[\] = \{(.*?)\};", text, re.S)
        assert match, "kIdleEmotions table not found in application.cc"
        entries = re.findall(r'\{"(\w+)", (\d+)\}', match.group(1))
        assert entries, "no entries in kIdleEmotions table"
        return {name: int(seconds) for name, seconds in entries}

    @staticmethod
    def _one_shot_durations():
        text = APPLICATION_CC.read_text()
        found = {}
        for name in ("welcome", "done"):
            match = re.search(
                rf"idle_emotion_rotate_after_ = (\d+);.*{name} is a \d+s clip",
                text)
            assert match, f"{name} one-shot duration not found in application.cc"
            found[name] = int(match.group(1))
        return found

    def clip_seconds(self, name):
        frames = efc_frame_count(TERRIER_DIR / f"{name}.efc")
        return frames * self.frame_delay_ms / 1000.0

    def test_idle_table_matches_clips_rounded_down(self):
        self.assertEqual(len(self.table), 8,
                         f"expected 8 idle emotions, got {sorted(self.table)}")
        for name, seconds in sorted(self.table.items()):
            clip = self.clip_seconds(name)
            self.assertEqual(seconds, math.floor(clip),
                             f"{name}: table says {seconds}s but clip is {clip}s")
            self.assertGreater(seconds, 0, f"{name}: duration must be positive")

    def test_welcome_and_done_match_clips_rounded_down(self):
        for name, seconds in sorted(self.one_shot.items()):
            clip = self.clip_seconds(name)
            self.assertEqual(seconds, math.floor(clip),
                             f"{name}: code says {seconds}s but clip is {clip}s")

    def test_all_efc_files_are_zlib_legacy_no_pred(self):
        for path in sorted(TERRIER_DIR.glob("*.efc")):
            self.assertEqual(efc_flags(path), EFC_FLAGS_ZLIB_LEGACY,
                             f"{path.name}: flags 0x{efc_flags(path):02x}, "
                             "want 0x04 (zlib+legacy, no PRED)")


if __name__ == "__main__":
    unittest.main()
