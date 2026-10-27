/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
#include "../kernel/png.h"
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv)
{
    static unsigned char b[200000]; int fails = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        FILE *f = fopen(argv[i], "rb"); size_t n = fread(b, 1, sizeof b, f); fclose(f);
        char out[16][17]; char exp[16][17];
        FILE *t = fopen(argv[i + 1], "r"); for (int r = 0; r < 16; r++) { if (fscanf(t, "%16s", exp[r]) != 1) exp[r][0] = 0; } fclose(t);
        int ok = !png_icon16(b, (unsigned)n, out);
        for (int r = 0; ok && r < 16; r++) if (strcmp(out[r], exp[r])) ok = 0;
        printf("%s %s\n", ok ? "PASS" : "FAIL", argv[i]); fails += !ok;
    }
    printf("pngtest: %s\n", fails ? "FAILED" : "all passed");
    return fails;
}
