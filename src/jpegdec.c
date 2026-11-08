/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* jpegdec.c (8.0.120) - 베이스라인 JPEG(허프만) 해독기. 1·3 성분, 표본화 1x1·2x1·1x2·2x2, 재시작 표시 지원.
 * DHT 가 없으면(MJPEG 의 AVI1 틀) 표준 허프만 표를 씀. 정수 IDCT (12비트 고정소수). */
#include "jpegdec.h"
#include <string.h>

static const uint8_t zz[64] = { 0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63 };

typedef struct { uint8_t bits[17]; uint8_t val[256]; int maxcode[18]; int mincode[17]; int valptr[17]; uint16_t fast[512]; int ok; } Huff;
typedef struct { const uint8_t *p, *end; uint32_t buf; int cnt; int mk; } Br;

static Huff hdc[4], hac[4];
static uint16_t qt[4][64];
static int16_t cos_t[8][8];                       /* [u][x] = 4096 * c(u) / 2 * cos((2x+1) u pi / 16) */
static int cos_ready;

static void cos_init(void)
{
    /* 정수 표 (소수 계산 없이): 값 = round(4096 * C(u)/2 * cos((2x+1)u*pi/16)), C(0)=1/sqrt2 */
    static const int16_t t[8][8] = {
        { 1448, 1448, 1448, 1448, 1448, 1448, 1448, 1448 },
        { 2008, 1702, 1138,  399, -399,-1138,-1702,-2008 },
        { 1892,  784, -784,-1892,-1892, -784,  784, 1892 },
        { 1702, -399,-2008,-1138, 1138, 2008,  399,-1702 },
        { 1448,-1448,-1448, 1448, 1448,-1448,-1448, 1448 },
        { 1138,-2008,  399, 1702,-1702, -399, 2008,-1138 },
        {  784,-1892, 1892, -784, -784, 1892,-1892,  784 },
        {  399,-1138, 1702,-2008, 2008,-1702, 1138, -399 } };
    memcpy(cos_t, t, sizeof t); cos_ready = 1;
}

static void huff_build(Huff *h)
{
    int code = 0, k = 0;
    memset(h->fast, 0, sizeof h->fast);
    for (int l = 1; l <= 16; l++) {
        h->valptr[l] = k; h->mincode[l] = code;
        for (int i = 0; i < h->bits[l]; i++, code++, k++) {
            if (l <= 9) { int base = code << (9 - l), n = 1 << (9 - l); for (int j = 0; j < n && base + j < 512; j++) h->fast[base + j] = (uint16_t)(l << 8 | h->val[k]); }
        }
        h->maxcode[l] = h->bits[l] ? code - 1 : -1;
        code <<= 1;
    }
    h->ok = 1;
}
static void huff_std(void)                          /* 표준 표 (ITU T.81 부록 K) */
{
    static const uint8_t dcl[17] = { 0, 0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0 }, dcv[12] = { 0,1,2,3,4,5,6,7,8,9,10,11 };
    static const uint8_t dcc[17] = { 0, 0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0 };
    static const uint8_t acl[17] = { 0, 0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d }, acc[17] = { 0, 0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77 };
    static const uint8_t acv[162] = {
        0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,
        0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,
        0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
        0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,
        0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
        0xf9,0xfa };
    static const uint8_t acvc[162] = {
        0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,
        0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,
        0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
        0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,
        0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
        0xf9,0xfa };
    memset(&hdc[0], 0, sizeof hdc[0]); memcpy(hdc[0].bits, dcl, 17); memcpy(hdc[0].val, dcv, 12); huff_build(&hdc[0]);
    memset(&hdc[1], 0, sizeof hdc[1]); memcpy(hdc[1].bits, dcc, 17); memcpy(hdc[1].val, dcv, 12); huff_build(&hdc[1]);
    memset(&hac[0], 0, sizeof hac[0]); memcpy(hac[0].bits, acl, 17); memcpy(hac[0].val, acv, 162); huff_build(&hac[0]);
    memset(&hac[1], 0, sizeof hac[1]); memcpy(hac[1].bits, acc, 17); memcpy(hac[1].val, acvc, 162); huff_build(&hac[1]);
}

static void fill(Br *b)
{
    while (b->cnt <= 24) {
        uint32_t c = 0;
        if (b->p < b->end && !b->mk) {
            c = *b->p++;
            if (c == 0xFF) {
                if (b->p < b->end) { uint8_t d = *b->p; if (d == 0) b->p++; else { b->mk = 1; b->p--; c = 0; } }
                else c = 0;
            }
        }
        b->buf |= c << (24 - b->cnt); b->cnt += 8;
    }
}
static int hdec(Br *b, const Huff *h)
{
    fill(b);
    uint16_t f = h->fast[b->buf >> 23];
    if (f) { int l = f >> 8; b->buf <<= l; b->cnt -= l; return f & 255; }
    for (int l = 10; l <= 16; l++) {
        int code = (int)(b->buf >> (32 - l));
        if (h->maxcode[l] >= 0 && code <= h->maxcode[l] && code >= h->mincode[l]) { int s = h->val[h->valptr[l] + code - h->mincode[l]]; b->buf <<= l; b->cnt -= l; return s; }
    }
    return -1;
}
static int rcv(Br *b, int n) { if (!n) return 0; fill(b); int v = (int)(b->buf >> (32 - n)); b->buf <<= n; b->cnt -= n; return v; }
static int ext(int v, int n) { return v < (1 << (n - 1)) ? v - (1 << n) + 1 : v; }

static int blk(Br *b, const Huff *dc, const Huff *ac, const uint16_t *q, int *pred, int32_t *o)
{
    int t = hdec(b, dc); if (t < 0 || t > 11) return -1;
    int diff = t ? ext(rcv(b, t), t) : 0; *pred += diff; o[0] = *pred * (int)q[0];
    for (int k = 1; k < 64; ) {
        int rs = hdec(b, ac); if (rs < 0) return -1;
        int r = rs >> 4, s = rs & 15;
        if (!s) { if (r == 15) { k += 16; continue; } break; }
        k += r; if (k > 63) return -1;
        o[zz[k]] = ext(rcv(b, s), s) * (int)q[k]; k++;
    }
    return 0;
}
static uint8_t clamp(int v) { return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v; }
static void idct(const int32_t *in, uint8_t *dst, int stride)
{
    int ac = 0; for (int i = 1; i < 64; i++) if (in[i]) { ac = 1; break; }
    if (!ac) { uint8_t v = clamp(((in[0] * 1448 >> 12) * 1448 >> 12) + 128); for (int y = 0; y < 8; y++) memset(dst + y * stride, v, 8); return; }
    int32_t tmp[64];
    for (int x = 0; x < 8; x++)                             /* 세로(v) 합 → tmp[y][x] 의 중간값 [v][x] 로 */
        for (int y = 0; y < 8; y++) {
            int64_t s = 0; for (int v = 0; v < 8; v++) s += (int64_t)cos_t[v][y] * in[v * 8 + x];
            tmp[y * 8 + x] = (int32_t)(s >> 12);
        }
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int64_t s = 0; for (int u = 0; u < 8; u++) s += (int64_t)cos_t[u][x] * tmp[y * 8 + u];
            dst[y * stride + x] = clamp((int)(s >> 12) + 128);
        }
}

static uint8_t pl[3][(JPG_MAXH + 32) * (JPG_MAXW + 32)];
typedef struct { int id, h, v, tq, td, ta, pw, ph, pred; } Comp;

int jpg_decode(const uint8_t *d, uint32_t n, uint8_t *out, uint32_t cap, int *ow, int *oh)
{
    if (!cos_ready) cos_init();
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return -1;
    memset(hdc, 0, sizeof hdc); memset(hac, 0, sizeof hac); memset(qt, 0, sizeof qt);
    Comp c[3]; int nc = 0, W = 0, H = 0, ri = 0, hmax = 1, vmax = 1;
    uint32_t p = 2;
    int gotsof = 0;
    while (p + 4 <= n) {
        if (d[p] != 0xFF) { p++; continue; }
        int m = d[p + 1];
        if (m == 0xFF) { p++; continue; }
        if (m == 0 || (m >= 0xD0 && m <= 0xD8) || m == 0x01) { p += 2; continue; }
        if (m == 0xD9) break;
        uint32_t len = (uint32_t)d[p + 2] << 8 | d[p + 3];
        if (len < 2 || p + 2 + len > n) return -1;
        const uint8_t *s = d + p + 4; uint32_t sl = len - 2;
        if (m == 0xDB) {
            while (sl >= 65 || (sl >= 1 && (s[0] >> 4) == 0 && sl >= 65)) {
                int pq = s[0] >> 4, tq = s[0] & 15; uint32_t need = 1 + (pq ? 128u : 64u);
                if (tq > 3 || sl < need) return -1;
                for (int i = 0; i < 64; i++) qt[tq][i] = pq ? (uint16_t)(s[1 + 2 * i] << 8 | s[2 + 2 * i]) : s[1 + i];
                s += need; sl -= need;
            }
        } else if (m == 0xC4) {
            while (sl >= 17) {
                int tc = s[0] >> 4, th = s[0] & 15; if (th > 1 || tc > 1) return -1;
                Huff *h = tc ? &hac[th] : &hdc[th]; memset(h, 0, sizeof *h);
                int tot = 0; for (int i = 1; i <= 16; i++) { h->bits[i] = s[i]; tot += s[i]; }
                if (tot > 256 || sl < 17u + (uint32_t)tot) return -1;
                memcpy(h->val, s + 17, (size_t)tot); huff_build(h);
                s += 17 + tot; sl -= 17 + (uint32_t)tot;
            }
        } else if (m == 0xC0 || m == 0xC1) {
            if (sl < 6 || s[0] != 8) return -2;
            H = s[1] << 8 | s[2]; W = s[3] << 8 | s[4]; nc = s[5];
            if (nc != 1 && nc != 3) return -2;
            if (sl < 6u + 3u * (uint32_t)nc) return -1;
            if (W < 1 || H < 1) return -1;
            if (W > JPG_MAXW || H > JPG_MAXH) return -3;
            for (int i = 0; i < nc; i++) { c[i].id = s[6 + 3 * i]; c[i].h = s[7 + 3 * i] >> 4; c[i].v = s[7 + 3 * i] & 15; c[i].tq = s[8 + 3 * i]; if (c[i].h < 1 || c[i].h > 2 || c[i].v < 1 || c[i].v > 2 || c[i].tq > 3) return -2; if (c[i].h > hmax) hmax = c[i].h; if (c[i].v > vmax) vmax = c[i].v; }
            if (nc == 3 && (c[1].h != c[2].h || c[1].v != c[2].v || c[0].h != hmax || c[0].v != vmax || hmax % c[1].h || vmax % c[1].v)) return -2;
            gotsof = 1;
        } else if (m >= 0xC2 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) return -2;     /* 프로그레시브·산술 부호 등 */
        else if (m == 0xDD) { if (sl >= 2) ri = s[0] << 8 | s[1]; }
        else if (m == 0xDA) {
            if (!gotsof || sl < 1 || s[0] != nc) return -2;
            for (int i = 0; i < nc; i++) { int id = s[1 + 2 * i], t = s[2 + 2 * i]; int k; for (k = 0; k < nc; k++) if (c[k].id == id) break; if (k == nc) return -1; c[k].td = t >> 4; c[k].ta = t & 15; if (c[k].td > 1 || c[k].ta > 1) return -1; }
            if (!hdc[0].ok && !hdc[1].ok && !hac[0].ok && !hac[1].ok) huff_std();
            else { /* 부분만 있으면 없는 것만 표준으로 */
                Huff sd[4]; (void)sd;
                if (!hdc[0].ok || !hdc[1].ok || !hac[0].ok || !hac[1].ok) { Huff sv[4]; memcpy(&sv[0], &hdc[0], sizeof(Huff)); memcpy(&sv[1], &hdc[1], sizeof(Huff)); memcpy(&sv[2], &hac[0], sizeof(Huff)); memcpy(&sv[3], &hac[1], sizeof(Huff)); huff_std();
                    if (sv[0].ok) memcpy(&hdc[0], &sv[0], sizeof(Huff)); if (sv[1].ok) memcpy(&hdc[1], &sv[1], sizeof(Huff)); if (sv[2].ok) memcpy(&hac[0], &sv[2], sizeof(Huff)); if (sv[3].ok) memcpy(&hac[1], &sv[3], sizeof(Huff)); }
            }
            uint32_t sp = p + 2 + len;
            int mx = (W + 8 * hmax - 1) / (8 * hmax), my = (H + 8 * vmax - 1) / (8 * vmax);
            for (int i = 0; i < nc; i++) { c[i].pw = mx * 8 * c[i].h; c[i].ph = my * 8 * c[i].v; c[i].pred = 0; if (c[i].pw > JPG_MAXW + 32 || c[i].ph > JPG_MAXH + 32) return -3; }
            Br br; br.p = d + sp; br.end = d + n; br.buf = 0; br.cnt = 0; br.mk = 0;
            int32_t co[64]; int cntmcu = 0;
            for (int my_ = 0; my_ < my; my_++)
                for (int mx_ = 0; mx_ < mx; mx_++) {
                    if (ri && cntmcu == ri) {                          /* 재시작 표시 */
                        cntmcu = 0; br.buf = 0; br.cnt = 0;
                        while (br.p + 1 < br.end && !(br.p[0] == 0xFF && br.p[1] >= 0xD0 && br.p[1] <= 0xD7)) br.p++;
                        if (br.p + 1 < br.end) br.p += 2;
                        br.mk = 0;
                        for (int i = 0; i < nc; i++) c[i].pred = 0;
                    }
                    for (int i = 0; i < nc; i++)
                        for (int by = 0; by < c[i].v; by++)
                            for (int bx = 0; bx < c[i].h; bx++) {
                                memset(co, 0, sizeof co);
                                if (blk(&br, &hdc[c[i].td], &hac[c[i].ta], qt[c[i].tq], &c[i].pred, co)) goto done;
                                idct(co, pl[i] + (my_ * c[i].v * 8 + by * 8) * c[i].pw + (mx_ * c[i].h * 8 + bx * 8), c[i].pw);
                            }
                    cntmcu++;
                }
        done:
            {
                int mw = (W + 1) & ~1, mh = (H + 1) & ~1;
                if ((uint32_t)(mw * mh * 3 / 2) > cap) return -3;
                uint8_t *Y = out, *U = out + mw * mh, *V = U + (mw / 2) * (mh / 2);
                static uint8_t ly[256], lc[256]; static int lut = 0;
                if (!lut) { for (int i = 0; i < 256; i++) { ly[i] = (uint8_t)(16 + (i * 219 + 127) / 255); lc[i] = (uint8_t)(16 + (i * 224 + 127) / 255); } lut = 1; }
                for (int y = 0; y < mh; y++) { int sy = y < H ? y : H - 1; const uint8_t *r = pl[0] + sy * c[0].pw; uint8_t *o = Y + y * mw; for (int x = 0; x < mw; x++) o[x] = ly[r[x < W ? x : W - 1]]; }
                for (int y = 0; y < mh / 2; y++)
                    for (int x = 0; x < mw / 2; x++) {
                        int uu = 128, vv = 128;
                        if (nc == 3) {
                            int rx = hmax / c[1].h, ry = vmax / c[1].v;           /* 1 = 색이 휘도와 같은 해상도(둘씩 평균), 2 = 이미 반 해상도 */
                            int su = 0, sv = 0, cnt = 0;
                            for (int dy = 0; dy < (ry == 1 ? 2 : 1); dy++)
                                for (int dx = 0; dx < (rx == 1 ? 2 : 1); dx++) {
                                    int sx = rx == 1 ? 2 * x + dx : x, sy = ry == 1 ? 2 * y + dy : y;
                                    int lx = rx == 1 ? (W - 1) : ((W - 1) / 2), ly_ = ry == 1 ? (H - 1) : ((H - 1) / 2);
                                    if (sx > lx) sx = lx; if (sy > ly_) sy = ly_;
                                    su += pl[1][sy * c[1].pw + sx]; sv += pl[2][sy * c[2].pw + sx]; cnt++;
                                }
                            uu = su / cnt; vv = sv / cnt;
                        }
                        U[y * (mw / 2) + x] = lc[uu]; V[y * (mw / 2) + x] = lc[vv];
                    }
                *ow = W; *oh = H;
                return 0;
            }
        }
        p += 2 + len;
    }
    return -1;
}
