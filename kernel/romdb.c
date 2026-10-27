/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * romdb.c - blueMSX 롬 데이터베이스 조회
 * 항목 구조: <dump> <rom>|<megarom>|<systemrom> <type>..</type> [<start>0x4000</start>] <hash algo="sha1">..</hash> </..> </dump>
 * 해시를 감싸는 <dump>..</dump> 범위 안에서만 읽는다 (2.4.6까지는 앞쪽 "<rom"을 찾아서
 * <megarom> 항목이면 다른 게임의 매퍼를 읽는 버그가 있었음).
 */
#include "romdb.h"
#include <string.h>

static int at(const uint8_t *db, uint32_t len, uint32_t i, const char *s)
{
    uint32_t n = (uint32_t)strlen(s);
    return i + n <= len && !memcmp(db + i, s, n);
}

/* [a, b) 안에서 <tag>..</tag> 글자를 out에 */
static int field(const uint8_t *db, uint32_t a, uint32_t b, const char *open, char *out, int max)
{
    uint32_t n = (uint32_t)strlen(open);
    for (uint32_t k = a; k + n < b; k++)
        if (!memcmp(db + k, open, n)) {
            int m = 0;
            for (uint32_t j = k + n; j < b && db[j] != '<' && m < max - 1; j++) out[m++] = (char)db[j];
            out[m] = 0;
            return 1;
        }
    return 0;
}

int romdb_find(const uint8_t *db, uint32_t len, const char *hex, RomDbInfo *out)
{
    out->type[0] = 0;
    out->start = -1;
    if (!db || !len) return 0;
    for (uint32_t i = 1; i + 40 <= len; i++) {
        if (db[i] != (uint8_t)hex[0] || db[i - 1] != '>' || memcmp(db + i, hex, 40)) continue;   /* <hash ...>해시 */
        uint32_t a = i, b = i;
        while (a > 0 && !at(db, len, a, "<dump>") && i - a < 4096) a--;                /* 감싸는 <dump> */
        while (b < len && !at(db, len, b, "</dump>") && b - i < 4096) b++;
        if (!at(db, len, a, "<dump>") || !at(db, len, b, "</dump>")) return 0;
        field(db, a, b, "<type>", out->type, sizeof out->type);
        char st[16];
        if (field(db, a, b, "<start>", st, sizeof st)) {
            int v = 0;
            for (const char *p = st[0] == '0' && (st[1] == 'x' || st[1] == 'X') ? st + 2 : st; *p; p++) {
                int d = (*p >= '0' && *p <= '9') ? *p - '0' : (*p >= 'a' && *p <= 'f') ? *p - 'a' + 10 : (*p >= 'A' && *p <= 'F') ? *p - 'A' + 10 : -1;
                if (d < 0) break;
                v = v * 16 + d;
            }
            out->start = v;
        }
        return 1;
    }
    return 0;
}

/* 3.2.15: 이진 DB 이분 찾기 */
static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0; }
int romdb_bin_find(const uint8_t *db, uint32_t len, const char *hex, int *type, int *page)
{
    if (!db || len < 12 || memcmp(db, "X86ROMDB", 8)) return 0;
    uint32_t n = (uint32_t)db[8] | (uint32_t)db[9] << 8 | (uint32_t)db[10] << 16 | (uint32_t)db[11] << 24;
    if (12 + (uint64_t)n * 22 > len) return 0;
    uint8_t key[20];
    for (int i = 0; i < 20; i++) key[i] = (uint8_t)(hexv(hex[2 * i]) << 4 | hexv(hex[2 * i + 1]));
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        const uint8_t *e = db + 12 + mid * 22;
        int c = memcmp(e, key, 20);
        if (!c) { *type = e[20]; *page = e[21]; return 1; }
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    return 0;
}
