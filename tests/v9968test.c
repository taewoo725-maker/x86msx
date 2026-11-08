/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* 8.0.69 V9968 1단계: 칩 번호, R#20·R#21 잠금, 확장 팔레트(EPAL), 스프라이트 16개(S16), 이전 칩에 영향 없음 */
#include "../src/vdp.h"
#include <stdio.h>
#include <string.h>
static VDP v;
static int fails;
#define CHK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static void reg(int r, int val) { vdp_write_ctrl(&v, (uint8_t)val); vdp_write_ctrl(&v, (uint8_t)(0x80 | r)); }
static int stat1(void) { reg(15, 1); int s = vdp_read_status(&v); reg(15, 0); return s; }
static void pal3(int a, int b) { vdp_write_palette(&v, (uint8_t)a); vdp_write_palette(&v, (uint8_t)b); }
/* 스프라이트 n개를 100줄에 놓고 줄을 그린 뒤 S#0 */
static int spr_line(int n)
{
    memset(v.vram, 0, 0x10000);
    reg(0, 0x06); reg(1, 0x40); reg(5, 0xF7); reg(6, 0x0F); reg(11, 0); reg(2, 0x1F);
    uint32_t sat = 0x7A00, sct = sat - 512, spg = 0x7800;
    for (int i = 0; i < 8; i++) v.vram[spg + i] = 0xFF;
    for (int i = 0; i < 32; i++) { v.vram[sat + i * 4] = i < n ? 99 : 216; v.vram[sat + i * 4 + 1] = (uint8_t)(i * 8); v.vram[sat + i * 4 + 2] = 0; for (int r = 0; r < 16; r++) v.vram[sct + i * 16 + r] = 0x0F; }
    v.stat[0] = 0;
    vdp_frame_start(&v);
    vdp_line_start(&v, 100); vdp_render_line(&v, 100);
    return v.stat[0];
}
int main(void)
{
    /* 칩 번호: V9938 0, V9958 2, V9968 3 (R#21 비트 0 = 1 이면 V9958 흉내) */
    vdp_init(&v, 1); vdp_reset(&v); CHK((stat1() >> 1) == 0, "V9938 id %d", stat1() >> 1);
    vdp_init(&v, 2); vdp_reset(&v); CHK((stat1() >> 1) == 2, "V9958 id");
    vdp_init(&v, 3); vdp_reset(&v); CHK((stat1() >> 1) == 2, "V9968 starts as V9958 id %d (R#21 V58=1)", stat1() >> 1);
    reg(21, 0x00); CHK((stat1() >> 1) == 3, "V9968 id 3 after V58=0");
    reg(21, 0x01); CHK((stat1() >> 1) == 2, "V9968 FID id");
    reg(21, 0x00); CHK((stat1() >> 1) == 3, "V9968 id back");
    /* 이전 칩은 R#20 으로 확장이 켜지지 않음 */
    vdp_init(&v, 2); vdp_reset(&v); reg(20, 0xFF); CHK(!vdp_epal(&v), "V9958 must not have EPAL");
    CHK(spr_line(9) & 0x40, "V9958 9 sprites should set 5th flag");
    /* V9968 잠금 */
    vdp_init(&v, 3); vdp_reset(&v);
    reg(20, 0x10); CHK(vdp_epal(&v), "R20 write unlocked");
    vdp_write_9c(&v, 0x80); CHK(vdp_read_9c(&v) & 0x80, "lock bit");
    reg(20, 0x00); CHK(vdp_epal(&v), "R20 write ignored when locked");
    vdp_write_9c(&v, 0x00); reg(20, 0x00); CHK(!vdp_epal(&v), "R20 write after unlock");
    /* EPAL 팔레트: R#16 8비트, R G B 5비트씩 세 번 */
    reg(20, 0x10);
    reg(16, 0x20);
    vdp_write_palette(&v, 31); vdp_write_palette(&v, 0); vdp_write_palette(&v, 0);
    CHK(v.pal256[0x20][0] == 255 && v.pal256[0x20][1] == 0 && v.pal256[0x20][2] == 0, "EPAL red %d %d %d", v.pal256[0x20][0], v.pal256[0x20][1], v.pal256[0x20][2]);
    CHK(v.reg[16] == 0x21, "R16 auto increment %02X", v.reg[16]);
    vdp_write_palette(&v, 0); vdp_write_palette(&v, 31); vdp_write_palette(&v, 16);
    CHK(v.pal256[0x21][1] == 255 && v.pal256[0x21][2] > 0 && v.pal256[0x21][2] < 255, "EPAL g/b");
    reg(16, 0x05);                                       /* 0~15 는 일반 팔레트와 같음 */
    vdp_write_palette(&v, 10); vdp_write_palette(&v, 20); vdp_write_palette(&v, 30);
    CHK(!memcmp(v.palette[5], v.pal256[5], 3) && v.palette[5][2] > v.palette[5][1] && v.palette[5][1] > v.palette[5][0], "palette[5] mirrors pal256");
    reg(16, 0xFF); vdp_write_palette(&v, 1); vdp_write_palette(&v, 1); vdp_write_palette(&v, 1);
    CHK(v.reg[16] == 0x00, "R16 wraps 8 bit");
    /* 쓰다 만 세 번 쓰기는 R#16 쓰기로 버려짐 */
    vdp_write_palette(&v, 31); reg(16, 0x30); vdp_write_palette(&v, 0); vdp_write_palette(&v, 0); vdp_write_palette(&v, 31);
    CHK(v.pal256[0x30][0] == 0 && v.pal256[0x30][2] == 255, "R16 write aborts partial palette load");
    /* SCREEN 8 직접색 변환이 확장 팔레트를 따름 */
    uint8_t r, g, b; vdp_g7_rgb(&v, 0x20, &r, &g, &b); CHK(r == 255 && g == 0 && b == 0, "g7 rgb via EPAL");
    reg(20, 0x00); vdp_g7_rgb(&v, 0xE0, &r, &g, &b); CHK(g > 200 && r < 50, "g7 rgb normal after EPAL off");
    /* EPAL 끈 상태에서는 3비트 두 번 쓰기 */
    reg(16, 0x02); pal3(0x70, 0x00);
    CHK(v.palette[2][0] == 255 && v.palette[2][1] == 0, "legacy palette write on V9968 (%d %d)", v.palette[2][0], v.palette[2][1]);
    /* S16 */
    reg(20, 0x00); CHK((spr_line(9) & 0x40) != 0, "9 sprites without S16 must set 5th flag");
    reg(20, 0x80); CHK((spr_line(9) & 0x40) == 0, "9 sprites with S16 must not set flag (%02X)", v.stat[0]);
    CHK((spr_line(16) & 0x40) == 0, "16 sprites with S16 ok");
    CHK((spr_line(17) & 0x40) != 0, "17 sprites with S16 sets flag");
    /* 8.0.70 SVNS: 스프라이트가 R#23 세로 스크롤을 따라가지 않음 */
    reg(20, 0x00); reg(23, 20); CHK((spr_line(9) & 0x40) == 0, "R23 scroll moves sprites away (no SVNS)");
    reg(20, 0x02); reg(23, 20); CHK((spr_line(9) & 0x40) != 0, "SVNS: sprites ignore R23");
    vdp_init(&v, 2); vdp_reset(&v); reg(20, 0x02); reg(23, 20); CHK((spr_line(9) & 0x40) == 0, "V9958 ignores SVNS bit");
    vdp_init(&v, 3); vdp_reset(&v);
    /* 8.0.70 ILNS: 줄 인터럽트 비교에서 R#23 을 빼지 않음 */
    reg(0, 0x10); reg(19, 10); reg(23, 3);
    v.stat[1] = 0; vdp_line_end(&v, 7);  CHK(v.stat[1] & 1, "line int at R19-R23");
    v.stat[1] = 0; vdp_line_end(&v, 10); CHK(!(v.stat[1] & 1), "no line int at R19 without ILNS");
    reg(20, 0x04);
    v.stat[1] = 0; vdp_line_end(&v, 7);  CHK(!(v.stat[1] & 1), "ILNS: no int at R19-R23");
    v.stat[1] = 0; vdp_line_end(&v, 10); CHK(v.stat[1] & 1, "ILNS: int at R19");
    vdp_init(&v, 2); vdp_reset(&v); reg(0, 0x10); reg(19, 10); reg(23, 3); reg(20, 0x04);
    v.stat[1] = 0; vdp_line_end(&v, 10); CHK(!(v.stat[1] & 1), "V9958 ignores ILNS bit");
    /* 8.0.100 EVR: 256 KB VRAM (R#20 비트 6 = EVR, 실제 칩 RTL 기준. R#21 비트 0 은 FID 로 ID 만 바꿈) */
    vdp_init(&v, 3); vdp_reset(&v);
    CHK(!vdp_evr(&v), "V9968 starts in V58 (128 KB) mode");
    reg(21, 0x00); CHK(!vdp_evr(&v), "R21 (FID) does not enable EVR");
    reg(20, 0x60); CHK(vdp_evr(&v), "R20 bit6 -> EVR");
    reg(14, 0x0C);                                   /* 주소 0x30000 */
    vdp_write_ctrl(&v, 0x00); vdp_write_ctrl(&v, 0x40);
    vdp_write_data(&v, 0xA5);
    reg(14, 0x00); vdp_write_ctrl(&v, 0x00); vdp_write_ctrl(&v, 0x40); vdp_write_data(&v, 0x11);
    reg(14, 0x0C); vdp_write_ctrl(&v, 0x00); vdp_write_ctrl(&v, 0x00);
    CHK(vdp_read_data(&v) == 0xA5, "VRAM beyond 128 KB readable");
    reg(14, 0x00); vdp_write_ctrl(&v, 0x00); vdp_write_ctrl(&v, 0x00);
    CHK(vdp_read_data(&v) == 0x11, "low VRAM independent from high");
    reg(14, 0x0F); CHK(v.reg[14] == 0x0F, "R14 keeps 4 bits under EVR");
    reg(20, 0x00); CHK(!vdp_evr(&v) && v.reg[14] <= 7, "R20 bit6 off -> 128 KB, R14 masked (%02X)", v.reg[14]);
    reg(14, 0x0F); CHK(v.reg[14] == 7, "R14 3 bits without EVR");
    reg(20, 0x60); CHK(vdp_evr(&v), "back to EVR");
    uint8_t ph[2 * 131072]; CHK(vdp_vram_physical(&v, ph) == 262144, "physical dump 256 KB");
    /* 8.0.71 제자리 끼워 넣기 변환: G4 → G7 (논리 = 실제[rr(l)] 각 128 KB 덩어리) → 되돌리면 원래 값 */
    {
        vdp_init(&v, 3); vdp_reset(&v); reg(20, 0x60); reg(0, 0x06); reg(1, 0x00);
        for (uint32_t i = 0; i < 131072; i++) { v.vram[i] = (uint8_t)(i * 7 + (i >> 8)); v.vram_x[i] = (uint8_t)(i * 13 + (i >> 5) + 1); }
        reg(0, 0x0E);
        int bad = 0;
        for (uint32_t l = 0; l < 131072; l++) {
            uint32_t p = (l >> 1) | ((l & 1) << 16);
            if (v.vram[l] != (uint8_t)(p * 7 + (p >> 8))) bad++;
            if (v.vram_x[l] != (uint8_t)(p * 13 + (p >> 5) + 1)) bad++;
        }
        CHK(v.il && bad == 0, "G7 logical layout of 256 KB (%d bad)", bad);
        reg(0, 0x06); bad = 0;
        for (uint32_t i = 0; i < 131072; i++) { if (v.vram[i] != (uint8_t)(i * 7 + (i >> 8))) bad++; if (v.vram_x[i] != (uint8_t)(i * 13 + (i >> 5) + 1)) bad++; }
        CHK(!v.il && bad == 0, "round trip back to G4 (%d bad)", bad);
    }
    /* 8.0.72 명령 엔진 확장: SP3 배치, FG4, LRMM, LFMM, LFMC */
    {
#define R16W(r, val) do { reg((r), (val) & 255); reg((r) + 1, ((val) >> 8) & 255); } while (0)
        vdp_init(&v, 3); vdp_reset(&v); reg(20, 0x60); reg(0, 0x0E); reg(1, 0x00);
        CHK(v.il, "G7 interleaved without SP3");
        reg(20, 0x68); CHK(!v.il, "SP3 -> not interleaved");
        reg(20, 0x60); CHK(v.il, "SP3 off -> interleaved again");
        reg(20, 0x68);                                                     /* 이하 비끼움 배치 */
        memset(v.vram, 0, sizeof v.vram);
        for (int y = 0; y < 4; y++) for (int x = 0; x < 8; x++) v.vram[(5 + y) * 256 + 10 + x] = (uint8_t)(0x10 + y * 16 + x);
        /* LRMM 등배 복사: VX=256, VY=0 */
        R16W(32, 10); R16W(34, 5); R16W(36, 50); R16W(38, 60); R16W(40, 4); R16W(42, 3);
        R16W(47, 256); R16W(49, 0); reg(45, 0); reg(46, 0x30);
        CHK(v.vram[60 * 256 + 50] == 0x10 && v.vram[60 * 256 + 53] == 0x13 && v.vram[62 * 256 + 52] == 0x32, "LRMM identity copy (%02X %02X %02X)", v.vram[60 * 256 + 50], v.vram[60 * 256 + 53], v.vram[62 * 256 + 52]);
        /* LRMM 2배 확대: VX=128 → 원본 한 점이 두 점, VY=128 */
        memset(v.vram + 70 * 256, 0, 8 * 256);
        R16W(47, 128); R16W(49, 0); R16W(40, 8); R16W(42, 6); R16W(36, 50); R16W(38, 70); reg(46, 0x30);
        CHK(v.vram[70 * 256 + 50] == 0x10 && v.vram[70 * 256 + 51] == 0x10 && v.vram[71 * 256 + 50] == 0x10 && v.vram[72 * 256 + 50] == 0x20, "LRMM 2x zoom (%02X %02X %02X)", v.vram[70 * 256 + 50], v.vram[71 * 256 + 50], v.vram[72 * 256 + 50]);
        /* 창 밖은 COL */
        R16W(47, 256); R16W(49, 0); reg(51, 12); reg(52, 0);      /* WSX=12 : 원본 x 10·11 은 창 밖 */
        R16W(40, 4); R16W(42, 1); R16W(36, 80); R16W(38, 90); reg(44, 0x77); reg(46, 0x30);
        CHK(v.vram[90 * 256 + 80] == 0x77 && v.vram[90 * 256 + 81] == 0x77 && v.vram[90 * 256 + 82] == 0x12 && v.vram[90 * 256 + 83] == 0x13, "LRMM window (%02X %02X %02X)", v.vram[90 * 256 + 80], v.vram[90 * 256 + 82], v.vram[90 * 256 + 83]);
        reg(51, 0);
        /* ECOM(R#20 비트 5) 이 꺼지면 LRMM 은 아무것도 안 함 */
        reg(20, 0x48); R16W(36, 100); R16W(38, 100); reg(46, 0x30);
        CHK(v.vram[100 * 256 + 100] == 0x00 && !(v.stat[2] & 1), "LRMM ignored without ECOM");
        reg(20, 0x68);
        /* LFMM: 글꼴 바이트 0xA5 → 8점 (1 = COL, 0 = R#12) */
        v.vram[0x3000] = 0xA5;
        reg(32, 0x00); reg(33, 0x30); reg(34, 0); R16W(36, 20); R16W(38, 30); R16W(40, 1); R16W(42, 1);
        reg(44, 0x55); reg(12, 0x11); reg(45, 0); reg(46, 0x10);
        { uint8_t *d = v.vram + 30 * 256 + 20; CHK(d[0] == 0x55 && d[1] == 0x11 && d[2] == 0x55 && d[3] == 0x11 && d[4] == 0x11 && d[5] == 0x55 && d[6] == 0x11 && d[7] == 0x55, "LFMM 0xA5 (%02X%02X%02X%02X%02X%02X%02X%02X)", d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]); }
        /* LFMC: CPU 가 R#44 로 바이트를 보냄. 가로 4점 → 한 줄이 끝나면 남은 비트는 버림 */
        R16W(36, 20); R16W(38, 40); R16W(40, 4); R16W(42, 2); reg(44, 0x66); reg(46, 0x20);
        reg(44, 0xF0); reg(44, 0x0F);
        { uint8_t *d = v.vram + 40 * 256 + 20, *e = v.vram + 41 * 256 + 20; CHK(d[0] == 0x66 && d[3] == 0x66 && e[0] == 0x11 && e[3] == 0x11 && !(v.cmd), "LFMC two rows (%02X %02X %02X %02X cmd=%X)", d[0], d[3], e[0], e[3], v.cmd); }
        /* FG4: SCREEN 8 에서도 명령이 4비트 256폭 점으로 (LMMV 로 점 4개 = 바이트 2개) */
        memset(v.vram + 120 * 128, 0, 128);
        R16W(36, 0); R16W(38, 120); R16W(40, 4); R16W(42, 1); reg(44, 0x0A); reg(45, 0x80); reg(46, 0x80);
        CHK(v.vram[120 * 128] == 0xAA && v.vram[120 * 128 + 1] == 0xAA && v.vram[120 * 128 + 2] == 0, "FG4 LMMV (%02X %02X %02X)", v.vram[120 * 128], v.vram[120 * 128 + 1], v.vram[120 * 128 + 2]);
        reg(45, 0);
        /* 끼워 넣은 배치에서 FG4 는 실제 칩 주소 기준: G4 처럼 (y<<7)|(x>>1) 번지에 */
        reg(20, 0x60); CHK(v.il, "interleaved for FG4 test");
        R16W(36, 0); R16W(38, 121); R16W(40, 2); R16W(42, 1); reg(44, 0x0B); reg(45, 0x80); reg(46, 0x80);
        { uint8_t pv[2 * 131072]; vdp_vram_physical(&v, pv); CHK(pv[121 * 128] == 0xBB, "FG4 physical address in interleaved mode (%02X)", pv[121 * 128]); }
        reg(45, 0);
    }
    /* 8.0.73 스프라이트 모드 3: 속성표 1개, 패턴 16x16 (색 1 = 팔레트 17번), 확장 팔레트 17번 = 빨강 */
    {
        vdp_init(&v, 3); vdp_reset(&v); reg(20, 0x60); reg(0, 0x0E); reg(1, 0x40); reg(8, 0x08);
        reg(20, 0x78);                                                      /* SP3 + EPAL */
        reg(2, 0x00); reg(5, 0x00); reg(11, 0x01); reg(6, 0x20);             /* SAT = 0x8000, 패턴 = 0x10000 */
        memset(v.vram, 0, sizeof v.vram); memset(v.vram_x, 0, sizeof v.vram_x);
        reg(16, 17); vdp_write_palette(&v, 31); vdp_write_palette(&v, 0); vdp_write_palette(&v, 0);
        uint8_t *sat = v.vram + 0x8000;
        sat[0] = 10; sat[1] = 0x00; sat[2] = 0; sat[3] = 0x01; sat[4] = 40; sat[5] = 0; sat[6] = 0; sat[7] = 0;    /* y=10, 팔레트 묶음 1, x=40, 16x16 (배율 0=256 → 늘림) */
        sat[2] = 16; sat[6] = 16;                                            /* 세로·가로 배율 16 = 원래 크기 */
        sat[8] = 216;                                                       /* 두 번째 스프라이트 Y=216 = 끝 */
        for (int r = 0; r < 16; r++) for (int b = 0; b < 8; b++) v.vram[0x10000 + r * 128 + b] = 0x11;    /* 전부 색 1 */
        vdp_render_line(&v, 12);
        CHK(v.line_direct[12] == 1 && v.fb[12][80] == 17 && v.fb[12][81] == 17 && v.fb[12][79] != 17, "SP3 draws pal 17 at x=40 (%d %d %d ld=%d)", v.fb[12][79], v.fb[12][80], v.fb[12][81], v.line_direct[12]);
        CHK(v.fb[12][80 + 2 * 16] != 17 && v.fb[12][80 + 2 * 15] == 17, "SP3 width 16");
        vdp_render_line(&v, 30);
        CHK(v.fb[30][80] != 17, "SP3 below sprite not drawn");
        /* 17개 한 줄: 5번째(17번째) 스프라이트 표시 */
        for (int i = 0; i < 17; i++) { sat[i * 8] = 10; sat[i * 8 + 1] = 0; sat[i * 8 + 2] = 16; sat[i * 8 + 3] = 1; sat[i * 8 + 4] = (uint8_t)(i * 8); sat[i * 8 + 5] = 0; sat[i * 8 + 6] = 16; sat[i * 8 + 7] = 0; }
        sat[17 * 8] = 216;
        v.stat[0] = 0; vdp_render_line(&v, 12);
        CHK((v.stat[0] & 0x40) && (v.stat[0] & 0x1F) == 16, "SP3 17th sprite flag (%02X)", v.stat[0]);
        CHK(v.stat[0] & 0x20, "SP3 collision");
        /* SP3 가 꺼지면 예전 스프라이트 규칙 */
        reg(20, 0x70); v.stat[0] = 0; vdp_render_line(&v, 12);
        CHK(!(v.stat[0] & 0x20) || 1, "legacy path runs");
    }
    /* EPAL + G7 스프라이트는 팔레트 번호 그대로 */
    /* 8.0.102 HS (R#20 비트 0): 명령 고속 — 호환 모드의 대기를 건너뜀. V9958 은 무시 */
    {
        uint32_t c[3];
        for (int t = 0; t < 3; t++) {
            vdp_init(&v, t == 2 ? 2 : 3); vdp_reset(&v);
            if (t == 1) reg(20, 0x01); else if (t == 2) reg(20, 0x01);
            reg(0, 0x06); reg(1, 0x40); R16W(36, 0); R16W(38, 0); R16W(40, 100); R16W(42, 100); reg(44, 0x0F); reg(45, 0); reg(46, 0x80);
            c[t] = v.cmd_cost;
        }
        CHK(c[1] == 64 + 100 * 100 * 3, "HS LMMV cost %u", c[1]);
        CHK(c[0] > c[1] * 2, "HS is much faster than compatible (%u vs %u)", c[0], c[1]);
        CHK(c[2] == c[0] || c[2] > c[1] * 2, "V9958 ignores HS (%u)", c[2]);
    }
    printf("v9968: %s\n", fails ? "FAILED" : "all passed");
    return fails;
}
