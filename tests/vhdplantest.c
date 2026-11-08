/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* vhdplantest.c - X86VHD 공용 계산부 시험: 사진 판정이 vf_mount 와 같은지, 복사 계획(이분 탐색)이 맞는지, 8.3 이름이 맞는지 */
#include "../tools/x86vhd/vhdplan.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int fill_files(VpTree *t, int count, uint32_t each, const char *dir)
{
    char name[64];
    for (int i = 0; i < count; i++) { snprintf(name, sizeof name, "FILE%04d.DAT", i); if (vp_add(t, dir, name, each, 0)) return -1; }
    return 0;
}

int main(void)
{
    VpTree base; vp_init(&base, "X:\\VM\\T");
    VpFit fit;

    /* 1. 빈 폴더는 어느 형식에도 들어간다 */
    CHECK(vp_check(&base, 8, VF_FAT12, &fit) && fit.fits && fit.used_kb == 0, "empty fat12 used=%u", fit.used_kb);
    CHECK(vp_check(&base, 16, VF_FAT16, &fit) && fit.kind == VF_FAT16, "fat16 kind %d", fit.kind);

    /* 2. 700000 바이트 파일 5개 = 약 3.3 MB: 2 MB(→ 3 MB 로 올려 잡힘 포함) 판정과 8 MB 판정 */
    CHECK(fill_files(&base, 5, 700000, "X:\\VM\\T") == 0, "fill");
    CHECK(vp_check(&base, 8, VF_FAT12, &fit) && fit.fits, "8MB should fit, why=%s", fit.why);
    CHECK(!vp_check(&base, 1, VF_FAT12, &fit) && !strcmp(fit.why, "full"), "1MB should be full (%s)", fit.why);
    CHECK(fit.need_mb >= 4 && fit.need_mb <= 5, "need %u MB", fit.need_mb);
    CHECK(fit.suggest_mb >= 4, "suggest %u", fit.suggest_mb);
    CHECK(vp_pick_size(VF_FAT12, 3) == 4 && vp_pick_size(VF_FAT16, 3) == 4 && vp_pick_size(VF_DOS1, 20) == 0 && vp_pick_size(VF_DOS1, 9) == 15, "pick %u %u %u",
          vp_pick_size(VF_FAT12, 3), vp_pick_size(VF_FAT16, 3), vp_pick_size(VF_DOS1, 20));

    /* 3. 복사 계획: 8 MB 하드에 700000 B 파일 20개를 넣으면 앞에서부터 일부만 */
    VpItem items[20];
    for (int i = 0; i < 20; i++) { memset(&items[i], 0, sizeof items[i]); strcpy(items[i].dest_dir, "X:\\VM\\T"); snprintf(items[i].name, sizeof items[i].name, "NEW%04d.BIN", i); items[i].size = 700000; }
    VpFit all;
    int k = vp_plan_fit(&base, items, 20, 8, VF_FAT12, &all);
    CHECK(!all.fits && k > 0 && k < 20, "plan k=%d fits=%d", k, all.fits);
    /* k 개는 들어가고 k+1 개는 안 들어가야 한다 */
    {
        VpTree a; vp_clone(&base, &a);
        for (int i = 0; i < k; i++) vp_add(&a, items[i].dest_dir, items[i].name, items[i].size, 0);
        CHECK(vp_check(&a, 8, VF_FAT12, &fit) && fit.fits, "k fits");
        vp_add(&a, items[k].dest_dir, items[k].name, items[k].size, 0);
        CHECK(!vp_check(&a, 8, VF_FAT12, &fit), "k+1 must not fit");
        vp_free(&a);
    }
    CHECK(vp_plan_fit(&base, items, 20, 32, VF_FAT12, &all) == 20 && all.fits, "all fit at 32MB");
    CHECK(vp_plan_fit(&base, items, 20, 4095, VF_FAT16, &all) == 20, "all fit FAT16");

    /* 4. 덮어쓰기: 같은 이름을 더 작게 바꾸면 사용량이 줄어든다 */
    {
        VpTree a; vp_clone(&base, &a);
        VpFit before, after;
        vp_check(&a, 8, VF_FAT12, &before);
        CHECK(vp_add(&a, "X:\\VM\\T", "file0000.dat", 1000, 0) == 0, "overwrite (case-insensitive)");
        vp_check(&a, 8, VF_FAT12, &after);
        CHECK(after.used_kb < before.used_kb, "used %u -> %u", before.used_kb, after.used_kb);
        CHECK(a.count == base.count, "no duplicate node");
        CHECK(vp_add(&a, "X:\\VM\\T", "FILE0001.DAT", 0, 1) == -2, "file vs dir conflict");
        vp_free(&a);
    }

    /* 5. 지우기: 폴더는 안까지 */
    {
        VpTree a; vp_init(&a, "X:\\VM\\T");
        vp_add(&a, "X:\\VM\\T", "SUB", 0, 1); vp_add(&a, "X:\\VM\\T\\SUB", "A.TXT", 10, 0); vp_add(&a, "X:\\VM\\T", "B.TXT", 5, 0);
        vp_remove(&a, "X:\\VM\\T", "SUB");
        CHECK(a.count == 1 && !strcmp(a.nodes[0].ent.name, "B.TXT"), "remove dir count=%d", a.count);
        vp_free(&a);
    }

    /* 6. MSX 이름: 긴 이름 → 8.3, 충돌하면 번호 */
    {
        VpTree a; vp_init(&a, "X:\\VM\\T");
        vp_add(&a, "X:\\VM\\T", "Moon Light Saga.dsk", 100, 0);
        vp_add(&a, "X:\\VM\\T", "Moon Light Saga 2.dsk", 100, 0);
        vp_add(&a, "X:\\VM\\T", "GAMES", 0, 1);
        vp_add(&a, "X:\\VM\\T\\GAMES", "readme.txt", 7, 0);
        VpName nm[16]; int n = vp_names(&a, 8, VF_FAT12, "X:\\VM\\T", nm, 16);
        CHECK(n == 3, "names n=%d", n);
        int seen_dir = 0, longs = 0;
        for (int i = 0; i < n; i++) { printf("  %s -> %s%s\n", nm[i].host_name, nm[i].msx_name, nm[i].is_dir ? " <DIR>" : ""); if (nm[i].is_dir) seen_dir = 1; else longs++; }
        CHECK(seen_dir && longs == 2 && strcmp(nm[1].msx_name, nm[2].msx_name) && strcmp(nm[0].msx_name, nm[1].msx_name), "distinct 8.3 names");
        n = vp_names(&a, 8, VF_FAT12, "X:\\VM\\T\\GAMES", nm, 16);
        CHECK(n == 1 && !strcmp(nm[0].msx_name, "README.TXT"), "sub names n=%d %s", n, n ? nm[0].msx_name : "");
        vp_free(&a);
    }

    /* 7. 루트 칸 수 한도(DOS1 112칸 등)를 넘으면 root 로 거절 */
    {
        VpTree a; vp_init(&a, "X:\\VM\\T");
        fill_files(&a, 600, 10, "X:\\VM\\T");
        CHECK(!vp_check(&a, 8, VF_FAT12, &fit) && !strcmp(fit.why, "root"), "root why=%s", fit.why);
        vp_free(&a);
    }
    /* 8. 파일 6000개 계획이 몇 초 안에 끝나는가 */
    {
        VpTree a; vp_init(&a, "X:\\VM\\T");
        for (int d = 0; d < 20; d++) { char dn[16], dp[64]; snprintf(dn, sizeof dn, "D%02d", d); vp_add(&a, "X:\\VM\\T", dn, 0, 1); snprintf(dp, sizeof dp, "X:\\VM\\T\\%s", dn); fill_files(&a, 100, 3000, dp); }
        static VpItem big[6000];
        for (int i = 0; i < 6000; i++) { snprintf(big[i].dest_dir, sizeof big[i].dest_dir, "X:\\VM\\T\\D%02d", i % 20); snprintf(big[i].name, sizeof big[i].name, "N%05d.BIN", i); big[i].size = 20000; big[i].is_dir = 0; }
        clock_t t0 = clock();
        int k = vp_plan_fit(&a, big, 6000, 64, VF_FAT16, &fit);
        double sec = (double)(clock() - t0) / CLOCKS_PER_SEC;
        printf("  plan 6000 items: fit %d in %.2fs\n", k, sec);
        CHECK(sec < 6.0, "plan too slow %.2f", sec);
        vp_free(&a);
    }
    vp_free(&base);
    printf(failures ? "vhdplantest: %d FAIL\n" : "vhdplantest: OK\n", failures);
    return failures != 0;
}
