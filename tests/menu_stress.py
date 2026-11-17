#!/usr/bin/env python3
"""Run explicit NICE64 scripts and retain HDMI stills and optional check results.

Power, deployment, starting menu state, and check criteria belong to the caller.
Scripts run from their original absolute paths so nested relative ``run`` paths
keep working; copies and hashes retain the top-level input used for each case.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys
import time


def timestamp():
    return datetime.now(timezone.utc).isoformat()


def positive_integer(value):
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("must be at least 1")
    return number


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="verified NICE64 serial port")
    parser.add_argument("--output", required=True, type=Path,
                        help="new or empty evidence directory")
    parser.add_argument("--capture-prefix", required=True,
                        help="exact detector-provided ffmpeg command prefix; parsed without a shell")
    parser.add_argument("--check-command",
                        help="optional command prefix to check each capture; image path appended, parsed without a shell")
    parser.add_argument("--repeat", type=positive_integer, default=1)
    parser.add_argument("--program", type=Path, action="append", default=[],
                        help="controller script; repeat this option for multiple scripts")
    parser.add_argument("--case", action="append", default=[], metavar="NAME=PATH",
                        help="named controller script; repeat for an ordered case sequence")
    parser.add_argument("--once", metavar="LABEL", help="label for the --inputs script")
    parser.add_argument("--inputs", help="literal controller program for a one-off checkpoint")
    parser.add_argument("--expected", help="expected visual outcome, retained without evaluation")
    parser.add_argument("--timeout", type=positive_integer, default=300,
                        help="maximum seconds for each controller, capture, or check subprocess (default: 300)")
    args = parser.parse_args()

    try:
        capture_prefix = shlex.split(args.capture_prefix)
    except ValueError as error:
        parser.error(f"invalid --capture-prefix: {error}")
    if not capture_prefix:
        parser.error("--capture-prefix must contain an executable and input options")
    check_command = None
    if args.check_command is not None:
        try:
            check_command = shlex.split(args.check_command)
        except ValueError as error:
            parser.error(f"invalid --check-command: {error}")
        if not check_command:
            parser.error("--check-command must contain an executable")
    if (args.once is None) != (args.inputs is None):
        parser.error("--once and --inputs must be supplied together")
    if args.once is not None and args.repeat != 1:
        parser.error("--once requires --repeat 1")

    cases = [(path.stem, path) for path in args.program]
    for value in args.case:
        name, separator, path = value.partition("=")
        if not separator or not name.strip() or not path:
            parser.error("each --case must be NAME=PATH with a nonempty name and path")
        cases.append((name, Path(path)))
    if args.once is not None:
        if not args.once.strip() or not args.inputs.strip():
            parser.error("--once and --inputs must not be blank")
        cases.append((args.once, None))
    if not cases:
        parser.error("supply --program, --case, or --once with --inputs")
    for _, path in cases:
        if path is not None and not path.expanduser().resolve().is_file():
            parser.error(f"controller script does not exist: {path}")

    output = args.output.expanduser().resolve()
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error(f"--output must be new or empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    (output / "scripts").mkdir()
    manifest = {
        "started_at": timestamp(), "argv": sys.argv,
        "working_directory": str(Path.cwd()), "port": args.port,
        "capture_prefix_argv": capture_prefix, "repeat": args.repeat,
        "check_command_argv": check_command,
        "expected": args.expected, "visual_verdict": "not evaluated by recorder",
        "check_verdict": "pending" if check_command else "not configured",
        "cases": [], "runs": [], "status": "preparing",
    }

    def save():
        (output / "manifest.json").write_text(
            json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    def execute(argv, folder, label):
        record = {"argv": argv, "started_at": timestamp()}
        started = time.monotonic()
        stdout, stderr = "", ""
        try:
            result = subprocess.run(argv, capture_output=True, text=True,
                                    errors="replace", timeout=args.timeout, check=False)
            stdout, stderr = result.stdout, result.stderr
            record["returncode"] = result.returncode
        except subprocess.TimeoutExpired as error:
            stdout = (error.stdout or b"")
            stderr = (error.stderr or b"")
            stdout = stdout.decode(errors="replace") if isinstance(stdout, bytes) else stdout
            stderr = stderr.decode(errors="replace") if isinstance(stderr, bytes) else stderr
            record.update(returncode=None, error=f"timeout after {args.timeout} seconds")
        except OSError as error:
            record.update(returncode=None, error=str(error))
        record.update(finished_at=timestamp(), elapsed_seconds=time.monotonic() - started,
                      stdout=stdout, stderr=stderr)
        (folder / f"{label}.stdout").write_text(stdout, encoding="utf-8")
        (folder / f"{label}.stderr").write_text(stderr, encoding="utf-8")
        if stdout:
            print(stdout, end="" if stdout.endswith("\n") else "\n", flush=True)
        if stderr:
            print(stderr, end="" if stderr.endswith("\n") else "\n", file=sys.stderr, flush=True)
        record["printed_error"] = bool(re.search(r"(?im)^\s*\ufeff?(?:\[[^\]\n]+\]\s*)?ERROR\b", stdout + "\n" + stderr))
        (folder / f"{label}.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
        return record

    try:
        for index, (name, source) in enumerate(cases, 1):
            copy = output / "scripts" / f"{index:02d}.n64script"
            if source is None:
                copy.write_text(args.inputs + "\n", encoding="utf-8")
                source = copy
            else:
                source = source.expanduser().resolve()
                copy.write_bytes(source.read_bytes())
            manifest["cases"].append({
                "name": name, "source": str(source), "copy": str(copy),
                "sha256": hashlib.sha256(copy.read_bytes()).hexdigest(),
            })
        manifest["status"] = "running"
        save()
        for repetition in range(1, args.repeat + 1):
            for index, case in enumerate(manifest["cases"], 1):
                slug = re.sub(r"[^A-Za-z0-9._-]+", "-", case["name"]).strip(".-")[:70] or "case"
                folder = output / f"{repetition:04d}-{index:02d}-{slug}"
                folder.mkdir()
                run = {"repetition": repetition, "case": case["name"],
                       "directory": str(folder), "visual_verdict": "not evaluated by recorder"}
                manifest["runs"].append(run)
                # Refuse to run a source modified after its evidence copy was made.
                if hashlib.sha256(Path(case["source"]).read_bytes()).hexdigest() != case["sha256"]:
                    run["error"] = "controller script changed after preparation"
                    manifest["status"] = "failed"
                    save()
                    return 1
                print(f"[{repetition}/{args.repeat}] {case['name']}", flush=True)
                run["controller"] = execute([
                    str(Path.home() / "bin/n64-controller"), "--port", args.port,
                    "run", case["source"],
                ], folder, "controller")
                controller_failed = (run["controller"]["returncode"] != 0
                                     or run["controller"]["printed_error"])
                save()
                if controller_failed:
                    manifest["status"] = "failed"
                    save()
                    return 1
                capture_path = folder / "capture.png"
                run["capture"] = execute(capture_prefix + [
                    "-an", "-frames:v", "1", "-update", "1", str(capture_path),
                ], folder, "capture")
                run["capture"]["path"] = str(capture_path)
                if (run["capture"]["returncode"] != 0 or run["capture"]["printed_error"]
                        or not capture_path.is_file() or capture_path.stat().st_size == 0):
                    manifest["status"] = "failed"
                    save()
                    return 1
                save()
                if check_command:
                    run["check"] = execute(check_command + [str(capture_path)], folder, "check")
                    if run["check"]["returncode"] != 0 or run["check"]["printed_error"]:
                        run["check_verdict"] = "failed"
                        manifest["check_verdict"] = "failed"
                        run["error"] = "capture check command failed"
                        manifest["status"] = "failed"
                        save()
                        return 1
                    run["check_verdict"] = "passed"
                    save()
        if check_command:
            manifest["check_verdict"] = "passed"
            manifest["status"] = "completed; capture checks passed"
        else:
            manifest["status"] = "completed; screen review required"
        return 0
    except KeyboardInterrupt:
        manifest["status"] = "interrupted"
        return 130
    except OSError as error:
        manifest.update(status="failed", error=str(error))
        print(str(error), file=sys.stderr)
        return 1
    finally:
        manifest["finished_at"] = timestamp()
        save()


if __name__ == "__main__":
    raise SystemExit(main())
