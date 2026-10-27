/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* wpa.c - WPA2-PSK 암호 부품 (표준 문서의 알고리즘을 직접 구현). 시험: tests/wpatest.c */
#include "wpa.h"
static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
static void sha1_block(uint32_t h[5], const uint8_t *p)
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) | ((uint32_t)p[4 * i + 2] << 8) | p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; } else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; } else { f = b ^ c ^ d; k = 0xCA62C1D6; }
        uint32_t t = rol(a, 5) + f + e + k + w[i]; e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}
void wpa_sha1(const uint8_t *d, uint32_t n, uint8_t out[20])
{
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 }, i = 0;
    for (; i + 64 <= n; i += 64) sha1_block(h, d + i);
    uint8_t t[128]; uint32_t r = n - i; for (uint32_t k = 0; k < 128; k++) t[k] = 0;
    for (uint32_t k = 0; k < r; k++) t[k] = d[i + k];
    t[r] = 0x80; uint32_t nn = r + 9 <= 64 ? 64 : 128; uint64_t bits = (uint64_t)n * 8;
    for (int k = 0; k < 8; k++) t[nn - 1 - k] = (uint8_t)(bits >> (8 * k));
    sha1_block(h, t); if (nn == 128) sha1_block(h, t + 64);
    for (int k = 0; k < 20; k++) out[k] = (uint8_t)(h[k / 4] >> (24 - 8 * (k % 4)));
}
/* HMAC: 메시지는 최대 약 300바이트 (EAPOL 포함)까지 */
void wpa_hmac_sha1(const uint8_t *key, int klen, const uint8_t *d, int n, uint8_t out[20])
{
    uint8_t k[64], buf[64 + 400], in[20];
    for (int i = 0; i < 64; i++) k[i] = 0;
    if (klen > 64) wpa_sha1(key, (uint32_t)klen, k); else for (int i = 0; i < klen; i++) k[i] = key[i];
    if (n > 400) n = 400;
    for (int i = 0; i < 64; i++) buf[i] = k[i] ^ 0x36;
    for (int i = 0; i < n; i++) buf[64 + i] = d[i];
    wpa_sha1(buf, (uint32_t)(64 + n), in);
    for (int i = 0; i < 64; i++) buf[i] = k[i] ^ 0x5C;
    for (int i = 0; i < 20; i++) buf[64 + i] = in[i];
    wpa_sha1(buf, 84, out);
}
static void pbkdf2_block(const char *pass, int plen, const uint8_t *salt, int slen, uint32_t blk, uint8_t out[20])
{
    uint8_t s[40 + 4], u[20], t[20];
    for (int i = 0; i < slen; i++) s[i] = salt[i];
    s[slen] = (uint8_t)(blk >> 24); s[slen + 1] = (uint8_t)(blk >> 16); s[slen + 2] = (uint8_t)(blk >> 8); s[slen + 3] = (uint8_t)blk;
    wpa_hmac_sha1((const uint8_t *)pass, plen, s, slen + 4, u);
    for (int i = 0; i < 20; i++) t[i] = u[i];
    for (int it = 1; it < 4096; it++) {
        wpa_hmac_sha1((const uint8_t *)pass, plen, u, 20, u);
        for (int i = 0; i < 20; i++) t[i] ^= u[i];
    }
    for (int i = 0; i < 20; i++) out[i] = t[i];
}
void wpa_pmk(const char *pass, const uint8_t *ssid, int ssidlen, uint8_t pmk[32])
{
    int plen = 0; while (pass[plen]) plen++;
    if (ssidlen > 32) ssidlen = 32;
    uint8_t b1[20], b2[20];
    pbkdf2_block(pass, plen, ssid, ssidlen, 1, b1); pbkdf2_block(pass, plen, ssid, ssidlen, 2, b2);
    for (int i = 0; i < 20; i++) pmk[i] = b1[i];
    for (int i = 0; i < 12; i++) pmk[20 + i] = b2[i];
}
static int cmp6(const uint8_t *a, const uint8_t *b, int n) { for (int i = 0; i < n; i++) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1; return 0; }
void wpa_ptk(const uint8_t pmk[32], const uint8_t aa[6], const uint8_t spa[6], const uint8_t an[32], const uint8_t sn[32], uint8_t ptk[48])
{
    static const char lab[] = "Pairwise key expansion";
    uint8_t d[22 + 1 + 6 + 6 + 32 + 32 + 1]; int n = 0;
    for (int i = 0; i < 22; i++) d[n++] = (uint8_t)lab[i];
    d[n++] = 0;
    const uint8_t *lo = cmp6(aa, spa, 6) < 0 ? aa : spa, *hi = cmp6(aa, spa, 6) < 0 ? spa : aa;
    for (int i = 0; i < 6; i++) d[n++] = lo[i];
    for (int i = 0; i < 6; i++) d[n++] = hi[i];
    const uint8_t *nlo = cmp6(an, sn, 32) < 0 ? an : sn, *nhi = cmp6(an, sn, 32) < 0 ? sn : an;
    for (int i = 0; i < 32; i++) d[n++] = nlo[i];
    for (int i = 0; i < 32; i++) d[n++] = nhi[i];
    uint8_t r[20];
    for (int c = 0; c < 3; c++) { d[n] = (uint8_t)c; wpa_hmac_sha1(pmk, 32, d, n + 1, r); for (int i = 0; i < 20 && c * 20 + i < 48; i++) ptk[c * 20 + i] = r[i]; }
}
void wpa_mic(const uint8_t kck[16], const uint8_t *eapol, int len, uint8_t mic[16])
{
    uint8_t r[20]; wpa_hmac_sha1(kck, 16, eapol, len, r);
    for (int i = 0; i < 16; i++) mic[i] = r[i];
}
/* ---- AES-128 ---- */
static uint8_t SB[256], ISB[256]; static int sbok;
static uint8_t xt(uint8_t x) { return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1B : 0)); }
static uint8_t gm(uint8_t a, uint8_t b) { uint8_t p = 0; while (b) { if (b & 1) p ^= a; a = xt(a); b >>= 1; } return p; }
static void sbinit(void)
{
    if (sbok) return;
    uint8_t p = 1, q = 1;
    do {
        p = (uint8_t)(p ^ (p << 1) ^ ((p & 0x80) ? 0x1B : 0));
        q ^= (uint8_t)(q << 1); q ^= (uint8_t)(q << 2); q ^= (uint8_t)(q << 4); if (q & 0x80) q ^= 0x09;
        uint8_t x = (uint8_t)(q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) ^ ((q << 3) | (q >> 5)) ^ ((q << 4) | (q >> 4)));
        SB[p] = (uint8_t)(x ^ 0x63);
    } while (p != 1);
    SB[0] = 0x63;
    for (int i = 0; i < 256; i++) ISB[SB[i]] = (uint8_t)i;
    sbok = 1;
}
void aes_key(Aes128 *a, const uint8_t key[16])
{
    sbinit();
    for (int i = 0; i < 4; i++) a->rk[i] = ((uint32_t)key[4 * i] << 24) | ((uint32_t)key[4 * i + 1] << 16) | ((uint32_t)key[4 * i + 2] << 8) | key[4 * i + 3];
    uint8_t rc = 1;
    for (int i = 4; i < 44; i++) {
        uint32_t t = a->rk[i - 1];
        if (i % 4 == 0) { t = (t << 8) | (t >> 24); t = ((uint32_t)SB[t >> 24] << 24) | ((uint32_t)SB[(t >> 16) & 255] << 16) | ((uint32_t)SB[(t >> 8) & 255] << 8) | SB[t & 255]; t ^= (uint32_t)rc << 24; rc = xt(rc); }
        a->rk[i] = a->rk[i - 4] ^ t;
    }
}
static void addrk(uint8_t s[16], const uint32_t *rk) { for (int i = 0; i < 4; i++) { s[4 * i] ^= (uint8_t)(rk[i] >> 24); s[4 * i + 1] ^= (uint8_t)(rk[i] >> 16); s[4 * i + 2] ^= (uint8_t)(rk[i] >> 8); s[4 * i + 3] ^= (uint8_t)rk[i]; } }
void aes_enc(const Aes128 *a, const uint8_t in[16], uint8_t out[16])
{
    uint8_t s[16], t[16]; for (int i = 0; i < 16; i++) s[i] = in[i];
    addrk(s, a->rk);
    for (int r = 1; r <= 10; r++) {
        for (int i = 0; i < 16; i++) t[i] = SB[s[i]];
        uint8_t u[16];
        for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) u[4 * c + rr] = t[4 * ((c + rr) & 3) + rr];
        if (r < 10) for (int c = 0; c < 4; c++) {
            uint8_t a0 = u[4 * c], a1 = u[4 * c + 1], a2 = u[4 * c + 2], a3 = u[4 * c + 3];
            s[4 * c] = (uint8_t)(xt(a0) ^ (xt(a1) ^ a1) ^ a2 ^ a3); s[4 * c + 1] = (uint8_t)(a0 ^ xt(a1) ^ (xt(a2) ^ a2) ^ a3);
            s[4 * c + 2] = (uint8_t)(a0 ^ a1 ^ xt(a2) ^ (xt(a3) ^ a3)); s[4 * c + 3] = (uint8_t)((xt(a0) ^ a0) ^ a1 ^ a2 ^ xt(a3));
        } else for (int i = 0; i < 16; i++) s[i] = u[i];
        addrk(s, a->rk + 4 * r);
    }
    for (int i = 0; i < 16; i++) out[i] = s[i];
}
void aes_dec(const Aes128 *a, const uint8_t in[16], uint8_t out[16])
{
    uint8_t s[16], u[16]; for (int i = 0; i < 16; i++) s[i] = in[i];
    addrk(s, a->rk + 40);
    for (int r = 9; r >= 0; r--) {
        for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) u[4 * ((c + rr) & 3) + rr] = s[4 * c + rr];   /* 역 행 이동 */
        for (int i = 0; i < 16; i++) s[i] = ISB[u[i]];
        addrk(s, a->rk + 4 * r);
        if (r > 0) for (int c = 0; c < 4; c++) {
            uint8_t a0 = s[4 * c], a1 = s[4 * c + 1], a2 = s[4 * c + 2], a3 = s[4 * c + 3];
            s[4 * c] = (uint8_t)(gm(a0, 14) ^ gm(a1, 11) ^ gm(a2, 13) ^ gm(a3, 9)); s[4 * c + 1] = (uint8_t)(gm(a0, 9) ^ gm(a1, 14) ^ gm(a2, 11) ^ gm(a3, 13));
            s[4 * c + 2] = (uint8_t)(gm(a0, 13) ^ gm(a1, 9) ^ gm(a2, 14) ^ gm(a3, 11)); s[4 * c + 3] = (uint8_t)(gm(a0, 11) ^ gm(a1, 13) ^ gm(a2, 9) ^ gm(a3, 14));
        }
    }
    for (int i = 0; i < 16; i++) out[i] = s[i];
}
/* ---- RFC 3394 키 풀기 ---- */
int wpa_unwrap(const uint8_t kek[16], const uint8_t *in, int inlen, uint8_t *out)
{
    if (inlen < 24 || (inlen & 7) || inlen > 8 * 40) return -1;
    Aes128 a; aes_key(&a, kek);
    int n = inlen / 8 - 1; uint8_t A[8], R[40][8], b[16];
    for (int i = 0; i < 8; i++) A[i] = in[i];
    for (int i = 0; i < n; i++) for (int j = 0; j < 8; j++) R[i][j] = in[8 + 8 * i + j];
    for (int j = 5; j >= 0; j--) for (int i = n; i >= 1; i--) {
        uint32_t t = (uint32_t)(n * j + i);
        for (int k = 0; k < 8; k++) b[k] = A[k];
        b[7] ^= (uint8_t)t; b[6] ^= (uint8_t)(t >> 8); b[5] ^= (uint8_t)(t >> 16); b[4] ^= (uint8_t)(t >> 24);
        for (int k = 0; k < 8; k++) b[8 + k] = R[i - 1][k];
        aes_dec(&a, b, b);
        for (int k = 0; k < 8; k++) { A[k] = b[k]; R[i - 1][k] = b[8 + k]; }
    }
    for (int i = 0; i < 8; i++) if (A[i] != 0xA6) return -1;
    for (int i = 0; i < n; i++) for (int j = 0; j < 8; j++) out[8 * i + j] = R[i][j];
    return 8 * n;
}
/* ---- CCM (RFC 3610) ---- */
static void ccm_mac(const Aes128 *a, int M, int L, const uint8_t *nonce, const uint8_t *aad, int aadlen, const uint8_t *pt, int len, uint8_t T[16])
{
    uint8_t x[16], blk[16]; int nl = 15 - L;
    blk[0] = (uint8_t)((aadlen ? 0x40 : 0) | (((M - 2) / 2) << 3) | (L - 1));
    for (int i = 0; i < nl; i++) blk[1 + i] = nonce[i];
    for (int i = 0; i < L; i++) blk[15 - i] = (uint8_t)((uint32_t)len >> (8 * i));
    aes_enc(a, blk, x);
    if (aadlen) {
        uint8_t buf[16]; int pos = 0, first = 1, left = aadlen;
        while (left > 0 || first) {
            int k = 0;
            if (first) { buf[0] = (uint8_t)(aadlen >> 8); buf[1] = (uint8_t)aadlen; k = 2; first = 0; }
            while (k < 16 && left > 0) { buf[k++] = aad[pos++]; left--; }
            while (k < 16) buf[k++] = 0;
            for (int i = 0; i < 16; i++) x[i] ^= buf[i];
            aes_enc(a, x, x);
        }
    }
    for (int p = 0; p < len; p += 16) {
        for (int i = 0; i < 16; i++) x[i] ^= (p + i < len) ? pt[p + i] : 0;
        aes_enc(a, x, x);
    }
    for (int i = 0; i < 16; i++) T[i] = x[i];
}
static void ccm_ctr(const Aes128 *a, int L, const uint8_t *nonce, uint32_t ctr, uint8_t s[16])
{
    uint8_t blk[16]; int nl = 15 - L; blk[0] = (uint8_t)(L - 1);
    for (int i = 0; i < nl; i++) blk[1 + i] = nonce[i];
    for (int i = 0; i < L; i++) blk[15 - i] = (uint8_t)(ctr >> (8 * i));
    aes_enc(a, blk, s);
}
void ccm_encrypt(const Aes128 *a, int M, int L, const uint8_t *nonce, const uint8_t *aad, int aadlen, const uint8_t *pt, int len, uint8_t *out)
{
    uint8_t T[16], s[16];
    ccm_mac(a, M, L, nonce, aad, aadlen, pt, len, T);
    for (int p = 0; p < len; p += 16) { ccm_ctr(a, L, nonce, (uint32_t)(p / 16 + 1), s); for (int i = 0; i < 16 && p + i < len; i++) out[p + i] = pt[p + i] ^ s[i]; }
    ccm_ctr(a, L, nonce, 0, s);
    for (int i = 0; i < M; i++) out[len + i] = T[i] ^ s[i];
}
int ccm_decrypt(const Aes128 *a, int M, int L, const uint8_t *nonce, const uint8_t *aad, int aadlen, const uint8_t *ct, int ctlen, uint8_t *out)
{
    int len = ctlen - M; if (len < 0) return -1;
    uint8_t T[16], s[16];
    for (int p = 0; p < len; p += 16) { ccm_ctr(a, L, nonce, (uint32_t)(p / 16 + 1), s); for (int i = 0; i < 16 && p + i < len; i++) out[p + i] = ct[p + i] ^ s[i]; }
    ccm_mac(a, M, L, nonce, aad, aadlen, out, len, T);
    ccm_ctr(a, L, nonce, 0, s);
    int bad = 0; for (int i = 0; i < M; i++) bad |= (T[i] ^ s[i]) ^ ct[len + i];
    return bad ? -1 : 0;
}
/* ---- CCMP (802.11) ---- */
static int ccmp_aad_nonce(const uint8_t *h, int hlen, const uint8_t pn[6], uint8_t aad[32], uint8_t nonce[13])
{
    int qos = (h[0] & 0x88) == 0x88, n = 0;
    aad[n++] = qos ? (uint8_t)(h[0] & ~0x70u) : h[0];                  /* QoS 데이터는 부분형식 비트4-6 을 0 으로 */
    aad[n++] = (uint8_t)(h[1] & ~0x38u & (qos ? ~0x80u : 0xFFu));
    for (int i = 0; i < 18; i++) aad[n++] = h[4 + i];                  /* A1 A2 A3 */
    aad[n++] = (uint8_t)(h[22] & 0x0F); aad[n++] = 0;                  /* 순서번호는 0, 조각번호만 */
    int tods_fromds = (h[1] & 3) == 3;
    if (tods_fromds) for (int i = 0; i < 6; i++) aad[n++] = h[24 + i];
    int tid = 0;
    if (qos) { int q = tods_fromds ? 30 : 24; aad[n++] = (uint8_t)(h[q] & 0x0F); aad[n++] = 0; tid = h[q] & 0x0F; }
    nonce[0] = (uint8_t)tid; for (int i = 0; i < 6; i++) nonce[1 + i] = h[10 + i];
    for (int i = 0; i < 6; i++) nonce[7 + i] = pn[5 - i];
    (void)hlen; return n;
}
int ccmp_encrypt(const Aes128 *a, const uint8_t *hdr, int hlen, const uint8_t pn[6], int keyid, const uint8_t *pt, int len, uint8_t *out)
{
    uint8_t aad[32], nonce[13]; int al = ccmp_aad_nonce(hdr, hlen, pn, aad, nonce);
    out[0] = pn[0]; out[1] = pn[1]; out[2] = 0; out[3] = (uint8_t)(0x20 | (keyid << 6)); out[4] = pn[2]; out[5] = pn[3]; out[6] = pn[4]; out[7] = pn[5];
    ccm_encrypt(a, 8, 2, nonce, aad, al, pt, len, out + 8);
    return 8 + len + 8;
}
int ccmp_decrypt(const Aes128 *a, const uint8_t *hdr, int hlen, const uint8_t *body, int blen, uint8_t *out, uint8_t pn[6])
{
    if (blen < 16 || !(body[3] & 0x20)) return -1;
    pn[0] = body[0]; pn[1] = body[1]; pn[2] = body[4]; pn[3] = body[5]; pn[4] = body[6]; pn[5] = body[7];
    uint8_t aad[32], nonce[13]; int al = ccmp_aad_nonce(hdr, hlen, pn, aad, nonce);
    if (ccm_decrypt(a, 8, 2, nonce, aad, al, body + 8, blen - 8, out)) return -1;
    return blen - 16;
}

/* ---- TKIP 수신 복호 (IEEE 802.11i). 그룹키가 TKIP 인 공유기의 방송 프레임을 읽기 위한 것. Michael MIC 은 검사하지 않고 ICV(CRC32)만 검사 ---- */
static uint16_t tk_sb16(unsigned i) { sbinit(); uint8_t v = SB[i & 0xFF], x = xt(v); return (uint16_t)((x << 8) | (x ^ v)); }
static uint16_t tkS(uint16_t v) { uint16_t a = tk_sb16(v & 0xFF), b = tk_sb16(v >> 8); return (uint16_t)(a ^ (uint16_t)((b << 8) | (b >> 8))); }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint16_t ror1(uint16_t x) { return (uint16_t)((x >> 1) | (x << 15)); }
static uint32_t crc32b(const uint8_t *p, int n) { uint32_t c = 0xFFFFFFFFu; for (int i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1)); } return ~c; }
int tkip_decrypt(const uint8_t tk[16], const uint8_t *hdr, int hlen, const uint8_t *body, int blen, uint8_t *out)
{
    if (blen < 8 + 12 || !(body[3] & 0x20)) return -1;
    const uint8_t *ta = hdr + 10;
    uint16_t iv16 = (uint16_t)(body[0] << 8 | body[2]); uint32_t iv32 = (uint32_t)(body[4] | body[5] << 8 | body[6] << 16 | (uint32_t)body[7] << 24);
    uint16_t p[5], q[6]; p[0] = (uint16_t)iv32; p[1] = (uint16_t)(iv32 >> 16); p[2] = le16(ta); p[3] = le16(ta + 2); p[4] = le16(ta + 4);
    for (int i = 0; i < 8; i++) { int j = 2 * (i & 1);
        p[0] = (uint16_t)(p[0] + tkS(p[4] ^ le16(tk + 0 + j))); p[1] = (uint16_t)(p[1] + tkS(p[0] ^ le16(tk + 4 + j)));
        p[2] = (uint16_t)(p[2] + tkS(p[1] ^ le16(tk + 8 + j))); p[3] = (uint16_t)(p[3] + tkS(p[2] ^ le16(tk + 12 + j)));
        p[4] = (uint16_t)(p[4] + tkS(p[3] ^ le16(tk + 0 + j)) + i); }
    for (int i = 0; i < 5; i++) q[i] = p[i]; q[5] = (uint16_t)(p[4] + iv16);
    q[0] = (uint16_t)(q[0] + tkS(q[5] ^ le16(tk + 0))); q[1] = (uint16_t)(q[1] + tkS(q[0] ^ le16(tk + 2))); q[2] = (uint16_t)(q[2] + tkS(q[1] ^ le16(tk + 4)));
    q[3] = (uint16_t)(q[3] + tkS(q[2] ^ le16(tk + 6))); q[4] = (uint16_t)(q[4] + tkS(q[3] ^ le16(tk + 8))); q[5] = (uint16_t)(q[5] + tkS(q[4] ^ le16(tk + 10)));
    q[0] = (uint16_t)(q[0] + ror1(q[5] ^ le16(tk + 12))); q[1] = (uint16_t)(q[1] + ror1(q[0] ^ le16(tk + 14)));
    q[2] = (uint16_t)(q[2] + ror1(q[1])); q[3] = (uint16_t)(q[3] + ror1(q[2])); q[4] = (uint16_t)(q[4] + ror1(q[3])); q[5] = (uint16_t)(q[5] + ror1(q[4]));
    uint8_t key[16]; key[0] = (uint8_t)(iv16 >> 8); key[1] = (uint8_t)(((iv16 >> 8) | 0x20) & 0x7F); key[2] = (uint8_t)iv16; key[3] = (uint8_t)(((q[5] ^ le16(tk)) >> 1) & 0xFF);
    for (int i = 0; i < 6; i++) { key[4 + 2 * i] = (uint8_t)q[i]; key[5 + 2 * i] = (uint8_t)(q[i] >> 8); }
    uint8_t S[256]; for (int i = 0; i < 256; i++) S[i] = (uint8_t)i;
    for (int i = 0, j = 0; i < 256; i++) { j = (j + S[i] + key[i & 15]) & 255; uint8_t t = S[i]; S[i] = S[j]; S[j] = t; }
    int n = blen - 8, plain = n - 4; if (plain < 0 || plain > 1900) return -1;
    static uint8_t tmp[1920];
    for (int k = 0, i = 0, j = 0; k < n; k++) { i = (i + 1) & 255; j = (j + S[i]) & 255; uint8_t t = S[i]; S[i] = S[j]; S[j] = t; tmp[k] = (uint8_t)(body[8 + k] ^ S[(S[i] + S[j]) & 255]); }
    uint32_t c = crc32b(tmp, plain), got = (uint32_t)(tmp[plain] | tmp[plain + 1] << 8 | tmp[plain + 2] << 16 | (uint32_t)tmp[plain + 3] << 24);
    (void)hlen; if (c != got) return -2;
    for (int i = 0; i < plain - 8 && plain >= 8; i++) out[i] = tmp[i];     /* 끝의 Michael MIC 8바이트는 뺌 */
    return plain >= 8 ? plain - 8 : 0;
}
