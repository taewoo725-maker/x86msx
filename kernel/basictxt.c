/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* basictxt.c - 3.2.7 MSX-BASIC 토큰 → 글 (설명은 basictxt.h) */
#include "basictxt.h"

/* 0x81~: 문(statement)과 연산자 */
static const char *const tok1[] = {
    "END", "FOR", "NEXT", "DATA", "INPUT", "DIM", "READ", "LET",                    /* 81 */
    "GOTO", "RUN", "IF", "RESTORE", "GOSUB", "RETURN", "REM", "STOP",               /* 89 */
    "PRINT", "CLEAR", "LIST", "NEW", "ON", "WAIT", "DEF", "POKE",                   /* 91 */
    "CONT", "CSAVE", "CLOAD", "OUT", "LPRINT", "LLIST", "CLS", "WIDTH",             /* 99 */
    "ELSE", "TRON", "TROFF", "SWAP", "ERASE", "ERROR", "RESUME", "DELETE",          /* A1 */
    "AUTO", "RENUM", "DEFSTR", "DEFINT", "DEFSNG", "DEFDBL", "LINE", "OPEN",        /* A9 */
    "FIELD", "GET", "PUT", "CLOSE", "LOAD", "MERGE", "FILES", "LSET",               /* B1 */
    "RSET", "SAVE", "LFILES", "CIRCLE", "COLOR", "DRAW", "PAINT", "BEEP",           /* B9 */
    "PLAY", "PSET", "PRESET", "SOUND", "SCREEN", "VPOKE", "SPRITE", "VDP",          /* C1 */
    "BASE", "CALL", "TIME", "KEY", "MAX", "MOTOR", "BLOAD", "BSAVE",                /* C9 */
    "DSKO$", "SET", "NAME", "KILL", "IPL", "COPY", "CMD", "LOCATE",                 /* D1 */
    "TO", "THEN", "TAB(", "STEP", "USR", "FN", "SPC(", "NOT",                       /* D9 */
    "ERL", "ERR", "STRING$", "USING", "INSTR", "'", "VARPTR", "CSRLIN",             /* E1 */
    "ATTR$", "DSKI$", "OFF", "INKEY$", "POINT", ">", "=", "<",                      /* E9 */
    "+", "-", "*", "/", "^", "AND", "OR", "XOR",                                    /* F1 */
    "EQV", "IMP", "MOD", "\\",                                                      /* F9~FC */
};
/* 0xFF 0x81~: 함수 */
static const char *const tok2[] = {
    "LEFT$", "RIGHT$", "MID$", "SGN", "INT", "ABS", "SQR", "RND",                   /* 81 */
    "SIN", "LOG", "EXP", "COS", "TAN", "ATN", "FRE", "INP",                         /* 89 */
    "POS", "LEN", "STR$", "VAL", "ASC", "CHR$", "PEEK", "VPEEK",                    /* 91 */
    "SPACE$", "OCT$", "HEX$", "LPOS", "BIN$", "CINT", "CSNG", "CDBL",               /* 99 */
    "FIX", "STICK", "STRIG", "PDL", "PAD", "DSKF", "FPOS", "CVI",                   /* A1 */
    "CVS", "CVD", "EOF", "LOC", "LOF", "MKI$", "MKS$", "MKD$",                      /* A9 */
};
#define T_REM 0x8F
#define T_DATA 0x84
#define T_ELSE 0xA1
#define T_QUOTE 0xE6

typedef struct { char *o; int n, max, err; } Out;
static void put(Out *w, char c) { if (w->n < w->max) w->o[w->n++] = c; else w->err = 1; }
static void puts_(Out *w, const char *s) { while (*s) put(w, *s++); }
static void putu(Out *w, uint32_t v)
{
    char t[12]; int k = 0;
    do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (k) put(w, t[--k]);
}

/* BCD 실수 (b[0] = 부호·지수, b[1..nb-1] = BCD 두 자리씩). MSX LIST처럼: 고정 소수점 또는 E 표기, 필요하면 ! # */
static void putreal(Out *w, const uint8_t *b, int nb, int dbl)
{
    if (!(b[0] & 0x7F)) { puts_(w, "0"); put(w, dbl ? '#' : '!'); return; }
    char d[16]; int nd = 0;
    for (int i = 1; i < nb; i++) { d[nd++] = (char)('0' + (b[i] >> 4)); d[nd++] = (char)('0' + (b[i] & 15)); }
    while (nd > 1 && d[nd - 1] == '0') nd--;
    int e = (b[0] & 0x7F) - 0x40, prec = dbl ? 14 : 6;   /* 값 = 0.d × 10^e */
    if (b[0] & 0x80) put(w, '-');
    int start = w->n, has = 0;
    if (e >= -1 && e <= prec) {                          /* 고정 소수점 */
        if (e <= 0) { put(w, '.'); for (int i = 0; i < -e; i++) put(w, '0'); for (int i = 0; i < nd; i++) put(w, d[i]); has = 1; }
        else {
            for (int i = 0; i < e; i++) put(w, i < nd ? d[i] : '0');
            if (nd > e) { put(w, '.'); for (int i = e; i < nd; i++) put(w, d[i]); has = 1; }
        }
    } else {                                             /* E 표기 */
        put(w, d[0]);
        if (nd > 1) { put(w, '.'); for (int i = 1; i < nd; i++) put(w, d[i]); }
        put(w, dbl ? 'D' : 'E');
        int x = e - 1;
        put(w, x < 0 ? '-' : '+');
        if (x < 0) x = -x;
        put(w, (char)('0' + x / 10)); put(w, (char)('0' + x % 10));
        has = 1;
    }
    (void)start;
    if (dbl && !has && nd <= 6) put(w, '#');             /* 1# 처럼: 안 붙이면 단정도로 읽힘 */
    else if (!dbl && !has) put(w, '!');                  /* 40000! 처럼: 안 붙이면 정수로 읽힘 */
}

int basic_detok_line(BasPeek pk, void *ctx, uint16_t p, char *out, int max)
{
    Out w = { out, 0, max, 0 };
    int quote = 0, data = 0, guard = 0;
    for (;;) {
        if (++guard > 1024) break;                       /* 줄이 0으로 안 끝남: 그만 */
        uint8_t c = pk(ctx, p++);
        if (!c) break;
        if (quote) { put(&w, (char)c); if (c == '"') quote = 0; continue; }
        if (c == '"') { put(&w, '"'); quote = 1; continue; }
        if (data) { if (c == ':') data = 0; put(&w, (char)c); continue; }
        if (c == ':' && pk(ctx, p) == T_ELSE) continue;                          /* :ELSE → ELSE */
        if (c == ':' && pk(ctx, p) == T_REM && pk(ctx, (uint16_t)(p + 1)) == T_QUOTE) {   /* :REM' → ' */
            put(&w, '\''); p += 2;
            for (uint8_t q; (q = pk(ctx, p)) != 0 && guard++ < 1024; p++) put(&w, (char)q);
            continue;
        }
        if (c == T_REM) {                                /* REM: 끝까지 그대로 */
            puts_(&w, "REM");
            for (uint8_t q; (q = pk(ctx, p)) != 0 && guard++ < 1024; p++) put(&w, (char)q);
            continue;
        }
        if (c >= 0x81 && c <= 0xFC) {
            puts_(&w, tok1[c - 0x81]);
            if (c == T_DATA) data = 1;
            continue;
        }
        if (c == 0xFF) {
            uint8_t f = pk(ctx, p++);
            if (f >= 0x81 && f <= 0xB0) puts_(&w, tok2[f - 0x81]);
            continue;
        }
        if (c >= 0x11 && c <= 0x1A) { put(&w, (char)('0' + c - 0x11)); continue; }
        if (c == 0x0F) { putu(&w, pk(ctx, p++)); continue; }
        if (c == 0x1C || c == 0x0B || c == 0x0C || c == 0x0E || c == 0x0D) {
            uint16_t v = (uint16_t)(pk(ctx, p) | pk(ctx, (uint16_t)(p + 1)) << 8);
            p += 2;
            if (c == 0x1C) { putu(&w, v); continue; }           /* 정수 (음수는 앞에 - 토큰) */
            if (c == 0x0E) { putu(&w, v); continue; }           /* 줄 번호 */
            if (c == 0x0D) {                                     /* RUN 뒤 줄 주소: 그 줄의 번호 */
                putu(&w, (uint16_t)(pk(ctx, (uint16_t)(v + 3)) | pk(ctx, (uint16_t)(v + 4)) << 8));
                continue;
            }
            char t[8]; int k = 0;
            puts_(&w, c == 0x0B ? "&O" : "&H");
            if (c == 0x0B) { do { t[k++] = (char)('0' + (v & 7)); v >>= 3; } while (v); }
            else { do { t[k++] = "0123456789ABCDEF"[v & 15]; v >>= 4; } while (v); }
            while (k) put(&w, t[--k]);
            continue;
        }
        if (c == 0x1D || c == 0x1F) {                    /* 단정도 4바이트, 배정도 8바이트 */
            uint8_t b[8]; int nb = c == 0x1D ? 4 : 8;
            for (int i = 0; i < nb; i++) b[i] = pk(ctx, p++);
            putreal(&w, b, nb, c == 0x1F);
            continue;
        }
        put(&w, (char)c);                                /* 글자 그대로 (변수 이름, 기호, 빈칸) */
    }
    return w.err ? -2 : w.n;
}

int basic_detok_at(BasPeek pk, void *ctx, uint16_t txttab, char *out, int max, int *lines)
{
    if (lines) *lines = 0;
    uint16_t p = txttab, last = 0;
    int n = 0, cnt = 0;
    for (;;) {
        uint16_t link = (uint16_t)(pk(ctx, p) | pk(ctx, (uint16_t)(p + 1)) << 8);
        if (!link) break;
        uint16_t ln = (uint16_t)(pk(ctx, (uint16_t)(p + 2)) | pk(ctx, (uint16_t)(p + 3)) << 8);
        if (link <= p || link >= 0xF380 || (cnt && ln <= last) || ln > 65529) return cnt ? n : -1;   /* 모양이 깨짐: 거기까지 */
        Out w = { out + n, 0, max - n, 0 };
        putu(&w, ln); put(&w, ' ');
        if (w.err) return -2;
        n += w.n;
        int k = basic_detok_line(pk, ctx, (uint16_t)(p + 4), out + n, max - n);
        if (k < 0) return -2;
        n += k;
        if (n + 2 > max) return -2;
        out[n++] = '\r'; out[n++] = '\n';
        last = ln; cnt++;
        p = link;
        if (cnt > 20000) break;
    }
    if (lines) *lines = cnt;
    return n;
}

int basic_detok(BasPeek pk, void *ctx, char *out, int max, int *lines)
{
    uint16_t txttab = (uint16_t)(pk(ctx, 0xF676) | pk(ctx, 0xF677) << 8);
    if (lines) *lines = 0;
    if (txttab < 0x8000 || txttab >= 0xF000) return -1;  /* 프로그램 자리가 RAM(8000~)이 아님 */
    return basic_detok_at(pk, ctx, txttab, out, max, lines);
}
