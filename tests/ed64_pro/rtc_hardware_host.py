#!/usr/bin/env python3
"""Bounded RTC evidence/recovery while the verified Pro's console is off.

This deliberately does not discover ports or manage console power. The caller
must verify the selected hardware and ownership before passing --console-off.
With the console off, first use ~/bin/edlink .link --port VERIFIED devinf
to select USB app mode. A raw GET can time out if the MCU retains N64 mode.
Calendar values use UTC, matching the production backend's POSIX conversion.
No failed or ambiguous SET is retried.

Example (after live device/power verification):
  python3 tests/ed64_pro/rtc_hardware_host.py baseline --port /dev/cu.VERIFIED \
      --console-off --baseline output/rtc-run/baseline.json
Use the same baseline with read or restore and a live verified port. The tty
path may change after USB power cycling; the recorded baseline path is evidence,
not device identity. The caller remains responsible for verifying the Pro.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import select
import socket
import subprocess
import sys
import termios
import time

FRAME = bytes.fromhex("2bd4837c")
SCHEMA = "ed64-pro-rtc-baseline-v1"


def decode(raw: bytes) -> dict:
    """Decode the exact MCU calendar; reject bad BCD/date/weekday coherence."""
    result = {"raw_hex": raw.hex(), "valid": False}
    try:
        if len(raw) != 8:
            raise ValueError("RTC response must contain exactly eight bytes")
        fields = []
        for value in raw[:7]:
            if value >> 4 > 9 or value & 15 > 9:
                raise ValueError("invalid BCD digit")
            fields.append((value >> 4) * 10 + (value & 15))
        year, month, day, hour, minute, second, weekday = fields
        stamp = datetime(2000 + year, month, day, hour, minute, second,
                         tzinfo=timezone.utc)
        if weekday != (stamp.weekday() + 1) % 7 + 1:
            raise ValueError("weekday contradicts calendar (Sunday must be 1)")
        result.update(valid=True, timestamp=int(stamp.timestamp()),
                      datetime_utc=stamp.isoformat(), weekday=weekday,
                      reserved_byte=raw[7])
    except ValueError as error:
        result["error"] = str(error)
    return result


def encode(timestamp: int) -> bytes:
    stamp = datetime.fromtimestamp(timestamp, timezone.utc)
    if not 2000 <= stamp.year <= 2099:
        raise ValueError("RTC timestamp must be in 2000 through 2099")
    fields = (stamp.year - 2000, stamp.month, stamp.day, stamp.hour,
              stamp.minute, stamp.second, (stamp.weekday() + 1) % 7 + 1)
    return bytes(((value // 10) << 4) | value % 10 for value in fields) + b"\0"


def boot_identity() -> dict:
    boot_file = Path("/proc/sys/kernel/random/boot_id")
    if boot_file.exists():
        boot = boot_file.read_text().strip()
    elif sys.platform == "darwin":
        boot = subprocess.check_output(
            ["/usr/sbin/sysctl", "-n", "kern.bootsessionuuid"],
            text=True, timeout=3).strip()
    else:
        raise RuntimeError("cannot establish host boot identity for monotonic restoration")
    if not boot:
        raise RuntimeError("empty host boot identity")
    return {"host": socket.gethostname(), "boot_id": boot}


class RTCLink:
    """Exclusive raw tty with one bounded deadline per complete operation."""
    def __init__(self, port: str, timeout: float):
        self.port, self.timeout = port, timeout
        self.fd = None
        self.settings = None

    def __enter__(self):
        self.fd = os.open(self.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        try:
            fcntl.flock(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            if hasattr(termios, "TIOCEXCL"):
                fcntl.ioctl(self.fd, termios.TIOCEXCL)
            self.settings = termios.tcgetattr(self.fd)
            settings = termios.tcgetattr(self.fd)
            settings[0] = 0
            settings[1] = 0
            settings[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            settings[3] = 0
            # Darwin accepts a literal speed_t; Linux exposes B921600.
            settings[4] = settings[5] = getattr(termios, "B921600", 921600)
            settings[6][termios.VMIN] = 0
            settings[6][termios.VTIME] = 0
            termios.tcsetattr(self.fd, termios.TCSANOW, settings)
            if select.select([self.fd], [], [], 0)[0]:
                pending = os.read(self.fd, 4096)
                raise RuntimeError(f"unsolicited serial bytes before RTC command: {pending.hex()}")
            return self
        except BaseException:
            self.__exit__(None, None, None)
            raise

    def __exit__(self, *_):
        if self.fd is not None:
            try:
                if self.settings is not None:
                    termios.tcsetattr(self.fd, termios.TCSANOW, self.settings)
            finally:
                if hasattr(termios, "TIOCNXCL"):
                    try:
                        fcntl.ioctl(self.fd, termios.TIOCNXCL)
                    except OSError:
                        pass
                os.close(self.fd)
                self.fd = None

    def _ready(self, deadline: float, *, writing: bool):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("RTC serial operation exceeded its deadline")
        ready = select.select([] if writing else [self.fd],
                              [self.fd] if writing else [], [], remaining)
        if not ready[1 if writing else 0]:
            raise TimeoutError("RTC serial operation exceeded its deadline")

    def _write(self, data: bytes, deadline: float):
        while data:
            self._ready(deadline, writing=True)
            try:
                count = os.write(self.fd, data)
            except BlockingIOError:
                continue
            if not count:
                raise RuntimeError("serial write made no progress")
            data = data[count:]

    def _read(self, count: int, deadline: float) -> bytes:
        data = bytearray()
        while len(data) < count:
            try:
                self._ready(deadline, writing=False)
            except TimeoutError as error:
                raise TimeoutError(f"{error}; received {len(data)}/{count} bytes: {data.hex()}") from error
            try:
                chunk = os.read(self.fd, count - len(data))
            except BlockingIOError:
                continue
            if not chunk:
                raise RuntimeError("serial port closed during RTC response")
            data.extend(chunk)
        return bytes(data)

    def get(self, *, deadline: float | None = None) -> dict:
        start = time.monotonic_ns()
        wall = time.time_ns()
        if deadline is None:
            deadline = time.monotonic() + self.timeout
        self._write(FRAME + b"\x10", deadline)
        raw = self._read(8, deadline)
        end = time.monotonic_ns()
        return {"monotonic_start_ns": start, "monotonic_end_ns": end,
                "wall_start_ns": wall, "wall_end_ns": time.time_ns(),
                "calendar": decode(raw)}

    def set_and_readback(self, timestamp: int) -> dict:
        raw = encode(timestamp)
        start = time.monotonic_ns()
        deadline = time.monotonic() + self.timeout
        self._write(FRAME + b"\x11" + raw, deadline)
        readback = self.get(deadline=deadline)
        elapsed = (readback["monotonic_end_ns"] - start) / 1e9
        calendar = readback["calendar"]
        verified = (calendar["valid"] and timestamp <= calendar["timestamp"]
                    <= timestamp + math.floor(elapsed) + 1)
        return {"target_timestamp": timestamp, "written_raw_hex": raw.hex(),
                "monotonic_start_ns": start, "readback": readback,
                "tolerance_seconds": math.floor(elapsed) + 1,
                "verified": verified}


def baseline_status(reads: list[dict]) -> str:
    if len(reads) != 2:
        raise ValueError("baseline must contain exactly two reads")
    calendars = [decode(bytes.fromhex(read["calendar"]["raw_hex"])) for read in reads]
    if not any(calendar["valid"] for calendar in calendars):
        return "invalid_calendar"
    if not all(calendar["valid"] for calendar in calendars):
        return "inconsistent_calendar"
    first, second = reads
    minimum = (second["monotonic_start_ns"] - first["monotonic_end_ns"]) / 1e9
    maximum = (second["monotonic_end_ns"] - first["monotonic_start_ns"]) / 1e9
    delta = calendars[1]["timestamp"] - calendars[0]["timestamp"]
    if minimum < 1 or maximum < minimum:
        return "invalid_observation_timing"
    return "valid_advancing" if max(1, math.floor(minimum)) <= delta <= math.ceil(maximum) else "not_advancing"


def load_baseline(path: Path, identity: dict) -> tuple[dict, str]:
    source = path.read_bytes()
    baseline = json.loads(source)
    if baseline.get("schema") != SCHEMA or baseline.get("identity") != identity:
        raise ValueError("baseline schema or host boot identity differs; monotonic trajectory is unavailable")
    status = baseline_status(baseline["reads"])
    if baseline.get("status") != status:
        raise ValueError("baseline status contradicts recorded observations")
    return baseline, hashlib.sha256(source).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("baseline", "read", "restore"))
    parser.add_argument("--port", required=True, help="live verified EverDrive Pro tty; never auto-discovered")
    parser.add_argument("--console-off", action="store_true", required=True,
                        help="caller has verified the N64 console is off")
    parser.add_argument("--baseline", required=True, type=Path,
                        help="new baseline output for baseline; original baseline input otherwise")
    parser.add_argument("--output", type=Path, help="also save the result JSON without replacing an existing file")
    parser.add_argument("--timeout", type=float, default=3,
                        help="complete serial operation timeout, 0.1 through 10 seconds")
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or not .1 <= args.timeout <= 10:
        parser.error("--timeout must be between 0.1 and 10 seconds")
    identity = boot_identity()
    result = {"command": args.command, "port": args.port, "identity": identity,
              "console_off_asserted": True, "calendar_convention": "UTC; Sunday=1"}
    # Validate inputs/output destinations before opening hardware.
    if args.output and args.output.exists():
        parser.error("--output already exists")
    baseline = None
    if args.command == "baseline":
        if args.baseline.exists():
            parser.error("baseline already exists; preserve the original baseline")
    else:
        baseline, digest = load_baseline(args.baseline, identity)
        result.update(baseline_path=str(args.baseline), baseline_sha256=digest)
        result["baseline_port"] = baseline["port"]
    with RTCLink(args.port, args.timeout) as link:
        if args.command == "baseline":
            first = link.get()
            time.sleep(1.1)
            result.update(schema=SCHEMA, reads=[first, link.get()])
            result["status"] = baseline_status(result["reads"])
            with args.baseline.open("x") as output:
                output.write(json.dumps(result, indent=2) + "\n")
        elif args.command == "read":
            result["observation"] = link.get()
        else:
            if baseline["status"] == "valid_advancing":
                reference = baseline["reads"][1]
                midpoint = (reference["monotonic_start_ns"] + reference["monotonic_end_ns"]) // 2
                now = time.monotonic_ns()
                if now < reference["monotonic_end_ns"]:
                    raise ValueError("monotonic clock precedes baseline; refusing restoration")
                elapsed = (now - midpoint) / 1e9
                timestamp = decode(bytes.fromhex(reference["calendar"]["raw_hex"]))["timestamp"] + math.floor(elapsed)
                result.update(restoration="original advancing RTC trajectory",
                              elapsed_since_reference_seconds=elapsed,
                              trajectory_uncertainty_seconds=1 + (reference["monotonic_end_ns"] - reference["monotonic_start_ns"]) / 2e9)
            elif baseline["status"] == "invalid_calendar":
                timestamp = math.floor(time.time())
                result["restoration"] = "initial RTC was invalid; initialized to current host UTC"
            else:
                raise ValueError("baseline does not establish a valid advancing or consistently invalid RTC")
            result["write"] = link.set_and_readback(timestamp)
    rendered = json.dumps(result, indent=2) + "\n"
    if args.output:
        with args.output.open("x") as output:
            output.write(rendered)
    print(rendered, end="")
    if args.command == "restore":
        return 0 if result["write"]["verified"] else 2
    if args.command == "baseline":
        return 0 if result["status"] in ("valid_advancing", "invalid_calendar") else 2
    return 0 if result["observation"]["calendar"]["valid"] else 2


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(json.dumps({"error": str(error), "write_retry": False}), file=sys.stderr)
        raise SystemExit(1)
