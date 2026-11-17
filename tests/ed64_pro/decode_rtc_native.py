#!/usr/bin/env python3
"""Decode the standalone RTC probe's CRC/nonce protected HDMI observation."""
import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import struct
import zlib

from PIL import Image

FIELDS = [
    ("stage", "I"), ("error", "i"), ("id", "I"), ("mapping", "I"),
    ("config_size", "I"), ("mode", "I"),
    ("fixture_offset", "I"), ("fixture_expected_crc32", "I"),
    ("fixture_pre_crc32", "I"), ("fixture_stop_crc32", "I"), ("fixture_start_crc32", "I"),
    ("match_pre", "I"), ("match_stop", "I"), ("match_start", "I"),
    ("stop_result", "i"), ("cfg_result", "i"), ("start_result", "i"),
    ("source_pre", "i"), ("available_pre", "i"),
    ("source_stop", "i"), ("available_stop", "i"),
    ("source_start", "i"), ("available_start", "i"),
    ("native_initial_result", "i"), ("native_initial", "q"),
    ("native_set_result", "i"), ("target", "q"),
    ("native_after_result", "i"), ("native_after", "q"),
    ("raw_before_result", "i"), ("raw_before", "8s"),
    ("raw_after_result", "i"), ("raw_after", "8s"),
    ("set_ticks", "Q"), ("fresh_result", "i"), ("fresh", "q"),
    ("ticks", "Q"), ("posix_time", "q"), ("memory_size", "I"),
    ("fresh_raw", "8s"), ("fresh_raw_result", "i"),
]


def calendar(raw_hex):
    """Independently validate the MCU's BCD Gregorian calendar and weekday."""
    raw = bytes.fromhex(raw_hex)
    try:
        if any((b >> 4) > 9 or (b & 15) > 9 for b in raw[:7]):
            raise ValueError("Invalid BCD")
        year, month, day, hour, minute, second, weekday = [
            (b >> 4) * 10 + (b & 15) for b in raw[:7]
        ]
        if not 1 <= weekday <= 7:
            raise ValueError("MCU weekday outside Sunday=1 through Saturday=7")
        stamp = datetime(2000 + year, month, day, hour, minute, second, tzinfo=timezone.utc)
        expected_weekday = (stamp.weekday() + 1) % 7 + 1
        return {"valid": True, "timestamp": int(stamp.timestamp()),
                "datetime_utc": stamp.isoformat(), "weekday": weekday,
                "weekday_matches": weekday == expected_weekday,
                "expected_weekday": expected_weekday, "reserved": raw[7]}
    except ValueError as error:
        return {"valid": False, "error": str(error)}


def parse_packet(raw, nonce):
    if len(raw) != 256 or raw[:4] != b"RTP2":
        raise ValueError("RTC probe packet magic/length mismatch")
    if int.from_bytes(raw[4:8], "big") != nonce:
        raise ValueError("RTC probe packet nonce mismatch")
    if zlib.crc32(raw[:252]) != int.from_bytes(raw[252:], "big"):
        raise ValueError("RTC probe packet CRC mismatch")
    result = {"nonce": nonce, "packet_hex": raw.hex(), "crc32": raw[252:].hex()}
    offset = 8
    for name, kind in FIELDS:
        value = struct.unpack_from(">" + kind, raw, offset)[0]
        offset += struct.calcsize(kind)
        result[name] = value.hex() if isinstance(value, bytes) else value
    assert offset <= 252, offset
    # Completed preserve-services probes leave all lifecycle-command fields at
    # their unattempted sentinel. Their zero mapping is unqueried, rather than
    # an observation that DEV_ROM_OFF was selected. The mode is only the policy
    # expected from the launching menu; this protocol has no GET_CFG command.
    inherited = result["stage"] == 7 and all(
        result[name] == -100 for name in ("stop_result", "cfg_result", "start_result"))
    result["services_preserved_by_probe"] = inherited
    result["mapping_name"] = ("inherited/unqueried" if inherited else
        {0: "OFF", 1: "MENU", 2: "GPAK"}.get(result["mapping"], "unknown"))
    mode_name = {0: "OFF", 1: "STD", 2: "RDO"}.get(result["mode"], "unknown")
    result["mode_name"] = f"expected {mode_name} (unqueried)" if inherited else mode_name
    for name in ("native_initial", "native_after", "target", "fresh", "posix_time"):
        try:
            result[name + "_utc"] = datetime.fromtimestamp(result[name], timezone.utc).isoformat()
        except (ValueError, OverflowError, OSError):
            pass
    for name in ("raw_before", "raw_after", "fresh_raw"):
        result[name + "_calendar"] = calendar(result[name])
    fixture = bytes((i * 37 + (i >> 8) * 11 + 0x6D) & 255 for i in range(1024))
    expected_crc = zlib.crc32(fixture)
    result["high_rom_reads_verified"] = (
        not inherited and result["stage"] >= 5 and result["error"] == 0
        and result["fixture_offset"] == 0x280000
        and result["fixture_expected_crc32"] == expected_crc
        and result["fixture_pre_crc32"] == expected_crc
        and result["fixture_start_crc32"] == expected_crc
        and result["match_pre"] == 1 and result["match_start"] == 1)
    # CP0 ticks run at CPU/2 = 46.875 MHz on retail N64. Allow quantization at
    # both observations plus ceiling(elapsed) for the immediate set/read pair.
    tolerance = math.ceil(result["set_ticks"] / 46875000) + 1
    result["immediate_set_tolerance_seconds"] = tolerance
    after = result["raw_after_calendar"]
    result["native_set_verified"] = (
        result["stage"] == 7 and result["error"] == 0
        and result["native_set_result"] == 0 and result["native_after_result"] == 0
        and result["raw_after_result"] == 0 and after["valid"] and after["weekday_matches"]
        and 0 <= after["timestamp"] - result["target"] <= tolerance
        and abs(result["native_after"] - after["timestamp"]) <= 1
    )
    if after["valid"]:
        result["mcu_minus_target_seconds"] = after["timestamp"] - result["target"]
        result["native_minus_mcu_seconds"] = result["native_after"] - after["timestamp"]
    fresh = result["fresh_raw_calendar"]
    result["fresh_native_mcu_agree"] = (
        result["fresh_result"] == 0 and result["fresh_raw_result"] == 0
        and fresh["valid"] and fresh["weekday_matches"]
        and abs(result["fresh"] - fresh["timestamp"]) <= 1)
    return result


def decode(path, nonce):
    im = Image.open(path).convert("RGB")
    magenta = [(x, y) for y in range(im.height) for x in range(im.width)
               if (lambda p: p[0] > 150 and p[2] > 110 and p[1] < 125)(im.getpixel((x, y)))]
    if not magenta:
        raise ValueError("No magenta packet border")
    x0, x1 = min(p[0] for p in magenta), max(p[0] for p in magenta) + 1
    y0, y1 = min(p[1] for p in magenta), max(p[1] for p in magenta) + 1
    sx, sy = (x1 - x0) / 520, (y1 - y0) / 264
    bits = []
    for bit in range(256 * 8):
        x = x0 + (8 + (bit % 64) * 8) * sx
        y = y0 + (8 + (bit // 64) * 8) * sy
        bits.append(sum(im.getpixel((round(x), round(y)))) > 384)
    raw = bytes(sum(bits[i + j] << (7 - j) for j in range(8)) for i in range(0, len(bits), 8))
    return {"image": str(path), "bounds": [x0, y0, x1, y1], **parse_packet(raw, nonce)}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--nonce", type=lambda s: int(s, 0), default=0x52544311)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = json.dumps(decode(args.image, args.nonce), indent=2) + "\n"
    if args.output:
        args.output.write_text(result)
    print(result, end="")
