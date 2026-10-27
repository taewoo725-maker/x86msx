/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fatstress.c - 3.4.5: X86MSX가 USB(FAT32)에 쓰는 동작을 많이 되풀이한 뒤 fsck.fat으로 검사 (USB 깨짐 조사)
 *   폴더 만들기(States·Screenshots·Audio·Printer·Video·SRAM·CAS·HARDDISK), 크기가 늘고 주는 파일 쓰기,
 *   같은 크기 덮어쓰기, 큰 파일 미리 잡기(하드 디스크 이미지), 긴 이름·한글 이름, 지우기, 폴더가 클러스터를 넘게 커지기 */
#include "../kernel/fat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int rd(void *ctx, uint64_t lba, uint32_t count, uint8_t *buf)
{ FILE *f = ctx; if (fseek(f, (long)(lba * 512), SEEK_SET)) return -1; return fread(buf, 512, count, f) == count ? 0 : -1; }
static int wr(void *ctx, uint64_t lba, uint32_t count, const uint8_t *buf)
{ FILE *f = ctx; if (fseek(f, (long)(lba * 512), SEEK_SET)) return -1; return fwrite(buf, 512, count, f) == count ? 0 : -1; }

static uint8_t data[3 << 20], back[3 << 20];
static uint32_t rnd = 12345;
static uint32_t R(void) { rnd = rnd * 1103515245u + 12345u; return rnd >> 8; }

int main(int argc, char **argv)
{
    const char *img = argc > 1 ? argv[1] : "build/stress.img";
    int rounds = argc > 2 ? atoi(argv[2]) : 300, bad = 0;
    FILE *f = fopen(img, "r+b");
    static FatVol vols[4];
    if (!f || fat_scan_disk(rd, f, vols, 4) < 1) { printf("open fail\n"); return 1; }
    FatVol *v = &vols[0];
    v->write = wr;
    static const char *dirs[] = { "\\States", "\\Screenshots", "\\Audio", "\\Printer", "\\Video", "\\SRAM", "\\CAS", "\\HARDDISK", "\\BASIC", "\\MYFILES" };
    for (unsigned i = 0; i < sizeof dirs / sizeof dirs[0]; i++) fat_create(v, dirs[i], 1);
    for (int r = 0; r < rounds; r++) {
        char p[160];
        int op = (int)(R() % 7), d = (int)(R() % 10), k = (int)(R() % 60);
        snprintf(p, sizeof p, "%s\\%s%03d%s", dirs[d], k & 1 ? "파일 이름이 긴 테스트 " : "F", k, k % 3 ? ".sta" : ".png");
        uint32_t len = R() % (k % 5 == 0 ? (2u << 20) : 70000u) + 1;
        for (uint32_t i = 0; i < len; i++) data[i] = (uint8_t)(i * 7 + r);
        switch (op) {
        case 0: case 1: case 2:                     /* 저장 (X86MSX: create 뒤 write_file) */
            { int cr = fat_create(v, p, 0);
            if (fat_write_file(v, p, data, len)) { printf("write fail %s %u (create %d)\n", p, len, cr); bad++; break; } }
            { uint32_t n = 0; if (fat_read_file(v, p, back, sizeof back, &n) || n != len || memcmp(back, data, len)) { printf("readback fail %s\n", p); bad++; } }
            break;
        case 3: { uint32_t n = 0; if (!fat_read_file(v, p, back, sizeof back, &n) && n) { for (uint32_t i = 0; i < n; i++) back[i] ^= 0x5A; if (fat_write_inplace(v, p, back, n)) { printf("inplace fail\n"); bad++; } } break; }
        case 4: fat_delete(v, p); break;
        case 5: { char q[160]; snprintf(q, sizeof q, "%s\\HD%02d.hdd", dirs[7], k % 8); fat_delete(v, q); fat_create(v, q, 0); if (fat_alloc_file(v, q, (R() % 8 + 1) << 20, data, 512, 0)) { printf("alloc fail\n"); } break; }
        case 6: { char q[160]; snprintf(q, sizeof q, "%s\\SUB%02d", dirs[d], k % 5); fat_create(v, q, 1); break; }
        }
    }
    fclose(f);
    printf("fatstress: %d rounds, %d errors\n", rounds, bad);
    return bad != 0;
}
