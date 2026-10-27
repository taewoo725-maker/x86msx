/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * machinetest.c - blueMSX Machines 폴더의 MSX1 기종을 machine_load로 조립하고 부팅해 본다.
 * 사용법: machinetest <USB 최상위처럼 쓸 폴더 (Machines 포함)>
 * "0:\Machines\..." 경로를 그 폴더 아래 파일로 바꿔 읽는다 (대소문자 무시).
 */
#include "../kernel/machine.h"
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

int main(int argc, char **argv)
{
    root = argc > 1 ? argv[1] : "/tmp/bmsx/out";
    { static uint8_t big[4194304]; machine_big = big; machine_big_size = sizeof big; }   /* 4.0.31: FS-A1GT 4 MB 펌웨어 */
    { static uint8_t wv[OPL4_MEM_SIZE]; opl4_set_memory(wv, 0); }   /* 3.1.0 MoonSound */
    char mdir[1024]; snprintf(mdir, sizeof mdir, "%s/Machines", root);
    DIR *d = opendir(mdir);
    if (!d) { perror(mdir); return 1; }
    static char names[400][128]; int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < 400) if (!strncmp(e->d_name, "MSX", 3)) snprintf(names[n++], 128, "%s", e->d_name);
    closedir(d);
    int ok = 0, boot = 0, msx2 = 0, rom = 0, other = 0;
    for (int i = 0; i < n; i++) {
        char err[200]; MachineProfile prof;
        if (getenv("ONLY") && getenv("SHOT") && strcmp(getenv("SHOT"), names[i])) continue;
        if (machine_load(&m, rd, names[i], &prof, err, sizeof err)) {
            if (strstr(err, "or later")) msx2++; else if (strstr(err, "missing ROM")) { rom++; }
            else { other++; printf("  error  %-40s %s\n", names[i], err); }
            continue;
        }
        ok++;
        const char *want = getenv("SHOT");
        static uint8_t disk[1474560];
        uint32_t disk_size = 0;
        const char *dpath = getenv("DISK");
        const char *dif = getenv("DISKIF");                 /* 카트리지 슬롯 2에 디스크 인터페이스 */
        static uint8_t ifrom[65536];
        if (want && dif && !strcmp(want, names[i])) {
            char rp[1100]; uint32_t sz = 0;
            snprintf(rp, sizeof rp, "0:\\Machines\\Shared Roms\\%s", atoi(dif) == 64 ? "NATIONALDISK.rom" : "PHILIPSDISK.rom");
            if (!rd(rp, ifrom, sizeof ifrom, &sz)) { int ds = getenv("DISKSLOT") ? atoi(getenv("DISKSLOT")) : 1;   /* DISKSLOT: 카트리지 번호 (0부터) */
                if (getenv("EXPAND")) msx_set_expand(&m, atoi(getenv("EXPAND")));
                msx_insert_disk_interface(&m, ds, ifrom, sz, atoi(dif)); msx_reset(&m); }
            else printf("  no disk interface ROM\n");
        }
        if (want && dpath && !strcmp(want, names[i])) {
            FILE *df = fopen(dpath, "rb");
            if (df) { disk_size = (uint32_t)fread(disk, 1, sizeof disk, df); fclose(df); fdc_insert(&m.fdc, 0, disk, disk_size, 0); }
        }
        const char *cpath = getenv("CART");                 /* 카트리지 슬롯 1에 롬 (MAPPER: 0 일반 1 Konami 2 KonamiSCC 3 ASCII8 4 ASCII16) */
        static uint8_t cart[1048576];
        if (want && cpath && !strcmp(want, names[i])) {
            FILE *cf = fopen(cpath, "rb");
            if (cf) {
                uint32_t cs = (uint32_t)fread(cart, 1, sizeof cart, cf); fclose(cf);
                const char *mp = getenv("MAPPER");
                msx_insert_cart(&m, 0, cart, cs, mp ? (MapperType)atoi(mp) : cart_guess_mapper(cart, cs));
                msx_reset(&m);
            }
        }
        if (want && getenv("MEGARAM") && !strcmp(want, names[i])) {   /* 2.16: 카트리지 슬롯 2에 MegaRAM (KiB) */
            static uint8_t mr[2097152];
            uint32_t kb = (uint32_t)atoi(getenv("MEGARAM"));
            memset(mr, 0xFF, sizeof mr);
            printf("  megaram %u KB: %d\n", kb, msx_insert_cart(&m, 1, mr, kb * 1024, MAPPER_MEGARAM));
            msx_reset(&m);
        }
        if (want && getenv("XRAM") && !strcmp(want, names[i])) {      /* 2.16: 카트리지 슬롯 2에 일반 RAM (KiB) */
            static uint8_t xr[65536];
            uint32_t kb = (uint32_t)atoi(getenv("XRAM"));
            printf("  ram %u KB: %d\n", kb, msx_insert_cart(&m, 1, xr, kb * 1024, MAPPER_RAM));
            msx_reset(&m);
        }
        if (want && getenv("HDD") && !strcmp(want, names[i]) && (hdd_f = fopen(getenv("HDD"), "r+b"))) {
            fseek(hdd_f, 0, SEEK_END);
            ide_attach(&m.ide, (uint32_t)(ftell(hdd_f) / 512), hdd_rd, (IdeSectorFn)hdd_wr, 0);
        }
        if (want && getenv("RTC") && !strcmp(want, names[i])) {   /* RTC=39자 (블록 1~3 배터리 백업 내용) */
            const char *h = getenv("RTC");
            for (int k = 0; k < 39 && h[k]; k++) { char c = h[k]; m.rtc[1 + k / 13][k % 13] = (uint8_t)(c <= '9' ? c - '0' : c - 'A' + 10); }
        }
        const char *type = getenv("TYPE");
        if (type && (!want || strcmp(want, names[i]))) type = 0;
        FILE *wav = (want && getenv("WAV") && !strcmp(want, names[i])) ? fopen(getenv("WAV"), "wb") : 0;
        int nfr = getenv("FRAMES") ? atoi(getenv("FRAMES")) : 1500;
        for (int f = 0; f < nfr; f++) {
            {   /* KEYSEQ="d,d,s": 300프레임부터 30프레임마다 키 하나 (d 아래, u 위, s 스페이스, r 리턴, e Esc) */
                const char *ks = want && !strcmp(want, names[i]) ? getenv("KEYSEQ") : 0;
                int k0 = getenv("KEYSTART") ? atoi(getenv("KEYSTART")) : 300;
                if (ks && f >= k0) {
                    int idx = (f - k0) / 30, ph = (f - k0) % 30, n = 0;
                    const char *c = ks;
                    while (*c && n < idx) { if (*c == ',') n++; c++; }
                    if (*c && n == idx) {
                        int row = -1, bit = 0;
                        switch (*c) { case 'd': row = 8; bit = 6; break; case 'u': row = 8; bit = 5; break; case 's': row = 8; bit = 0; break;
                                      case 'r': row = 7; bit = 7; break; case 'e': row = 7; bit = 2; break; case 'y': row = 5; bit = 6; break; case 'n': row = 4; bit = 3; break; }
                        if (row >= 0) msx_key(&m, row, bit, ph < 6);
                        if (getenv("JOY")) { int jb = *c == 'd' ? 1 : *c == 'u' ? 0 : *c == 's' ? 4 : *c == 'l' ? 2 : *c == 'g' ? 3 : -1; m.joy[0] = (uint8_t)(jb >= 0 && ph < 10 ? 1 << jb : 0); }   /* 4.0.31: 조이스틱 1로 키 시퀀스 */
                    }
                }
            }
            if (want && !strcmp(want, names[i]) && getenv("HOLD")) {   /* HOLD=키 HOLDFROM/HOLDTO 프레임 사이 계속 누름 (d u l r s) */
                int hf = getenv("HOLDFROM") ? atoi(getenv("HOLDFROM")) : 1500, ht = getenv("HOLDTO") ? atoi(getenv("HOLDTO")) : 1600;
                int row = 8, bit = 0; switch (getenv("HOLD")[0]) { case 'd': bit = 6; break; case 'u': bit = 5; break; case 'l': bit = 4; break; case 'r': bit = 7; break; default: bit = 0; }
                msx_key(&m, row, bit, f >= hf && f < ht);
                if (getenv("JOY") && f >= hf && f < ht) { int jb = getenv("HOLD")[0] == 'd' ? 1 : getenv("HOLD")[0] == 'u' ? 0 : getenv("HOLD")[0] == 'l' ? 2 : getenv("HOLD")[0] == 'r' ? 3 : 4; m.joy[0] = (uint8_t)(1 << jb); } else if (getenv("JOY") && !getenv("KEYSEQ")) m.joy[0] = 0;
            }
            if (getenv("CTRLBOOT")) msx_key(&m, 6, getenv("CTRLBIT") ? atoi(getenv("CTRLBIT")) : 1, f < atoi(getenv("CTRLBOOT")));   /* 4.0.31: 부팅 때 CTRL 누름 (Psycho World 등 메모리 확인) */
            msx_run_frame(&m);
            if (want && !strcmp(want, names[i]) && getenv("DUMPF") && f >= atoi(getenv("DUMPF")) && f < atoi(getenv("DUMPF")) + 3) {
                char dp[64]; snprintf(dp, sizeof dp, "/tmp/ram_%d.bin", f); FILE *df = fopen(dp, "wb"); fwrite(m.ram, 1, sizeof m.ram, df); fclose(df);
            }
            if (getenv("MEMDUMP") && want && !strcmp(want, names[i]) && f == nfr - 1) {   /* 4.0.31: 메모리 보기 (MEMDUMP=시작 16진수, 256바이트) */
                unsigned a0 = (unsigned)strtoul(getenv("MEMDUMP"), 0, 16);
                for (unsigned a = a0; a < a0 + 256; a += 16) { printf("  %04X:", a); for (unsigned b = 0; b < 16; b++) printf(" %02X", m.cpu.mem_read(m.cpu.ctx, (uint16_t)(a + b))); printf("\n"); }
                printf("  AF %04X BC %04X DE %04X HL %04X IX %04X IY %04X\n", m.cpu.af.w, m.cpu.bc.w, m.cpu.de.w, m.cpu.hl.w, m.cpu.ix.w, m.cpu.iy.w);
            }
            if (getenv("ADJLOG") && want && !strcmp(want, names[i]) && f >= atoi(getenv("ADJLOG")) && f < atoi(getenv("ADJLOG")) + 6) {   /* 4.0.31: 줄마다 R#18 */
                printf("  f%d adj:", f); int pv = -1; for (int y = 0; y < m.vdp.lines; y++) if (m.vdp.line_adj[y] != pv) { pv = m.vdp.line_adj[y]; printf(" %d=%02X", y, pv); } printf(" R23=%02X R25=%02X R26=%02X R27=%02X\n", m.vdp.reg[23], m.vdp.reg[25], m.vdp.reg[26], m.vdp.reg[27]);
            }
            if (getenv("PCLOG") && want && !strcmp(want, names[i]) && f >= atoi(getenv("PCLOG")) && f < atoi(getenv("PCLOG")) + (getenv("PCN") ? atoi(getenv("PCN")) : 40))   /* 멈춘 자리 찾기 */
                printf("  f%d pc %04X sp %04X A8 %02X ss %02X iff %d\n", f, m.cpu.pc, m.cpu.sp.w, m.primary, m.subslot[0], m.cpu.iff1);
            if (wav && f >= 1200) fwrite(m.psg.buf, 2, (size_t)m.psg.buf_n, wav);     /* 마지막 5초 소리 (16비트 모노 48 kHz) */
            if (f == 900 && !type) {                    /* 날짜 입력 화면이면 Enter로 넘김 (BASIC이면 영향 없음) */
                m.cpu.mem_write(m.cpu.ctx, 0xFBF0, 13);
                m.cpu.mem_write(m.cpu.ctx, 0xF3F8, 0xF1); m.cpu.mem_write(m.cpu.ctx, 0xF3F9, 0xFB);
                m.cpu.mem_write(m.cpu.ctx, 0xF3FA, 0xF0); m.cpu.mem_write(m.cpu.ctx, 0xF3FB, 0xFB);
            }
            if (want && type && !strcmp(want, names[i]) && f >= 1000 && *type) {   /* KEYBUF가 비면 30자씩 */
                uint8_t gl = 0, gh = 0, pl = 0, ph = 0;
                gl = m.vdp.vram[0]; (void)gl;
                uint16_t get = 0, put = 0;
                /* mem_read로 GETPNT/PUTPNT 읽기 */
                get = (uint16_t)(m.cpu.mem_read(m.cpu.ctx, 0xF3FA) | (m.cpu.mem_read(m.cpu.ctx, 0xF3FB) << 8));
                put = (uint16_t)(m.cpu.mem_read(m.cpu.ctx, 0xF3F8) | (m.cpu.mem_read(m.cpu.ctx, 0xF3F9) << 8));
                (void)gh; (void)pl; (void)ph;
                if (get == put) {
                    uint16_t buf = 0xFBF0, n = 0;
                    for (; *type && n < 30; type++, n++) m.cpu.mem_write(m.cpu.ctx, (uint16_t)(buf + n), (uint8_t)(*type == '|' ? 13 : *type));
                    m.cpu.mem_write(m.cpu.ctx, 0xF3F8, (uint8_t)((buf + n) & 0xFF));
                    m.cpu.mem_write(m.cpu.ctx, 0xF3F9, (uint8_t)((buf + n) >> 8));
                    m.cpu.mem_write(m.cpu.ctx, 0xF3FA, (uint8_t)(buf & 0xFF));
                    m.cpu.mem_write(m.cpu.ctx, 0xF3FB, (uint8_t)(buf >> 8));
                }
            }
            if (want && !strcmp(want, names[i]) && (f == 40 || f == 80 || f == 120 || f == 200 || f == 260 || f == nfr - 1 || (getenv("EVERY") && f % atoi(getenv("EVERY")) == 0))) {
                char p[64]; snprintf(p, sizeof p, "/tmp/shot_%d.ppm", f); shot(p);
                if (getenv("SAT")) { uint32_t sa = ((uint32_t)(m.vdp.reg[5] & 0xFC) << 7) | ((uint32_t)(m.vdp.reg[11] & 3) << 15); printf("  f%d SAT@%05X:", f, sa); for (int q = 0; q < 6; q++) printf(" (%d,%d)", m.vdp.vram[sa + q * 4 + 1], m.vdp.vram[sa + q * 4]); printf("\n"); }
                if (getenv("VREG")) { printf("  f%d vdp:", f); for (int r = 0; r < 28; r++) printf(" %02X", m.vdp.reg[r]); printf(" lines %d nsnap %d\n", m.vdp.lines, m.vdp.pal_nsnap); }
            }
        }
        if (wav) fclose(wav);
        if (want && !strcmp(want, names[i])) printf("  cpu: r800 %d, BASIC loop variable at end: %d\n", m.cpu.r800,
            m.cpu.mem_read(m.cpu.ctx, 0xE000) | (m.cpu.mem_read(m.cpu.ctx, 0xE001) << 8));
        if (disk_size && m.fdc.drv[0].dirty) {                 /* 바뀐 디스크를 파일로 */
            FILE *df = fopen(dpath, "wb");
            if (df) { fwrite(disk, 1, disk_size, df); fclose(df); printf("  disk written back (%u bytes)\n", disk_size); }
        }
        if (want && !strcmp(want, names[i])) {
            const char *find = getenv("FIND");
            if (find) printf("  screen has \"%s\": %s\n", find, screen_has(find) ? "yes" : "no");
        }
        if (m.cart_slot[MSX_INT_CART] >= 0) {          /* 3.1.0: 내장 PAC 시험 (4D 69 → 0x4000에 SRAM) */
            int ps = m.cart_slot[MSX_INT_CART], ss = m.cart_ss[MSX_INT_CART];
            msx_io_write(&m, 0xA8, (uint8_t)((m.primary & ~0x0C) | (ps << 2)));
            m.subslot[ps] = (uint8_t)((m.subslot[ps] & ~0x0C) | (ss << 2));
            msx_reset_maps(&m);
            m.cpu.mem_write(m.cpu.ctx, 0x5FFE, 0x4D); m.cpu.mem_write(m.cpu.ctx, 0x5FFF, 0x69);
            m.cpu.mem_write(m.cpu.ctx, 0x4000, 0x5A); m.cpu.mem_write(m.cpu.ctx, 0x5FFD, 0xA5);
            int ok1 = m.cpu.mem_read(m.cpu.ctx, 0x4000) == 0x5A && m.cpu.mem_read(m.cpu.ctx, 0x5FFD) == 0xA5;
            m.cpu.mem_write(m.cpu.ctx, 0x5FFF, 0x00);
            int ok2 = m.cpu.mem_read(m.cpu.ctx, 0x4000) == 0xFF;
            printf("  PAC %d-%d: %s\n", ps, ss, ok1 && ok2 ? "OK" : "FAIL");
        }
        int b = screen_has("Ok") || screen_has("BASIC");
        boot += b;
        printf("  %s %-40s %s unsupported devices:", b ? "BOOT" : "noOK", names[i], m.vdp.v9938 ? "V9938" : "TMS  ");
        for (int u = 0; u < prof.unsupported_count; u++) printf(" %d", prof.unsupported[u]);
        printf("\n");
    }
    printf("machinetest: %d MSX machines, loaded %d, reached BASIC %d, needs MSX2+ VDP %d, ROM missing %d, other %d\n",
           n, ok, boot, msx2, rom, other);
    return 0;
}
