#!/usr/bin/env python3
"""Compile actual ticker/SDK layout code on the host; no copied implementation.

Extract only CPU layout functions, omitting MIPS/GPU includes and submission.
The C fixture uses the SDK's existing built-in font metrics. No hardware is used.
"""
import argparse
from pathlib import Path
import re
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def function(path, name):
    text = path.read_text()
    match = re.search(r"(?m)^[^\n;]*\b" + re.escape(name) + r"\s*\([^;]*?\)\s*\{", text)
    if not match:
        raise ValueError(f"function {name} not found in {path}")
    start = match.start()
    end, depth = match.end(), 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return f'\n#line {text.count(chr(10), 0, start) + 1} "{path}"\n' + text[start:end] + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/tests/file-list-ticker")
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    sdk = ROOT / "libdragon"
    paragraph = sdk / "src/rdpq/rdpq_paragraph.c"
    text = paragraph.read_text()
    start = text.index("static void __rdpq_paragraph_builder_newline")
    end = text.index("void rdpq_paragraph_render(", start)
    # SDK diagnostics assume its 32-bit ABI; keep host-only warnings scoped here.
    generated = '#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored "-Wsign-compare"\n#pragma GCC diagnostic ignored "-Wformat"\n'
    generated += function(sdk / "src/utils.c", "__utf8_decode")
    for name in ("__rdpq_font_glyph", "__rdpq_font_kerning", "rdpq_font_get_glyph_metrics"):
        generated += function(sdk / "src/rdpq/rdpq_font.c", name)
    generated += f'\n#line {text.count(chr(10), 0, start) + 1} "{paragraph}"\n' + text[start:end]
    generated += function(paragraph, "rdpq_paragraph_free")
    generated += "\n#pragma GCC diagnostic pop\n"
    ticker = ROOT / "src/menu/ui_components/file_list.c"
    text = ticker.read_text()
    for name in ("FILE_ENTRY_TICKER_CHUNK_WIDTH", "FILE_ENTRY_TICKER_CHUNK_BYTES"):
        generated += re.search(r"(?m)^#define " + name + r"[^\n]*", text).group() + "\n"
    for name in ("file_list_ticker_widths_valid", "file_list_ticker_name_width"):
        generated += re.search(r"(?m)^static [^\n;]*\b" + name + r"[^\n]*;", text).group() + "\n"
    start = text.index("typedef struct {")
    end = text.index("static rdpq_paragraph_t *file_list_layout_buffer;", start)
    generated += f'\n#line {text.count(chr(10), 0, start) + 1} "{ticker}"\n' + text[start:end]
    (build / "file_list_ticker_under_test.inc").write_text(generated)
    binary = build / "file_list_ticker_test"
    flags = ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-g", "-O1"]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(shlex.split(args.cc) + flags + [
        "-I" + str(build), "-I" + str(ROOT / "src"), "-I" + str(sdk / "include"),
        "-I" + str(sdk / "src/rdpq"), str(ROOT / "tests/file_list_ticker_test.c"),
        "-lm", "-o", str(binary)], check=True)
    control = subprocess.run([str(binary), "--old-overflow"], capture_output=True, text=True)
    if control.returncode != 7 or "Paragraph width" not in control.stderr:
        raise RuntimeError(f"old overflow positive control failed: {control.returncode}: {control.stderr}")
    print("PASS: former unbounded paragraph reproduces the 2048-pixel assertion", flush=True)
    subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
