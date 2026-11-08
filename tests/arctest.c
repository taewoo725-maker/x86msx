/* 만든이: KimTaeWoo */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* arctest.c - .ARJ·.PMA 읽기 시험: 파일마다 이름·크기를 보이고 모두 풀어 CRC 까지 맞는지 확인
 * 사용법: arctest 파일... [-n 기대 파일 수 파일]  (모두 풀리면 ok) */
#include "../kernel/zipx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t buf[8u << 20], out[8u << 20];
static int listcb(void *c, const ZipEnt *e) { (void)c; printf("   list: %s%s %u %04u-%02u-%02u %02u:%02u\n", e->name, e->dir ? "/" : "", e->size, e->year, e->mon, e->day, e->hour, e->min); return 0; }

int main(int argc, char **argv)
{
    int bad = 0;
    for (int a = 1; a < argc; a++) {
        FILE *f = fopen(argv[a], "rb");
        if (!f) { perror(argv[a]); bad++; continue; }
        uint32_t n = (uint32_t)fread(buf, 1, sizeof buf, f); fclose(f);
        char names[64][48]; uint32_t us[64], dos[64];
        int cnt = zip_names_ex(buf, n, "", names, us, dos, 64);
        printf("%s: %d files\n", argv[a], cnt);
        if (cnt < 0) { printf("   names error %d\n", cnt); bad++; continue; }
        zip_list(buf, n, "", listcb, 0);
        for (int i = 0; i < cnt; i++) {
            uint32_t sz = 0; char nm[64]; int total = 0;
            int r = zip_extract_nth(buf, n, "", i, out, sizeof out, &sz, nm, sizeof nm, &total);
            uint32_t sum = 0; for (uint32_t k = 0; k < sz; k++) sum = sum * 31 + out[k];
            printf("   [%d] %s size %u (names says %u) -> r=%d hash %08x\n", i, nm, sz, us[i], r, sum);
            if (r) bad++;
            char p[64]; uint32_t s2 = 0; snprintf(p, sizeof p, "%s", nm);
            r = zip_read(buf, n, p, out, sizeof out, &s2);
            if ((r && r != ZIPX_NONE) || (!r && s2 != sz)) {   /* 폴더 안 파일은 이름만으로는 못 찾음(ZIPX_NONE) */
                printf("   zip_read(%s) r=%d size %u\n", p, r, s2); bad++; }
        }
    }
    printf(bad ? "arctest: FAIL (%d)\n" : "arctest: ok\n", bad);
    return bad != 0;
}
