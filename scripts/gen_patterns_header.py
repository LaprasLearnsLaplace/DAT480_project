#!/usr/bin/env python3
"""
Generate Project_kernels_HLS/src/patterns.h from a readable pattern list.

Input:  MINI_pattern_match_snort3_content.txt (one pattern per line, hex
        bytes wrapped in |...|, e.g. "Subject|3A 20|test")
Output: Project_kernels_HLS/src/patterns.h with make_pattern("...") entries
        using C string literals (hex rendered as \\xNN escapes).
"""
from __future__ import annotations

from pathlib import Path
import sys

MAX_LEN = 32
PATTERN_FILE = Path(__file__).resolve().parent.parent / "MINI_pattern_match_snort3_content.txt"
OUT_FILE = Path(__file__).resolve().parent.parent / "Project_kernels_HLS" / "src" / "patterns.h"


def parse_pattern_line(line: str) -> bytes:
    """Convert a line with |AA BB| hex groups into raw bytes."""
    out: list[int] = []
    i = 0
    while i < len(line):
        ch = line[i]
        if ch == "|":
            j = line.find("|", i + 1)
            if j == -1:
                raise ValueError(f"Unclosed '|' in line: {line!r}")
            hex_part = line[i + 1 : j].strip()
            if hex_part:
                for hb in hex_part.split():
                    out.append(int(hb, 16))
            i = j + 1
        else:
            out.append(ord(ch))
            i += 1
    return bytes(out)


def bytes_initializer(data: bytes) -> str:
    """Return C-style hex initializer list padded to MAX_LEN."""
    items = [f"0x{b:02x}" for b in data]
    while len(items) < MAX_LEN:
        items.append("0x00")
    return "{ " + ", ".join(items) + " }"


def tap_initializer(length: int) -> str:
    taps = [str(length - 1 - i) if i < length else "0" for i in range(MAX_LEN)]
    return "{ " + ", ".join(taps) + " }"


def main() -> int:
    lines = [ln.rstrip("\n") for ln in PATTERN_FILE.read_text(encoding="utf-8").splitlines()]
    patterns = [parse_pattern_line(ln) for ln in lines]

    for idx, p in enumerate(patterns, start=1):
        if len(p) > MAX_LEN:
            raise ValueError(f"Pattern {idx} longer than {MAX_LEN} bytes: {len(p)}")

    header = []
    header.append("#ifndef PATTERNS_H")
    header.append("#define PATTERNS_H")
    header.append("")
    header.append("#include <ap_int.h>")
    header.append("")
    header.append(f"#define NUM_PATTERNS {len(patterns)}")
    header.append(f"#define PATTERN_MAX_LEN {MAX_LEN}")
    header.append("")
    header.append("typedef struct {")
    header.append("    unsigned char data[PATTERN_MAX_LEN];")
    header.append("    unsigned char tap_idx[PATTERN_MAX_LEN];")
    header.append("    int len;")
    header.append("} Pattern;")
    header.append("")
    header.append("static const Pattern rules[NUM_PATTERNS] = {")

    for idx, p in enumerate(patterns):
        header.append("    {")
        header.append(f"        {bytes_initializer(p)},")
        header.append(f"        {tap_initializer(len(p))},")
        header.append(f"        {len(p)}")
        header.append(f"    }},  // {idx + 1}: {lines[idx]}")

    header.append("};")
    header.append("")
    header.append("#endif // PATTERNS_H")
    header.append("")

    OUT_FILE.write_text("\n".join(header), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
