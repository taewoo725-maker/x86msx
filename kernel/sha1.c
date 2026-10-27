/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* sha1.c - SHA-1 (롬 데이터베이스 조회용) */
#include "sha1.h"
#include <string.h>

static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void block(uint32_t h[5], const uint8_t *p)
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) | ((uint32_t)p[4 * i + 2] << 8) | p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6; }
        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void sha1_hex(const uint8_t *data, uint32_t len, char out[41])
{
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    uint32_t i = 0;
    for (; i + 64 <= len; i += 64) block(h, data + i);
    uint8_t tail[128];
    uint32_t r = len - i;
    memset(tail, 0, sizeof tail);
    memcpy(tail, data + i, r);
    tail[r] = 0x80;
    uint32_t n = r + 1 + 8 <= 64 ? 64 : 128;
    uint64_t bits = (uint64_t)len * 8;
    for (int k = 0; k < 8; k++) tail[n - 1 - k] = (uint8_t)(bits >> (8 * k));
    block(h, tail);
    if (n == 128) block(h, tail + 64);
    static const char hx[] = "0123456789abcdef";
    for (int k = 0; k < 20; k++) {
        uint8_t b = (uint8_t)(h[k / 4] >> (24 - 8 * (k % 4)));
        out[2 * k] = hx[b >> 4];
        out[2 * k + 1] = hx[b & 15];
    }
    out[40] = 0;
}
