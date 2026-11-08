/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* vfattest.c - 3.7.1 가상 하드 시험: vfattest <폴더> <MB> <형식 0~3> dump <이미지>
 *                                    vfattest <폴더> <MB> <형식> apply <고친 이미지>   (다른 섹터만 vf_write → vf_sync) */
#include "../kernel/vfat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static void hp(const char *p, char *o) { int k = 0; for (; *p; p++) o[k++] = *p == '\\' ? '/' : *p; o[k] = 0; }
static int h_list(void *c, const char *path, VfHostEnt *out, int max)
{
    (void)c; char p[1024]; hp(path, p);
    DIR *d = opendir(p); if (!d) return -1;
    struct dirent *e; int n = 0;
    while ((e = readdir(d)) && n < max) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char f[2048]; snprintf(f, sizeof f, "%s/%s", p, e->d_name);
        struct stat st; if (stat(f, &st)) continue;
        snprintf(out[n].name, sizeof out[n].name, "%s", e->d_name);
        out[n].is_dir = S_ISDIR(st.st_mode); out[n].size = (uint32_t)st.st_size; out[n].date = out[n].time = 0;
        n++;
    }
    closedir(d);
    return n;
}
static int h_read_at(void *c, const char *path, uint32_t off, uint8_t *b, uint32_t len)
{
    (void)c; char p[1024]; hp(path, p);
    FILE *f = fopen(p, "rb"); if (!f) return -1;
    fseek(f, off, SEEK_SET); size_t n = fread(b, 1, len, f); fclose(f);
    if (n < len) memset(b + n, 0, len - n);
    return 0;
}
static int h_write(void *c, const char *path, const uint8_t *d, uint32_t len)
{
    (void)c; char p[1024]; hp(path, p);
    FILE *f = fopen(p, "wb"); if (!f) return -1;
    fwrite(d, 1, len, f); fclose(f);
    printf("write %s %u\n", p, len);
    return 0;
}
static int h_mkdir(void *c, const char *path) { (void)c; char p[1024]; hp(path, p); printf("mkdir %s\n", p); return mkdir(p, 0755); }
static int h_del(void *c, const char *path) { (void)c; char p[1024]; hp(path, p); printf("delete %s\n", p); return unlink(p); }

static uint32_t fake_t;
static uint32_t h_now(void) { return fake_t += 50; }   /* 8.0.42: 부를 때마다 50 ms 가 흐른 것처럼 → 일을 하나 하면 항상 시간 초과 */
int main(int argc, char **argv)
{
    if (argc < 6) { fprintf(stderr, "usage\n"); return 2; }
    static uint8_t arena[VF_ARENA];
    uint8_t *pool = malloc(16u << 20);
    vf_pool_init(pool, 16u << 20);
    VfHost h = { 0, h_list, h_read_at, h_write, h_mkdir, h_del, getenv("VF_SLICE") ? h_now : 0 };
    static Vf v; char err[64];
    if (vf_mount(&v, &h, argv[1], (uint32_t)atoi(argv[2]), atoi(argv[3]), 1, arena, err, sizeof err)) { printf("mount fail %s need %u MB\n", err, vf_need_mb(&v)); return 1; }   /* 8.0.169: 못 담을 때도 필요한 MB */
    printf("kind %d total %u spc %u fatsz %u clus %u used %u pool %u/%u\n", v.kind, v.total, v.spc, v.fatsz, v.nclus, v.used_clus, vf_pool_used(), vf_pool_cap());
    uint8_t s[512], t[512];
    if (!strcmp(argv[4], "find")) {                     /* 8.0.66: 짧은 이름 경로 → USB 전체 경로·크기 (소리 카트리지 파일 재생) */
        for (int i = 5; i < argc; i++) { char o[300]; uint32_t sz = 0; int r = vf_find_file(&v, argv[i], o, sizeof o, &sz); printf("find %s -> %s %u\n", argv[i], r ? o : "(none)", r ? sz : 0); }
        return 0;
    }
    if (!strcmp(argv[4], "lfn")) {                      /* 8.0.62: 짧은 이름 경로 → 긴 이름 */
        for (int i = 5; i < argc; i++) { char o[300]; int r = vf_lfn(&v, argv[i], o, sizeof o); printf("lfn %s -> %s\n", argv[i], r ? o : "(none)"); }
        return 0;
    }
    if (!strcmp(argv[4], "dump")) {
        FILE *f = fopen(argv[5], "wb");
        for (uint32_t l = 0; l < v.total; l++) { if (vf_read(&v, l, s)) { printf("read fail %u\n", l); return 1; } fwrite(s, 1, 512, f); }
        fclose(f);
        return 0;
    }
    /* apply: 마운트 그대로의 이미지와 고친 이미지를 비교해 다른 섹터를 씀 */
    FILE *f = fopen(argv[5], "rb"); if (!f) return 1;
    int nw = 0;
    for (uint32_t l = 0; l < v.total; l++) {
        if (fread(t, 1, 512, f) != 512) break;
        vf_read(&v, l, s);
        if (memcmp(s, t, 512)) { if (vf_write(&v, l, t)) { printf("write fail %u\n", l); return 1; } nw++; }
    }
    fclose(f);
    static uint8_t tmp[32u << 20];
    int r, calls = 1;
    if (getenv("VF_SLICE")) { r = vf_sync2(&v, tmp, sizeof tmp, err, sizeof err, 40); while (v.dirty && calls < 1000) { r = vf_sync2(&v, tmp, sizeof tmp, err, sizeof err, 40); calls++; } printf("slices %d\n", calls); if (calls < 2) { printf("NOT SLICED\n"); return 1; } }
    else r = vf_sync(&v, tmp, sizeof tmp, err, sizeof err);
    printf("sectors %d, sync %d %s\n", nw, r, err);
    /* 다시 쓴 뒤에도 같은 내용인지 */
    f = fopen(argv[5], "rb");
    int bad = 0;
    for (uint32_t l = 0; l < v.total; l++) { if (fread(t, 1, 512, f) != 512) break; vf_read(&v, l, s); if (memcmp(s, t, 512)) bad++; }
    fclose(f);
    printf("after sync differing sectors %d (빈 클러스터의 옛 자료), pool %u\n", bad, vf_pool_used());
    if (argc > 6) { f = fopen(argv[6], "wb"); for (uint32_t l = 0; l < v.total; l++) { vf_read(&v, l, s); fwrite(s, 1, 512, f); } fclose(f); }
    return 0;
}
