/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* dskimgtest.c - 3.0.2 이미지 만들기 시험: 만든 이미지를 파일로 남기고 mtools로 확인 (Makefile) */
#include "../kernel/dskimg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t buf[2880 * 512];
static uint8_t pre[1 << 20];

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/dskt";
    char p[256];
    int fail = 0;
    for (int f = 0; f < DSK_FORMATS; f++) {
        uint32_t n = dsk_build(f, buf, "X86MSX");
        snprintf(p, sizeof p, "%s/fmt%d.dsk", dir, f);
        FILE *o = fopen(p, "wb"); if (!o) return 1; fwrite(buf, 1, n, o); fclose(o);
        if (n != (uint32_t)dsk_formats[f].sectors * 512) fail++;
    }
    static const struct { int kind; uint32_t mb; } hd[] = { { HDI_NEXTOR_FAT16, 32 }, { HDI_NEXTOR_FAT16, 512 }, { HDI_DOS2_FAT12, 32 }, { HDI_BLANK, 16 } };
    for (unsigned i = 0; i < sizeof hd / sizeof hd[0]; i++) {
        uint32_t total = hd[i].mb * 2048u;
        uint32_t n = hd_build_prefix(hd[i].kind, total, pre, sizeof pre, "X86MSX HD");
        if (!n) { printf("FAIL hd %u\n", i); fail++; continue; }
        snprintf(p, sizeof p, "%s/hd%u.img", dir, i);
        FILE *o = fopen(p, "wb"); if (!o) return 1; fwrite(pre, 1, n, o);
        fseek(o, (long)total * 512 - 1, SEEK_SET); fputc(0, o); fclose(o);
    }
    printf("dskimgtest: built (%d errors)\n", fail);
    return fail;
}
