#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
"""mkkbd.py - MSX 기종 키보드 배열 표 만들기 (2.13)

openMSX의 share/unicodemaps/unicodemap.* (글자 → MSX 키 매트릭스 행·열 + 보조키)를 읽어
ASCII 0x20~0x7E 글자마다 (행, 열, 보조키) 표를 kernel/kbdmaps.c로 만든다.
출력 한 칸 = 행<<4 | 열 (0xFF = 없음), 보조키 비트 0 SHIFT, 1 GRAPH, 2 CODE.
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "third_party", "unicodemaps")
OUT = os.path.join(HERE, "..", "kernel", "kbdmaps.c")

# 순서 = keymap.h의 KBD_* 번호 (0 = 일본식은 keymap.c의 손 표를 쓰므로 표 없음)
LAYOUTS = [("int", "International"), ("fr", "French AZERTY"), ("gb", "UK"), ("de", "German DIN"),
           ("es", "Spanish"), ("se", "Swedish"), ("ru", "Russian JCUKEN"), ("ar", "Arabic"), ("pl", "Polish")]

def parse(path):
    t = {}
    for line in open(path, encoding="utf-8", errors="replace"):
        line = line.split("#", 1)[0].strip()
        if not line or ":" in line.split(",")[0] or line.startswith("DEADKEY"):
            continue
        parts = [p.strip() for p in line.split(",")]
        if len(parts) < 2 or not parts[1]:
            continue
        try:
            u = int(parts[0], 16)
        except ValueError:
            continue
        if u < 0x20 or u > 0x7E or u in t:
            continue
        rc = parts[1]
        row, col = int(rc[0], 16), int(rc[1])
        mods = parts[2].split() if len(parts) > 2 else []
        if "CTRL" in mods or "CAPSLOCK" in mods:
            continue
        m = (1 if "SHIFT" in mods else 0) | (2 if "GRAPH" in mods else 0) | (4 if "CODE" in mods else 0)
        t[u] = (row << 4 | col, m)
    return t

def main():
    out = ["/* kbdmaps.c - tools/mkkbd.py가 만든 파일 (고치지 말 것).",
           " * 자료: openMSX share/unicodemaps (GPL-2.0, third_party/unicodemaps).",
           " * [배열][글자 - 0x20] = { 행<<4 | 열 (0xFF 없음), 보조키 (1 SHIFT, 2 GRAPH, 4 CODE) } */",
           "#include <stdint.h>", "",
           "const uint8_t kbd_maps[%d][95][2] = {" % len(LAYOUTS)]
    for code, name in LAYOUTS:
        t = parse(os.path.join(SRC, "unicodemap." + code))
        out.append("    /* %s (%s) */ {" % (code, name))
        row = []
        for c in range(0x20, 0x7F):
            rc, m = t.get(c, (0xFF, 0))
            row.append("{0x%02X,%d}" % (rc, m))
        for i in range(0, len(row), 8):
            out.append("        " + ",".join(row[i:i + 8]) + ",")
        out.append("    },")
        missing = "".join(chr(c) for c in range(0x20, 0x7F) if c not in t)
        if missing:
            print("%s: 없는 글자 %r" % (code, missing), file=sys.stderr)
    out.append("};")
    open(OUT, "w").write("\n".join(out) + "\n")

if __name__ == "__main__":
    main()
