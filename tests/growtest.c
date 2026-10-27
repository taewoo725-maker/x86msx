/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* growtest.c - kernel/usbgrow.c를 큰 디스크 이미지 파일로 시험 */
#include "../kernel/usbgrow.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int rd(void *ctx, uint64_t lba, uint32_t count, uint8_t *buf)
{
    FILE *f = ctx;
    if (count > 64 || fseeko(f, (off_t)(lba * 512), SEEK_SET)) return -1;
    return fread(buf, 512, count, f) == count ? 0 : -1;
}
static int wr(void *ctx, uint64_t lba, uint32_t count, const uint8_t *buf)
{
    FILE *f = ctx;
    if (count > 64 || fseeko(f, (off_t)(lba * 512), SEEK_SET)) return -1;
    return fwrite(buf, 512, count, f) == count ? 0 : -1;
}
static int last = -1;
static void prog(int p, const char *w) { if (p / 10 != last / 10) { printf("  %3d%% %s\n", p, w); last = p; } }

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    FILE *f = fopen(argv[1], "r+b");
    if (!f) { perror(argv[1]); return 1; }
    fseeko(f, 0, SEEK_END);
    GrowIO io = { rd, wr, f, (uint64_t)ftello(f) / 512, malloc(80u << 20), 80u << 20, prog };
    printf("disk %llu sectors, partition %llu sectors\n", (unsigned long long)io.disk_sectors, (unsigned long long)usbgrow_part_sectors(&io));
    char err[128] = "";
    int r = usbgrow_run(&io, err, sizeof err);
    printf("result %d %s, partition now %llu sectors\n", r, err, (unsigned long long)usbgrow_part_sectors(&io));
    fclose(f);
    return r < 0;
}
