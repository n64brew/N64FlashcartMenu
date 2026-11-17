"""Pure packet regressions for the native and inherited-policy RTC probes."""
import struct
import unittest
import zlib

from decode_rtc_native import FIELDS, parse_packet


def packet(**fields):
    values = dict(stage=7, mode=2, stop_result=-100, cfg_result=-100, start_result=-100)
    values.update(fields)
    raw = bytearray(256)
    raw[:4] = b"RTP2"
    raw[4:8] = (0x52544303).to_bytes(4, "big")
    offset = 8
    for name, kind in FIELDS:
        default = bytes(struct.calcsize(kind)) if kind.endswith("s") else 0
        struct.pack_into(">" + kind, raw, offset, values.get(name, default))
        offset += struct.calcsize(kind)
    raw[252:] = zlib.crc32(raw[:252]).to_bytes(4, "big")
    return bytes(raw)


class NativePacketTests(unittest.TestCase):
    def test_inherited_mapping_and_mode_are_not_claimed_as_queried(self):
        decoded = parse_packet(packet(), 0x52544303)
        self.assertTrue(decoded["services_preserved_by_probe"])
        self.assertEqual(decoded["mapping_name"], "inherited/unqueried")
        self.assertEqual(decoded["mode_name"], "expected RDO (unqueried)")

    def test_configured_off_is_distinct_from_an_inherited_mapping(self):
        decoded = parse_packet(packet(stop_result=0, cfg_result=0, start_result=0), 0x52544303)
        self.assertFalse(decoded["services_preserved_by_probe"])
        self.assertEqual(decoded["mapping_name"], "OFF")
        self.assertEqual(decoded["mode_name"], "RDO")

    def test_high_fixture_requires_known_crc_and_complete_comparison(self):
        fields = dict(stop_result=0, cfg_result=0, start_result=0,
                      fixture_offset=0x280000, fixture_expected_crc32=0xEF107C8F,
                      fixture_pre_crc32=0xEF107C8F, fixture_start_crc32=0xEF107C8F,
                      match_pre=1, match_start=1)
        self.assertTrue(parse_packet(packet(**fields), 0x52544303)["high_rom_reads_verified"])
        for change in ({"fixture_start_crc32": 0}, {"match_start": 0},
                       {"fixture_offset": 0xF0000}, {"fixture_expected_crc32": 0}):
            self.assertFalse(parse_packet(packet(**(fields | change)), 0x52544303)["high_rom_reads_verified"])

    def test_wrong_nonce_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "nonce"):
            parse_packet(packet(), 0x52544301)

    def test_modified_packet_is_rejected(self):
        raw = bytearray(packet())
        raw[84] ^= 1
        with self.assertRaisesRegex(ValueError, "CRC"):
            parse_packet(bytes(raw), 0x52544303)


if __name__ == "__main__":
    unittest.main()
