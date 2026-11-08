/* 만든이: KimTaeWoo */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* mvsndtest.c - 8.0.55: X86MV 소리 카트리지 (포트 2Ch~2Fh, src/mvsnd.c) — 「X86MV 소리 카트리지 요청서 V1.1.0」 5장 시험
 *   1) 카트리지 없음: 2Ch/2Dh = FFh. 있음: IDENT 뒤 'M','V','S',01h, 5번째부터 00h. 상태 비트 6·5·4·2 = 0
 *   2) OPEN(1500) → PLAY 전 위치 1500, PAUSE 중 안 변함
 *   3) MV2 처럼 1066 바이트 조각으로 MP3(44.1 kHz 모노 128 kbps, tests/data/mvsnd_test.mp3 3초) 를 보내고 PLAY → 위치가 15 fps 로 가고 믹서 PCM 채널 진폭 ≠ 0
 *   4) 같은 스트림을 ffmpeg 가 48 kHz 로 해독한 값(tests/data/mvsnd_ref48.raw) 과 상관계수 ≥ 0.99 (지연 보정)
 *   5) FIFO 가득 → 비트 1 = 0. 끊김 → 비트 0 = 1, 읽으면 0
 *   6) 상태 저장·불러오기 → STOP (비트 3 = 0), 위치는 유지
 * 실제 FLYHIGH.MV2 조각 대신 ffmpeg 로 만든 같은 형식의 스트림을 쓴다 (이 방에 MV2 파일이 없음). */
#include "../host/msx_host.h"
#include "../src/msxstate.h"
#include "../src/mvsnd.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>
#include <strings.h>

/* 8.0.56: 일꾼 코어 흉내 (스레드). 번호 = 칸 */
typedef struct { void (*fn)(void *); void *a; pthread_t t; int used; } WJob;
static WJob wj[8]; static int async_posts;
static void *wthr(void *x) { WJob *j = (WJob *)x; j->fn(j->a); return 0; }
static int wpost(void (*fn)(void *), void *arg) { for (int i = 0; i < 8; i++) if (!wj[i].used) { wj[i].used = 1; wj[i].fn = fn; wj[i].a = arg; pthread_create(&wj[i].t, 0, wthr, &wj[i]); async_posts++; return i; } return -1; }
static int wwait(int h) { pthread_join(wj[h].t, 0); wj[h].used = 0; return 1; }
extern int (*msx_job_post)(void (*)(void *), void *);
extern int (*msx_job_wait)(int);

static MSX m;
static uint8_t blank[16384];
static uint8_t mp3[200000]; static int mp3n;
static int16_t ref[400000]; static int refn;
static uint8_t stbuf[8 << 20];

static int expect(const char *what, long got, long want)
{
    printf("%s %-50s %ld (want %ld)\n", got == want ? "PASS" : "FAIL", what, got, want);
    return got != want;
}
static int expect_range(const char *what, long got, long lo, long hi)
{
    int ok = got >= lo && got <= hi;
    printf("%s %-50s %ld (want %ld~%ld)\n", ok ? "PASS" : "FAIL", what, got, lo, hi);
    return !ok;
}
static void cmd(uint8_t c) { msx_io_write(&m, 0x2C, c); }
static void param(uint16_t v) { msx_io_write(&m, 0x2E, (uint8_t)v); msx_io_write(&m, 0x2F, (uint8_t)(v >> 8)); }
static uint16_t pos(void) { uint8_t lo = msx_io_read(&m, 0x2E); uint8_t hi = msx_io_read(&m, 0x2F); return (uint16_t)(lo | (hi << 8)); }
static void lfn_ask(const char *p) { cmd(0x07); for (; *p; p++) msx_io_write(&m, 0x2D, (uint8_t)*p); msx_io_write(&m, 0x2D, 0); cmd(0x08); }
static uint8_t st(void) { return msx_io_read(&m, 0x2C); }
static int load(const char *path, void *buf, int max)
{
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    int n = (int)fread(buf, 1, (size_t)max, f); fclose(f); return n;
}

static int fake_lfn(const char *path, char *out, int max)
{
    (void)max;
    if (!strcasecmp(path, "\\MP3\\C27479~1.MP3") || !strcasecmp(path, "a:\\mp3\\c27479~1.mp3")) { strcpy(out, "c2747995-YB.mp3"); return 1; }
    if (!strcmp(path, "\\MP3")) { strcpy(out, "\xED\x95\x9C"); return 1; }
    if (!strcmp(path, "\\KOR\\BTS~1.MP3")) { strcpy(out, "BTS(\xEB\xB0\xA9\xED\x83\x84\xEC\x86\x8C\xEB\x85\x84\xEB\x8B\xA8) - \xEB\xB4\x84\xEB\x82\xA0.mp3"); return 1; }   /* BTS(방탄소년단) - 봄날.mp3 */
    if (!strcmp(path, "\\KOR")) { strcpy(out, "\xEC\x9D\x8C\xEC\x95\x85"); return 1; }                                     /* 음악 (폴더) */
    if (!strcmp(path, "\\SHORT.MP3")) { strcpy(out, "SHORT.MP3"); return 1; }
    if (!strcmp(path, "\\LOWER.MP3")) { strcpy(out, "lower.mp3"); return 1; }
    if (!strcmp(path, "\\HANGUL40.MP3")) { out[0] = 0; for (int i = 0; i < 40; i++) strcat(out, "\xEA\xB0\x80"); strcat(out, ".mp3"); return 1; }
    if (!strcmp(path, "\\LONG.MP3")) { strcpy(out, "0123456789012345678901234567890123456789ABCDEFGH.mp3"); return 1; }
    return 0;
}

/* ---- 8.0.66 파일 직접 재생 시험용: DOS 이름 → tests/data 파일 ---- */
static const struct { const char *dos, *host; } fmap[] = {
    { "\\W48.WAV", "tests/data/mvsnd_w48.wav" }, { "\\F48.FLA", "tests/data/mvsnd_f48.flac" },
    { "\\W44.WAV", "tests/data/mvsnd_w44.wav" }, { "\\F44.FLA", "tests/data/mvsnd_f44.flac" },
    { "\\W96.WAV", "tests/data/mvsnd_w96m24.wav" }, { "\\W8.WAV", "tests/data/mvsnd_w8.wav" },
    { "\\BAD.WAV", "tests/data/mvsnd_test.mp3" }, { 0, 0 } };
static int ffind(const char *path, char *host, int max, uint32_t *size)
{
    for (int i = 0; fmap[i].dos; i++) if (!strcasecmp(path, fmap[i].dos)) {
        FILE *fp = fopen(fmap[i].host, "rb"); if (!fp) return 0;
        fseek(fp, 0, SEEK_END); *size = (uint32_t)ftell(fp); fclose(fp);
        snprintf(host, (size_t)max, "%s", fmap[i].host); return 1;
    }
    return 0;
}
static int fread_at(const char *host, uint32_t off, uint8_t *buf, uint32_t len)
{
    FILE *fp = fopen(host, "rb"); if (!fp) return -1;
    fseek(fp, off, SEEK_SET); size_t n = fread(buf, 1, len, fp); fclose(fp);
    return n == len ? 0 : -1;
}
static uint32_t fnow(void) { return 0; }
static const MvFileOps tfops = { ffind, fread_at, fnow };
static uint8_t info[96]; 
static void fopen_dos(const char *p) { cmd(0x07); for (; *p; p++) msx_io_write(&m, 0x2D, (uint8_t)*p); msx_io_write(&m, 0x2D, 0); cmd(0x09); cmd(0x0A); for (int i = 0; i < 96; i++) info[i] = msx_io_read(&m, 0x2D); }
/* PLAY 해서 끝까지 렌더. async = 1 이면 일꾼 흉내로 해독. 렌더한 샘플 수 */
static int play_all(int16_t *out, int max, int async)
{
    cmd(0x02); int tot = 0;
    for (int guard = 0; guard < 100000 && (st() & 0x08) && tot < max; guard++) {
        int n = 800;
        if (async) { mvsnd_tick(wpost, wwait); n = (int)mvsnd_pcm_ready(); if (n > 800) n = 800; }
        if (n > max - tot) n = max - tot;
        mvsnd_render(out + tot, n); tot += n;
    }
    if (async) mvsnd_tick(0, 0);
    { uint32_t fi, el; int pl; mvsnd_state_get(&fi, &el, &pl); (void)fi; (void)pl; if ((int)el < tot) tot = (int)el; }   /* 실제로 나간 샘플 수 (마지막 800 개 덩어리의 빈 꼬리는 뺌) */
    return tot;
}
static int16_t ra[100000], rb[100000], rc[100000];
/* WAV 의 16비트 스테레오 자료를 모노((l+r)/2) 로 → 48 kHz 통과(한 샘플 지연) 모양의 기준 */
static int wav_ref48(const char *path, int16_t *out, int max)
{
    static uint8_t buf[400000]; int n = load(path, buf, sizeof buf); if (n < 44) return -1;
    int p = 12; while (p + 8 < n && memcmp(buf + p, "data", 4)) p += 8 + (int)(buf[p + 4] | (buf[p + 5] << 8) | (buf[p + 6] << 16)) + (buf[p + 4] & 1);
    int dl = (int)(buf[p + 4] | (buf[p + 5] << 8) | (buf[p + 6] << 16) | ((uint32_t)buf[p + 7] << 24)); p += 8;
    int frames = dl / 4; if (frames + 1 > max) frames = max - 1;
    out[0] = 0;
    for (int i = 0; i < frames; i++) { int l = (int16_t)(buf[p + i * 4] | (buf[p + i * 4 + 1] << 8)), r = (int16_t)(buf[p + i * 4 + 2] | (buf[p + i * 4 + 3] << 8)); out[i + 1] = (int16_t)((l + r) / 2); }
    return frames + 1;
}
int main(int argc, char **argv)
{
    char err[256]; int f = 0;
    if (msx_host_init(&m, argc > 1 ? argv[1] : "machines/DPC-200", err, sizeof err)) { printf("init: %s\n", err); return 1; }
    mp3n = load("tests/data/mvsnd_test.mp3", mp3, sizeof mp3);
    refn = load("tests/data/mvsnd_ref48.raw", ref, sizeof ref) / 2;
    if (mp3n <= 0 || refn <= 0) { printf("FAIL test data missing (tests/data/mvsnd_test.mp3, mvsnd_ref48.raw)\n"); return 1; }

    /* 1) 없음 → FFh, 있음 → 식별 */
    f += expect("no cart: 2Ch = FF", st(), 0xFF);
    f += expect("no cart: 2Dh = FF", msx_io_read(&m, 0x2D), 0xFF);
    memset(blank, 0xFF, sizeof blank);
    f += expect("insert X86 Multimedia", msx_insert_cart(&m, 1, blank, sizeof blank, MAPPER_MMEDIA), 0);
    msx_reset(&m);
    f += expect("before IDENT: 2Dh = 00", msx_io_read(&m, 0x2D), 0x00);
    cmd(0x00);
    f += expect("IDENT 1 = 'M'", msx_io_read(&m, 0x2D), 'M');
    f += expect("IDENT 2 = 'V'", msx_io_read(&m, 0x2D), 'V');
    f += expect("IDENT 3 = 'S'", msx_io_read(&m, 0x2D), 'S');
    f += expect("IDENT 4 = 01", msx_io_read(&m, 0x2D), 0x01);
    f += expect("IDENT 5 = 00", msx_io_read(&m, 0x2D), 0x00);
    cmd(0x00);
    f += expect("IDENT again restarts at 'M'", msx_io_read(&m, 0x2D), 'M');
    { uint8_t s = st(); f += expect("status bit7 = 1", (s >> 7) & 1, 1); f += expect("status bits 6/5/4/2 = 0", s & 0x74, 0); f += expect("status: not playing", (s >> 3) & 1, 0); f += expect("status: FIFO has room", (s >> 1) & 1, 1); }
    f += expect("unknown command ignored", (cmd(0x77), st() & 0x7F), 0x02);

    /* 1b) 8.0.62 긴 이름 조회 (07h 경로 / 08h 읽기) */
    {
        uint32_t fu = mvsnd_fifo_used();
        cmd(0x00); msx_io_read(&m, 0x2D); msx_io_read(&m, 0x2D); msx_io_read(&m, 0x2D); msx_io_read(&m, 0x2D);
        f += expect("LFN: no hook -> IDENT 5 = 00", msx_io_read(&m, 0x2D), 0x00);
        mvsnd_set_lfn(fake_lfn);
        cmd(0x00); for (int i = 0; i < 4; i++) msx_io_read(&m, 0x2D);
        f += expect("LFN: hook -> IDENT 5 bit0 + bit3 (0Dh UTF-8) = 09", msx_io_read(&m, 0x2D), 0x09);
        lfn_ask("\\MP3\\C27479~1.MP3");
        { char b[64]; int n = 0, c; while ((c = msx_io_read(&m, 0x2D)) && n < 60) b[n++] = (char)c; b[n] = 0;
          f += expect("LFN: long name read", !strcmp(b, "c2747995-YB.mp3"), 1); if (strcmp(b, "c2747995-YB.mp3")) { printf("   got '%s' hex:", b); for (int i = 0; i < 20; i++) printf(" %02x", (uint8_t)b[i]); printf("\n"); } }
        lfn_ask("a:\\mp3\\c27479~1.mp3");
        { char b[64]; int n = 0, c; while ((c = msx_io_read(&m, 0x2D)) && n < 60) b[n++] = (char)c; b[n] = 0; f += expect("LFN: drive letter + lower case ok", !strcmp(b, "c2747995-YB.mp3"), 1); }
        lfn_ask("\\MP3");
        { char b[64]; int n = 0, c; while ((c = msx_io_read(&m, 0x2D)) && n < 60) b[n++] = (char)c; b[n] = 0; f += expect("LFN: non-ASCII -> '?'", !strcmp(b, "?"), 1); }
        /* 8.0.89 0Dh LFN_READ_UTF8: 길이(1) + UTF-8 */
        { const char *want = "BTS(\xEB\xB0\xA9\xED\x83\x84\xEC\x86\x8C\xEB\x85\x84\xEB\x8B\xA8) - \xEB\xB4\x84\xEB\x82\xA0.mp3";
          cmd(0x07); { const char *q = "\\KOR\\BTS~1.MP3"; for (; *q; q++) msx_io_write(&m, 0x2D, (uint8_t)*q); msx_io_write(&m, 0x2D, 0); } cmd(0x0D);
          int n = msx_io_read(&m, 0x2D); char b[128]; for (int i = 0; i < n && i < 120; i++) b[i] = (char)msx_io_read(&m, 0x2D); b[n < 120 ? n : 120] = 0;
          f += expect("0Dh: Korean long name length", n, (int)strlen(want)); f += expect("0Dh: Korean long name bytes", !strcmp(b, want), 1);
          f += expect("0Dh: after the name comes 00", msx_io_read(&m, 0x2D), 0x00); }
        { cmd(0x07); { const char *q = "\\KOR"; for (; *q; q++) msx_io_write(&m, 0x2D, (uint8_t)*q); msx_io_write(&m, 0x2D, 0); } cmd(0x0D);
          int n = msx_io_read(&m, 0x2D); char b[16]; for (int i = 0; i < n && i < 15; i++) b[i] = (char)msx_io_read(&m, 0x2D); b[n < 15 ? n : 15] = 0;
          f += expect("0Dh: folder name", !strcmp(b, "\xEC\x9D\x8C\xEC\x95\x85"), 1); }
        lfn_ask("\\SHORT.MP3"); cmd(0x0D); f += expect("0Dh: 8.3 name only -> length 0", msx_io_read(&m, 0x2D), 0);
        cmd(0x07); { const char *q = "\\LOWER.MP3"; for (; *q; q++) msx_io_write(&m, 0x2D, (uint8_t)*q); msx_io_write(&m, 0x2D, 0); } cmd(0x0D);
        f += expect("0Dh: lower-case 8.3 is a long name (length 9)", msx_io_read(&m, 0x2D), 9);
        cmd(0x07); { const char *q = "\\HANGUL40.MP3"; for (; *q; q++) msx_io_write(&m, 0x2D, (uint8_t)*q); msx_io_write(&m, 0x2D, 0); } cmd(0x0D);
        { int n = msx_io_read(&m, 0x2D); f += expect("0Dh: >100 bytes cut at a character boundary (99)", n, 99);
          int ok = 1; for (int i = 0; i < n; i++) { int c = msx_io_read(&m, 0x2D); if (i % 3 == 0 && c != 0xEA) ok = 0; } f += expect("0Dh: cut name is whole characters", ok, 1); }
        cmd(0x07); { const char *q = "\\NOPE.MP3"; for (; *q; q++) msx_io_write(&m, 0x2D, (uint8_t)*q); msx_io_write(&m, 0x2D, 0); } cmd(0x0D);
        f += expect("0Dh: not found -> length 0", msx_io_read(&m, 0x2D), 0);
        /* 08h 는 그대로 (한글 → ?) */
        lfn_ask("\\KOR\\BTS~1.MP3"); { char b[64]; int n = 0, c; while ((c = msx_io_read(&m, 0x2D)) && n < 60) b[n++] = (char)c; b[n] = 0; f += expect("08h unchanged: Korean -> '?'", !strncmp(b, "BTS(\?\?\?\?\?) - \?\?.mp3", 19), 1); if (strncmp(b, "BTS(\?\?\?\?\?) - \?\?.mp3", 19)) { printf("   got '%s' hex:", b); for (int i = 0; i < 20; i++) printf(" %02x", (uint8_t)b[i]); printf("\n"); } }
        /* 0Dh 결과를 읽다가 다른 명령이 오면 끝: 08h 가 다시 08h 의 결과로 */
        cmd(0x07); { const char *q = "\\KOR\\BTS~1.MP3"; for (; *q; q++) msx_io_write(&m, 0x2D, (uint8_t)*q); msx_io_write(&m, 0x2D, 0); } cmd(0x0D); msx_io_read(&m, 0x2D); lfn_ask("\\MP3\\C27479~1.MP3");
        { char b[64]; int n = 0, c; while ((c = msx_io_read(&m, 0x2D)) && n < 60) b[n++] = (char)c; b[n] = 0; f += expect("0Dh then 08h: 08h result", !strcmp(b, "c2747995-YB.mp3"), 1); }
        cmd(0x00); for (int i = 0; i < 6; i++) msx_io_read(&m, 0x2D);
        lfn_ask("\\NOPE.MP3");
        f += expect("LFN: not found -> first byte 00", msx_io_read(&m, 0x2D), 0x00);
        lfn_ask("\\LONG.MP3");
        { char b[64]; int n = 0, c; while ((c = msx_io_read(&m, 0x2D)) && n < 60) b[n++] = (char)c; b[n] = 0; f += expect("LFN: capped at 40 chars", n, 40); }
        f += expect("LFN: nothing went into the sound FIFO", mvsnd_fifo_used(), fu);
        /* 00h 없이 다른 명령이 오면 경로 받기를 끝냄 → 그 뒤 2Dh 쓰기는 다시 소리용 */
        cmd(0x07); msx_io_write(&m, 0x2D, 'X'); cmd(0x77);
        { uint32_t before = mvsnd_fifo_used(); msx_io_write(&m, 0x2D, 0x55); f += expect("LFN: other command ends path mode", mvsnd_fifo_used(), before + 1); }
        /* 80바이트가 차면 끝 */
        cmd(0x07); for (int i = 0; i < 79; i++) msx_io_write(&m, 0x2D, 'A');
        { uint32_t before = mvsnd_fifo_used(); msx_io_write(&m, 0x2D, 'B'); msx_io_write(&m, 0x2D, 'C'); f += expect("LFN: 80 bytes ends path mode", mvsnd_fifo_used(), before + 1); }
        mvsnd_reset();
        cmd(0x00);
    }

    /* 2) OPEN(1500) → 위치 1500, PAUSE 중 그대로 */
    param(1500); cmd(0x01);
    f += expect("OPEN(1500): position = 1500", pos(), 1500);
    cmd(0x03);
    for (int i = 0; i < 30; i++) msx_run_frame(&m);
    f += expect("PAUSE: position unchanged", pos(), 1500);
    cmd(0x02);                                          /* 자료 없이 PLAY → 끊김 비트 */
    for (int i = 0; i < 3; i++) msx_run_frame(&m);
    f += expect("PLAY without data: position stays", pos(), 1500);
    { uint8_t s = st(); f += expect("underrun bit set", s & 1, 1); f += expect("underrun bit cleared on read", st() & 1, 0); f += expect("playing bit", (s >> 3) & 1, 1); }

    /* 3) 조각으로 보내고 재생 → 위치 진행, 믹서 진폭 */
    param(1500); cmd(0x01);
    { int sent = 0, chunk = 1066, frames = 0, played = 0; uint16_t maxlvl = 0; int chunks = 0;
      while (sent < mp3n || frames < 200) {
          if (sent < mp3n && (st() & 0x02)) { int n = mp3n - sent < chunk ? mp3n - sent : chunk; for (int i = 0; i < n; i++) msx_io_write(&m, 0x2D, mp3[sent + i]); sent += n; chunks++; }
          if (!played && chunks >= 8) { cmd(0x02); played = 1; }
          if (played) { msx_run_frame(&m); frames++; if (m.lvl[MSX_CH_MMX] > maxlvl) maxlvl = m.lvl[MSX_CH_MMX]; }
          if (frames > 400) break;
      }
      /* 스트림 3.03초 = 145450 샘플 × 15 / 48000 = 45.45 프레임 → 1545 */
      f += expect_range("after stream: position ~ 1545", pos(), 1543, 1546);
      f += expect("mixer PCM channel amplitude > 0", maxlvl > 100, 1);
      printf("      (sent %d bytes in %d chunks, %d frames, max level %u, decoded %u mp3 frames)\n", sent, chunks, frames, maxlvl, mvsnd_frames_decoded()); }

    /* 5) FIFO 가득 → 비트 1 = 0 (해독은 PCM 예비 0.5초까지만 미리 하므로 나머지는 FIFO 에 쌓임) */
    param(0); cmd(0x01);
    for (int k = 0; k < 2; k++) for (int i = 0; i < mp3n; i++) msx_io_write(&m, 0x2D, mp3[i]);   /* 97 KB > 64 KB (FIFO) */
    f += expect("FIFO full: status bit1 = 0", (st() >> 1) & 1, 0);
    f += expect("FIFO used = size - 1", (long)mvsnd_fifo_used(), (long)MVSND_FIFO_SIZE - 1);
    cmd(0x04);
    f += expect("STOP: FIFO empty", (long)mvsnd_fifo_used(), 0);
    f += expect("STOP: not playing, room again", st() & 0x7F, 0x02);

    /* SETFPS·SETVOL */
    param(1000); cmd(0x01); param(300); cmd(0x05);
    f += expect("SETFPS 30: position before play = first", pos(), 1000);
    param(50); cmd(0x06);
    param(0); cmd(0x05);
    f += expect("SETFPS 0 → default 150 (still 1000)", pos(), 1000);

    /* 6) 상태 저장·불러오기 → STOP, 위치 유지 */
    param(1500); cmd(0x01);
    for (int i = 0; i < mp3n; i++) msx_io_write(&m, 0x2D, mp3[i]);
    cmd(0x02);
    for (int i = 0; i < 60; i++) msx_run_frame(&m);
    { uint16_t p0 = pos(); uint32_t n = msx_st_save(&m, stbuf, sizeof stbuf);
      f += expect("state saved", n > 0, 1);
      f += expect("playing before load", (st() >> 3) & 1, 1);
      for (int i = 0; i < 10; i++) msx_run_frame(&m);
      f += expect("state load", msx_st_load(&m, stbuf, n, 0, 0), 0);
      f += expect("after load: STOP (bit3 = 0)", (st() >> 3) & 1, 0);
      f += expect("after load: position kept", pos(), p0);
      f += expect("after load: FIFO dropped", (long)mvsnd_fifo_used(), 0);
      cmd(0x02); for (int i = 0; i < 5; i++) msx_run_frame(&m);
      f += expect("PLAY after load without OPEN: ignored", (st() >> 3) & 1, 0); }

    /* 4b) 8.0.67: OPEN 하자마자 PLAY (미리 채우지 않음) + 앞에 큰 ID3v2 태그(앨범 그림 같은 0xFF 가 많은 덩어리).  8.0.66 까지는 바닥난 소리를 채우는 짧은 해독이 부분 프레임을 해독기에 넣어 5 프레임만 해독되고 무음이 되었음 */
    for (int variant = 0; variant < 2; variant++) {
        static uint8_t mb[300000]; int mbn = 0;
        if (variant) { memcpy(mb, "ID3\x03\x00\x00\x00\x00\x40\x00", 10); mbn = 10; for (int i = 0; i < 8192; i++) mb[mbn++] = (i % 7 == 0) ? 0xFF : (i % 7 == 1) ? 0xFB : (uint8_t)(i * 31); }   /* 크기 8192 (synchsafe 0x40 00) */
        memcpy(mb + mbn, mp3, (size_t)mp3n); mbn += mp3n;
        mvsnd_reset(); mvsnd_write(0x2E, 0); mvsnd_write(0x2F, 0); mvsnd_write(0x2C, 0x01); mvsnd_write(0x2C, 0x02);
        static int16_t o2[800]; long nzs = 0; int snt = 0;
        for (int fr = 0; fr < 400; fr++) {
            int k = 0; while (snt < mbn && k < 1066 && (mvsnd_read(0x2C) & 0x02)) { mvsnd_write(0x2D, mb[snt++]); k++; }
            mvsnd_render(o2, 800); mvsnd_tick(0, 0); for (int i = 0; i < 800; i++) if (o2[i]) nzs++;
        }
        f += expect_range(variant ? "immediate PLAY + big ID3: decoded frames" : "immediate PLAY: decoded frames", (long)mvsnd_frames_decoded(), 100, 130);
        f += expect_range(variant ? "immediate PLAY + big ID3: nonzero samples" : "immediate PLAY: nonzero samples", nzs, 100000, 400000);
    }

    /* 4) 해독 정확도: 카트리지만 따로 돌려 ffmpeg 해독과 상관계수 */
    mvsnd_reset();
    mvsnd_write(0x2E, 0); mvsnd_write(0x2F, 0); mvsnd_write(0x2C, 0x01);
    mvsnd_write(0x2C, 0x02);
    static int16_t out[400000]; int outn = 0, sent = 0;
    while (outn + 800 <= (int)(sizeof out / sizeof out[0]) && outn < refn + 4800) {
        while (sent < mp3n && (mvsnd_read(0x2C) & 0x02)) { int n = mp3n - sent < 1066 ? mp3n - sent : 1066; for (int i = 0; i < n; i++) mvsnd_write(0x2D, mp3[sent + i]); sent += n; }   /* X86MV 처럼 상태 비트 1 을 보고 보냄 */
        mvsnd_render(out + outn, 800); outn += 800;
    }
    /* 지연 찾기 (처음 1초, 0~4800 샘플) 뒤 상관계수 */
    int best = 0; double bestc = -2;
    for (int pass = 0; pass < 2; pass++)
    for (int lag = pass ? best - 4 : -2400; lag <= (pass ? best + 4 : 4800); lag += pass ? 1 : 4) {
        double sxy = 0, sxx = 0, syy = 0; int cnt = 0;
        for (int i = 4800; i < 48000; i += 3) { int j = i + lag; if (j < 0 || j >= outn) continue; double x = ref[i], y = out[j]; sxy += x * y; sxx += x * x; syy += y * y; cnt++; }
        double c = cnt && sxx > 0 && syy > 0 ? sxy / sqrt(sxx * syy) : 0;
        if (c > bestc) { bestc = c; best = lag; }
    }
    { double sxy = 0, sxx = 0, syy = 0;
      for (int i = 2400; i < refn - 4800; i++) { int j = i + best; if (j < 0 || j >= outn) continue; double x = ref[i], y = out[j]; sxy += x * y; sxx += x * x; syy += y * y; }
      double c = sxy / sqrt(sxx * syy);
      printf("      (lag %d samples, correlation %.4f over %d samples, cart %d samples)\n", best, c, refn, outn);
      f += expect("correlation with ffmpeg >= 0.99", c >= 0.99, 1); }

    /* 7) 8.0.56 일꾼 해독: 같은 스트림을 msx_run_frame 으로 재생하되 해독은 다른 스레드가. 위치·진폭이 메인 혼자와 같아야 하고, 도중 OPEN/STOP 을 섞어도 멈추거나 깨지지 않음 */
    msx_job_post = wpost; msx_job_wait = wwait;
    for (int round = 0; round < 3; round++) {
        param(1500); cmd(0x01);
        int sent = 0, frames = 0, played = 0, chunks = 0, reopened = 0; uint16_t maxlvl = 0;
        while ((sent < mp3n || frames < 200) && frames <= 400) {
            if (sent < mp3n && (st() & 0x02)) { int n = mp3n - sent < 1066 ? mp3n - sent : 1066; for (int i = 0; i < n; i++) msx_io_write(&m, 0x2D, mp3[sent + i]); sent += n; chunks++; }
            if (!played && chunks >= 8) { cmd(0x02); played = 1; }
            if (played) { msx_run_frame(&m); frames++; if (m.lvl[MSX_CH_MMX] > maxlvl) maxlvl = m.lvl[MSX_CH_MMX]; }
            if (round == 2 && frames == 40 && !reopened) { reopened = 1; param(1500); cmd(0x01); sent = 0; chunks = 0; played = 0; frames = 0; }   /* 도중 OPEN: 일꾼이 해독 중이어도 안전 */
        }
        f += expect_range("async: position ~ 1545", pos(), 1543, 1546);
        f += expect("async: mixer PCM amplitude > 0", maxlvl > 100, 1);
    }
    /* 8.0.57 X86MV 처럼 프레임마다 딱 맞춰(1066 바이트) 보내도 해독이 일꾼에서 돌아야 함 (메인이 들어올 때 풀어 버리면 일이 안 올라갔음) */
    { mvsnd_main_n = 0; mvsnd_post_n = 0; uint32_t j0 = mvsnd_job_n;
      param(1500); cmd(0x01);
      int sent = 0, frames = 0; for (int i = 0; i < 8 && sent < mp3n; i++) { for (int k = 0; k < 1066 && sent < mp3n; k++) msx_io_write(&m, 0x2D, mp3[sent++]); }
      cmd(0x02);
      while (frames < 200) { if (sent < mp3n) { int n = mp3n - sent < 1066 ? mp3n - sent : 1066; for (int k = 0; k < n; k++) msx_io_write(&m, 0x2D, mp3[sent + k]); sent += n; } msx_run_frame(&m); frames++; }
      f += expect_range("jit async: position ~ 1545", pos(), 1543, 1546);
      f += expect("jit async: worker did the decoding (posts)", mvsnd_post_n > 20, 1);
      f += expect("jit async: main did not decode (render path)", mvsnd_main_n <= 2, 1);
      printf("      (jit: posts %u, main %u, worker steps %u)\n", mvsnd_post_n, mvsnd_main_n, mvsnd_job_n - j0); }
    f += expect("async: jobs were posted", async_posts > 50, 1);
    printf("      (async posts %d, worker-decoded steps %u)\n", async_posts, mvsnd_job_n);
    for (int k = 0; k < 40; k++) { cmd(0x04); param(0); cmd(0x01); for (int i = 0; i < 3000; i++) msx_io_write(&m, 0x2D, mp3[i]); cmd(0x02); msx_run_frame(&m); }   /* OPEN/STOP/PLAY 를 번갈아 (경쟁 시험) */
    cmd(0x04); msx_run_frame(&m);
    f += expect("async: STOP leaves nothing running", (st() >> 3) & 1, 0);
    msx_job_post = 0; msx_job_wait = 0; msx_run_frame(&m);


    /* ===== 8.0.66 파일 직접 재생 (WAV·FLAC) ===== */
    {
        msx_job_post = 0; msx_job_wait = 0;
        mvsnd_set_file(0);
        cmd(0x00); { uint8_t b[6]; for (int i = 0; i < 6; i++) b[i] = msx_io_read(&m, 0x2D); f += expect("file: no ops → IDENT 5th bit1 = 0", (b[4] >> 1) & 1, 0); f += expect("file: no ops → IDENT 6th = 0", b[5], 0); }
        mvsnd_set_file(&tfops);
        cmd(0x00); { uint8_t b[6]; for (int i = 0; i < 6; i++) b[i] = msx_io_read(&m, 0x2D); f += expect("file: IDENT 5th bit1 = 1", (b[4] >> 1) & 1, 1); f += expect("file: IDENT 6th = 03 (WAV, FLAC; APE bit 0)", b[5], 3); f += expect("file: old features kept (IDENT 'M')", b[0], 'M'); }
        param(100); cmd(0x05);                                          /* 위치 0.1초 단위 */
        int nref = wav_ref48("tests/data/mvsnd_w48.wav", rc, 100000);
        f += expect("file: reference length 72001 (72000 input + 1)", nref, 72001);
        /* WAV 48k */
        fopen_dos("\\W48.WAV");
        f += expect("wav: FILE_INFO format = 1", info[0], 1);
        f += expect("wav: sample rate 48000", info[1] | (info[2] << 8), 48000);
        f += expect("wav: channels 2 / bits 16", info[3] * 100 + info[4], 216);
        f += expect("wav: length 15 (1.5 s)", info[5] | (info[6] << 8) | (info[7] << 16), 15);
        f += expect("wav: title/artist empty", info[8] + info[9], 0);
        f += expect("wav: not playing before PLAY", (st() >> 3) & 1, 0);
        f += expect("wav: position 0 at open", pos(), 0);
        int na = play_all(ra, 100000, 0);
        f += expect("wav: rendered exactly 72000 samples", na, 72000);
        f += expect("wav: bit3 = 0 after the end", (st() >> 3) & 1, 0);
        f += expect("wav: no underrun bit at the end", st() & 1, 0);
        { int bad = 0; for (int i = 0; i < nref && i < na; i++) if (ra[i] != rc[i]) bad++; f += expect("wav 48k: matches reference sample by sample", bad, 0); }
        f += expect_range("wav: position at the end ~ 15", pos(), 14, 15);
        /* FLAC 48k = WAV 48k (무손실이라 같아야 함) */
        fopen_dos("\\F48.FLA");
        f += expect("flac: FILE_INFO format = 2", info[0], 2);
        f += expect("flac: sample rate 48000", info[1] | (info[2] << 8), 48000);
        f += expect("flac: channels 2 / bits 16", info[3] * 100 + info[4], 216);
        f += expect("flac: length 15", info[5] | (info[6] << 8) | (info[7] << 16), 15);
        int nb = play_all(rb, 100000, 0);
        f += expect("flac: same length as WAV", nb, na);
        { int bad = 0; for (int i = 0; i < na && i < nb; i++) if (ra[i] != rb[i]) bad++; f += expect("flac 48k: identical to WAV output", bad, 0); }
        f += expect("flac: bit3 = 0 after the end", (st() >> 3) & 1, 0);
        /* 44.1k: WAV 와 FLAC 이 같고, 길이 = 1.5초 × 48000 */
        fopen_dos("\\W44.WAV"); f += expect("wav44: sample rate 44100", info[1] | (info[2] << 8), 44100);
        na = play_all(ra, 100000, 0);
        fopen_dos("\\F44.FLA"); f += expect("flac44: sample rate 44100", info[1] | (info[2] << 8), 44100);
        f += expect("flac44: title tag", !strcmp((char *)info + 8, "Test Song"), 1);
        f += expect("flac44: artist tag", !strcmp((char *)info + 8 + 10, "Tester"), 1);
        nb = play_all(rb, 100000, 0);
        f += expect_range("44.1k: ~ 72000 samples", na, 71990, 72010);
        f += expect("44.1k: FLAC length = WAV length", nb, na);
        { int bad = 0, mx = 0; for (int i = 0; i < na && i < nb; i++) { if (ra[i] != rb[i]) bad++; if (abs(ra[i]) > mx) mx = abs(ra[i]); } f += expect("44.1k: FLAC identical to WAV", bad, 0); f += expect("44.1k: amplitude > 5000", mx > 5000, 1); }
        /* 24비트 96 kHz 모노 / 8비트 */
        fopen_dos("\\W96.WAV"); f += expect("wav24: FILE_INFO rate FFFFh (>= 96000)", info[1] | (info[2] << 8), 0xFFFF); f += expect("wav24: mono 24-bit", info[3] * 100 + info[4], 124);
        na = play_all(ra, 100000, 0); f += expect_range("wav24 96k: ~ 24000 samples", na, 23990, 24010);
        { int mx = 0; for (int i = 0; i < na; i++) if (abs(ra[i]) > mx) mx = abs(ra[i]); f += expect("wav24: amplitude > 3000", mx > 3000, 1); }
        fopen_dos("\\W8.WAV"); f += expect("wav8: 22050 mono 8-bit", (info[1] | (info[2] << 8)) * 1000 + info[3] * 10 + info[4] / 8, 22050 * 1000 + 10 + 1);
        na = play_all(ra, 100000, 0); f += expect_range("wav8 22.05k: ~ 14400 samples", na, 14390, 14410);
        /* 시간 이동 (0.1초 단위). 48 kHz 라 샘플이 정확히 같아야 함 */
        { int tt[] = { 5, 7, 12, 14, 0 };
          for (int k = 0; k < 5; k++) {
              for (int fm = 0; fm < 2; fm++) {
                  fopen_dos(fm ? "\\F48.FLA" : "\\W48.WAV");
                  param((uint16_t)tt[k]); cmd(0x0B);
                  char w[64]; snprintf(w, sizeof w, "%s seek %d.%ds: position = %d", fm ? "flac" : "wav", tt[k] / 10, tt[k] % 10, tt[k]); f += expect(w, pos(), tt[k]);
                  int n = play_all(ra, 100000, 0); long base = (long)tt[k] * 4800;
                  int bad = 0, cmp = 0;
                  for (int i = 1; i < n && base + i - 1 + 1 < nref; i++) { cmp++; if (ra[i] != rc[base + i]) bad++; }
                  snprintf(w, sizeof w, "%s seek %d.%ds: samples match the file", fm ? "flac" : "wav", tt[k] / 10, tt[k] % 10); f += expect(w, bad, 0);
                  snprintf(w, sizeof w, "%s seek %d.%ds: remaining length", fm ? "flac" : "wav", tt[k] / 10, tt[k] % 10); f += expect_range(w, n, 72000 - (int)base - 6, 72000 - (int)base + 6);
              }
          }
        }
        /* 끝을 지난 시간 이동 → 바로 끝 */
        fopen_dos("\\F48.FLA"); param(500); cmd(0x0B); cmd(0x02); { int16_t t[800]; mvsnd_render(t, 800); mvsnd_render(t, 800); } f += expect("flac: seek past end → ends (bit3 = 0)", (st() >> 3) & 1, 0);
        /* 일꾼 코어 해독으로도 같은 소리 */
        msx_job_post = wpost; msx_job_wait = wwait;
        fopen_dos("\\F48.FLA"); nb = play_all(rb, 100000, 1);
        f += expect("async flac: 72000 samples", nb, 72000);
        { int bad = 0; for (int i = 0; i < nb && i < nref; i++) if (rb[i] != rc[i]) bad++; f += expect("async flac: identical to reference", bad, 0); }
        fopen_dos("\\W44.WAV"); na = play_all(ra, 100000, 1); f += expect_range("async wav44: ~ 72000", na, 71990, 72010);
        msx_job_post = 0; msx_job_wait = 0;
        /* 음량 */
        fopen_dos("\\W48.WAV"); param(50); cmd(0x06); na = play_all(ra, 100000, 0);
        { int bad = 0; for (int i = 0; i < na && i < nref; i++) if (ra[i] != (int16_t)(rc[i] * 50 / 100)) bad++; f += expect("volume 50 applies to file source", bad, 0); }
        param(100); cmd(0x06);
        /* 오류: 없는 파일 / 내용이 틀린 파일 */
        fopen_dos("\\NOPE.WAV"); f += expect("missing file → FILE_INFO[0] = 0", info[0], 0); cmd(0x02); f += expect("missing file: PLAY does nothing", (st() >> 3) & 1, 0);
        fopen_dos("\\BAD.WAV"); f += expect("wrong content (mp3 named .wav) → FILE_INFO[0] = 0", info[0], 0);
        /* 오류 뒤에도 정상 */
        fopen_dos("\\W48.WAV"); f += expect("recovers after an error", info[0], 1);
        /* 파일 재생 뒤 MP3 (OPEN 01h) 도 그대로 */
        cmd(0x04); param(0); cmd(0x01); for (int i = 0; i < 6000; i++) msx_io_write(&m, 0x2D, mp3[i]); cmd(0x02);
        { int16_t t[4800]; int mx = 0; for (int k = 0; k < 6; k++) { mvsnd_render(t, 800); for (int i = 0; i < 800; i++) if (abs(t[i]) > mx) mx = abs(t[i]); }
          f += expect("MP3 path still works after file playback", mx > 500, 1); }
        cmd(0x04);
        /* 읽는 순서: FILE_INFO 뒤 LFN_READ(08h) 는 긴 이름으로 돌아감 */
        lfn_ask("\\MP3\\C27479~1.MP3"); { char nm[20]; for (int i = 0; i < 16; i++) nm[i] = (char)msx_io_read(&m, 0x2D); nm[16] = 0; f += expect("08h after 0Ah returns to the long name", !strncmp(nm, "c2747995-YB.mp3", 15), 1); }
    }
    printf("mvsndtest: %s\n", f ? "FAIL" : "ok");
    return f ? 1 : 0;
}
