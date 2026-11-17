"""Evidence validation must reject corrupt packets and distinguish failed checks."""
import struct
import tempfile
from pathlib import Path
import unittest
import zlib

from PIL import Image, ImageDraw

import decode_rtc_menu as decoder
from rtc_hardware_host import encode


NONCE = 0x52544D01


def packet(**changes):
    values = dict(
        source=1, available=1, raw_result=0, raw=encode(1749990896),
        time=1749990896, ticks=46875000, storage_state=3,
        workload_result=0, cycles=2, file_size=32768, file_crc32=0x77D6EB7A,
        file_mtime=1749990896, file_clock=1749990896, rtc_reads=18,
        directory_found=1, failure_stage=0, asset_crc32=0x12345678,
        asset_rom_end=0x240000, asset_high_bytes=500000, asset_count=4,
    )
    values.update(changes)
    raw = bytearray(112)
    raw[:8] = b"RTM1" + NONCE.to_bytes(4, "big")
    offset = 8
    for name, kind in decoder.FIELDS:
        struct.pack_into(">" + kind, raw, offset, values[name])
        offset += struct.calcsize(kind)
    struct.pack_into(">I", raw, 108, zlib.crc32(raw[:108]))
    return raw


class MenuEvidenceTests(unittest.TestCase):
    def test_valid_source_hardware_file_and_timestamp(self):
        result = decoder.parse_packet(packet(), NONCE)
        self.assertTrue(result["native_source_available"])
        self.assertTrue(result["fresh_mcu_posix_agree"])
        self.assertTrue(result["exact_file_contents"])
        self.assertTrue(result["file_timestamp_agrees"])
        self.assertTrue(result["high_rom_assets_read"])

    def test_small_or_failed_asset_check_is_not_high_rom_proof(self):
        for change in ({"asset_rom_end": 0x100000}, {"asset_high_bytes": 0},
                       {"asset_count": 3}, {"failure_stage": 10}):
            self.assertFalse(decoder.parse_packet(packet(**change), NONCE)["high_rom_assets_read"])

    def test_nonce_and_corruption_rejected(self):
        with self.assertRaisesRegex(ValueError, "nonce"):
            decoder.parse_packet(packet(), NONCE + 1)
        corrupt = packet()
        corrupt[80] ^= 1
        with self.assertRaisesRegex(ValueError, "CRC"):
            decoder.parse_packet(corrupt, NONCE)

    def test_plausible_posix_time_does_not_prove_hardware(self):
        for change in ({"source": 0}, {"available": 0}):
            result = decoder.parse_packet(packet(**change), NONCE)
            self.assertFalse(result["native_source_available"])
        result = decoder.parse_packet(packet(raw_result=3), NONCE)
        self.assertFalse(result["fresh_mcu_posix_agree"])

    def test_bad_contents_or_directory_refuses_file_success(self):
        for change in ({"file_crc32": 0}, {"file_size": 32767},
                       {"directory_found": 0}, {"failure_stage": 3},
                       {"workload_result": -5}):
            result = decoder.parse_packet(packet(**change), NONCE)
            self.assertFalse(result["exact_file_contents"], change)

    def test_wrong_timestamp_separate_from_contents(self):
        result = decoder.parse_packet(packet(file_mtime=1749990900), NONCE)
        self.assertTrue(result["exact_file_contents"])
        self.assertFalse(result["file_timestamp_agrees"])

    def test_scaled_capture_is_byte_exact(self):
        raw = packet()
        image = Image.new("RGB", (640, 480))
        draw = ImageDraw.Draw(image)
        draw.rectangle((60, 332, 579, 451), fill=(255, 0, 255))
        for bit in range(112 * 8):
            x, y = 64 + bit % 64 * 8, 336 + bit // 64 * 8
            value = raw[bit // 8] >> (7 - bit % 8) & 1
            draw.rectangle((x, y, x + 7, y + 7),
                           fill=(255, 255, 255) if value else (0, 0, 0))
        capture = Image.new("RGB", (1280, 720))
        capture.paste(image.resize((960, 720), Image.Resampling.NEAREST), (160, 0))
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "capture.png"
            capture.save(path)
            self.assertEqual(decoder.decode(path, NONCE)["packet_hex"], raw.hex())


if __name__ == "__main__":
    unittest.main()
