"""Pure calendar checks; this suite never opens a serial port."""
from datetime import datetime, timezone
import unittest

from rtc_hardware_host import decode, encode


class CalendarTest(unittest.TestCase):
    def test_known_saturday(self):
        result = decode(bytes.fromhex("0001010000000700"))
        self.assertTrue(result["valid"])
        self.assertEqual(result["timestamp"], 946684800)

    def test_sunday_and_last_supported_second(self):
        for raw, stamp in (("0001020000000100", 946771200),
                           ("9912312359590500", 4102444799)):
            self.assertEqual(decode(bytes.fromhex(raw))["timestamp"], stamp)
            self.assertEqual(encode(stamp).hex(), raw)

    def test_leap_day(self):
        raw = bytes.fromhex("2402291234560500")
        stamp = int(datetime(2024, 2, 29, 12, 34, 56, tzinfo=timezone.utc).timestamp())
        self.assertEqual(decode(raw)["timestamp"], stamp)
        self.assertEqual(encode(stamp), raw)

    def test_invalid_bcd_and_calendar(self):
        for raw in ("241a291234560500", "2302291234560400", "2402292434560500",
                    "2402291260560500", "2402291234600500", "2402291234560000",
                    "2402291234560400", "0000000000000000", "ff" * 8):
            with self.subTest(raw=raw):
                self.assertFalse(decode(bytes.fromhex(raw))["valid"])

    def test_reserved_read_byte_is_observed_not_a_calendar_field(self):
        result = decode(bytes.fromhex("00010100000007ff"))
        self.assertTrue(result["valid"])
        self.assertEqual(result["reserved_byte"], 255)

    def test_exact_response_length(self):
        for size in (0, 7, 9):
            self.assertFalse(decode(bytes(size))["valid"])

    def test_encoder_refuses_outside_hardware_range(self):
        for stamp in (946684799, 4102444800):
            with self.assertRaises(ValueError):
                encode(stamp)


if __name__ == "__main__":
    unittest.main()
