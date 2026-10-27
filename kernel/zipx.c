/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* zipx.c - 3.2.0 zip 안의 파일 꺼내기 (요구사항 MED-05). 설명은 zipx.h */
#include "zipx.h"
#include "../third_party/tinf/tinf.h"
#include <string.h>

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (rd16(p + 2) << 16); }

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int ext_match(const uint8_t *name, uint32_t nl, const char *exts)
{
    if (!exts || !exts[0]) return 1;
    uint32_t d = nl;
    while (d > 0 && name[d - 1] != '.' && name[d - 1] != '/') d--;
    if (!d || name[d - 1] != '.') return 0;
    const char *e = exts;
    while (*e) {
        const char *s = e;
        while (*e && *e != '|') e++;
        uint32_t el = (uint32_t)(e - s);
        if (el == nl - d) {
            uint32_t i = 0;
            while (i < el && lower(name[d + i]) == lower(s[i])) i++;
            if (i == el) return 1;
        }
        if (*e == '|') e++;
    }
    return 0;
}

int zip_extract(const uint8_t *zip, uint32_t zlen, const char *exts, uint8_t *out, uint32_t max, uint32_t *size, char *name, int namemax)
{
    if (zlen < 22) return ZIPX_BAD;
    /* 끝 쪽 중앙 디렉터리 끝 기록 (PK 5 6) */
    int64_t e = (int64_t)zlen - 22, stop = e - 65536;
    if (stop < 0) stop = 0;
    while (e >= stop && !(zip[e] == 'P' && zip[e + 1] == 'K' && zip[e + 2] == 5 && zip[e + 3] == 6)) e--;
    if (e < stop) return ZIPX_BAD;
    uint32_t count = rd16(zip + e + 10), cd = rd32(zip + e + 16);
    int found = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (cd + 46 > zlen || rd32(zip + cd) != 0x02014B50u) return ZIPX_BAD;
        uint32_t method = rd16(zip + cd + 10), csize = rd32(zip + cd + 20), usize = rd32(zip + cd + 24);
        uint32_t nl = rd16(zip + cd + 28), xl = rd16(zip + cd + 30), cl = rd16(zip + cd + 32), lho = rd32(zip + cd + 42);
        const uint8_t *nm = zip + cd + 46;
        cd += 46 + nl + xl + cl;
        if (nl && nm[nl - 1] == '/') continue;                  /* 폴더 */
        if (!ext_match(nm, nl, exts)) continue;
        found = 1;
        if (name && namemax > 0) {                              /* 경로를 뺀 이름 */
            uint32_t b = nl;
            while (b > 0 && nm[b - 1] != '/') b--;
            int k = 0;
            for (uint32_t j = b; j < nl && k < namemax - 1; j++) name[k++] = (char)nm[j];
            name[k] = 0;
        }
        if (usize > max) return ZIPX_BIG;
        if (lho + 30 > zlen || rd32(zip + lho) != 0x04034B50u) return ZIPX_BAD;
        uint32_t data = lho + 30 + rd16(zip + lho + 26) + rd16(zip + lho + 28);
        if (data + csize > zlen) return ZIPX_BAD;
        if (method == 0) {
            if (csize != usize) return ZIPX_BAD;
            memcpy(out, zip + data, usize);
        } else if (method == 8) {
            unsigned int dl = max;
            if (tinf_uncompress(out, &dl, zip + data, csize) != TINF_OK || dl != usize) return ZIPX_BAD;
        } else return ZIPX_METHOD;
        *size = usize;
        return ZIPX_OK;
    }
    return found ? ZIPX_BAD : ZIPX_NONE;
}

/* ---- 4.0.3: 폴더처럼 보기 ---- */
static int cd_find(const uint8_t *zip, uint32_t zlen, uint32_t *count, uint32_t *cd)
{
    if (zlen < 22) return ZIPX_BAD;
    int64_t e = (int64_t)zlen - 22, stop = e - 65536;
    if (stop < 0) stop = 0;
    while (e >= stop && !(zip[e] == 'P' && zip[e + 1] == 'K' && zip[e + 2] == 5 && zip[e + 3] == 6)) e--;
    if (e < stop) return ZIPX_BAD;
    *count = rd16(zip + e + 10); *cd = rd32(zip + e + 16);
    return 0;
}
static int slash(int c) { return c == '/' || c == '\\'; }

int zip_list(const uint8_t *zip, uint32_t zlen, const char *dir, ZipListCb cb, void *ctx)
{
    uint32_t count, cd;
    if (cd_find(zip, zlen, &count, &cd)) return ZIPX_BAD;
    uint32_t pl = 0;
    while (dir[pl]) pl++;
    for (uint32_t i = 0; i < count; i++) {
        if (cd + 46 > zlen || rd32(zip + cd) != 0x02014B50u) return ZIPX_BAD;
        uint32_t usize = rd32(zip + cd + 24), nl = rd16(zip + cd + 28), xl = rd16(zip + cd + 30), cl = rd16(zip + cd + 32);
        uint32_t tm = rd16(zip + cd + 12), dt = rd16(zip + cd + 14);
        const uint8_t *nm = zip + cd + 46;
        cd += 46 + nl + xl + cl;
        uint32_t o = 0;
        if (pl) {                                               /* dir + "/" 로 시작해야 */
            if (nl <= pl + 1) continue;
            uint32_t k = 0;
            while (k < pl && lower(nm[k]) == lower((uint8_t)dir[k])) k++;
            if (k < pl || !slash(nm[pl])) continue;
            o = pl + 1;
        }
        uint32_t k = o;
        while (k < nl && !slash(nm[k])) k++;
        if (k == o) continue;                                    /* 폴더 자신 */
        ZipEnt z;
        uint32_t L = k - o;
        if (L > 255) L = 255;
        for (uint32_t q = 0; q < L; q++) z.name[q] = (char)nm[o + q];
        z.name[L] = 0;
        z.dir = k < nl;                                          /* 빗금이 있으면 폴더 (끝 빗금 = 폴더 칸) */
        z.size = z.dir ? 0 : usize;
        z.year = (uint16_t)(1980 + (dt >> 9)); z.mon = (uint8_t)((dt >> 5) & 15); z.day = (uint8_t)(dt & 31);
        z.hour = (uint8_t)(tm >> 11); z.min = (uint8_t)((tm >> 5) & 63);
        if (!z.mon || !z.day) z.year = 0;                       /* 날짜 모름 */
        if (cb(ctx, &z)) break;
    }
    return 0;
}

int zip_read(const uint8_t *zip, uint32_t zlen, const char *path, uint8_t *out, uint32_t max, uint32_t *size)
{
    uint32_t count, cd;
    if (cd_find(zip, zlen, &count, &cd)) return ZIPX_BAD;
    uint32_t pl = 0;
    while (path[pl]) pl++;
    for (uint32_t i = 0; i < count; i++) {
        if (cd + 46 > zlen || rd32(zip + cd) != 0x02014B50u) return ZIPX_BAD;
        uint32_t method = rd16(zip + cd + 10), csize = rd32(zip + cd + 20), usize = rd32(zip + cd + 24);
        uint32_t nl = rd16(zip + cd + 28), xl = rd16(zip + cd + 30), cl = rd16(zip + cd + 32), lho = rd32(zip + cd + 42);
        const uint8_t *nm = zip + cd + 46;
        cd += 46 + nl + xl + cl;
        if (nl != pl) continue;
        uint32_t k = 0;
        while (k < nl && (slash(nm[k]) ? slash((uint8_t)path[k]) : lower(nm[k]) == lower((uint8_t)path[k]))) k++;
        if (k < nl) continue;
        if (usize > max) return ZIPX_BIG;
        if (lho + 30 > zlen || rd32(zip + lho) != 0x04034B50u) return ZIPX_BAD;
        uint32_t data = lho + 30 + rd16(zip + lho + 26) + rd16(zip + lho + 28);
        if (data + csize > zlen) return ZIPX_BAD;
        if (method == 0) {
            if (csize != usize) return ZIPX_BAD;
            memcpy(out, zip + data, usize);
        } else if (method == 8) {
            unsigned int dl = max;
            if (tinf_uncompress(out, &dl, zip + data, csize) != TINF_OK || dl != usize) return ZIPX_BAD;
        } else return ZIPX_METHOD;
        *size = usize;
        return ZIPX_OK;
    }
    return ZIPX_NONE;
}

/* 4.0.34: 꺼내지 않고 첫 맞는 파일의 이름·풀린 크기만 (목록 표시용). 0 = 있음 */
int zip_peek(const uint8_t *zip, uint32_t zlen, const char *exts, char *name, int namemax, uint32_t *usize_out)
{
    if (zlen < 22) return ZIPX_BAD;
    int64_t e = (int64_t)zlen - 22, stop = e - 65536;
    if (stop < 0) stop = 0;
    while (e >= stop && !(zip[e] == 'P' && zip[e + 1] == 'K' && zip[e + 2] == 5 && zip[e + 3] == 6)) e--;
    if (e < stop) return ZIPX_BAD;
    uint32_t count = rd16(zip + e + 10), cd = rd32(zip + e + 16);
    for (uint32_t i = 0; i < count; i++) {
        if (cd + 46 > zlen || rd32(zip + cd) != 0x02014B50u) return ZIPX_BAD;
        uint32_t usize = rd32(zip + cd + 24);
        uint32_t nl = rd16(zip + cd + 28), xl = rd16(zip + cd + 30), cl = rd16(zip + cd + 32);
        const uint8_t *nm = zip + cd + 46;
        cd += 46 + nl + xl + cl;
        if (nl && nm[nl - 1] == '/') continue;
        if (!ext_match(nm, nl, exts)) continue;
        uint32_t b = nl;
        while (b > 0 && nm[b - 1] != '/') b--;
        int k = 0;
        for (uint32_t j = b; j < nl && k < namemax - 1; j++) name[k++] = (char)nm[j];
        name[k] = 0;
        *usize_out = usize;
        return 0;
    }
    return ZIPX_NONE;
}

/* 4.0.34: 맞는 파일이 여러 개면 이름순(대소문자 무시)으로 정렬해 nth번째(총 개수로 나눈 나머지)를 꺼냄. total = 맞는 파일 개수 */
static int ci_cmp(const uint8_t *a, uint32_t al, const uint8_t *b, uint32_t bl)
{
    uint32_t i = 0;
    while (i < al && i < bl) { int x = lower(a[i]), y = lower(b[i]); if (x != y) return x - y; i++; }
    return (int)al - (int)bl;
}
int zip_extract_nth(const uint8_t *zip, uint32_t zlen, const char *exts, int nth, uint8_t *out, uint32_t max, uint32_t *size, char *name, int namemax, int *total)
{
    uint32_t count, cd;
    if (cd_find(zip, zlen, &count, &cd)) return ZIPX_BAD;
    uint32_t pos[64]; int n = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (cd + 46 > zlen || rd32(zip + cd) != 0x02014B50u) return ZIPX_BAD;
        uint32_t nl = rd16(zip + cd + 28), xl = rd16(zip + cd + 30), cl = rd16(zip + cd + 32);
        const uint8_t *nm = zip + cd + 46;
        uint32_t here = cd;
        cd += 46 + nl + xl + cl;
        if (nl && nm[nl - 1] == '/') continue;
        if (!ext_match(nm, nl, exts) || n >= 64) continue;
        int k = n++;                                            /* 이름순 끼워 넣기 */
        while (k > 0) {
            const uint8_t *pn = zip + pos[k - 1] + 46; uint32_t pl = rd16(zip + pos[k - 1] + 28);
            if (ci_cmp(pn, pl, nm, nl) <= 0) break;
            pos[k] = pos[k - 1]; k--;
        }
        pos[k] = here;
    }
    if (total) *total = n;
    if (!n) return ZIPX_NONE;
    uint32_t c = pos[((nth % n) + n) % n];
    uint32_t method = rd16(zip + c + 10), csize = rd32(zip + c + 20), usize = rd32(zip + c + 24);
    uint32_t nl = rd16(zip + c + 28), lho = rd32(zip + c + 42);
    const uint8_t *nm = zip + c + 46;
    if (name && namemax > 0) {
        uint32_t b = nl;
        while (b > 0 && nm[b - 1] != '/') b--;
        int k = 0;
        for (uint32_t j = b; j < nl && k < namemax - 1; j++) name[k++] = (char)nm[j];
        name[k] = 0;
    }
    if (usize > max) return ZIPX_BIG;
    if (lho + 30 > zlen || rd32(zip + lho) != 0x04034B50u) return ZIPX_BAD;
    uint32_t data = lho + 30 + rd16(zip + lho + 26) + rd16(zip + lho + 28);
    if (data + csize > zlen) return ZIPX_BAD;
    if (method == 0) {
        if (csize != usize) return ZIPX_BAD;
        memcpy(out, zip + data, usize);
    } else if (method == 8) {
        unsigned int dl = max;
        if (tinf_uncompress(out, &dl, zip + data, csize) != TINF_OK || dl != usize) return ZIPX_BAD;
    } else return ZIPX_METHOD;
    *size = usize;
    return ZIPX_OK;
}

/* 4.0.34: zip 안에서 exts가 맞는 파일 이름들 (zip_extract_nth와 같은 이름순). names[i] = 폴더 뺀 이름. 돌려주는 값 = 개수 (음수 = 오류)
 * 4.0.136: usize/dos(날짜 16비트 << 16 | 시간 16비트)가 있으면 풀었을 때 크기와 수정 시각도 돌려줌 */
int zip_names_ex(const uint8_t *zip, uint32_t zlen, const char *exts, char (*names)[48], uint32_t *usize, uint32_t *dos, int max)
{
    uint32_t count, cd;
    if (cd_find(zip, zlen, &count, &cd)) return ZIPX_BAD;
    uint32_t pos[64]; int n = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (cd + 46 > zlen || rd32(zip + cd) != 0x02014B50u) return ZIPX_BAD;
        uint32_t nl = rd16(zip + cd + 28), xl = rd16(zip + cd + 30), cl = rd16(zip + cd + 32);
        const uint8_t *nm = zip + cd + 46;
        uint32_t here = cd;
        cd += 46 + nl + xl + cl;
        if (nl && nm[nl - 1] == '/') continue;
        if (!ext_match(nm, nl, exts) || n >= 64) continue;
        int k = n++;
        while (k > 0) {
            const uint8_t *pn = zip + pos[k - 1] + 46; uint32_t pl = rd16(zip + pos[k - 1] + 28);
            if (ci_cmp(pn, pl, nm, nl) <= 0) break;
            pos[k] = pos[k - 1]; k--;
        }
        pos[k] = here;
    }
    for (int i = 0; i < n && i < max; i++) {
        const uint8_t *nm = zip + pos[i] + 46; uint32_t nl = rd16(zip + pos[i] + 28), b = nl;
        while (b > 0 && nm[b - 1] != '/') b--;
        int k = 0;
        uint32_t fl = nl - b, ex = 0;                   /* 이름이 47자를 넘으면 확장자(.dsk 등)가 잘려 '형식' 칸이 비던 것 — 앞을 줄이고 확장자는 남김 */
        if (fl > 47) { for (uint32_t j = nl; j > b + 1 && nl - j < 8; j--) if (nm[j - 1] == '.') { ex = nl - j + 1; break; } }
        if (ex) {
            for (uint32_t j = b; j < nl - ex && k < 47 - (int)ex; j++) names[i][k++] = (char)nm[j];
            for (uint32_t j = nl - ex; j < nl && k < 47; j++) names[i][k++] = (char)nm[j];
        } else
            for (uint32_t j = b; j < nl && k < 47; j++) names[i][k++] = (char)nm[j];
        names[i][k] = 0;
        if (usize) usize[i] = rd32(zip + pos[i] + 24);
        if (dos) dos[i] = ((uint32_t)rd16(zip + pos[i] + 14) << 16) | rd16(zip + pos[i] + 12);
    }
    return n < max ? n : max;
}
int zip_names(const uint8_t *zip, uint32_t zlen, const char *exts, char (*names)[48], int max)
{
    return zip_names_ex(zip, zlen, exts, names, 0, 0, max);
}
