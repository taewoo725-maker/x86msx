#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 시험용 ARJ 만들기 (방법 0·1·4). 진짜 ARJ 가 없는 환경에서 arcx.c 의 해독을 확인하려는 것. 규격은 unarj 를 따름.
import sys, zlib, struct, heapq, os, random

class BW:
    def __init__(s): s.b = []; s.acc = 0; s.n = 0
    def put(s, v, n):
        for i in range(n - 1, -1, -1):
            s.acc = (s.acc << 1) | ((v >> i) & 1); s.n += 1
            if s.n == 8: s.b.append(s.acc); s.acc = 0; s.n = 0
    def done(s):
        while s.n: s.put(0, 1)
        return bytes(s.b)

def lz(data, maxlen=256, maxdist=26624, minlen=3):
    out = []; i = 0; head = {}
    while i < len(data):
        best = (0, 0)
        key = data[i:i+3]
        if len(key) == 3 and key in head:
            for j in reversed(head[key][-16:]):
                if i - j > maxdist: break
                l = 0
                while i + l < len(data) and l < maxlen and data[j + l] == data[i + l]: l += 1
                if l > best[0]: best = (l, i - j)
        if best[0] >= minlen:
            out.append((best[0], best[1]))
            for k in range(best[0]):
                kk = data[i+k:i+k+3]
                if len(kk) == 3: head.setdefault(kk, []).append(i + k)
            i += best[0]
        else:
            out.append(data[i]); kk = data[i:i+3]
            if len(kk) == 3: head.setdefault(kk, []).append(i)
            i += 1
    return out

def huff_lens(freq, limit=16):
    syms = [(f, s) for s, f in enumerate(freq) if f]
    lens = [0] * len(freq)
    if not syms: return lens
    if len(syms) == 1: lens[syms[0][1]] = 1; return lens
    h = [(f, i, [s]) for i, (f, s) in enumerate(syms)]; heapq.heapify(h); cnt = len(h)
    while len(h) > 1:
        a = heapq.heappop(h); b = heapq.heappop(h)
        for s in a[2] + b[2]: lens[s] += 1
        cnt += 1; heapq.heappush(h, (a[0] + b[0], cnt, a[2] + b[2]))
    while max(lens) > limit:                  # 거칠게 줄임
        freq = [min(f, 1) + (f > 0) if f else 0 for f in freq]
        return huff_lens([ (f and 1+f//2) for f in freq], limit)
    return lens

def codes(lens):
    order = sorted((l, s) for s, l in enumerate(lens) if l)
    code = 0; prev = 0; cs = {}
    for l, s in order:
        code <<= (l - prev); prev = l
        cs[s] = (code, l); code += 1
    return cs

def write_pt_len(bw, lens, nbit, special):
    n = len(lens)
    while n and lens[n-1] == 0: n -= 1
    used = [s for s in range(len(lens)) if lens[s]]
    if len(used) <= 1:
        bw.put(0, nbit); bw.put(used[0] if used else 0, nbit); return
    bw.put(n, nbit)
    i = 0
    while i < n:
        l = lens[i]
        if l < 7: bw.put(l, 3)
        else:
            bw.put(7, 3)
            for _ in range(l - 7): bw.put(1, 1)
            bw.put(0, 1)
        i += 1
        if i == special: bw.put(0, 2)

def enc_method1(data):
    toks = lz(data)
    cf = [0] * 510; pf = [0] * 17
    for t in toks:
        if isinstance(t, int): cf[t] += 1
        else:
            cf[t[0] + 253] += 1
            d = t[1] - 1; pf[0 if d == 0 else d.bit_length()] += 1
    if not toks: cf[0] = 1
    clen = huff_lens(cf); plen = huff_lens(pf)
    # c_len 을 pt(19) 기호로: 0 → 기호 0(1개), 3 이상 → 길이+2 (실제 ARJ 처럼 0 이 이어지면 묶음)
    n = len(clen)
    while n and clen[n-1] == 0: n -= 1
    syms = []; i = 0
    while i < n:
        if clen[i] == 0:
            j = i
            while j < n and clen[j] == 0: j += 1
            run = j - i
            while run > 0:
                if run >= 20 and run <= 20 + 511: r = run; syms.append((2, r - 20)); run = 0
                elif run >= 3 and run <= 18: syms.append((1, run - 3)); run = 0
                else: syms.append((0, 0)); run -= 1
            i = j
        else: syms.append((clen[i] + 2, 0)); i += 1
    tf = [0] * 19
    for s, _ in syms: tf[s] += 1
    tlen = huff_lens(tf)
    bw = BW()
    bw.put(len(toks) if len(toks) < 65536 else 65535, 16) if False else None
    # 한 블록에 모두 담기 (기호 수 < 65536 으로 제한)
    assert len(toks) < 65536
    bw.put(len(toks), 16)
    write_pt_len(bw, tlen, 5, 3)
    tc = codes(tlen)
    # c_len
    if sum(1 for l in clen if l) <= 1:
        bw.put(0, 9); bw.put(max(range(510), key=lambda s: clen[s]) if any(clen) else 0, 9)
    else:
        bw.put(n, 9)
        for s, extra in syms:
            c, l = tc[s] if len(used_syms(tlen)) > 1 else (0, 0)
            bw.put(c, l)
            if s == 1: bw.put(extra, 4)
            elif s == 2: bw.put(extra, 9)
    write_pt_len(bw, plen, 5, -1)
    cc = codes(clen) if sum(1 for l in clen if l) > 1 else {}
    pc = codes(plen) if sum(1 for l in plen if l) > 1 else {}
    for t in toks:
        if isinstance(t, int):
            c, l = cc.get(t, (0, 0)); bw.put(c, l)
        else:
            c, l = cc.get(t[0] + 253, (0, 0)); bw.put(c, l)
            d = t[1] - 1; k = 0 if d == 0 else d.bit_length()
            c, l = pc.get(k, (0, 0)); bw.put(c, l)
            if k: bw.put(d - (1 << (k - 1)), k - 1)
    return bw.done()

def used_syms(l): return [s for s, x in enumerate(l) if x]

def enc_method4(data):
    toks = lz(data, maxlen=256, maxdist=26624, minlen=3)
    bw = BW()
    def put_len(c):                       # decode_len 의 거꾸로
        plus = 0; pwr = 1; width = 0
        while width < 7:
            if c >= plus + pwr: bw.put(1, 1); plus += pwr; pwr <<= 1; width += 1
            else: bw.put(0, 1); break
        if width: bw.put(c - plus, width)
    def put_ptr(c):
        plus = 0; pwr = 1 << 9; width = 9
        while width < 13:
            if c >= plus + pwr: bw.put(1, 1); plus += pwr; pwr <<= 1; width += 1
            else: bw.put(0, 1); break
        bw.put(c - plus, width)
    for t in toks:
        if isinstance(t, int): put_len(0); bw.put(t, 8)
        else:
            l, d = t
            if l > 3 + 253: l = 3 + 253
            put_len(l - 3 + 1); put_ptr(d - 1)
    return bw.done()

def make_arj(files, method):
    out = bytearray()
    def hdr(ftype, name, csize=0, usize=0, crc=0, meth=0, flags=0, dos=0x4D2B2800):
        body = struct.pack('<BBBBBBBB', 30, 11, 1, 0, flags, meth, ftype, 0) + struct.pack('<IIIIHHH', dos, csize, usize, crc, 0, 0, 0)[0:0]
        body += struct.pack('<IIII', dos, csize, usize, crc) + struct.pack('<HHH', 0, 0o644, 0)
        body = body[:0] + struct.pack('<BBBBBBBB', 30, 11, 1, 0, flags, meth, ftype, 0) + struct.pack('<IIII', dos, csize, usize, crc) + struct.pack('<HHH', 0, 0o644, 0)
        body += name.encode() + b'\0' + b'\0'
        return b'\x60\xea' + struct.pack('<H', len(body)) + body + struct.pack('<I', zlib.crc32(body)) + b'\0\0'
    out += hdr(2, 'test.arj')
    for name, data in files:
        m = method
        if m == 0 or not data: comp = data; mm = 0
        elif m == 4: comp = enc_method4(data); mm = 4
        else: comp = enc_method1(data); mm = m
        out += hdr(0, name, len(comp), len(data), zlib.crc32(data), mm) + comp
    out += b'\x60\xea\0\0'
    return bytes(out)

if __name__ == '__main__':
    d = sys.argv[1]; os.makedirs(d, exist_ok=True)
    random.seed(7)
    text = (open('/home/claude/msxemu8/README.md', 'rb').read()[:30000] or b'hello ' * 100)
    rnd = bytes(random.randrange(256) for _ in range(3000))
    skew = bytes(random.choice([0]*200 + list(range(1, 40)) + [200, 201, 202]) for _ in range(6000))
    zeros = b'\0' * 5000
    one = b'AAAA'
    files = [('TEXT.TXT', text), ('RND.BIN', rnd), ('SKEW.BIN', skew), ('ZERO.BIN', zeros), ('ONE.TXT', b'A'), ('EMPTY.TXT', b'')]
    for m in (0, 1, 4):
        open(f'{d}/m{m}.arj', 'wb').write(make_arj(files, m))
    for n, dta in files:
        print(n, len(dta), hex(zlib.crc32(dta)))
