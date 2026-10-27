/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * machinetest.c - blueMSX Machines 폴더의 MSX1 기종을 machine_load로 조립하고 부팅해 본다.
 * 사용법: machinetest <USB 최상위처럼 쓸 폴더 (Machines 포함)>
 * "0:\Machines\..." 경로를 그 폴더 아래 파일로 바꿔 읽는다 (대소문자 무시).
 */
#include "../kernel/machine.h"
#include "../kernel/sha1.h"
#include "../kernel/romdb.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *root;
static MSX m;
static FILE *hdd_f;                                     /* HDD: 하드 디스크 이미지 (섹터 단위로 파일을 직접) */
static int hdd_rd(void *c, uint32_t lba, uint8_t *b) { (void)c; fseek(hdd_f, (long)lba * 512, SEEK_SET); return fread(b, 512, 1, hdd_f) == 1 ? 0 : -1; }
static int hdd_wr(void *c, uint32_t lba, const uint8_t *b) { (void)c; fseek(hdd_f, (long)lba * 512, SEEK_SET); return fwrite(b, 512, 1, hdd_f) == 1 ? 0 : -1; }

/* 대소문자를 무시하고 경로 한 단계씩 찾기 */
static int resolve(const char *path, char *out, size_t n)
{
    snprintf(out, n, "%s", root);
    const char *p = path;
    if (p[0] == '0' && p[1] == ':') p += 2;
    while (*p) {
        while (*p == '\\' || *p == '/') p++;
        if (!*p) break;
        char comp[256]; int k = 0;
        while (*p && *p != '\\' && *p != '/' && k < 255) comp[k++] = *p++;
        comp[k] = 0;
        DIR *d = opendir(out);
        if (!d) return -1;
        struct dirent *e; int found = 0;
        while ((e = readdir(d))) if (!strcasecmp(e->d_name, comp)) { size_t l = strlen(out); snprintf(out + l, n - l, "/%s", e->d_name); found = 1; break; }
        closedir(d);
        if (!found) return -1;
    }
    return 0;
}

static int rd(const char *path, uint8_t *buf, uint32_t max, uint32_t *size)
{
    char real[1024];
    if (resolve(path, real, sizeof real)) return -1;
    FILE *f = fopen(real, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    size_t got = fread(buf, 1, n < (long)max ? (size_t)n : max, f);
    fclose(f);
    *size = (uint32_t)n;
    return got > 0 || n == 0 ? 0 : -1;
}

static int screen_has(const char *a)
{
    int text = (m.vdp.reg[1] & 0x10) != 0, w80 = text && m.vdp.v9938 && (m.vdp.reg[0] & 0x04);
    int cols = w80 ? 80 : text ? 40 : 32;
    uint32_t nt = m.vdp.v9938 ? (w80 ? (uint32_t)(m.vdp.reg[2] & 0xFC) << 10 : (uint32_t)(m.vdp.reg[2] & 0x7F) << 10)
                              : (uint32_t)(m.vdp.reg[2] & 0x0F) << 10;
    static char t[27 * 81 + 1]; int k = 0;
    for (int i = 0; i < 26 * cols; i++) { uint8_t c = m.vdp.vram[(nt + i) & 0x1FFFF]; t[k++] = (c >= 32 && c < 127) ? (char)c : ' '; }
    t[k] = 0;
    if (getenv("TEXTDUMP") && !strcmp(a, "Ok")) for (int r = 0; r < 8; r++) fprintf(stderr, "|%.*s|\n", cols, t + r * cols);
    return strstr(t, a) != 0;
}

static void shot(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6 %d %d 255\n", VDP_W, VDP_H * 2);
    for (int y = 0; y < VDP_H * 2; y++) {
        int d = m.vdp.line_direct[y / 2];
        if (d >= 2) {                                   /* YJK 줄 */
            uint8_t raw[256], rgb[256][3], pi[256];
            for (int x = 0; x < 256; x++) raw[x] = m.vdp.fb[y / 2][2 * x];
            vdp_yjk_line(raw, d == 3, rgb, pi);
            for (int x = 0; x < VDP_W; x++) {
                int sx = x / 2; uint8_t c[3];
                if (m.vdp.spr_ov[y / 2][sx] != 0xFF) vdp_g7_rgb(&m.vdp, m.vdp.spr_ov[y / 2][sx], &c[0], &c[1], &c[2]);
                else if (pi[sx] != 0xFF) memcpy(c, m.vdp.palette[pi[sx]], 3);
                else memcpy(c, rgb[sx], 3);
                fwrite(c, 1, 3, f);
            }
            continue;
        }
        for (int x = 0; x < VDP_W; x++) {
            int oddrow = (y & 1) && m.vdp.line_eo[y / 2];
            uint8_t c = oddrow ? m.vdp.fb_odd[y / 2][x] : m.vdp.fb[y / 2][x], rgb[3];
            if (oddrow ? m.vdp.line_direct_odd[y / 2] : m.vdp.line_direct[y / 2]) vdp_g7_rgb(&m.vdp, c, &rgb[0], &rgb[1], &rgb[2]);
            else memcpy(rgb, m.vdp.palette[c & 15], 3);
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}


/* romcheck: 롬 여러 개를 한 기종에서 차례로 돌려 보고 결과 줄과 화면을 남긴다 (3.0.8 게임 점검).
 * romcheck <root(Machines 포함)> <기종 이름> <msxromdb.xml> <출력 폴더> 롬...  환경: FRAMES(기본 1800), KEYS=1(600·1200프레임에 스페이스) */
static uint8_t dbbuf[8 << 20];
static uint32_t dblen;
static MapperType db_mapper(const char *t, int *plain_start, const RomDbInfo *ri, int *known)
{
    *known = 1;
    if (!strcmp(t, "KonamiSCC")) return MAPPER_KONAMI_SCC;
    if (!strcmp(t, "Konami") || !strcmp(t, "GenericKonami")) return MAPPER_KONAMI;
    if (!strncmp(t, "ASCII8SRAM", 10)) return MAPPER_ASCII8_SRAM;
    if (!strncmp(t, "ASCII16SRAM", 11)) return MAPPER_ASCII16_SRAM;
    if (!strncmp(t, "ASCII8", 6)) return MAPPER_ASCII8;
    if (!strncmp(t, "KoeiSRAM", 8)) return MAPPER_ASCII8_SRAM;
    if (!strncmp(t, "ASCII16", 7)) return MAPPER_ASCII16;
    if (!strcmp(t, "Normal")) { *plain_start = ri->start; return MAPPER_PLAIN; }
    if (!strcmp(t, "MSXDOS2")) return MAPPER_DOS2;
    if (!strcmp(t, "R-Type") || !strcmp(t, "RType")) return MAPPER_RTYPE;
    if (!strcmp(t, "HarryFox")) return MAPPER_HARRYFOX;
    if (!strcmp(t, "Zemina80in1")) return MAPPER_ZEMINA80;
    if (!strcmp(t, "Zemina90in1")) return MAPPER_ZEMINA90;
    if (!strcmp(t, "Zemina126in1")) return MAPPER_ZEMINA126;
    if (!strcmp(t, "CrossBlaim")) return MAPPER_CROSSBLAIM;
    if (!strcmp(t, "SuperLodeRunner")) return MAPPER_LODERUNNER;
    if (!strcmp(t, "NettouYakyuu")) return MAPPER_NETTOU;
    if (!strcmp(t, "SuperPierrot")) return MAPPER_ASCII16;
    *known = 0;
    return MAPPER_COUNT;
}

static int colors_used(void)
{
    int seen[256] = { 0 }, n = 0;
    for (int y = 0; y < VDP_H; y += 2) for (int x = 0; x < VDP_W; x += 2) { uint8_t c = m.vdp.fb[y][x]; if (!seen[c]) { seen[c] = 1; n++; } }
    return n;
}

int main(int argc, char **argv)
{
    if (argc < 6) { fprintf(stderr, "usage\n"); return 1; }
    root = argv[1];
    { static uint8_t wv[OPL4_MEM_SIZE]; opl4_set_memory(wv, 0); }   /* 3.1.0 MoonSound */
    const char *mach = argv[2], *out = argv[4];
    FILE *df = fopen(argv[3], "rb");
    if (df) { dblen = (uint32_t)fread(dbbuf, 1, sizeof dbbuf, df); fclose(df); }
    int nfr = getenv("FRAMES") ? atoi(getenv("FRAMES")) : 1800;
    static uint8_t cart[1 << 21];
    for (int a = 5; a < argc; a++) {
        char err[200]; MachineProfile prof;
        if (machine_load(&m, rd, mach, &prof, err, sizeof err)) { printf("MACHINE-ERR\t%s\n", err); return 1; }
        FILE *cf = fopen(argv[a], "rb");
        if (!cf) { printf("%d\tOPENFAIL\t%s\n", a - 5, argv[a]); continue; }
        uint32_t cs = (uint32_t)fread(cart, 1, sizeof cart, cf); fclose(cf);
        char hex[41]; sha1_hex(cart, cs, hex);
        RomDbInfo ri; int indb = dblen && romdb_find(dbbuf, dblen, hex, &ri);
        MapperType guess = cart_guess_mapper(cart, cs), mt = guess;
        int start = -1, known = 1;
        const char *dbt = indb && ri.type[0] ? ri.type : "-";
        if (indb && ri.type[0]) { MapperType t = db_mapper(ri.type, &start, &ri, &known); if (known) mt = t; }
        if (getenv("FORCE")) { for (int t = 0; t < MAPPER_COUNT; t++) if (!strcmp(cart_mapper_name((MapperType)t), getenv("FORCE"))) mt = (MapperType)t; }   /* 3.8.0 */
        int rc = msx_insert_cart(&m, 0, cart, cs, mt);
        if (!rc && mt == MAPPER_PLAIN && start >= 0) msx_cart_set_base(&m, 0, start / 0x2000);
        msx_reset(&m);
        int halted_di = 0;
        uint16_t pcs[16]; int npc = 0;
        for (int f = 0; f < nfr; f++) {
            if (getenv("CTRLBOOT")) msx_key(&m, 6, atoi(getenv("CTRLBOOT")), f < 500);   /* 1 = CTRL, 0 = SHIFT */
            if (getenv("KEYS") && ((f >= 600 && f < 606) || (f >= 1200 && f < 1206))) msx_key(&m, 8, 0, 1);
            else if (getenv("KEYS") && (f == 606 || f == 1206)) msx_key(&m, 8, 0, 0);
            msx_run_frame(&m);
            if (f >= nfr - 16) pcs[npc++] = m.cpu.pc;
        }
        if (m.cpu.halted && !m.cpu.iff1) halted_di = 1;
        int same = 1; for (int i = 1; i < npc; i++) if (pcs[i] != pcs[0]) same = 0;
        int cols = colors_used();
        int basic = screen_has("Ok") || screen_has("BASIC") || screen_has("Copyright");
        const char *st = rc ? "INSERT-FAIL" : halted_di ? "HALT-DI" : basic ? "BASIC" : cols <= 1 ? "BLANK" : same ? "PC-STUCK" : "RUNS";
        char p[1200]; snprintf(p, sizeof p, "%s/%03d.ppm", out, a - 5); shot(p);
        printf("%d\t%s\t%u\t%s\t%s\t%s%s\t%d\t%04X\t%s\n", a - 5, st, cs, dbt, cart_mapper_name(mt), indb ? "db" : "guess", known ? "" : "(unknown-type)", cols, m.cpu.pc, argv[a]);
        if (getenv("MEMDUMP")) fprintf(stderr, "HIMEM %04X BOTTOM %04X MODE %02X\n", msx_peek(&m, 0xFC4A) | (msx_peek(&m, 0xFC4B) << 8), msx_peek(&m, 0xFC48) | (msx_peek(&m, 0xFC49) << 8), msx_peek(&m, 0xFAFC));
        if (getenv("VDPREGS")) { fprintf(stderr, "regs:"); for (int i = 0; i < 48; i++) fprintf(stderr, " %02X", m.vdp.reg[i]); fprintf(stderr, "\n"); }
        fflush(stdout);
    }
    return 0;
}
