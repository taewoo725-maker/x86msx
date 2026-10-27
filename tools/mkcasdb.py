#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
"""mkcasdb.py - 3.3.2 MED-08: blueMSX msxcaswavdb.xml → X86MSX 카세트 DB 이진 파일 (Databases\\x86msx_casdb.bin)

머리 "X86CASDB" + 항목 수(4바이트) + 항목: SHA-1 20바이트(.cas) + 제목 길이 1 + 제목(UTF-8) + 시작 명령 길이 1 + 시작 명령.
쓰기: mkcasdb.py <msxcaswavdb.xml> <출력 .bin>
"""
import html, re, struct, sys

def main():
    text = open(sys.argv[1], encoding='utf-8', errors='replace').read()
    out = bytearray()
    n = 0
    seen = set()
    for sw in re.finditer(r'<software>(.*?)</software>', text, re.S):
        body = sw.group(1)
        t = re.search(r'<title[^>]*>(.*?)</title>', body, re.S)
        title = html.unescape(t.group(1).strip()) if t else ''
        y = re.search(r'<year>(.*?)</year>', body)
        c = re.search(r'<company>(.*?)</company>', body)
        extra = ', '.join(x for x in (html.unescape(c.group(1)).strip() if c else '', y.group(1).strip() if y else '') if x)
        if extra:
            title += ' (' + extra + ')'
        for d in re.finditer(r'<dump>(.*?)</dump>', body, re.S):
            st = re.search(r'<start>\s*<text>(.*?)</text>', d.group(1), re.S)
            start = html.unescape(st.group(1).strip()) if st else ''
            for h in re.finditer(r'<format type="cas">\s*<hash algo="sha1">([0-9a-fA-F]{40})</hash>', d.group(1)):
                k = bytes.fromhex(h.group(1))
                if k in seen:
                    continue
                seen.add(k)
                tb = title.encode('utf-8')[:120]
                sb = start.encode('utf-8')[:60]
                out += k + bytes([len(tb)]) + tb + bytes([len(sb)]) + sb
                n += 1
    open(sys.argv[2], 'wb').write(b'X86CASDB' + struct.pack('<I', n) + out)
    print('mkcasdb: %d tapes, %d bytes' % (n, len(out) + 12))

if __name__ == '__main__':
    main()
