/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * inputtest.c - 베어메탈 키 입력 경로(kernel/input.c, keymap.c)를 호스트에서 시험
 *
 * PC 스캔 코드(세트 1)를 프레임 단위로 넣고, MSX BIOS가 화면에 찍은 글자를 확인한다.
 *   1) 누름 길이 1/2/6프레임, 다음 키와 겹치는 입력(롤오버) - 글자 누락이 없어야 함
 *   2) 기호 변환 (* : ( ) ")
 *   3) 키를 2초 누르고 있으면 MSX BIOS 키 반복이 일어나야 함
 */
#include "../host/msx_host.h"
#include "../kernel/input.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static MSX m;
static Input in;

#define QMAX 4096
static struct { long frame; uint8_t code; } q[QMAX];
static int qn, qpos;

static void add(long f, uint8_t c) { if (qn < QMAX) { q[qn].frame = f; q[qn].code = c; qn++; } }
static void tap(long *f, int sc, int hold, int gap, int shift)
{
    if (shift) add(*f, 0x2A);
    add(*f, (uint8_t)sc);
    add(*f + hold, (uint8_t)(sc | 0x80));
    if (shift) add(*f + hold, 0xAA);
    *f += gap;
}

/* ASCII -> PC 스캔 코드 (미국식) */
static int ascii_sc(char ch, int *shift)
{
    static const char *row1 = "1234567890-=", *row1s = "!@#$%^&*()_+";
    static const char *qrow = "qwertyuiop", *arow = "asdfghjkl", *zrow = "zxcvbnm";
    const char *p;
    *shift = 0;
    if ((p = strchr(row1, ch))) return 0x02 + (int)(p - row1);
    if ((p = strchr(row1s, ch))) { *shift = 1; return 0x02 + (int)(p - row1s); }
    if ((p = strchr(qrow, ch))) return 0x10 + (int)(p - qrow);
    if ((p = strchr(arow, ch))) return 0x1E + (int)(p - arow);
    if ((p = strchr(zrow, ch))) return 0x2C + (int)(p - zrow);
    switch (ch) {
    case ' ': return 0x39;
    case '\n': return 0x1C;
    case ';': return 0x27;
    case ':': *shift = 1; return 0x27;
    case '\'': return 0x28;
    case '"': *shift = 1; return 0x28;
    case ',': return 0x33;
    case '.': return 0x34;
    case '/': return 0x35;
    case '@': *shift = 1; return 0x03;
    case '[': return 0x1A;
    case ']': return 0x1B;
    case '=': return 0x0D;
    case '+': *shift = 1; return 0x0D;
    case '-': return 0x0C;
    case '<': *shift = 1; return 0x33;
    case '>': *shift = 1; return 0x34;
    case '?': *shift = 1; return 0x35;
    }
    return -1;
}

static void type(long *f, const char *s, int hold, int gap)
{
    for (; *s; s++) {
        int sh, sc = ascii_sc(*s, &sh);
        if (sc >= 0) tap(f, sc, hold, gap, sh);
    }
}

static void run_until(long frames)
{
    while ((long)m.frame < frames) {
        while (qpos < qn && q[qpos].frame <= (long)m.frame) {
            int r_ = input_byte(&in, &m, q[qpos].code);
            if (getenv("TRACE") && m.frame >= 430 && m.frame < 450)
                printf("  f%ld byte %02X -> %s\n", (long)m.frame, q[qpos].code, r_ == INPUT_DEFER ? "DEFER" : "ok");
            if (r_ == INPUT_DEFER) break;
            qpos++;
        }
        input_frame(&in, &m);
        if (getenv("TRACE") && m.frame >= 420 && m.frame < 480)
            printf("frame %ld row0=%02X row1=%02X row8=%02X row6=%02X\n", (long)m.frame, m.kbd[0], m.kbd[1], m.kbd[8], m.kbd[6]);
        msx_run_frame(&m);
    }
}

static void screen_text(char *out, size_t n)
{
    int text = (m.vdp.reg[1] & 0x10) != 0, cols = text ? 40 : 32;
    uint16_t nt = (uint16_t)((m.vdp.reg[2] & 0x0F) << 10);
    size_t k = 0;
    for (int r = 0; r < 24 && k + cols + 2 < n; r++) {
        for (int c = 0; c < cols; c++) {
            uint8_t ch = m.vdp.vram[nt + r * cols + c];
            out[k++] = (ch >= 32 && ch < 127) ? (char)ch : ' ';
        }
        out[k++] = '\n';
    }
    out[k] = 0;
}

static int fails;
static int verbose;
static void expect(const char *what, const char *screen, const char *needle)
{
    int ok = strstr(screen, needle) != NULL;
    if (!ok && verbose) {
        printf("---- screen ----\n");
        for (const char *p = screen; *p; ) {
            const char *e = strchr(p, '\n');
            int len = (int)(e - p), blank = 1;
            for (int i = 0; i < len; i++) if (p[i] != ' ') blank = 0;
            if (!blank) printf("|%.*s|\n", len, p);
            p = e + 1;
        }
    }
    printf("%s %-40s \"%s\"\n", ok ? "PASS" : "FAIL", what, needle);
    if (!ok) fails++;
}

static void clear_screen(long *f)
{
    /* CLS 후 RETURN */
    type(f, "cls\n", 3, 8);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "machines/DPC-200";
    char err[256], scr[2048];
    if (msx_host_init(&m, dir, err, sizeof err)) { printf("init: %s\n", err); return 1; }
    input_init(&in);
    verbose = argc > 2;
    if (getenv("KBDTYPE")) {             /* 2.13: KBDTYPE="글자" ./inputtest <기종 폴더> → 그 기종 배열로 쳐서 화면 확인 */
        const uint8_t *b = m.slot_rd[0][0][0];
        int b2c = (b && b[0] == 0xF3) ? b[0x2C] : -1;
        keymap_machine(keymap_detect(m.prof.key_layout, b2c)); input_init(&in);
        printf("layout %s (KeyLayout=%s, BIOS 2C=%02X)\n", keymap_name(keymap_machine_get()), m.prof.key_layout, b2c & 0xFF);
        long g = 600; const char *t = getenv("KBDTYPE");
        type(&g, t, 3, 8); g += 60; run_until(g);
        screen_text(scr, sizeof scr);
        verbose = 1; expect("typed on machine layout", scr, t);
        return fails;
    }

    long f = 420;
    /* 1) 누름 길이 1프레임, 간격 3프레임 (아주 빠른 타자) */
    type(&f, "print 11*2\n", 1, 3);
    f += 30;
    run_until(f);
    screen_text(scr, sizeof scr);
    expect("1-frame taps", scr, "print 11*2");
    expect("1-frame taps result", scr, " 22");
    clear_screen(&f); f += 30;

    /* 2) 겹치는 입력: 다음 키를 먼저 누르고 앞 키를 뗌 (롤오버) */
    for (const char *s = "print 7*3\n"; *s; s++) {
        int sh, sc = ascii_sc(*s, &sh);
        if (sh) add(f, 0x2A);
        add(f, (uint8_t)sc);
        if (sh) { add(f + 3, (uint8_t)(sc | 0x80)); add(f + 3, 0xAA); }
        else add(f + 4, (uint8_t)(sc | 0x80));      /* 다음 키(f+2) 뒤에 뗌 */
        f += 2;
    }
    f += 30;
    run_until(f);
    screen_text(scr, sizeof scr);
    expect("rollover typing", scr, "print 7*3");
    expect("rollover result", scr, " 21");
    clear_screen(&f); f += 30;

    /* 3) 기호 */
    type(&f, "print \"a:b\";(1+2)\n", 4, 8);
    f += 30;
    run_until(f);
    screen_text(scr, sizeof scr);
    expect("symbols", scr, "print \"a:b\";(1+2)");
    expect("symbols result", scr, "a:b 3");
    clear_screen(&f); f += 30;

    /* 3b) 한꺼번에 도착 (2.10.1): 저장으로 막힌 동안 친 키가 한 프레임에 몰려 옴 — 순서대로, 눌린 채로 남지 않아야 함 */
    type(&f, "print 5*5", 0, 0);
    add(f, 0x1C); add(f, 0x9C);
    f += 120;
    run_until(f);
    screen_text(scr, sizeof scr);
    expect("burst in one frame", scr, "print 5*5");
    expect("burst result", scr, " 25");
    clear_screen(&f); f += 30;

    /* 4) 키 반복: 'a'를 120프레임(2초) 누름 */
    run_until(f);
    long before = f;
    add(f, 0x1E);
    add(f + 120, 0x9E);
    f += 140;
    run_until(f);
    screen_text(scr, sizeof scr);
    int count = 0;
    for (char *p = scr; *p; p++) if (*p == 'a') count++;
    printf("%s %-40s %d characters in 2 s\n", count >= 10 ? "PASS" : "FAIL", "key repeat (hold 'a')", count);
    if (count < 10) fails++;
    (void)before;

    /* 5) PC 자동 반복: 'b'를 누른 채 30프레임 후부터 2프레임마다 같은 누름 코드가 옴 */
    clear_screen(&f); f += 30;
    add(f, 0x30);
    for (long t = f + 30; t < f + 120; t += 2) add(t, 0x30);
    add(f + 120, 0xB0);
    f += 140;
    run_until(f);
    screen_text(scr, sizeof scr);
    count = 0;
    for (char *p = scr; *p; p++) if (*p == 'b') count++;
    printf("%s %-40s %d characters in 2 s\n", count >= 10 ? "PASS" : "FAIL", "key repeat with PC typematic ('b')", count);
    if (count < 10) fails++;

    /* 6) 기능키: PC F6~F8은 MSX 키 없음, Shift+F1은 MSX SHIFT+F1(=F6) (단축키는 fe.c가 처리) */
    {
        Input t;
        uint8_t mtx[16];
        input_init(&t);
        input_byte(&t, &m, 0x40); keymap_matrix(&t.ks, mtx);
        int f6_none = mtx[6] == 0xFF && mtx[7] == 0xFF;
        input_init(&t);
        input_byte(&t, &m, 0x2A); input_byte(&t, &m, 0x3B); keymap_matrix(&t.ks, mtx);
        int shf1 = !(mtx[6] & 0x01) && !(mtx[6] & 0x20);
        int ok = f6_none && shf1;
        printf("%s %-40s PC F6 ignored=%d, Shift+F1 -> SHIFT+F1=%d\n", ok ? "PASS" : "FAIL", "function keys like real MSX", f6_none, shf1);
        if (!ok) fails++;
    }

    /* 7) 기종 키보드 (2.13): 글자 기준으로 그 나라 배열의 자리 */
    {
        static const struct { int kbd; int shift; uint8_t sc; int row, bit, mshift, mods; const char *what; } c[] = {
            { KBD_DE, 0, 0x2C, 5, 6, -1, 0, "DE: PC z -> MSX Z (QWERTZ)" },
            { KBD_DE, 0, 0x15, 5, 7, -1, 0, "DE: PC y -> MSX Y" },
            { KBD_FR, 0, 0x1E, 4, 6, -1, 0, "FR: PC a -> MSX A (AZERTY)" },
            { KBD_FR, 0, 0x02, 0, 1, 1, 0, "FR: PC 1 -> SHIFT+1" },
            { KBD_DE, 1, 0x03, 0, 2, 0, 2, "DE: PC @ -> CODE+2" },
            { KBD_INT, 1, 0x03, 0, 2, 1, 0, "INT: PC @ -> SHIFT+2" },
            { KBD_INT, 0, 0x27, 1, 7, 0, 0, "INT: PC ; -> ;" },
            { KBD_JP, 1, 0x03, 1, 5, 0, 0, "JP: PC @ -> @ key" },
        };
        int bad = 0;
        for (size_t i = 0; i < sizeof c / sizeof c[0]; i++) {
            Input t; uint8_t mtx[16];
            keymap_machine(c[i].kbd); input_init(&t);
            if (c[i].shift) input_byte(&t, &m, 0x2A);
            input_byte(&t, &m, c[i].sc); keymap_matrix(&t.ks, mtx);
            int hit = !(mtx[c[i].row] & (1 << c[i].bit));
            int sh = !(mtx[6] & 1), gr = !(mtx[6] & 4), cd = !(mtx[6] & 16);
            int ok = hit && (c[i].mshift < 0 || sh == c[i].mshift) && gr == !!(c[i].mods & 1) && cd == !!(c[i].mods & 2);
            if (!ok) { bad++; printf("FAIL %s (hit %d shift %d graph %d code %d)\n", c[i].what, hit, sh, gr, cd); }
        }
        int det = keymap_detect("unknown", 0x14) == KBD_DE && keymap_detect(0, 0x00) == KBD_JP && keymap_detect("russian", 0x11) == KBD_RU
               && keymap_detect("european2", -1) == KBD_UK && keymap_detect("korean", -1) == KBD_JP;
        if (!det) bad++;
        keymap_machine(KBD_JP);
        printf("%s %-40s %d cases, detect=%d\n", bad ? "FAIL" : "PASS", "machine keyboard layouts", (int)(sizeof c / sizeof c[0]), det);
        if (bad) fails++;
    }

    printf("inputtest: %s\n", fails ? "FAILED" : "all passed");
    msx_host_free(&m);
    return fails ? 1 : 0;
}
