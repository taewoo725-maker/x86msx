/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* smbios.c - 5.2.26 (smbios.h) */
#include "smbios.h"

static int eq(const uint8_t *p, const char *s, int n) { for (int i = 0; i < n; i++) if (p[i] != (uint8_t)s[i]) return 0; return 1; }
static uint32_t r32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static const char *str_of(const uint8_t *st, int idx)  /* 구조 뒤 문자열 묶음의 idx 번째 (1부터) */
{
    if (!idx) return "";
    const char *s = (const char *)st + st[1];
    for (int i = 1; i < idx && *s; i++) { while (*s) s++; s++; }
    return *s ? s : "";
}
static void add(char *d, int *k, const char *s)
{
    if (*k && *k < 62) d[(*k)++] = ' ';
    for (; s && *s && *k < 63; s++) { char c = *s; if (c < 32 || c > 126) c = '?'; d[(*k)++] = c; }
    while (*k && d[*k - 1] == ' ') (*k)--;
    d[*k] = 0;
}

int smbios_lines(const uint8_t *ep, char (*lines)[64], int max)
{
    if (!ep || max <= 0) return 0;
    uint64_t addr; uint32_t len;
    if (eq(ep, "_SM3_", 5)) { addr = (uint64_t)r32(ep + 0x10) | (uint64_t)r32(ep + 0x14) << 32; len = r32(ep + 0x0C); }
    else if (eq(ep, "_SM_", 4) && eq(ep + 0x10, "_DMI_", 5)) { addr = r32(ep + 0x18); len = (uint32_t)(ep[0x16] | ep[0x17] << 8); }
    else return 0;
    if (!addr || !len || len > 0x100000) return 0;
    if (sizeof(void *) < 8 && (addr >> 32)) return 0;
    const uint8_t *p = (const uint8_t *)(uintptr_t)addr, *end = p + len;
    int n = 0;
    char sys[64] = "", bios[64] = "", board[64] = "";
    int ks = 0, kb = 0, kd = 0;
    for (int guard = 0; p + 4 <= end && guard < 512; guard++) {
        uint8_t ty = p[0], fl = p[1];
        if (fl < 4) break;
        if (ty == 0 && !kb && fl >= 9) { add(bios, &kb, str_of(p, p[4])); add(bios, &kb, str_of(p, p[5])); add(bios, &kb, str_of(p, p[8])); }
        if (ty == 1 && !ks && fl >= 8) { add(sys, &ks, str_of(p, p[4])); add(sys, &ks, str_of(p, p[5])); }
        if (ty == 2 && !kd && fl >= 6) { add(board, &kd, str_of(p, p[4])); add(board, &kd, str_of(p, p[5])); }
        if (ty == 127) break;
        const uint8_t *s = p + fl;                                  /* 문자열 묶음 끝 (0 두 개) */
        while (s + 1 < end && !(s[0] == 0 && s[1] == 0)) s++;
        p = s + 2;
    }
    static const char *const pre[3] = { "SMBIOS system ", "SMBIOS bios ", "SMBIOS board " };
    const char *val[3] = { sys, bios, board };
    for (int i = 0; i < 3 && n < max; i++) {
        if (!val[i][0]) continue;
        int k = 0; for (const char *q = pre[i]; *q; q++) lines[n][k++] = *q;
        for (const char *q = val[i]; *q && k < 63; q++) lines[n][k++] = *q;
        lines[n][k] = 0; n++;
    }
    return n;
}

const uint8_t *smbios_find_legacy(void)
{
#if defined(__i386__)
    for (uintptr_t a = 0xF0000; a < 0x100000; a += 16) {
        const uint8_t *p = (const uint8_t *)a;
        if (eq(p, "_SM3_", 5) || (eq(p, "_SM_", 4) && eq(p + 0x10, "_DMI_", 5))) return p;
    }
#endif
    return 0;
}
