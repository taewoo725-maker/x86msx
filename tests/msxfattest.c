/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* msxfattest.c - 3.0.2 디스크 이미지 안 파일 쓰기·읽기·지우기 시험 (tests/msxfat.sh가 mtools로 확인)
 * msxfattest DIR          : 형식마다 이미지를 만들고 파일을 넣어 DIR/wN.dsk로 남김
 * msxfattest read IMG     : 이미지의 파일 목록과 크기·합계 출력 (mtools로 만든 이미지 읽기 확인)
 * msxfattest sub IMG      : IMG의 첫 폴더에 작은 파일 40개 (폴더 늘리기) */
#include "../kernel/dskimg.h"
#include "../kernel/msxfat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t img[2880 * 512], data[1 << 20], back[1 << 20];
static int fail;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fail++; } } while (0)

static uint32_t load(const char *p)
{
    FILE *f = fopen(p, "rb"); if (!f) { perror(p); exit(1); }
    uint32_t n = (uint32_t)fread(img, 1, sizeof img, f); fclose(f); return n;
}
static void save(const char *p, uint32_t n) { FILE *f = fopen(p, "wb"); fwrite(img, 1, n, f); fclose(f); }
static void pattern(uint32_t n, int seed) { for (uint32_t i = 0; i < n; i++) data[i] = (uint8_t)(i * 7 + seed + (i >> 9)); }

int main(int argc, char **argv)
{
    MsxFat f;
    MfEntry e[300];
    if (argc > 2 && !strcmp(argv[1], "read")) {
        uint32_t n = load(argv[2]);
        int rc = mf_open(&f, img, n);
        if (rc) { printf("open %d\n", rc); return 1; }
        int k = mf_list(&f, 0, e, 300);
        for (int i = 0; i < k; i++) {
            uint32_t s = e[i].is_dir ? 0 : mf_read(&f, &e[i], back, sizeof back), sum = 0;
            for (uint32_t j = 0; j < s; j++) sum = sum * 31 + back[j];
            printf("%s %s %u %08x\n", e[i].name, e[i].is_dir ? "DIR" : "FILE", e[i].size, sum);
        }
        printf("free %u\n", mf_free(&f));
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "sub")) {
        uint32_t n = load(argv[2]);
        if (mf_open(&f, img, n)) return 1;
        int k = mf_list(&f, 0, e, 300), d = -1;
        for (int i = 0; i < k; i++) if (e[i].is_dir) { d = i; break; }
        if (d < 0) { printf("no dir\n"); return 1; }
        for (int i = 0; i < 40; i++) {
            char nm[16]; uint8_t raw[11];
            snprintf(nm, sizeof nm, "F%02d.TXT", i);
            mf_make83(nm, raw);
            pattern(100 + i, i);
            int rc = mf_write(&f, e[d].clus, raw, data, 100 + i, 0, 0);
            CHECK(!rc, "sub write %d rc %d", i, rc);
        }
        save(argv[2], n);
        printf("sub: 40 files\n");
        return fail;
    }
    const char *dir = argc > 1 ? argv[1] : "build/mft";
    char p[256];
    /* 8+3 이름 */
    uint8_t raw[11];
    CHECK(!mf_make83("game.bas", raw) && !memcmp(raw, "GAME    BAS", 11), "83 simple");
    CHECK(mf_make83("long file name.basic", raw) && !memcmp(raw, "LONGFILEBAS", 11), "83 long %.11s", raw);
    CHECK(mf_make83("\xED\x95\x9C\xEA\xB8\x80.txt", raw) && !memcmp(raw, "__      TXT", 11), "83 utf8 %.11s", raw);
    for (int fm = 0; fm < DSK_FORMATS; fm++) {
        uint32_t n = dsk_build(fm, img, "MFTEST");
        CHECK(!mf_open(&f, img, n), "open fmt %d", fm);
        uint32_t free0 = mf_free(&f);
        /* 1: 작은 파일, 2: 여러 클러스터, 3: 바꿔 쓰기, 4: 지우기 */
        pattern(1000, 1); mf_make83("SMALL.BIN", raw); CHECK(!mf_write(&f, 0, raw, data, 1000, 0x5C21, 0), "small");
        pattern(70000, 2); mf_make83("BIG.BIN", raw); CHECK(!mf_write(&f, 0, raw, data, 70000, 0x5C21, 0), "big");
        pattern(5000, 3); mf_make83("GONE.BIN", raw); CHECK(!mf_write(&f, 0, raw, data, 5000, 0, 0), "gone");
        pattern(3000, 4); mf_make83("SMALL.BIN", raw); CHECK(!mf_write(&f, 0, raw, data, 3000, 0, 0), "rewrite");
        pattern(0, 0); mf_make83("EMPTY", raw); CHECK(!mf_write(&f, 0, raw, data, 0, 0, 0), "empty");
        int k = mf_list(&f, 0, e, 300);
        CHECK(k == 4, "list %d", k);
        for (int i = 0; i < k; i++) if (!strcmp(e[i].name, "GONE.BIN")) CHECK(!mf_delete(&f, &e[i]), "delete");
        k = mf_list(&f, 0, e, 300);
        CHECK(k == 3, "list after delete %d", k);
        for (int i = 0; i < k; i++) if (!strcmp(e[i].name, "BIG.BIN")) {
            uint32_t s = mf_read(&f, &e[i], back, sizeof back);
            pattern(70000, 2);
            CHECK(s == 70000 && !memcmp(back, data, s), "read back big");
        }
        /* 가득 채우기: 남은 자리보다 큰 파일은 거절, 이미지는 그대로 */
        uint32_t fr = mf_free(&f);
        mf_make83("HUGE.BIN", raw);
        CHECK(mf_write(&f, 0, raw, data, fr + 1, 0, 0) == MF_ENOSPACE, "nospace");
        CHECK(mf_free(&f) == fr, "nospace keeps free");
        CHECK(free0 > fr, "free shrank");
        snprintf(p, sizeof p, "%s/w%d.dsk", dir, fm);
        save(p, n);
    }
    /* 루트가 가득 차면 MF_EDIRFULL */
    uint32_t n = dsk_build(6, img, 0);                  /* 루트 64개 */
    mf_open(&f, img, n);
    int rc = 0, i;
    for (i = 0; i < 70 && !rc; i++) { char nm[16]; snprintf(nm, sizeof nm, "R%d", i); mf_make83(nm, raw); rc = mf_write(&f, 0, raw, data, 10, 0, 0); }
    CHECK(rc == MF_EDIRFULL && i == 65, "rootfull rc %d i %d", rc, i);
    /* BPB가 없는 디스크 (MSX-DOS 1 옛 형식): 매체 바이트로 */
    n = dsk_build(0, img, 0);
    memset(img + 11, 0, 20);
    CHECK(!mf_open(&f, img, n) && f.root_ents == 112 && f.spc == 2, "no-bpb open");
    printf("msxfattest: %s (%d errors)\n", fail ? "FAIL" : "OK", fail);
    return fail;
}
