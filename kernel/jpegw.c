/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* jpegw.c - 3.2.13 영상 녹화용 작은 JPEG 부호기 (기본 순차, 4:4:4, 표준 허프만 표, 정수 DCT). 설명은 jpegw.h */
#include "jpegw.h"
#include <string.h>

static const int16_t dct_t[8][8] = {           /* round(8192 · c(u)/2 · cos((2x+1)uπ/16)) */
    {2896,2896,2896,2896,2896,2896,2896,2896},
    {4017,3406,2276,799,-799,-2276,-3406,-4017},
    {3784,1567,-1567,-3784,-3784,-1567,1567,3784},
    {3406,-799,-4017,-2276,2276,4017,799,-3406},
    {2896,-2896,-2896,2896,2896,-2896,-2896,2896},
    {2276,-4017,799,3406,-3406,-799,4017,-2276},
    {1567,-3784,3784,-1567,-1567,3784,-3784,1567},
    {799,-2276,3406,-4017,4017,-3406,2276,-799},
};
static const uint8_t zz[64] = {
    0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,
    35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63,
};
static const uint8_t q_lum[64] = {              /* 표준 (자연 순서) */
    16,11,10,16,24,40,51,61, 12,12,14,19,26,58,60,55, 14,13,16,24,40,57,69,56, 14,17,22,29,51,87,80,62,
    18,22,37,56,68,109,103,77, 24,35,55,64,81,104,113,92, 49,64,78,87,103,121,120,101, 72,92,95,98,112,100,103,99,
};
static const uint8_t q_chr[64] = {
    17,18,24,47,99,99,99,99, 18,21,26,66,99,99,99,99, 24,26,56,99,99,99,99,99, 47,66,99,99,99,99,99,99,
    99,99,99,99,99,99,99,99, 99,99,99,99,99,99,99,99, 99,99,99,99,99,99,99,99, 99,99,99,99,99,99,99,99,
};
static const uint8_t dc_l_bits[16] = { 0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0 }, dc_c_bits[16] = { 0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0 };
static const uint8_t dc_vals[12] = { 0,1,2,3,4,5,6,7,8,9,10,11 };
static const uint8_t ac_l_bits[16] = { 0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d }, ac_c_bits[16] = { 0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77 };
static const uint8_t ac_l_vals[162] = {
    0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,
    0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
    0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,
    0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
    0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,
    0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
    0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,
};
static const uint8_t ac_c_vals[162] = {
    0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,
    0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
    0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,
    0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
    0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,
    0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
    0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa,
};

typedef struct { uint16_t code[256]; uint8_t len[256]; } Huff;
static Huff h_dc[2], h_ac[2];
static int ready;
static void huff_build(Huff *h, const uint8_t *bits, const uint8_t *vals)
{
    int k = 0; uint16_t code = 0;
    for (int l = 1; l <= 16; l++) {
        for (int i = 0; i < bits[l - 1]; i++) { h->code[vals[k]] = code++; h->len[vals[k]] = (uint8_t)l; k++; }
        code <<= 1;
    }
}

typedef struct { uint8_t *o; int n, max, err; uint32_t acc; int nb; } JW;
static void put8(JW *w, int v) { if (w->n < w->max) w->o[w->n++] = (uint8_t)v; else w->err = 1; }
static void putbits(JW *w, uint32_t v, int n)
{
    w->acc = (w->acc << n) | (v & ((1u << n) - 1)); w->nb += n;
    while (w->nb >= 8) {
        int b = (int)((w->acc >> (w->nb - 8)) & 255);
        put8(w, b); if (b == 0xFF) put8(w, 0);
        w->nb -= 8;
    }
}
static void put_marker(JW *w, int m, int len) { put8(w, 0xFF); put8(w, m); put8(w, len >> 8); put8(w, len & 255); }
static int nbits(int v) { if (v < 0) v = -v; int n = 0; while (v) { n++; v >>= 1; } return n; }
static void put_val(JW *w, int v, int n) { if (n) putbits(w, (uint32_t)(v < 0 ? v - 1 : v), n); }

static void block(JW *w, const int16_t px[64], const uint16_t *qt, int *dc_prev, const Huff *hd, const Huff *ha)
{
    int32_t tmp[64];
    for (int y = 0; y < 8; y++)                          /* 가로 */
        for (int u = 0; u < 8; u++) {
            int32_t s = 0;
            for (int x = 0; x < 8; x++) s += px[y * 8 + x] * dct_t[u][x];
            tmp[y * 8 + u] = s >> 6;                     /* 8192 → 128배 */
        }
    int q[64];
    for (int u = 0; u < 8; u++)                          /* 세로 + 양자화 */
        for (int v = 0; v < 8; v++) {
            int64_t s = 0;
            for (int y = 0; y < 8; y++) s += (int64_t)tmp[y * 8 + u] * dct_t[v][y];
            int32_t c = (int32_t)(s >> 13);              /* 값 × 128 */
            int32_t d = (int32_t)qt[v * 8 + u] * 128;
            q[v * 8 + u] = c >= 0 ? (c + d / 2) / d : -((-c + d / 2) / d);
        }
    int dc = q[0], diff = dc - *dc_prev; *dc_prev = dc;
    int nb = nbits(diff);
    putbits(w, hd->code[nb], hd->len[nb]); put_val(w, diff, nb);
    int run = 0;
    for (int k = 1; k < 64; k++) {
        int v = q[zz[k]];
        if (!v) { run++; continue; }
        while (run > 15) { putbits(w, ha->code[0xF0], ha->len[0xF0]); run -= 16; }
        int n = nbits(v), sym = (run << 4) | n;
        putbits(w, ha->code[sym], ha->len[sym]); put_val(w, v, n);
        run = 0;
    }
    if (run) putbits(w, ha->code[0], ha->len[0]);
}

int jpeg_encode_rgb(const uint8_t *rgb, int w, int h, int stride, int quality, uint8_t *out, int max)
{
    if (!ready) { huff_build(&h_dc[0], dc_l_bits, dc_vals); huff_build(&h_dc[1], dc_c_bits, dc_vals); huff_build(&h_ac[0], ac_l_bits, ac_l_vals); huff_build(&h_ac[1], ac_c_bits, ac_c_vals); ready = 1; }
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;
    int sc = quality < 50 ? 5000 / quality : 200 - quality * 2;
    uint16_t qt[2][64];
    for (int i = 0; i < 64; i++) {
        int a = (q_lum[i] * sc + 50) / 100, b = (q_chr[i] * sc + 50) / 100;
        qt[0][i] = (uint16_t)(a < 1 ? 1 : a > 255 ? 255 : a); qt[1][i] = (uint16_t)(b < 1 ? 1 : b > 255 ? 255 : b);
    }
    JW o = { out, 0, max, 0, 0, 0 };
    put8(&o, 0xFF); put8(&o, 0xD8);                     /* SOI */
    for (int t = 0; t < 2; t++) { put_marker(&o, 0xDB, 67); put8(&o, t); for (int k = 0; k < 64; k++) put8(&o, qt[t][zz[k]]); }
    put_marker(&o, 0xC0, 17); put8(&o, 8); put8(&o, h >> 8); put8(&o, h & 255); put8(&o, w >> 8); put8(&o, w & 255); put8(&o, 3);
    for (int c = 0; c < 3; c++) { put8(&o, c + 1); put8(&o, 0x11); put8(&o, c ? 1 : 0); }
    {
        const uint8_t *bits[4] = { dc_l_bits, ac_l_bits, dc_c_bits, ac_c_bits }, *vals[4] = { dc_vals, ac_l_vals, dc_vals, ac_c_vals };
        const int cls[4] = { 0x00, 0x10, 0x01, 0x11 };
        for (int t = 0; t < 4; t++) {
            int n = 0; for (int i = 0; i < 16; i++) n += bits[t][i];
            put_marker(&o, 0xC4, 2 + 1 + 16 + n); put8(&o, cls[t]);
            for (int i = 0; i < 16; i++) put8(&o, bits[t][i]);
            for (int i = 0; i < n; i++) put8(&o, vals[t][i]);
        }
    }
    put_marker(&o, 0xDA, 12); put8(&o, 3);
    for (int c = 0; c < 3; c++) { put8(&o, c + 1); put8(&o, c ? 0x11 : 0x00); }
    put8(&o, 0); put8(&o, 63); put8(&o, 0);
    int dcp[3] = { 0, 0, 0 };
    int16_t Y[64], Cb[64], Cr[64];
    for (int by = 0; by < h; by += 8)
        for (int bx = 0; bx < w; bx += 8) {
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) {
                    int sx = bx + x < w ? bx + x : w - 1, sy = by + y < h ? by + y : h - 1;
                    const uint8_t *p = rgb + (size_t)sy * (size_t)stride + (size_t)sx * 3;
                    int r = p[0], g = p[1], b = p[2];
                    Y[y * 8 + x] = (int16_t)(((77 * r + 150 * g + 29 * b) >> 8) - 128);
                    Cb[y * 8 + x] = (int16_t)((-43 * r - 85 * g + 128 * b) >> 8);
                    Cr[y * 8 + x] = (int16_t)((128 * r - 107 * g - 21 * b) >> 8);
                }
            block(&o, Y, qt[0], &dcp[0], &h_dc[0], &h_ac[0]);
            block(&o, Cb, qt[1], &dcp[1], &h_dc[1], &h_ac[1]);
            block(&o, Cr, qt[1], &dcp[2], &h_dc[1], &h_ac[1]);
            if (o.err) return 0;
        }
    if (o.nb) putbits(&o, 0x7F, 8 - o.nb);              /* 1로 채움 */
    put8(&o, 0xFF); put8(&o, 0xD9);
    return o.err ? 0 : o.n;
}
