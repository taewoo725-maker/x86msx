# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# assets/DRV/rtw8821c_phy.bin 생성: 리눅스 rtw88 의 rtw8821c_table.c (MAC·BB·AGC·RF-A 표)에서 조건 분기(cut/pkg/intf/rfe)를 풀어
# 이 칩(컷 E, 패키지 0, PCIe, RFE 0)에 해당하는 줄만 뽑아 "RTWP" 이진 파일로 만든다. 형식은 8822C 용(tools/genphy_rtw8822c.py)과 같고 구역만 4개(MAC, BB, AGC, RF-A).
# 사용법: python3 tools/genphy_rtw8821c.py <rtw8821c_table.c> <출력 rtw8821c_phy.bin> [cut pkg intf rfe]
# rtw88 : SPDX GPL-2.0 OR BSD-3-Clause (BSD-3-Clause 로 사용). Copyright(c) Realtek Corporation.
import re, struct, sys
src = open(sys.argv[1], encoding='utf-8', errors='replace').read()
CUT, PKG, INTF, RFE = (int(x) for x in sys.argv[3:7]) if len(sys.argv) >= 7 else (4, 0, 1, 0)
def arr(name):
    m = re.search(r'static const u32 ' + name + r'\[\] = \{(.*?)\n\};', src, re.S)
    body = re.sub(r'/\*.*?\*/', '', m.group(1), flags=re.S)
    body = re.sub(r'//.*', '', body)
    return [int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]+', body)]
def check(c):
    cut = (c >> 24) & 15; pkg = (c >> 12) & 15; intf = (c >> 8) & 15; rfe = c & 0xFF
    if cut and cut != CUT: return False
    if pkg and pkg != PKG: return False
    if intf and intf != INTF: return False
    return rfe == RFE
def parse(a):                                    # rtw_parse_tbl_phy_cond 와 같은 규칙
    out = []; matched = True; skipped = False; pos = 0
    for i in range(0, len(a), 2):
        f, s = a[i], a[i + 1]
        if f & 0x80000000:
            br = (f >> 28) & 3
            if br == 3: matched = True; skipped = False
            elif br == 2: matched = False if skipped else True
            else: pos = f
        elif f & 0x40000000:
            if not skipped:
                if check(pos): matched = True; skipped = True
                else: matched = False; skipped = False
            else: matched = False
        elif matched: out.append((f, s))
    return out
def sec(name, kind):
    r = []
    for f, s in parse(arr(name)):
        if kind == 'mac': r.append((1 << 28 | f & 0xFFFF, s & 0xFF))
        elif kind == 'agc': r.append((f & 0xFFFF, s))
        elif kind == 'bb':
            d = {0xfe: 50000, 0xfd: 5000, 0xfc: 1000, 0xfb: 50, 0xfa: 5, 0xf9: 1}
            r.append((3 << 28, d[f]) if f in d else (f & 0xFFFF, s))
        elif kind == 'rf':
            if f == 0xffe: r.append((3 << 28, 50000))
            elif f == 0xfe: r.append((3 << 28, 100))
            else: r.append((2 << 28 | f & 0xFF, s & 0xFFFFF))
    return r
secs = [('mac', 'rtw8821c_mac'), ('bb', 'rtw8821c_bb'), ('agc', 'rtw8821c_agc'), ('rf', 'rtw8821c_rf_a')]
blob = b'RTWP\x01\x00\x00\x00' + struct.pack('<I', len(secs)); body = b''
for k, n in secs:
    r = sec(n, k); print(n, len(r), file=sys.stderr)
    blob += struct.pack('<I', len(r)); body += b''.join(struct.pack('<II', a, d) for a, d in r)
open(sys.argv[2], 'wb').write(blob + body)
print(len(blob + body), 'bytes', file=sys.stderr)
