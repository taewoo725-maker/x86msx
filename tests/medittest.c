/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* medittest.c - 머신 편집기 자료: config.ini 읽기 → 쓰기 → profile_parse 결과가 원본과 같은지 (2.9) */
#include "../kernel/medit.h"
#include "../src/profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails, total;
static void check(const char *name, int ok) { total++; if (!ok) fails++; printf("%s %s\n", ok ? "PASS" : "FAIL", name); }
static int same(const MachineProfile *a, const MachineProfile *b)
{
    if (a->region_count != b->region_count || strcmp(a->id, b->id) || strcmp(a->vdp, b->vdp) || a->vram_kib != b->vram_kib) return 0;
    if (a->z80_hz != b->z80_hz || a->fdc_count != b->fdc_count || a->cmos != b->cmos || a->cmos_battery != b->cmos_battery) return 0;
    if (memcmp(a->subslotted, b->subslotted, sizeof a->subslotted) || memcmp(a->ext_slot, b->ext_slot, sizeof a->ext_slot)) return 0;
    if (a->s1985 != b->s1985 || a->s1990 != b->s1990 || a->opll != b->opll || a->pcm != b->pcm || a->f4 != b->f4) return 0;
    for (int i = 0; i < a->region_count; i++) {
        const ProfileRegion *x = &a->region[i], *y = &b->region[i];
        if (x->slot != y->slot || x->subslot != y->subslot || x->start_block != y->start_block || x->block_count != y->block_count ||
            x->blue_type != y->blue_type || strcmp(x->file, y->file)) return 0;
    }
    return 1;
}
int main(int argc, char **argv)
{
    static char text[65536], out[65536];
    static MeMachine m;
    static MachineProfile p1, p2;
    char err[128];
    for (int a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb");
        if (!f) { check(argv[a], 0); continue; }
        size_t n = fread(text, 1, sizeof text - 1, f); fclose(f); text[n] = 0;
        int r1 = me_parse(text, (uint32_t)n, &m);
        int w = me_write(&m, out, sizeof out);
        int e1 = profile_parse(text, n, &p1, err, sizeof err);
        int e2 = w > 0 ? profile_parse(out, (size_t)w, &p2, err, sizeof err) : -1;
        if (!m.name[0]) strcpy(m.name, "no id");     /* Id가 없는 프로필: 편집기는 폴더 이름을 씀 */
        char msgs[8][ME_MSG];
        int nc = me_check(&m, 0, msgs, 8);
        char nm[300]; snprintf(nm, sizeof nm, "round trip %s", argv[a]);
        check(nm, !r1 && w > 0 && !e1 && !e2 && same(&p1, &p2));
        snprintf(nm, sizeof nm, "check clean %s (%d%s%s)", argv[a], nc, nc ? ": " : "", nc ? msgs[0] : "");
        check(nm, nc == 0);
    }
    /* 겹침·빈 이름·RAM 없음 검사 */
    me_parse("[General]\nId=X\n[Slots]\n0 0 0 4 20 \"a.rom\" \"\"\n0 0 2 4 20 \"b.rom\" \"\"\n", 70, &m);
    char msgs[8][ME_MSG];
    int nc = me_check(&m, 0, msgs, 8);
    int ov = 0, nr = 0;
    for (int i = 0; i < nc && i < 8; i++) { if (!strncmp(msgs[i], "OVERLAP", 7)) ov = 1; if (!strcmp(msgs[i], "NORAM")) nr = 1; }
    check("detect overlap and no RAM", ov && nr);
    /* 2.15: blueMSX 형식 표 */
    check("type list = blueMSX (111)", me_type_count() == 111 && me_type_at(0)->t == 23 && me_type_at(110)->t == 107);
    check("type names", !strcmp(me_type_name(65), "Philips Disk Controller") && !strcmp(me_type_name(84), "MSX Printer") && !strcmp(me_type_name(22), "Mapped RAM"));
    check("windows", me_window(65, 4) == 4 && me_window(22, 64) == 8 && me_window(84, 0) == 0 && me_window(20, 2) == 2 && me_window(78, 2) == 2);
    check("db types", me_db_type("WD2793") == 65 && me_db_type("MB8877A") == 64 && me_db_type("MSX-DOS2") == 2 && me_db_type("KonamiSCC") == 3 && me_db_type("zzz") == -1);
    { int pg[8]; check("size options", me_size_options(23, pg, 8) == 4 && me_size_options(22, pg, 8) == 7 && pg[6] == 512 && me_size_options(20, pg, 8) == 0); }
    printf("medittest: %d/%d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
