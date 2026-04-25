from __future__ import annotations

import queue
import sys
import time
import unittest
from pathlib import Path


APP_ROOT = Path(__file__).resolve().parents[1]
if str(APP_ROOT) not in sys.path:
    sys.path.insert(0, str(APP_ROOT))

from app.serial_worker import SerialReader


class SerialReaderWriteRecoveryTests(unittest.TestCase):
    def test_write_blocked_stays_true_before_cooldown_ends(self) -> None:
        reader = SerialReader("COM1")
        reader._enter_write_cooldown()

        self.assertTrue(reader.write_blocked)

    def test_write_blocked_clears_after_cooldown(self) -> None:
        reader = SerialReader("COM1")
        reader._write_timeout_streak = reader._MAX_WRITE_TIMEOUT_STREAK
        reader._enter_write_cooldown()
        reader._write_blocked_until = time.monotonic() - 0.01

        self.assertFalse(reader.write_blocked)
        self.assertEqual(reader._write_timeout_streak, 0)

    def test_write_line_recovers_after_cooldown_expires(self) -> None:
        reader = SerialReader("COM1")
        reader._serial = object()
        reader._write_queue = queue.Queue()
        reader._enter_write_cooldown()

        self.assertFalse(reader.write_line("GET_CONFIG"))

        reader._write_blocked_until = time.monotonic() - 0.01

        self.assertTrue(reader.write_line("GET_CONFIG"))
        self.assertEqual(reader._write_queue.get_nowait(), b"GET_CONFIG\n")


if __name__ == "__main__":
    unittest.main()
