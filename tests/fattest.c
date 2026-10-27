/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fattest.c - kernel/fat.c를 디스크 이미지 파일로 시험 */
#include "../kernel/fat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int rd(void *ctx, uint64_t lba, uint32_t count, uint8_t *buf)
{
    FILE *f = ctx;
    if (count > 64) return -1;
    if (fseek(f, (long)(lba * 512), SEEK_SET)) return -1;
    return fread(buf, 512, count, f) == count ? 0 : -1;
}

static int wr(void *ctx, uint64_t lba, uint32_t count, const uint8_t *buf)
{
    FILE *f = ctx;
    if (count > 64 || fseek(f, (long)(lba * 512), SEEK_SET)) return -1;
    return fwrite(buf, 512, count, f) == count ? 0 : -1;
}

static int fails, total;
static void check(const char *what, int ok, const char *detail)
{
    total++; if (!ok) fails++;
    printf("%s %-46s %s\n", ok ? "PASS" : "FAIL", what, detail ? detail : "");
}

static int has(FatEntry *e, int n, const char *key, int dir)
{
    for (int i = 0; i < n; i++) if (!strcmp(e[i].key, key) && e[i].is_dir == dir) return i;
    return -1;
}


static void compare_file(FatVol *v, const char *path, const char *host)
{
    static uint8_t a[4 << 20], b[4 << 20];               /* 3.4.1: 본체가 1 MiB를 넘음 */
    uint32_t size = 0;
    int r = fat_read_file(v, path, a, sizeof a, &size);
    FILE *f = fopen(host, "rb");
    size_t n = f ? fread(b, 1, sizeof b, f) : 0;
    if (f) fclose(f);
    char d[96];
    snprintf(d, sizeof d, "%u bytes", size);
    check(path, !r && size == n && !memcmp(a, b, n), d);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    static FatVol vols[4];
    static FatEntry e[256];
    char d[160];

    /* 1. USB 이미지 (MBR + FAT32) */
    FILE *f = fopen("build/msx1.img", "rb");
    int nv = f ? fat_scan_disk(rd, f, vols, 4) : 0;
    snprintf(d, sizeof d, "%d volume(s), fat32=%d, label '%s'", nv, nv ? vols[0].fat32 : -1, nv ? vols[0].label : "");
    check("usb image: MBR FAT32 volume", nv == 1 && vols[0].fat32, d);
    if (nv) {
        int n = fat_list(&vols[0], "", e, 256);
        snprintf(d, sizeof d, "%d entries", n);
        check("usb image: root has Storage Device dir and X86MSX.BIN", has(e, n, "Storage Device", 1) >= 0 && has(e, n, "X86MSX.BIN", 0) >= 0, d);
        n = fat_list(&vols[0], "\\Storage Device\\ROMS", e, 256);
        int k = has(e, n, "TEST-Konami SCC mapper.ROM", 0);
        snprintf(d, sizeof d, "%d entries, date %04d-%02d-%02d", n, k >= 0 ? e[k].year : 0, k >= 0 ? e[k].mon : 0, k >= 0 ? e[k].day : 0);
        check("usb image: long file name in \\Storage Device\\ROMS", k >= 0 && e[k].size == 131072 && e[k].year >= 2026, d);
        {   static const char *const need[] = { "\\Storage Device\\DISK\\DirFolder", "\\Storage Device\\DISK\\TEMP", "\\Storage Device\\VMFolder\\MSXDOSA", "\\Storage Device\\VMFolder\\NEXTORA", "\\Storage Device\\CAS", "\\Storage Device\\HARDDISK", "\\Storage Device\\BASIC", "\\States", "\\SRAM", "\\Screenshots", "\\Audio", "\\Video", "\\Printer", "\\MidiSound\\SoundFont", "\\MidiSound\\MT32", "\\MidiSound\\SC55", "\\Databases", "\\Icons", "\\Keyboard Config", "\\Machines\\Shared Roms" };
            int miss = 0; for (unsigned q = 0; q < sizeof need / sizeof need[0]; q++) if (fat_list(&vols[0], need[q], e, 256) < 0) miss++;
            check("usb image: all folders of the layout exist", !miss, 0);
            check("usb image: old top-level ROMS/DISK/VMFolder are gone", fat_list(&vols[0], "\\ROMS", e, 256) < 0 && fat_list(&vols[0], "\\DISK", e, 256) < 0 && fat_list(&vols[0], "\\VMFolder", e, 256) < 0, 0);
        }
        n = fat_list(&vols[0], "storage device\\roms", e, 256);
        check("usb image: case-insensitive path", n >= 6, 0);
        compare_file(&vols[0], "\\Storage Device\\ROMS\\TEST-Konami SCC mapper.ROM", "tests/carts/konamiscc.rom");
        compare_file(&vols[0], "\\X86MSX.BIN", "build/img_kernel.bin");
        check("usb image: missing file", fat_list(&vols[0], "\\NOPE", e, 256) < 0, 0);
        fclose(f);
    }

    /* 2. FAT16, 파티션 없는 디스크, 클러스터 4섹터, 하위 폴더 */
    f = fopen("build/fat16.img", "rb");
    nv = f ? fat_scan_disk(rd, f, vols, 4) : 0;
    snprintf(d, sizeof d, "%d volume(s), fat32=%d, label '%s', spc %u", nv, nv ? vols[0].fat32 : -1, nv ? vols[0].label : "", nv ? vols[0].spc : 0);
    check("fat16 superfloppy", nv == 1 && !vols[0].fat32 && !strcmp(vols[0].label, "FAT16TEST"), d);
    if (nv) {
        int n = fat_list(&vols[0], "\\Games\\Konami", e, 256);
        snprintf(d, sizeof d, "%d entries", n);
        check("fat16: nested folder listing", n == 2 && has(e, n, "Konami Test Game.rom", 0) >= 0, d);
        n = fat_list(&vols[0], "", e, 256);
        check("fat16: 8.3 lowercase flag (readme.txt)", has(e, n, "readme.txt", 0) >= 0, 0);
        check("fat16: korean name as UTF-8", has(e, n, "한글이름.rom", 0) >= 0, 0);
        {   /* 3.4.7: Windows 최대 길이 이름 255자 (mtools가 쓴 것) */
            char lk[800] = "", ld[256] = "", la[300] = "", p[1400];
            const char *u = "아주긴파일이름시험";                       /* 9글자 x 3바이트 */
            for (int i = 0; i < 251; i++) strncat(lk, u + (i % 9) * 3, 3);
            strcat(lk, ".rom");
            const char *dn = "Very Long Folder Name ";
            for (int i = 0; i < 200; i++) ld[i] = dn[i % 22];
            const char *fn = "A very long English file name for X86MSX test ";
            for (int i = 0; i < 251; i++) la[i] = fn[i % 46];
            strcat(la, ".rom");
            int nr = fat_list(&vols[0], "", e, 256);
            check("fat16: 255-char korean name listed in full", has(e, nr, lk, 0) >= 0, 0);
            snprintf(p, sizeof p, "\\%s", lk);
            compare_file(&vols[0], p, "tests/carts/plain16.rom");
            snprintf(p, sizeof p, "\\%s\\%s", ld, la);
            compare_file(&vols[0], p, "tests/carts/konami.rom");
            snprintf(p, sizeof p, "\\%s", ld);
            int nd = fat_list(&vols[0], p, e, 256);
            check("fat16: 255-char name in 200-char folder", nd == 1 && has(e, nd, la, 0) >= 0, 0);
        }
        compare_file(&vols[0], "\\Games\\Konami\\Konami Test Game.rom", "tests/carts/konami.rom");
        compare_file(&vols[0], "/한글이름.rom", "tests/carts/plain16.rom");
        {   /* 파일 구간표 (하드 디스크 이미지 섹터 접근, 2.8): 구간으로 읽은 내용 = 파일 내용 */
            static FatExtent ext[64]; static uint8_t a[131072], b[131072]; uint32_t sz = 0, got = 0;
            int ne = fat_file_extents(&vols[0], "\\Games\\Konami\\Konami Test Game.rom", ext, 64, &sz);
            int ok = ne > 0 && !fat_read_file(&vols[0], "\\Games\\Konami\\Konami Test Game.rom", a, sizeof a, &got);
            uint32_t pos = 0;
            for (int e = 0; ok && e < ne; e++)
                for (uint32_t k = 0; k < ext[e].secs && pos < sz; k++, pos += 512) ok = !rd(f, ext[e].lba + k, 1, b + pos);
            char d2[48]; snprintf(d2, sizeof d2, "%d extent(s), %u bytes", ne, sz);
            check("file extents read back", ok && pos >= sz && !memcmp(a, b, sz), d2);
        }
        fclose(f);
    }
    /* 3. 같은 자리 덮어쓰기 (msxemulator.ini 방식) */
    f = fopen("build/fat16.img", "r+b");
    nv = f ? fat_scan_disk(rd, f, vols, 4) : 0;
    if (nv) {
        vols[0].write = wr;
        static uint8_t a[16384], b[16384];
        for (int i = 0; i < 16384; i++) a[i] = (uint8_t)(i * 7 + 3);
        int r1 = fat_write_inplace(&vols[0], "/한글이름.rom", a, 16384);
        uint32_t size = 0;
        int r2 = fat_read_file(&vols[0], "/한글이름.rom", b, sizeof b, &size);
        int r3 = fat_write_inplace(&vols[0], "/한글이름.rom", a, 100);      /* 크기가 다르면 거부 */
        check("write in place (same size) and read back", !r1 && !r2 && size == 16384 && !memcmp(a, b, 16384), 0);
        check("write in place rejects size change", r3 != 0, 0);
        /* 크기를 바꾸는 쓰기: 16 KiB → 40 KiB (늘림) → 3 KiB (줄임), 매번 다시 읽어 비교 */
        static uint8_t g[40960], gb[40960];
        for (int i = 0; i < 40960; i++) g[i] = (uint8_t)(i * 13 + 7);
        int w1 = fat_write_file(&vols[0], "/한글이름.rom", g, 40960);
        uint32_t s1 = 0; int r1b = fat_read_file(&vols[0], "/한글이름.rom", gb, sizeof gb, &s1);
        check("write file grows 16 KiB -> 40 KiB", !w1 && !r1b && s1 == 40960 && !memcmp(g, gb, 40960), 0);
        int w2 = fat_write_file(&vols[0], "/한글이름.rom", g + 100, 3000);
        uint32_t s2 = 0; int r2b = fat_read_file(&vols[0], "/한글이름.rom", gb, sizeof gb, &s2);
        check("write file shrinks 40 KiB -> 3000 B", !w2 && !r2b && s2 == 3000 && !memcmp(g + 100, gb, 3000), 0);
        /* 새 폴더·파일 만들기 (2.9): 긴 이름, 폴더 늘리기(파일 40개) */
        int c1 = fat_create(&vols[0], "/Machines", 1);
        int c2 = fat_create(&vols[0], "/Machines/MSX2 - My Machine (copy)", 1);
        int c3 = fat_create(&vols[0], "/Machines/MSX2 - My Machine (copy)/config.ini", 0);
        const char *ini = "[General]\nId=MSX2 - My Machine (copy)\n";
        int c4 = fat_write_file(&vols[0], "/Machines/MSX2 - My Machine (copy)/config.ini", (const uint8_t *)ini, (uint32_t)strlen(ini));
        uint32_t cs = 0; int c5 = fat_read_file(&vols[0], "/Machines/MSX2 - My Machine (copy)/config.ini", gb, sizeof gb, &cs);
        check("create folder and file with long names", !c1 && !c2 && !c3 && !c4 && !c5 && cs == strlen(ini) && !memcmp(gb, ini, cs), 0);
        int cf = 0;
        for (int i = 0; i < 40; i++) {
            char nm[96]; snprintf(nm, sizeof nm, "/Machines/MSX2 - My Machine (copy)/a long file name number %02d.txt", i);
            if (fat_create(&vols[0], nm, 0) || fat_write_file(&vols[0], nm, (const uint8_t *)nm, (uint32_t)strlen(nm))) cf++;
        }
        static FatEntry le[64];
        int nle = fat_list(&vols[0], "/Machines/MSX2 - My Machine (copy)", le, 64);
        uint32_t s39 = 0; int r39 = fat_read_file(&vols[0], "/Machines/MSX2 - My Machine (copy)/a long file name number 39.txt", gb, sizeof gb, &s39);
        check("create 40 files (folder grows)", !cf && nle >= 41 && !r39 && s39 == strlen("/Machines/MSX2 - My Machine (copy)/a long file name number 39.txt"), 0);
        check("create existing folder returns 0", fat_create(&vols[0], "/Machines", 1) == 0, 0);
        {   /* 3.4.7: 255자 한글 이름 새로 만들기 → 읽기 (mtools로도 확인: Makefile) */
            char nk[800] = "/Machines/", tail[800] = "";
            const char *u = "새로만든긴이름";                           /* 7글자 */
            for (int i = 0; i < 251; i++) strncat(tail, u + (i % 7) * 3, 3);
            strcat(tail, ".txt");
            strcat(nk, tail);
            const char *txt = "long name write test";
            int k1 = fat_create(&vols[0], nk, 0), k2 = fat_write_file(&vols[0], nk, (const uint8_t *)txt, (uint32_t)strlen(txt));
            uint32_t ks = 0; int k3 = fat_read_file(&vols[0], nk, gb, sizeof gb, &ks);
            static FatEntry me[64];
            int nm = fat_list(&vols[0], "/Machines", me, 64);
            check("create 255-char korean file and read back", !k1 && !k2 && !k3 && ks == strlen(txt) && has(me, nm, tail, 0) >= 0, 0);
        }
        {   /* 3.0.2: 큰 파일 자리 잡기 (하드 디스크 이미지) */
            static uint8_t pre[3000];
            for (int i = 0; i < 3000; i++) pre[i] = (uint8_t)(i * 7 + 1);
            int a1 = fat_create(&vols[0], "/Machines/hd image.dsk", 0);
            int a2 = fat_alloc_file(&vols[0], "/Machines/hd image.dsk", 2u << 20, pre, sizeof pre, 0);
            uint32_t as = 0; int a3 = fat_read_file(&vols[0], "/Machines/hd image.dsk", gb, 4096, &as);
            FatExtent ex[16]; uint32_t es = 0; int ne = fat_file_extents(&vols[0], "/Machines/hd image.dsk", ex, 16, &es);
            check("alloc 2 MiB file with prefix", !a1 && !a2 && !a3 && !memcmp(gb, pre, sizeof pre) && ne >= 1 && es == (2u << 20), 0);
            int d1 = fat_delete(&vols[0], "/Machines/hd image.dsk");
            uint32_t ds = 0; int d2 = fat_read_file(&vols[0], "/Machines/hd image.dsk", gb, 16, &ds);
            int d3 = fat_delete(&vols[0], "/Machines");                     /* 폴더는 안 됨 */
            check("delete file with long name", !d1 && d2 && d3, 0);
            int e1 = fat_create(&vols[0], "/빈 폴더 지우기 시험", 1);          /* 3.6.4: 빈 폴더 지우기 */
            int e2 = fat_delete(&vols[0], "/빈 폴더 지우기 시험");
            uint32_t es2 = 0; int e3 = fat_read_file(&vols[0], "/빈 폴더 지우기 시험/x", gb, 16, &es2);
            check("delete empty folder (non-empty refused)", !e1 && !e2 && e3, 0);
        }
        FILE *o = fopen("tests/carts/plain16.rom", "rb");               /* 원래 내용으로 되돌림 */
        if (o) { size_t k = fread(b, 1, sizeof b, o); fclose(o); fat_write_file(&vols[0], "/한글이름.rom", b, (uint32_t)k); }
        fclose(f);
    }
    printf("fattest: %d/%d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
