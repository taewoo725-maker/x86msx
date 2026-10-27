/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* romdbtest.c - 실제 blueMSX DB로 조회 시험 (third_party/databases) */
#include "../kernel/romdb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t *load(const char *p, uint32_t *n)
{
    FILE *f = fopen(p, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); long s = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)s); *n = (uint32_t)fread(b, 1, (size_t)s, f); fclose(f); return b;
}
static int fails, total;
static void expect(const char *name, const uint8_t *db, uint32_t n, const char *hex, int found, const char *type, int start)
{
    RomDbInfo r; int f = romdb_find(db, n, hex, &r);
    int ok = f == found && (!found || (!strcmp(r.type, type) && r.start == start));
    total++; fails += !ok;
    printf("%s %-34s found %d type '%s' start %d\n", ok ? "PASS" : "FAIL", name, f, r.type, r.start);
}
int main(void)
{
    uint32_t n1, n2;
    uint8_t *rom = load("third_party/databases/msxromdb.xml", &n1), *sys = load("third_party/databases/msxsysromdb.xml", &n2);
    if (!rom || !sys) { printf("romdbtest: databases missing\n"); return 1; }
    expect("KonamiSCC megarom (2010)", rom, n1, "be56425b2eb0a26edad1b5fd26e876cd726869cf", 1, "KonamiSCC", -1);
    expect("Normal rom with start 0x8000", rom, n1, "9a63d48c4fc05e76c9de25e5c5f2a0c99b5d1be4", 1, "Normal", 0x8000);
    expect("not in database", rom, n1, "0000000000000000000000000000000000000000", 0, "", -1);
    expect("system rom MB8877A (sysromdb)", sys, n2, "358e69f427390041b5aa28018550a88f996bddb6", 1, "MB8877A", -1);
    /* 모든 KonamiSCC 항목이 KonamiSCC로 읽혀야 함 (메가롬 회귀 시험) */
    int scc = 0, bad = 0;
    for (uint32_t i = 0; i + 60 < n1; i++) {
        if (memcmp(rom + i, "<type>KonamiSCC</type>", 22)) continue;
        uint8_t *h = (uint8_t *)strstr((char *)rom + i, "sha1\">");
        if (!h) break;
        char hex[41]; memcpy(hex, h + 6, 40); hex[40] = 0;
        RomDbInfo r;
        scc++; if (!romdb_find(rom, n1, hex, &r) || strcmp(r.type, "KonamiSCC")) bad++;
    }
    total++; fails += bad != 0;
    printf("%s every KonamiSCC entry (%d) reads back as KonamiSCC (%d wrong)\n", bad ? "FAIL" : "PASS", scc, bad);
    printf("romdbtest: %d/%d passed\n", total - fails, total);
    free(rom); free(sys);
    return fails;
}
