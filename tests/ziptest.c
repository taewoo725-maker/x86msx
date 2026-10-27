/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* ziptest.c - 3.2.0 zip 꺼내기 시험: ziptest a.zip expect.bin exts */
#include "../kernel/zipx.h"
#include <stdio.h>
#include <string.h>
static uint8_t z[4 << 20], out[2 << 20], ex[2 << 20];
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb"); uint32_t zl = (uint32_t)fread(z, 1, sizeof z, f); fclose(f);
    f = fopen(argv[2], "rb"); uint32_t el = (uint32_t)fread(ex, 1, sizeof ex, f); fclose(f);
    uint32_t sz = 0; char nm[64] = "";
    int r = zip_extract(z, zl, argc > 3 ? argv[3] : "", out, sizeof out, &sz, nm, sizeof nm);
    int ok = r == 0 && sz == el && !memcmp(out, ex, el);
    printf("%s %s -> %s (%u bytes)\n", ok ? "PASS" : "FAIL", argv[1], nm, sz);
    return !ok;
}
