/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* arcx.c - 8.0.44 .ARJ·.PMA 묶음 파일 읽기. 설명은 arcx.h
 *
 * 묶음 안 파일 목록(머리글)을 한 번 훑어 표로 만들고, zipx.c 와 같은 모양의 함수 여섯 개를 그 표로 구현한다.
 * 푸는 쪽:
 *   ARJ  방법 0 저장 / 1·2·3 (LZ77 + 허프만, unarj 와 같은 식) / 4 (빠른 방식). 풀린 뒤 CRC32 를 머리글 값과 맞춰 봄.
 *   PMA  -pm0- 저장 / -pm1- / -pm2- (Simon Howard 의 lhasa 해독기를 third_party/lhasa 에 그대로 가져옴). 풀린 뒤 CRC16 확인.
 * 풀 때 쓰는 작업 자리는 이 파일의 정적 변수뿐 (한 번에 하나만 풂). */
#include "arcx.h"
#include "../third_party/lhasa/lha_decoder.h"
#include <string.h>

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (rd16(p + 2) << 16); }
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int slash(int c) { return c == '/' || c == '\\'; }

static int lha_method_ok(const uint8_t *m)
{
    if (m[0] != '-' || m[4] != '-') return 0;
    if (m[1] == 'l' && m[2] == 'h' && m[3] == 'd') return 1;                /* 폴더 칸 */
    return (m[1] == 'p' && m[2] == 'm' && m[3] >= '0' && m[3] <= '2') || (m[1] == 'l' && m[2] == 'h' && m[3] >= '0' && m[3] <= '7') || (m[1] == 'l' && m[2] == 'z' && (m[3] == 's' || m[3] == '5' || m[3] == '4'));
}
int arc_kind(const uint8_t *d, uint32_t len)
{
    if (len >= 4 && d[0] == 0x60 && d[1] == 0xEA) return 1;
    if (len >= 26 && d[2] == '-' && d[6] == '-' && (d[3] == 'p' || d[3] == 'l') && d[20] <= 2 && lha_method_ok(d + 2)) return 2;   /* LHA 머리글: .LZH·.LHA·.PMA */
    return 0;
}

/* ---- 묶음 안 항목 표 ---- */
#define ARC_MAX 512
typedef struct {
    const uint8_t *name; uint16_t nlen;
    uint8_t kind, method, dir, bad;         /* bad = 암호·조각 등 못 푸는 것 */
    uint32_t data, csize, usize, dos, crc;
} ArcEnt;
static ArcEnt tab[ARC_MAX];
static int tabn;

static int scan_arj(const uint8_t *d, uint32_t len)
{
    uint32_t p = 0;
    tabn = 0;
    for (;;) {
        if (p + 4 > len || d[p] != 0x60 || d[p + 1] != 0xEA) return ZIPX_BAD;
        uint32_t hs = rd16(d + p + 2);
        if (hs == 0) return 0;                                  /* 묶음 끝 */
        if (hs < 30 || hs > 2600 || p + 4 + hs + 4 > len) return ZIPX_BAD;
        const uint8_t *h = d + p + 4;
        uint32_t fhs = h[0], q = p + 4 + hs + 4;
        if (fhs < 30 || fhs >= hs) return ZIPX_BAD;
        for (;;) {                                              /* 확장 머리글들 */
            if (q + 2 > len) return ZIPX_BAD;
            uint32_t e = rd16(d + q); q += 2;
            if (!e) break;
            q += e + 4;
        }
        int ftype = h[6];
        if (ftype == 2) { p = q; continue; }                    /* 묶음 머리글 */
        uint32_t cs = rd32(h + 12);
        if (q > len || cs > len - q) return ZIPX_BAD;
        if ((ftype == 0 || ftype == 1 || ftype == 3) && tabn < ARC_MAX) {
            ArcEnt *e = &tab[tabn++];
            uint32_t nl = 0;
            e->name = h + fhs;
            while (fhs + nl < hs && h[fhs + nl]) nl++;
            e->nlen = (uint16_t)nl;
            e->kind = 1; e->method = h[5]; e->dir = ftype == 3;
            e->bad = (h[4] & 0x05) != 0;                        /* 0x01 암호, 0x04 여러 조각의 이어지는 부분 */
            e->data = q; e->csize = cs; e->usize = rd32(h + 16); e->dos = rd32(h + 8); e->crc = rd32(h + 20);
        }
        p = q + cs;
    }
}

/* LHA 머리글 (.LZH·.LHA·.PMA): 0·1·2 단계. 방법 이름 "-lh5-" 따위를 해독기 번호로 */
enum { M_STORE = 0, M_PM1, M_PM2, M_LH1, M_LH4, M_LH5, M_LH6, M_LH7, M_LZS, M_LZ5, M_BAD = 255 };
static uint8_t lha_method(const uint8_t *m)                    /* m = 5글자 "-lh5-" */
{
    if (m[0] != '-' || m[4] != '-') return M_BAD;
    if (m[1] == 'p' && m[2] == 'm') return m[3] == '0' ? M_STORE : m[3] == '1' ? M_PM1 : m[3] == '2' ? M_PM2 : M_BAD;
    if (m[1] == 'l' && m[2] == 'h') {
        switch (m[3]) { case '0': return M_STORE; case '1': return M_LH1; case '4': return M_LH4; case '5': return M_LH5; case '6': return M_LH6; case '7': return M_LH7; default: return M_BAD; }
    }
    if (m[1] == 'l' && m[2] == 'z') return m[3] == 's' ? M_LZS : m[3] == '5' ? M_LZ5 : m[3] == '4' ? M_STORE : M_BAD;
    return M_BAD;
}
static char lha_pool[16 * 1024];                                /* 2단계 머리글의 폴더+파일 이름을 이어 붙여 두는 자리 */
static uint32_t lha_pool_n;
static uint32_t unix_to_dos(uint32_t t)
{
    uint32_t days = t / 86400, r = t % 86400;
    int y = 1970;
    for (;;) { uint32_t yl = ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366u : 365u; if (days < yl) break; days -= yl; y++; }
    static const uint8_t md[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int mo = 0;
    for (;;) { uint32_t ml = md[mo] + (mo == 1 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)); if (days < ml) break; days -= ml; mo++; }
    if (y < 1980) return 0;
    uint32_t dt = ((uint32_t)(y - 1980) << 9) | ((uint32_t)(mo + 1) << 5) | (days + 1), tm = (r / 3600 << 11) | (r / 60 % 60 << 5) | (r % 60 / 2);
    return dt << 16 | tm;
}

static int scan_lha(const uint8_t *d, uint32_t len)
{
    uint32_t p = 0;
    tabn = 0; lha_pool_n = 0;
    while (p < len && d[p]) {
        if (p + 24 > len || d[p + 2] != '-' || d[p + 6] != '-') return ZIPX_BAD;
        int lvl = d[p + 20];
        uint32_t ps = rd32(d + p + 7), os = rd32(d + p + 11), data, crcpos = 0;
        const uint8_t *nm = 0, *dir = 0; uint32_t nl = 0, dl = 0, dos = rd32(d + p + 15), extsum = 0, q;
        uint32_t hs = d[p];
        if (lvl == 0 || lvl == 1) {
            uint32_t nlen = d[p + 21];
            if (hs < 20 || p + 2 + hs > len || 22 + nlen + 2 > hs + 2) return ZIPX_BAD;
            nm = d + p + 22; nl = nlen; crcpos = p + 22 + nlen;
            data = p + 2 + hs;
            if (lvl == 1) {                                     /* 확장 머리글: 이름·폴더가 여기 있을 수 있음 */
                if (22 + nlen + 5 > hs + 2) return ZIPX_BAD;        /* 이름 다음 CRC(2)·OS(1)·다음 머리글 크기(2) */
                q = p + hs;                                     /* 첫 "다음 머리글 크기" 칸 (기본 머리글 맨 끝) */
                for (;;) {
                    if (q + 2 > len) return ZIPX_BAD;
                    uint32_t es = rd16(d + q);
                    if (!es) break;
                    if (es < 3 || q + es + 2 > len) return ZIPX_BAD;
                    if (d[q + 2] == 1) { nm = d + q + 3; nl = es - 3; }
                    else if (d[q + 2] == 2) { dir = d + q + 3; dl = es - 3; }
                    extsum += es; q += es;
                }
                data = q + 2;
                if (ps < extsum) return ZIPX_BAD;
                ps -= extsum;                                   /* 1단계: 묶음 크기에 확장 머리글이 들어 있음 */
            }
        } else if (lvl == 2) {
            hs = rd16(d + p);
            if (hs < 26 || p + hs > len) return ZIPX_BAD;
            dos = unix_to_dos(rd32(d + p + 15));
            crcpos = p + 21;
            data = p + hs;
            q = p + 24;
            for (;;) {
                if (q + 2 > p + hs) break;
                uint32_t es = rd16(d + q);
                if (!es) break;
                if (es < 3 || q + es > p + hs) return ZIPX_BAD;
                if (d[q + 2] == 1) { nm = d + q + 3; nl = es - 3; }
                else if (d[q + 2] == 2) { dir = d + q + 3; dl = es - 3; }
                q += es;
            }
        } else return ZIPX_BAD;
        if (ps > len - data) return ZIPX_BAD;
        if (tabn < ARC_MAX && nm) {
            ArcEnt *e = &tab[tabn++];
            const uint8_t *np = nm; uint32_t nn = nl;
            if (dl) {                                           /* 폴더 + 이름 (폴더 안의 0xFF 는 빗금) */
                if (lha_pool_n + dl + nl + 2 > sizeof lha_pool) { tabn--; goto next; }
                char *o = lha_pool + lha_pool_n;
                uint32_t k = 0;
                for (uint32_t i = 0; i < dl; i++) o[k++] = d[(dir - d) + i] == 0xFF ? '/' : (char)dir[i];
                if (k && o[k - 1] != '/') o[k++] = '/';
                for (uint32_t i = 0; i < nl; i++) o[k++] = (char)np[i];
                np = (const uint8_t *)o; nn = k; lha_pool_n += k;
            }
            e->name = np; e->nlen = (uint16_t)nn;
            e->kind = 2; e->method = lha_method(d + p + 2);
            e->dir = (d[p + 3] == 'l' && d[p + 4] == 'h' && d[p + 5] == 'd');
            e->bad = e->method == M_BAD && !e->dir;
            e->data = data; e->csize = ps; e->usize = os; e->dos = dos;
            e->crc = rd16(d + crcpos);
        }
    next:
        p = data + ps;
    }
    return 0;
}

static int scan(const uint8_t *d, uint32_t len)
{
    int k = arc_kind(d, len);
    return k == 1 ? scan_arj(d, len) : k == 2 ? scan_lha(d, len) : ZIPX_BAD;
}

/* ---- ARJ 풀기 (방법 1~4) ---- */
#define A_NC   510
#define A_NT   19
#define A_NP   18                              /* 가장 큰 거리 부호 수 (lh7 = 18) */
#define A_NPT  0x80
#define A_CBIT 9
#define A_THRESHOLD 3
typedef struct {
    const uint8_t *in; uint32_t inpos, inlen;
    uint32_t bitbuf; int bits;                  /* 위쪽 정렬된 32비트 창 */
    int err;
    uint8_t *out; uint32_t outn, outmax;
} AjCtx;
static AjCtx aj;
static uint16_t aj_left[2 * A_NC], aj_right[2 * A_NC], aj_ctab[4096], aj_ptab[256];
static uint8_t aj_clen[A_NC], aj_plen[A_NPT];
static int aj_np = 17, aj_pbit = 5;                   /* 거리 부호 수·그 개수를 적는 비트 수: ARJ·lh6 = 17·5, lh5 = 15·4, lh7 = 18·5 */

static void aj_fill(void)
{
    while (aj.bits <= 24) {
        uint32_t b = 0;
        if (aj.inpos < aj.inlen) b = aj.in[aj.inpos];
        else if (aj.inpos > aj.inlen + 8) aj.err = 1;       /* 끝을 한참 넘어 읽음 */
        aj.inpos++;
        aj.bitbuf |= b << (24 - aj.bits);
        aj.bits += 8;
    }
}
static uint32_t aj_peek16(void) { aj_fill(); return aj.bitbuf >> 16; }
static void aj_skip(int n) { aj_fill(); aj.bitbuf <<= n; aj.bits -= n; }
static uint32_t aj_get(int n) { if (!n) return 0; uint32_t v = aj_peek16() >> (16 - n); aj_skip(n); return v; }

static int aj_make_table(int nchar, const uint8_t *bitlen, int tablebits, uint16_t *table, int tablesize)
{
    uint16_t count[17], weight[17], start[18], *p;
    unsigned i, k, len, ch, jutbits, avail, nextcode, mask;
    for (i = 1; i <= 16; i++) count[i] = 0;
    for (i = 0; (int)i < nchar; i++) { if (bitlen[i] > 16) return -1; count[bitlen[i]]++; }
    start[1] = 0;
    for (i = 1; i <= 16; i++) start[i + 1] = (uint16_t)(start[i] + (count[i] << (16 - i)));
    if (start[17] != 0) return -1;                              /* 허프만 부호가 꽉 차지 않음 */
    jutbits = 16 - tablebits;
    for (i = 1; (int)i <= tablebits; i++) { start[i] >>= jutbits; weight[i] = (uint16_t)(1u << (tablebits - i)); }
    while (i <= 16) { weight[i] = (uint16_t)(1u << (16 - i)); i++; }
    i = start[tablebits + 1] >> jutbits;
    if (i != 0) { k = 1u << tablebits; while (i != k) table[i++] = 0; }
    avail = (unsigned)nchar; mask = 1u << (15 - tablebits);
    for (ch = 0; (int)ch < nchar; ch++) {
        if ((len = bitlen[ch]) == 0) continue;
        nextcode = start[len] + weight[len];
        if ((int)len <= tablebits) {
            if (nextcode > (unsigned)tablesize) return -1;
            for (i = start[len]; i < nextcode; i++) table[i] = (uint16_t)ch;
        } else {
            k = start[len];
            p = &table[k >> jutbits];
            i = len - (unsigned)tablebits;
            while (i != 0) {
                if (*p == 0) { if (avail >= 2 * A_NC - 2) return -1; aj_right[avail] = aj_left[avail] = 0; *p = (uint16_t)avail++; }
                if (*p >= 2 * A_NC) return -1;
                p = (k & mask) ? &aj_right[*p] : &aj_left[*p];
                k <<= 1; i--;
            }
            *p = (uint16_t)ch;
        }
        start[len] = (uint16_t)nextcode;
    }
    return 0;
}

static int aj_read_pt_len(int nn, int nbit, int i_special)
{
    int i, n, c;
    n = (int)aj_get(nbit);
    if (n == 0) {
        c = (int)aj_get(nbit);
        for (i = 0; i < nn; i++) aj_plen[i] = 0;
        for (i = 0; i < 256; i++) aj_ptab[i] = (uint16_t)c;
    } else {
        if (n > nn) return -1;
        i = 0;
        while (i < n) {
            uint32_t bb = aj_peek16();
            c = (int)(bb >> 13);
            if (c == 7) {
                uint32_t mask = 1u << 12;
                while (mask & bb) { mask >>= 1; c++; }
            }
            aj_skip(c < 7 ? 3 : c - 3);
            if (c > 16) return -1;
            aj_plen[i++] = (uint8_t)c;
            if (i == i_special) {
                c = (int)aj_get(2);
                while (--c >= 0 && i < nn) aj_plen[i++] = 0;
            }
        }
        while (i < nn) aj_plen[i++] = 0;
        if (aj_make_table(nn, aj_plen, 8, aj_ptab, 256)) return -1;
    }
    return 0;
}

static int aj_read_c_len(void)
{
    int i, c, n;
    n = (int)aj_get(A_CBIT);
    if (n == 0) {
        c = (int)aj_get(A_CBIT);
        for (i = 0; i < A_NC; i++) aj_clen[i] = 0;
        for (i = 0; i < 4096; i++) aj_ctab[i] = (uint16_t)c;
    } else {
        if (n > A_NC) return -1;
        i = 0;
        while (i < n) {
            uint32_t bb = aj_peek16();
            c = aj_ptab[bb >> 8];
            if (c >= A_NT) {
                uint32_t mask = 1u << 7;
                do { c = (bb & mask) ? aj_right[c] : aj_left[c]; mask >>= 1; } while (c >= A_NT && mask);
                if (c >= A_NT) return -1;
            }
            aj_skip(aj_plen[c]);
            if (c <= 2) {
                if (c == 0) c = 1;
                else if (c == 1) c = (int)aj_get(4) + 3;
                else c = (int)aj_get(A_CBIT) + 20;
                while (--c >= 0 && i < A_NC) aj_clen[i++] = 0;
            } else aj_clen[i++] = (uint8_t)(c - 2);
        }
        while (i < A_NC) aj_clen[i++] = 0;
        if (aj_make_table(A_NC, aj_clen, 12, aj_ctab, 4096)) return -1;
    }
    return 0;
}

static int aj_decode_c(void)
{
    uint32_t bb = aj_peek16();
    int j = aj_ctab[bb >> 4];
    if (j >= A_NC) {
        uint32_t mask = 1u << 3;
        do { j = (bb & mask) ? aj_right[j] : aj_left[j]; mask >>= 1; } while (j >= A_NC && mask);
        if (j >= A_NC) return -1;
    }
    aj_skip(aj_clen[j]);
    return j;
}
static int aj_decode_p(void)
{
    uint32_t bb = aj_peek16();
    int j = aj_ptab[bb >> 8];
    if (j >= aj_np) {
        uint32_t mask = 1u << 7;
        do { j = (bb & mask) ? aj_right[j] : aj_left[j]; mask >>= 1; } while (j >= aj_np && mask);
        if (j >= aj_np) return -1;
    }
    aj_skip(aj_plen[j]);
    if (j != 0) { j--; j = (int)((1u << j) + aj_get(j)); }
    return j;
}

static int aj_copy(uint32_t dist, uint32_t n)                   /* 이미 나온 글자에서 dist 앞부터 n 바이트 */
{
    if (dist == 0 || dist > aj.outn || n > aj.outmax - aj.outn) return -1;
    while (n--) { aj.out[aj.outn] = aj.out[aj.outn - dist]; aj.outn++; }
    return 0;
}

static int aj_decode(uint32_t origsize)                         /* 방법 1~3 */
{
    uint32_t blocksize = 0;
    while (aj.outn < origsize) {
        if (aj.err) return -1;
        if (blocksize == 0) {
            blocksize = aj_get(16);
            if (aj_read_pt_len(A_NT, 5, 3) || aj_read_c_len() || aj_read_pt_len(aj_np, aj_pbit, -1)) return -1;
        }
        blocksize--;
        int c = aj_decode_c();
        if (c < 0) return -1;
        if (c <= 255) {
            if (aj.outn >= aj.outmax) return -1;
            aj.out[aj.outn++] = (uint8_t)c;
        } else {
            uint32_t n = (uint32_t)c - (256 - A_THRESHOLD);
            int i = aj_decode_p();
            if (i < 0 || aj_copy((uint32_t)i + 1, n)) return -1;
        }
    }
    return aj.outn == origsize ? 0 : -1;
}

static uint32_t aj_fast_len(void)                               /* 방법 4: 길이 (0 = 글자 하나) */
{
    uint32_t plus = 0, pwr = 1; int width;
    for (width = 0; width < 7; width++) { if (aj_get(1) == 0) break; plus += pwr; pwr <<= 1; }
    return (width ? aj_get(width) : 0) + plus;
}
static uint32_t aj_fast_ptr(void)
{
    uint32_t plus = 0, pwr = 1u << 9; int width;
    for (width = 9; width < 13; width++) { if (aj_get(1) == 0) break; plus += pwr; pwr <<= 1; }
    return (width ? aj_get(width) : 0) + plus;
}
static int aj_decode_f(uint32_t origsize)
{
    while (aj.outn < origsize) {
        if (aj.err) return -1;
        uint32_t c = aj_fast_len();
        if (c == 0) {
            if (aj.outn >= aj.outmax) return -1;
            aj.out[aj.outn++] = (uint8_t)aj_get(8);
        } else {
            uint32_t n = c - 1 + A_THRESHOLD, i = aj_fast_ptr();
            if (aj_copy(i + 1, n)) return -1;
        }
    }
    return aj.outn == origsize ? 0 : -1;
}

/* ---- PMA 풀기 ---- */
typedef struct { const uint8_t *in; uint32_t pos, len; } PmCtx;
static size_t pm_cb(void *buf, size_t n, void *ud)
{
    PmCtx *c = ud;
    size_t k = c->len - c->pos;
    if (k > n) k = n;
    memcpy(buf, c->in + c->pos, k);
    c->pos += (uint32_t)k;
    return k;
}
static uint64_t pm_state[4096];                                 /* lhasa 해독기 상태 32 KiB (pm1 17 KB 가 가장 큼. lh4~lh7 은 위 aj 엔진이라 따로 안 씀) */
static uint32_t crc32_tab[256];
static uint32_t crc32_of(const uint8_t *p, uint32_t n)           /* 표준 CRC32 (ARJ·zip 과 같음) */
{
    if (!crc32_tab[1]) for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : c >> 1; crc32_tab[i] = c; }
    uint32_t c = 0xFFFFFFFFu;
    while (n--) c = crc32_tab[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return ~c;
}
static uint16_t crc16_lha(const uint8_t *p, uint32_t n)
{
    uint16_t c = 0;
    while (n--) { c ^= *p++; for (int i = 0; i < 8; i++) c = (uint16_t)((c & 1) ? (c >> 1) ^ 0xA001 : c >> 1); }
    return c;
}
static int pm_decode(const ArcEnt *e, const uint8_t *a, uint8_t *out, uint32_t max)
{
    const LHADecoderType *t;
    if (e->method >= M_LH4 && e->method <= M_LH7) {              /* lh4~lh7 은 ARJ 와 같은 뼈대 (LZ77 + 허프만): 위쪽 aj_decode 를 거리 부호 수만 바꿔 씀 */
        memset(&aj, 0, sizeof aj);
        aj.in = a + e->data; aj.inlen = e->csize; aj.out = out; aj.outmax = max;
        aj_np = e->method == M_LH7 ? 18 : e->method == M_LH6 ? 17 : 15;
        aj_pbit = e->method == M_LH5 || e->method == M_LH4 ? 4 : 5;
        int r = aj_decode(e->usize);
        aj_np = 17; aj_pbit = 5;
        return r;
    }
    switch (e->method) {
    case M_PM1: t = &lha_pm1_decoder; break;
    case M_PM2: t = &lha_pm2_decoder; break;
    case M_LH1: t = &lha_lh1_decoder; break;
    case M_LZS: t = &lha_lzs_decoder; break;
    case M_LZ5: t = &lha_lz5_decoder; break;
    default: return -1;
    }
    PmCtx c = { a + e->data, 0, e->csize };
    static uint8_t buf[4096];                            /* 해독기가 한 번에 내놓는 최대 (lh1 4096) */
    if (t->extra_size > sizeof pm_state || t->max_read > sizeof buf) return -1;
    memset(pm_state, 0, t->extra_size);
    if (!t->init(pm_state, pm_cb, &c)) return -1;
    uint32_t n = 0;
    while (n < e->usize) {
        size_t k = t->read(pm_state, buf);
        if (!k) return -1;
        if (k > e->usize - n) k = e->usize - n;
        if (n + k > max) return -1;
        memcpy(out + n, buf, k);
        n += (uint32_t)k;
    }
    return 0;
}

/* 항목 하나 풀기: 0 = 성공 */
static int unpack(const uint8_t *a, const ArcEnt *e, uint8_t *out, uint32_t max)
{
    if (e->usize > max) return ZIPX_BIG;
    if (e->bad) return ZIPX_METHOD;
    if (e->kind == 1) {
        if (e->method == 0) {
            if (e->csize != e->usize) return ZIPX_BAD;
            memcpy(out, a + e->data, e->usize);
        } else if (e->method >= 1 && e->method <= 4) {
            memset(&aj, 0, sizeof aj);
            aj.in = a + e->data; aj.inlen = e->csize; aj.out = out; aj.outmax = max;
            if ((e->method == 4 ? aj_decode_f(e->usize) : aj_decode(e->usize))) return ZIPX_BAD;
        } else return ZIPX_METHOD;
        if (crc32_of(out, e->usize) != e->crc) return ZIPX_BAD;
    } else {
        if (e->dir) { if (e->usize) return ZIPX_BAD; }
        else if (e->method == M_STORE) {
            if (e->csize != e->usize) return ZIPX_BAD;
            memcpy(out, a + e->data, e->usize);
        } else if (e->method != M_BAD) {
            if (pm_decode(e, a, out, max)) return ZIPX_BAD;
        } else return ZIPX_METHOD;
        if (crc16_lha(out, e->usize) != (uint16_t)e->crc) return ZIPX_BAD;
    }
    return 0;
}

/* ---- zipx.c 와 같은 모양의 함수들 ---- */
static int ext_match(const ArcEnt *e, const char *exts)
{
    if (!exts || !exts[0]) return 1;
    uint32_t nl = e->nlen, d = nl;
    while (d > 0 && e->name[d - 1] != '.' && !slash(e->name[d - 1])) d--;
    if (!d || e->name[d - 1] != '.') return 0;
    const char *x = exts;
    while (*x) {
        const char *s = x;
        while (*x && *x != '|') x++;
        uint32_t el = (uint32_t)(x - s);
        if (el == nl - d) {
            uint32_t i = 0;
            while (i < el && lower(e->name[d + i]) == lower(s[i])) i++;
            if (i == el) return 1;
        }
        if (*x == '|') x++;
    }
    return 0;
}
static uint32_t base_off(const ArcEnt *e) { uint32_t b = e->nlen; while (b > 0 && !slash(e->name[b - 1])) b--; return b; }
static void put_base(const ArcEnt *e, char *name, int namemax)
{
    if (!name || namemax <= 0) return;
    int k = 0;
    for (uint32_t j = base_off(e); j < e->nlen && k < namemax - 1; j++) name[k++] = (char)e->name[j];
    name[k] = 0;
}
static int ci_cmp(const ArcEnt *a, const ArcEnt *b)
{
    uint32_t i = 0;
    while (i < a->nlen && i < b->nlen) { int x = lower(a->name[i]), y = lower(b->name[i]); if (x != y) return x - y; i++; }
    return (int)a->nlen - (int)b->nlen;
}
static int pick(const char *exts, int *pos)                     /* 맞는 파일을 이름순으로 pos[] 에, 최대 64 */
{
    int n = 0;
    for (int i = 0; i < tabn; i++) {
        const ArcEnt *e = &tab[i];
        if (e->dir || !ext_match(e, exts) || n >= 64) continue;
        int k = n++;
        while (k > 0 && ci_cmp(&tab[pos[k - 1]], e) > 0) { pos[k] = pos[k - 1]; k--; }
        pos[k] = i;
    }
    return n;
}

int arc_extract(const uint8_t *a, uint32_t alen, const char *exts, uint8_t *out, uint32_t max, uint32_t *size, char *name, int namemax)
{
    int r = scan(a, alen);
    if (r) return r;
    for (int i = 0; i < tabn; i++) {
        const ArcEnt *e = &tab[i];
        if (e->dir || !ext_match(e, exts)) continue;
        put_base(e, name, namemax);
        r = unpack(a, e, out, max);
        if (r) return r;
        *size = e->usize;
        return ZIPX_OK;
    }
    return ZIPX_NONE;
}
int arc_extract_nth(const uint8_t *a, uint32_t alen, const char *exts, int nth, uint8_t *out, uint32_t max, uint32_t *size, char *name, int namemax, int *total)
{
    int r = scan(a, alen), pos[64];
    if (r) return r;
    int n = pick(exts, pos);
    if (total) *total = n;
    if (!n) return ZIPX_NONE;
    const ArcEnt *e = &tab[pos[((nth % n) + n) % n]];
    put_base(e, name, namemax);
    r = unpack(a, e, out, max);
    if (r) return r;
    *size = e->usize;
    return ZIPX_OK;
}
int arc_names_ex(const uint8_t *a, uint32_t alen, const char *exts, char (*names)[48], uint32_t *usize, uint32_t *dos, int max)
{
    int r = scan(a, alen), pos[64];
    if (r) return r;
    int n = pick(exts, pos);
    for (int i = 0; i < n && i < max; i++) {
        const ArcEnt *e = &tab[pos[i]];
        uint32_t b = base_off(e), nl = e->nlen, fl = nl - b, ex = 0;
        int k = 0;
        if (fl > 47) { for (uint32_t j = nl; j > b + 1 && nl - j < 8; j--) if (e->name[j - 1] == '.') { ex = nl - j + 1; break; } }
        if (ex) {
            for (uint32_t j = b; j < nl - ex && k < 47 - (int)ex; j++) names[i][k++] = (char)e->name[j];
            for (uint32_t j = nl - ex; j < nl && k < 47; j++) names[i][k++] = (char)e->name[j];
        } else
            for (uint32_t j = b; j < nl && k < 47; j++) names[i][k++] = (char)e->name[j];
        names[i][k] = 0;
        if (usize) usize[i] = e->usize;
        if (dos) dos[i] = e->dos;                         /* 날짜 16비트 << 16 | 시간 16비트 (zip 과 같은 꼴) */
    }
    return n < max ? n : max;
}
int arc_peek(const uint8_t *a, uint32_t alen, const char *exts, char *name, int namemax, uint32_t *usize_out)
{
    int r = scan(a, alen);
    if (r) return r;
    for (int i = 0; i < tabn; i++) {
        const ArcEnt *e = &tab[i];
        if (e->dir || !ext_match(e, exts)) continue;
        put_base(e, name, namemax);
        *usize_out = e->usize;
        return 0;
    }
    return ZIPX_NONE;
}
int arc_list(const uint8_t *a, uint32_t alen, const char *dir, ZipListCb cb, void *ctx)
{
    int r = scan(a, alen);
    if (r) return r;
    uint32_t pl = 0;
    while (dir[pl]) pl++;
    for (int i = 0; i < tabn; i++) {
        const ArcEnt *e = &tab[i];
        const uint8_t *nm = e->name; uint32_t nl = e->nlen, o = 0;
        if (pl) {
            if (nl <= pl + 1) continue;
            uint32_t k = 0;
            while (k < pl && lower(nm[k]) == lower((uint8_t)dir[k])) k++;
            if (k < pl || !slash(nm[pl])) continue;
            o = pl + 1;
        }
        uint32_t k = o;
        while (k < nl && !slash(nm[k])) k++;
        if (k == o) continue;
        ZipEnt z;
        uint32_t L = k - o;
        if (L > 255) L = 255;
        for (uint32_t q = 0; q < L; q++) z.name[q] = (char)nm[o + q];
        z.name[L] = 0;
        z.dir = (uint8_t)(k < nl || e->dir);
        z.size = z.dir ? 0 : e->usize;
        uint32_t dt = e->dos >> 16, tm = e->dos & 0xFFFF;
        z.year = (uint16_t)(1980 + (dt >> 9)); z.mon = (uint8_t)((dt >> 5) & 15); z.day = (uint8_t)(dt & 31);
        z.hour = (uint8_t)(tm >> 11); z.min = (uint8_t)((tm >> 5) & 63);
        if (!z.mon || !z.day) z.year = 0;
        if (cb(ctx, &z)) break;
    }
    return 0;
}
int arc_read(const uint8_t *a, uint32_t alen, const char *path, uint8_t *out, uint32_t max, uint32_t *size)
{
    int r = scan(a, alen);
    if (r) return r;
    uint32_t pl = 0;
    while (path[pl]) pl++;
    for (int i = 0; i < tabn; i++) {
        const ArcEnt *e = &tab[i];
        if (e->nlen != pl || e->dir) continue;
        uint32_t k = 0;
        while (k < pl && (slash(e->name[k]) ? slash((uint8_t)path[k]) : lower(e->name[k]) == lower((uint8_t)path[k]))) k++;
        if (k < pl) continue;
        r = unpack(a, e, out, max);
        if (r) return r;
        *size = e->usize;
        return ZIPX_OK;
    }
    return ZIPX_NONE;
}
