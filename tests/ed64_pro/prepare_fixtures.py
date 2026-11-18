#!/usr/bin/env python3
"""Create ED64 Pro hardware-test data, or check the raw-write scratch result."""

import argparse
from pathlib import Path
import zlib

MIB = 1024 * 1024
PATTERN = bytes((i * 37 + (i >> 8) * 11 + 0x6D) & 255 for i in range(65536))


def write_fixture(path, size, *, rom=False, byteswap=False):
    with path.open("wb") as output:
        for offset in range(0, size, len(PATTERN)):
            block = bytearray(PATTERN[:min(len(PATTERN), size - offset)])
            if rom and offset == 0:
                block[:4] = bytes.fromhex("80371240")
            if byteswap:
                block[::2], block[1::2] = block[1::2], block[::2]
            output.write(block)
    print(f"{path}: {size} bytes")


def check_scratch(path):
    expected = bytes(0xA5 if (i // 512) & 1 else (i * 29 + 0x3C) & 255
                     for i in range(65536))
    actual = path.read_bytes()
    if actual != expected:
        raise SystemExit(f"FAIL: {path} does not match the raw-write result")
    print(f"PASS: {path} matches all 65536 bytes")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="fixture directory (ed64pro-test on the SD card)")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--extended", action="store_true", help="also create timing and oversize fixtures")
    mode.add_argument("--check-scratch", action="store_true", help="verify scratch.bin after the raw-write test")
    args = parser.parse_args()
    directory = args.directory
    if args.check_scratch:
        check_scratch(directory / "scratch.bin")
        return

    directory.mkdir(parents=True, exist_ok=True)
    write_fixture(directory / "read.bin", MIB)
    print(f"read.bin CRC32: {zlib.crc32(PATTERN * 16):08X}")
    (directory / "scratch.bin").write_bytes(bytes([0xA5]) * 65536)
    write_fixture(directory / "load.z64", 8 * MIB, rom=True)
    write_fixture(directory / "load.v64", 8 * MIB, rom=True, byteswap=True)
    write_fixture(directory / "load-offset.bin", MIB + 512)
    if args.extended:
        for size in (32, 64):
            write_fixture(directory / f"load-{size}m.z64", size * MIB, rom=True)
        # Larger than the backend's maximum 240 MiB capacity. Only its size
        # matters: the load test must reject it before transferring any data.
        with (directory / "oversize.z64").open("wb") as output:
            output.write(bytes.fromhex("80371240"))
            output.truncate(241 * MIB)
        print(f"{directory / 'oversize.z64'}: {241 * MIB} bytes")


if __name__ == "__main__":
    main()
