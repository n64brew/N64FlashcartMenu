#!/usr/bin/env python3
"""Decode real-source production-menu RTC/FatFs observations from HDMI stills."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import struct
import zlib

from PIL import Image

from rtc_hardware_host import decode as decode_calendar

FIELDS = [
    ("source", "i"), ("available", "i"), ("raw_result", "i"), ("raw", "8s"),
    ("time", "q"), ("ticks", "Q"), ("storage_state", "I"),
    ("workload_result", "i"), ("cycles", "I"), ("file_size", "I"), ("file_crc32", "I"),
    ("file_mtime", "q"), ("file_clock", "q"), ("rtc_reads", "I"),
    ("directory_found", "I"), ("failure_stage", "I"), ("asset_crc32", "I"),
    ("asset_rom_end", "I"), ("asset_high_bytes", "I"), ("asset_count", "I"),
]


def expected_contents():
    return bytes((position * 37 + (position >> 8) * 11 + 0x6D) & 255
                 for position in range(32768))


def parse_packet(raw, nonce):
    if len(raw) != 112 or raw[:4] != b"RTM1":
        raise ValueError("RTC menu packet magic/length mismatch")
    if int.from_bytes(raw[4:8], "big") != nonce:
        raise ValueError("RTC menu packet nonce mismatch")
    if zlib.crc32(raw[:108]) != int.from_bytes(raw[108:], "big"):
        raise ValueError("RTC menu packet CRC mismatch")
    result = {"nonce": nonce, "packet_hex": raw.hex(), "packet_crc32": raw[108:].hex()}
    offset = 8
    for name, kind in FIELDS:
        value = struct.unpack_from(">" + kind, raw, offset)[0]
        offset += struct.calcsize(kind)
        result[name] = value.hex() if isinstance(value, bytes) else value
    assert offset == 108, offset
    result["calendar"] = decode_calendar(bytes.fromhex(result["raw"]))
    for name in ("time", "file_mtime", "file_clock"):
        try:
            result[name + "_utc"] = datetime.fromtimestamp(result[name], timezone.utc).isoformat()
        except (ValueError, OverflowError, OSError):
            pass
    expected_crc = zlib.crc32(expected_contents())
    result["expected_file_crc32"] = expected_crc
    result["native_source_available"] = result["source"] == 1 and result["available"] == 1
    result["fresh_mcu_posix_agree"] = (result["raw_result"] == 0
        and result["calendar"]["valid"] and abs(result["calendar"]["timestamp"] - result["time"]) <= 2)
    result["exact_file_contents"] = (result["workload_result"] == 0 and result["cycles"] > 0
        and result["file_size"] == 32768 and result["file_crc32"] == expected_crc
        and result["directory_found"] == 1 and result["failure_stage"] == 0)
    result["high_rom_assets_read"] = (result["failure_stage"] == 0
        and result["asset_count"] == 4 and result["asset_rom_end"] > 0x100000
        and result["asset_high_bytes"] > 0)
    # FAT stores whole two-second timestamps. The workload captures time just
    # after open and closes eight 4KiB writes later. A delta beyond two seconds
    # is reported, never silently accepted as a clock/storage success.
    result["file_timestamp_delta_seconds"] = result["file_mtime"] - result["file_clock"]
    result["file_timestamp_agrees"] = (result["exact_file_contents"]
        and abs(result["file_timestamp_delta_seconds"]) <= 2)
    return result


def decode(path, nonce):
    image = Image.open(path).convert("RGB")
    magenta = [(x, y) for y in range(image.height) for x in range(image.width)
               if (lambda pixel: pixel[0] > 150 and pixel[2] > 110 and pixel[1] < 125)(image.getpixel((x, y)))]
    if not magenta:
        raise ValueError("No magenta RTC menu packet border")
    x0, x1 = min(p[0] for p in magenta), max(p[0] for p in magenta) + 1
    y0, y1 = min(p[1] for p in magenta), max(p[1] for p in magenta) + 1
    sx, sy = (x1 - x0) / 520, (y1 - y0) / 120
    bits = []
    for bit in range(112 * 8):
        x = x0 + (8 + bit % 64 * 8) * sx
        y = y0 + (8 + bit // 64 * 8) * sy
        bits.append(sum(image.getpixel((round(x), round(y)))) > 384)
    raw = bytes(sum(bits[i + j] << (7 - j) for j in range(8)) for i in range(0, len(bits), 8))
    return {"image": str(path), "bounds": [x0, y0, x1, y1], **parse_packet(raw, nonce)}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--nonce", type=lambda value: int(value, 0), default=0x52544D01)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    rendered = json.dumps(decode(args.image, args.nonce), indent=2) + "\n"
    if args.output:
        args.output.write_text(rendered)
    print(rendered, end="")
