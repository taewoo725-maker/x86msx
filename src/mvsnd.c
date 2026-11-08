/* 만든이: KimTaeWoo */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * mvsnd.c (8.0.55) - X86MV 소리 카트리지. 설명은 mvsnd.h.
 *
 * 흐름: 2Dh 쓰기 → FIFO(64 KB 고리) → minimp3(CC0, third_party/minimp3) 가 MPEG-1 Layer 3 프레임 단위로 해독
 *       → 44.1 kHz(또는 스트림이 말하는 주파수) → 48 kHz 선형 보간 → PCM 고리(미리 0.5초) → msx.c 믹서가 프레임마다 n 샘플을 가져감.
 * 해독은 바이트가 들어올 때(2Dh 쓰기)와 믹서가 가져갈 때 조금씩 한다 (한 번에 몇 프레임만 — 시간이 한 곳에 몰리지 않게).
 * 조각 경계가 MP3 프레임 경계가 아니고 OPEN 직후는 프레임 중간부터 올 수 있으므로, minimp3 가 동기 글자를 찾아 앞을 건너뛴다.
 *
 * 8.0.56 일꾼 코어 해독: 메인이 프레임 끝에 mvsnd_tick() 으로 해독 일(mvsnd_job)을 일꾼에게 올리면, 일꾼이 다음 프레임 동안 FIFO→PCM 고리를 채운다.
 *   소유: FIFO 쓰는 쪽(fw)·PCM 읽는 쪽(pr)=메인, FIFO 읽는 쪽(fr)·PCM 쓰는 쪽(pw)·해독기·보간 상태=해독하는 쪽(일꾼, 일이 없으면 메인). 고리는 하나가 쓰고 하나가 읽으므로 acquire/release 만으로 충분.
 *   OPEN·STOP·리셋·상태 불러오기는 해독기를 건드리므로 일이 끝난 뒤(job_wait)에 한다. 일꾼이 없거나 일이 거절되면 전처럼 메인이 직접 해독.
 *
 * 부동소수점을 쓰므로 32비트 커널에서는 opll.c 처럼 387 옵션으로 따로 컴파일한다 (Makefile).
 */
#include <string.h>
#include "mvsnd.h"
#include "mmusb.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#define MINIMP3_NO_STDIO
#include "../third_party/minimp3/minimp3.h"

#define MV_FILE 1
#include "mvflac.c"

#define MV_LIN 4096                            /* 해독기에 한 번에 보여 주는 이어 붙인 바이트 수 (MP3 한 프레임 최대 1440) */
#define MV_MIN 1800                            /* 이만큼은 모여야 해독 시도 (최대 프레임 1440 + 다음 머리 + 여유). 마지막 조각은 다음 바이트가 올 때 해독됨 */

static struct {
    uint8_t fifo[MVSND_FIFO_SIZE]; uint32_t fr, fw;        /* MP3 바이트 고리 (fw - fr = 든 양, 가득 = SIZE - 1) */
    int16_t pcm[MVSND_PCM_SIZE]; uint32_t pr, pw;          /* 48 kHz 해독 결과 고리 (모노 = 좌우 평균) */
    int16_t pcm_l[MVSND_PCM_SIZE];                         /* 8.0.232 왼쪽 몫. 같은 pr/pw 로 함께 움직임 (오른쪽 = 2 × 모노 − 왼쪽) */
    int16_t last_l;                                        /* 8.0.232 push_resampled 의 왼쪽 지난 입력 */
    mp3dec_t dec;
    mp3d_sample_t fbuf[MINIMP3_MAX_SAMPLES_PER_FRAME];
    uint8_t lin[MV_LIN];
    int opened, play;
    uint32_t first, elapsed;                               /* OPEN 첫 프레임, 믹서로 내보낸 샘플 수 */
    int fps10, vol;
    int ident_pos;
    int id3_chk; uint32_t id3_skip;                        /* 8.0.67 스트림 맨 앞의 ID3v2 태그(앨범 그림 등)를 건너뜀: 확인했는지 / 남은 바이트 (해독하는 쪽 소유) */
    int lfn_mode, lfn_n, lfn_pos;                          /* 8.0.62 긴 이름: 경로 받는 중 / 받은 글자 수 / 읽는 위치 */
    char lfn_path[80], lfn_out[44];
    int u8_on, u8_n, u8_pos; uint8_t u8[104];               /* 8.0.89 0Dh LFN_READ_UTF8: 길이 1바이트 + UTF-8 (최대 100) */
    uint16_t pos_latch, param;
    uint8_t underrun;
    int ext, ext_done;                                     /* 8.0.93 카트리지가 해독한 PCM(MP4 의 AAC)을 직접 넣는 중 / 더 넣을 것 없음 */
    int16_t last; uint32_t phase;                          /* 보간: 지난 입력 샘플, 위치(16.16) */
    uint32_t frames, skipped;                              /* 진단: 해독한 프레임 수, 건너뛴 바이트 */
#ifdef MV_FILE
    /* 8.0.66 파일 원본. src: 0 = 2Dh 로 받는 MP3, 1 = WAV, 2 = FLAC.
     * 소유: fpos·fend·feof·rd_mode·finfo = 메인(USB 읽기). dec_done·fl_*·fa/fb·flin = 해독하는 쪽 */
    int src; uint32_t fpos, fend, fsize; int feof, dec_done, rd_mode, finfo_pos;
    char hpath[260];
    uint32_t w_sr, w_off; int w_ch, w_bits, w_ba;
    FlacInfo fi; uint32_t fl_audio, fl_need, fl_skip, fl_n, fl_i;
    int16_t cv[1024];
    int32_t fa[MVFLAC_MAXBS], fb[MVFLAC_MAXBS];
    uint8_t flin[65536];
    uint8_t finfo[96];
    /* 8.0.81 USB 의 MP3 파일 직접 재생: 카트리지가 파일을 FIFO 로 채움 (src 는 0 그대로, 해독은 2Dh 로 받는 MP3 와 같은 길) */
    int mfeed; uint32_t m_off, m_bps; uint32_t m_len10; int m_toc; uint8_t m_tocv[100];
#endif
} S;

#define LD(p) __atomic_load_n(&(p), __ATOMIC_ACQUIRE)
#define ST(p, v) __atomic_store_n(&(p), (v), __ATOMIC_RELEASE)
static uint32_t fifo_used(void) { return (LD(S.fw) - LD(S.fr)) & (MVSND_FIFO_SIZE - 1); }
static uint32_t pcm_ready(void) { return (LD(S.pw) - LD(S.pr)) & (MVSND_PCM_SIZE - 1); }

/* 8.0.56 일꾼에 일 올리기/기다리기 (msx.c 가 플랫폼 함수를 넣어 줌. 없으면 메인이 직접) */
static int (*job_post)(void (*)(void *), void *);
static int (*job_waitf)(int);
static int job_h = -1;                                 /* 올려 둔 일의 번호 (-1 = 없음) */
static int job_dead;                                   /* 일이 시간 초과로 안 돌아옴 → 이후 메인 혼자 */
uint32_t mvsnd_job_n;                                  /* 일꾼이 해독한 횟수 (DIAG) */
uint32_t mvsnd_post_n, mvsnd_rej_n, mvsnd_main_n;      /* 8.0.57 DIAG: 올린 횟수 / 거절돼 메인이 한 횟수 / 일꾼 없이 메인이 해독한 횟수 */
uint64_t mvsnd_job_tsc;                                /* 일꾼이 해독에 쓴 TSC 합 */
static int async_ok(void) { return job_post && job_waitf && !job_dead; }   /* 8.0.57: 일꾼을 쓸 수 있으면 메인은 해독하지 않음 (X86MV 는 소리를 프레임마다 딱 맞춰 보내서, 메인이 들어올 때 풀어 버리면 일꾼에 올릴 것이 안 쌓였음) */
static void job_wait(void) { if (job_h >= 0) { int h = job_h; job_h = -1; if (!job_waitf || !job_waitf(h)) job_dead = 1; } }

static void stream_clear(void)
{
    S.fr = S.fw = 0; S.pr = S.pw = 0;                      /* (호출하는 쪽이 job_wait 를 먼저 했음) */
    mp3dec_init(&S.dec);
    S.last = 0; S.last_l = 0; S.phase = 0; S.underrun = 0;
    S.id3_chk = 0; S.id3_skip = 0;
#ifdef MV_FILE
    S.feof = 0; S.dec_done = 0; S.fl_n = S.fl_i = 0; S.fl_skip = 0;
#endif
}

void mvsnd_reset(void)
{
    job_wait();
    memset(&S, 0, sizeof S);
    stream_clear();
    S.fps10 = 150; S.vol = 100; S.ident_pos = 6;           /* IDENT 를 쓰기 전에는 식별 글자가 안 나옴 */
}

/* 해독 결과(입력 주파수 hz, 모노 또는 스테레오)를 48 kHz 로 바꿔 PCM 고리에 */
static void push_resampled(const int16_t *x, int n, int ch, int hz)
{
    if (hz <= 0) hz = 44100;
    uint32_t step = (uint32_t)(((uint64_t)hz << 16) / 48000u);
    int i = 0;
    while (i < n) {
        int cur = ch >= 2 ? ((int)x[i * 2] + x[i * 2 + 1]) / 2 : x[i];   /* 모노 줄: 예전과 똑같은 값 */
        int cur_l = ch >= 2 ? x[i * 2] : x[i];                            /* 왼쪽 줄 (모노 파일이면 모노와 같음) */
        /* S.phase 는 S.last(지난 입력) 과 cur(이번 입력) 사이의 위치 */
        while (S.phase < 65536u) {
            int v = S.last + (int)(((int64_t)(cur - S.last) * (int)S.phase) >> 16);
            int vl = S.last_l + (int)(((int64_t)(cur_l - S.last_l) * (int)S.phase) >> 16);
            if (((S.pw + 1) & (MVSND_PCM_SIZE - 1)) == LD(S.pr)) return;   /* 고리가 가득 (pump 가 미리 막지만 안전망) */
            S.pcm[S.pw] = (int16_t)v; S.pcm_l[S.pw] = (int16_t)vl; ST(S.pw, (S.pw + 1) & (MVSND_PCM_SIZE - 1));   /* 왼쪽도 pw 를 올리기 전에 써 둠 */
            S.phase += step;
        }
        S.phase -= 65536u; S.last = (int16_t)cur; S.last_l = (int16_t)cur_l; i++;
    }
}

#ifdef MV_FILE
static void pump_file(int budget);
static int file_work(void);
static void feed_file(int maxreads);
#endif
/* 8.0.67: FIFO 맨 앞이 MPEG 오디오 레이어 3 머리이고 그 프레임이 통째로 들어와 있는가 */
static int low_whole(uint32_t used)
{
    static const uint16_t br1[16] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 };
    static const uint8_t br2[16] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 };
    static const uint16_t sr1[4] = { 44100, 48000, 32000, 0 };
    if (used < 4) return 0;
    uint8_t h[4]; for (int i = 0; i < 4; i++) h[i] = S.fifo[(S.fr + (uint32_t)i) & (MVSND_FIFO_SIZE - 1)];
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return 0;
    int ver = (h[1] >> 3) & 3, layer = (h[1] >> 1) & 3, bi = h[2] >> 4, si = (h[2] >> 2) & 3;
    if (ver == 1 || layer != 1 || !bi || bi == 15 || si == 3) return 0;     /* 레이어 3 만 (다른 것은 보통 경로로) */
    uint32_t sr = sr1[si] >> (ver == 3 ? 0 : ver == 2 ? 1 : 2);
    uint32_t len = (ver == 3 ? 144000u * br1[bi] : 72000u * br2[bi]) / sr + ((h[2] >> 1) & 1);
    if (used < len + 4) return 0;                                           /* 다음 머리까지 와야 minimp3 가 이 프레임을 믿음 (마지막 프레임 하나는 다음 데이터가 올 때까지 남음) */
    return S.fifo[(S.fr + len) & (MVSND_FIFO_SIZE - 1)] == 0xFF && (S.fifo[(S.fr + len + 1) & (MVSND_FIFO_SIZE - 1)] & 0xE0) == 0xE0;
}

/* FIFO 의 앞부분을 이어 붙여 해독기에 넣고, 쓴 만큼 FIFO 에서 뺀다. 최대 budget 프레임 */
static void pump(int budget, uint32_t minb)
{
    if (!S.opened) return;
#ifdef MV_FILE
    if (S.src) { pump_file(budget); return; }
#endif
    while (budget-- > 0 && pcm_ready() < MVSND_PRE_SAMPLES) {
        uint32_t used = fifo_used();
        if (used < minb) break;                        /* 프레임 하나 + 다음 머리까지 있어야 동기 글자를 믿고 찾는다 (적게 주면 minimp3 가 전부 쓰레기로 보고 버림) */
        if (!S.id3_chk) {                              /* 8.0.67: 맨 앞이 "ID3" 면 태그 크기만큼 통째로 건너뜀. 앨범 그림(JPEG) 안의 가짜 동기 글자에 속아 한 바이트씩 버리느라 소리가 안 나던 문제 */
            if (used < 10) break;
            uint8_t h[10]; for (int i = 0; i < 10; i++) h[i] = S.fifo[(S.fr + (uint32_t)i) & (MVSND_FIFO_SIZE - 1)];
            if (h[0] == 'I' && h[1] == 'D' && h[2] == '3' && h[3] < 0xFF && h[4] < 0xFF && !((h[6] | h[7] | h[8] | h[9]) & 0x80))
                S.id3_skip = 10u + (((uint32_t)h[6] << 21) | ((uint32_t)h[7] << 14) | ((uint32_t)h[8] << 7) | h[9]) + ((h[5] & 0x10) ? 10u : 0u);
            S.id3_chk = 1;
        }
        if (S.id3_skip) {                              /* 태그 건너뛰기 (한 번에 있는 만큼) */
            uint32_t k = used < S.id3_skip ? used : S.id3_skip;
            ST(S.fr, (S.fr + k) & (MVSND_FIFO_SIZE - 1)); S.id3_skip -= k; S.skipped += k;
            continue;
        }
        if (minb < MV_MIN && !low_whole(used)) break;   /* 8.0.67: 바닥난 소리를 채우는 짧은 해독은 프레임이 통째로 들어와 있을 때만 (부분 프레임을 주면 minimp3 가 동기를 잃고 스트림을 통째로 버려 무음이 됨 — 느린 기종·바로 PLAY) */
        uint32_t n = used < MV_LIN ? used : MV_LIN;
        uint32_t a = MVSND_FIFO_SIZE - S.fr;
        if (a >= n) memcpy(S.lin, &S.fifo[S.fr], n);
        else { memcpy(S.lin, &S.fifo[S.fr], a); memcpy(S.lin + a, S.fifo, n - a); }
        mp3dec_frame_info_t info; memset(&info, 0, sizeof info);
        int samples = mp3dec_decode_frame(&S.dec, S.lin, (int)n, S.fbuf, &info);
        if (info.frame_bytes <= 0) {                     /* 아직 프레임 하나가 다 안 들어옴 → 더 기다림 (가득 찼는데도 못 찾으면 한 바이트 버림) */
            if (used >= MV_LIN) { ST(S.fr, (S.fr + 2048) & (MVSND_FIFO_SIZE - 1)); S.skipped += 2048; continue; }   /* 8.0.67: 4096 바이트 창에 프레임이 없으면 앞 2048 바이트를 버림 (한 바이트씩이면 프레임당 일꾼 한 번에 16바이트밖에 못 넘음). 뒤 2048 바이트는 남겨 창 끝의 프레임을 놓치지 않음 */
            break;
        }
        ST(S.fr, (S.fr + (uint32_t)info.frame_bytes) & (MVSND_FIFO_SIZE - 1));
        if (samples > 0) { S.frames++; push_resampled(S.fbuf, samples, info.channels, info.hz); }
        else S.skipped += (uint32_t)info.frame_bytes;   /* 동기 글자 찾느라 건너뛴 바이트 */
    }
}

/* 해독 한 걸음 (일꾼 또는 메인): 모인 바이트로 몇 프레임, 소리가 거의 바닥나면 스트림 끝(마지막 약 1.8 KB)도 모인 바이트만으로 */
static void decode_step(int budget)
{
    pump(budget, MV_MIN);
    if (S.play && pcm_ready() < 2400) pump(4, 4);
}
static void mvsnd_job(void *arg)
{
    (void)arg;
#if defined(__x86_64__) || defined(__i386__)
    uint64_t t0 = __builtin_ia32_rdtsc(); decode_step(16); mvsnd_job_tsc += __builtin_ia32_rdtsc() - t0;
#else
    decode_step(16);
#endif
    mvsnd_job_n++;
}

/* 프레임 끝(믹서가 소리를 가져간 뒤)에 msx.c 가 부름: 지난 일이 끝나기를 확인하고 새 해독 일을 올린다.
 * post/wait 가 NULL 이면 일꾼 없음 → 메인이 직접(mvsnd_write·mvsnd_render 안에서). */
void mvsnd_tick(int (*post)(void (*)(void *), void *), int (*waitf)(int))
{
    job_wait();                                          /* 지난 일은 이전에 받아 둔 wait 로 */
    job_post = post; job_waitf = waitf;
#ifdef MV_FILE
    feed_file(2);                                        /* 8.0.66 파일 원본이면 메인이 USB 에서 조금 채움 */
#endif
    if (!post || !waitf || job_dead || !S.opened) return;
    if (pcm_ready() >= MVSND_PRE_SAMPLES) return;
#ifdef MV_FILE
    if (S.src) { if (!file_work()) return; }
    else
#endif
    if (fifo_used() < (S.play && pcm_ready() < 2400 ? 4u : MV_MIN)) return;   /* 해독할 것이 없으면 안 올림 */
    job_h = post(mvsnd_job, 0);
    if (job_h >= 0) mvsnd_post_n++;
    else { mvsnd_rej_n++; decode_step(8); }             /* 일꾼이 모두 바쁘면 이번만 메인이 (소리가 끊기지 않게) */
}

uint32_t mvsnd_position(void)
{
    return S.first + (uint32_t)(((uint64_t)S.elapsed * (uint32_t)S.fps10) / 480000u);   /* × fps / 48000, fps = fps10 / 10 */
}

/* 8.0.62 긴 이름 조회: 커널이 넣어 주는 함수 (DOS 가 보는 짧은 이름 경로 → 긴 이름). 없으면 기능 비트 0 */
static int (*lfn_hook)(const char *path, char *out, int max);
void mvsnd_set_lfn(int (*fn)(const char *path, char *out, int max)) { lfn_hook = fn; }

/* 8.0.89 0Dh: 긴 이름을 UTF-8 원문으로. 8.3 형식 그대로(대문자·숫자만, 이름 8 + 확장 3)인 이름은 긴 이름이 따로 없는 것 → 길이 0.
 * 100 바이트를 넘으면 글자 중간에서 끊지 않고 앞에서 자름 */
static int short83(const char *n)
{
    int l = 0, dot = -1;
    for (; n[l]; l++) {
        unsigned char c = (unsigned char)n[l];
        if (c == '.') { if (dot >= 0 || l == 0) return 0; dot = l; continue; }
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c > 0x20 && c < 0x7F && strchr("!#$%&'()-@^_`{}~", c)))) return 0;
    }
    if (l == 0) return 0;
    int base = dot < 0 ? l : dot, ext = dot < 0 ? 0 : l - dot - 1;
    return base <= 8 && ext <= 3 && !(dot >= 0 && ext == 0);
}
static void lfn_utf8(void)
{
    char tmp[260];
    S.lfn_path[S.lfn_n] = 0; S.u8_n = 0; S.u8_pos = 0; S.u8_on = 1; S.u8[0] = 0;
    tmp[0] = 0;
    if (!lfn_hook || lfn_hook(S.lfn_path, tmp, sizeof tmp) <= 0 || short83(tmp)) return;
    int n = (int)strlen(tmp); if (n > 100) { n = 100; while (n > 0 && ((uint8_t)tmp[n] & 0xC0) == 0x80) n--; }   /* tmp[n] 가 이어지는 바이트면 그 글자 앞에서 끊음 */
    S.u8_n = n; S.u8[0] = (uint8_t)n; memcpy(S.u8 + 1, tmp, (size_t)n);
}
static void lfn_lookup(void)
{
    char tmp[260];
    S.lfn_path[S.lfn_n] = 0; S.lfn_out[0] = 0; S.lfn_pos = 0;
    tmp[0] = 0;
    if (lfn_hook && lfn_hook(S.lfn_path, tmp, sizeof tmp) > 0) {
        int k = 0;
        for (const char *q = tmp; *q && k < 40; q++) {                        /* ASCII 만, 그 밖의 글자(UTF-8)는 글자 하나당 '?', 앞 40글자 */
            uint8_t c = (uint8_t)*q;
            if (c >= 0x80 && c < 0xC0) continue;                              /* UTF-8 이어지는 바이트 */
            S.lfn_out[k++] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
        }
        S.lfn_out[k] = 0;
    }
}

#ifdef MV_FILE
/* ===== 8.0.66 파일 직접 재생 (WAV·FLAC) =====
 * 메인: 09h 에서 파일을 열고 머리를 읽고, 틱·렌더에서 16 KB 씩 FIFO 로 채움 (시간 제한).  해독하는 쪽: FIFO → 16비트 → 48 kHz → PCM 고리. */
static const MvFileOps *fops;
void mvsnd_set_file(const MvFileOps *ops) { fops = ops; }
static uint32_t le16(const uint8_t *b) { return (uint32_t)b[0] | ((uint32_t)b[1] << 8); }
static uint32_t le32(const uint8_t *b) { return le16(b) | (le16(b + 2) << 16); }
static int frd(uint32_t off, void *buf, uint32_t n) { return fops->read_at(S.hpath, off, (uint8_t *)buf, n); }

static void feed_file(int maxreads)                    /* 메인 */
{
    if ((!S.src && !S.mfeed) || !fops || LD(S.feof)) return;
    uint32_t t0 = fops->now_ms ? fops->now_ms() : 0;
    while (maxreads-- > 0) {
        uint32_t freeb = MVSND_FIFO_SIZE - 1 - fifo_used();
        if (freeb < 16384) break;
        uint32_t left = S.fend > S.fpos ? S.fend - S.fpos : 0;
        if (!left) { ST(S.feof, 1); break; }
        uint32_t n = left < 16384 ? left : 16384;
        uint32_t a = MVSND_FIFO_SIZE - S.fw; if (n > a) n = a;       /* 고리 끝까지만 (이어서 다음 번에) */
        if (frd(S.fpos, &S.fifo[S.fw], n)) { ST(S.feof, 1); break; }  /* 읽기 실패 = 거기서 끝 */
        S.fpos += n; ST(S.fw, (S.fw + n) & (MVSND_FIFO_SIZE - 1));
        if (S.fpos >= S.fend) { ST(S.feof, 1); break; }
        if (fops->now_ms && fops->now_ms() - t0 >= 8) break;          /* 한 번에 8 ms 이상 USB 에 매이지 않음 */
    }
}
static int file_work(void)                              /* 해독할 일이 있나 */
{
    if (!S.src) return 0;
    if (S.fl_i < S.fl_n) return 1;
    if (LD(S.feof)) return !LD(S.dec_done);
    return fifo_used() >= (S.src == 2 ? S.fl_need : (uint32_t)S.w_ba);
}

static void wav_chunk(uint32_t n)                       /* FIFO 앞 n 바이트(프레임 정수 배)를 16비트로 */
{
    uint32_t a = MVSND_FIFO_SIZE - S.fr;
    if (a >= n) memcpy(S.lin, &S.fifo[S.fr], n); else { memcpy(S.lin, &S.fifo[S.fr], a); memcpy(S.lin + a, S.fifo, n - a); }
    uint32_t fr = n / (uint32_t)S.w_ba; const uint8_t *p = S.lin; int ch = S.w_ch;
    for (uint32_t i = 0; i < fr; i++)
        for (int c = 0; c < ch; c++) {
            int v;
            switch (S.w_bits) {
            case 8: v = ((int)p[0] - 128) << 8; p += 1; break;
            case 16: v = (int16_t)le16(p); p += 2; break;
            case 24: v = (int16_t)le16(p + 1); p += 3; break;
            default: v = (int16_t)le16(p + 2); p += 4; break;
            }
            S.cv[i * (uint32_t)ch + (uint32_t)c] = (int16_t)v;
        }
    ST(S.fr, (S.fr + n) & (MVSND_FIFO_SIZE - 1));
    push_resampled(S.cv, (int)fr, ch, (int)S.w_sr);
}
static void pump_wav(int budget)
{
    uint32_t ba = (uint32_t)S.w_ba;
    while (budget-- > 0 && pcm_ready() < MVSND_PRE_SAMPLES) {
        int e = LD(S.feof); uint32_t used = fifo_used();
        uint32_t n = used < 512 * ba ? used : 512 * ba; n -= n % ba;
        if (!n) { if (e) ST(S.dec_done, 1); break; }
        wav_chunk(n);
    }
}
static void pump_flac(int budget)
{
    while (budget-- > 0 && pcm_ready() < MVSND_PRE_SAMPLES) {
        if (S.fl_i < S.fl_n) {                                     /* 풀어 둔 프레임을 조금씩 내보냄 */
            uint32_t k = S.fl_n - S.fl_i; if (k > 512) k = 512;
            int ch = S.fi.ch, sh = S.fi.bps - 16;
            for (uint32_t i = 0; i < k; i++) {
                int32_t l = S.fa[S.fl_i + i], r = ch > 1 ? S.fb[S.fl_i + i] : 0;
                if (sh > 0) { l >>= sh; r >>= sh; } else if (sh < 0) { l *= (1 << -sh); r *= (1 << -sh); }
                S.cv[i * (uint32_t)ch] = (int16_t)l; if (ch > 1) S.cv[i * 2 + 1] = (int16_t)r;
            }
            S.fl_i += k;
            push_resampled(S.cv, (int)k, ch, (int)S.fi.sr);
            continue;
        }
        int e = LD(S.feof); uint32_t used = fifo_used();
        if (used < S.fl_need && !e) break;
        if (!used) { if (e) ST(S.dec_done, 1); break; }
        uint32_t n = used < S.fl_need ? used : S.fl_need;
        uint32_t a = MVSND_FIFO_SIZE - S.fr;
        if (a >= n) memcpy(S.flin, &S.fifo[S.fr], n); else { memcpy(S.flin, &S.fifo[S.fr], a); memcpy(S.flin + a, S.fifo, n - a); }
        FlacHdr h; int r = mvflac_frame(&S.fi, S.flin, n, S.fa, S.fb, &h);
        if (r > 0) {
            ST(S.fr, (S.fr + (uint32_t)r) & (MVSND_FIFO_SIZE - 1));
            S.frames++;
            uint32_t sk = S.fl_skip >= h.bs ? h.bs : S.fl_skip; S.fl_skip -= sk;   /* SEEK 로 앞을 건너뜀 */
            S.fl_n = h.bs; S.fl_i = sk;
        } else if (r == 0 && e && n == used) {                       /* 파일 끝에서 잘린 마지막 조각 */
            ST(S.fr, S.fw); ST(S.dec_done, 1); break;
        } else {                                                     /* 깨짐: 다음 동기 글자까지 건너뜀 */
            uint32_t k = 1;
            while (k < n && !(S.flin[k] == 0xFF && (S.flin[k + 1 < n ? k + 1 : k] & 0xFE) == 0xF8)) k++;
            if (k >= n) k = n > 1 ? n - 1 : 1;
            ST(S.fr, (S.fr + k) & (MVSND_FIFO_SIZE - 1)); S.skipped += k;
        }
    }
}
static void pump_file(int budget) { if (S.src == 1) pump_wav(budget); else pump_flac(budget); }

static void ascii_tag(char *dst, const char *src, int len)   /* ASCII 만, 그 밖은 글자 하나당 '?', 앞 40글자 */
{
    int k = 0;
    for (int i = 0; i < len && src[i] && k < 40; i++) {
        uint8_t c = (uint8_t)src[i];
        if (c >= 0x80 && c < 0xC0) continue;
        dst[k++] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    }
    dst[k] = 0;
}
static int tag_is(const char *t, const char *key, int n)     /* 대소문자 가리지 않고 앞이 key 인가 (커널에는 strncasecmp 가 없음) */
{
    for (int i = 0; i < n; i++) { char a = t[i], b = key[i]; if (a >= 'a' && a <= 'z') a = (char)(a - 32); if (a != b) return 0; }
    return 1;
}
static void flac_tags(const uint8_t *b, uint32_t n, char *title, char *artist)
{
    if (n < 8) return;
    uint32_t p = 4 + le32(b); if (p + 4 > n) return;
    uint32_t cnt = le32(b + p); p += 4;
    for (uint32_t i = 0; i < cnt && p + 4 <= n; i++) {
        uint32_t l = le32(b + p); p += 4;
        if (l > n - p) l = n - p;
        const char *t = (const char *)b + p;
        if (l >= 6 && tag_is(t, "TITLE=", 6)) ascii_tag(title, t + 6, (int)l - 6);
        else if (l >= 7 && tag_is(t, "ARTIST=", 7)) ascii_tag(artist, t + 7, (int)l - 7);
        p += l;
    }
}

/* FILE_INFO 블록을 만들어 둠 */
static void make_info(int fmt, uint32_t sr, int ch, int bits, uint32_t len10, const char *title, const char *artist)
{
    uint8_t *o = S.finfo; int k = 0;
    memset(o, 0, sizeof S.finfo);
    o[k++] = (uint8_t)fmt;
    o[k++] = (uint8_t)(sr > 65534 ? 0xFF : sr & 0xFF); o[k++] = (uint8_t)(sr > 65534 ? 0xFF : sr >> 8);
    o[k++] = (uint8_t)ch; o[k++] = (uint8_t)bits;
    o[k++] = (uint8_t)len10; o[k++] = (uint8_t)(len10 >> 8); o[k++] = (uint8_t)(len10 >> 16);
    for (const char *q = title; *q && k < 48; q++) o[k++] = (uint8_t)*q;
    o[k++] = 0;
    for (const char *q = artist; *q && k < 90; q++) o[k++] = (uint8_t)*q;
    o[k++] = 0;
}

static int open_wav(void)
{
    uint8_t h[12];
    if (frd(0, h, 12) || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return -1;
    uint32_t p = 12; int have_fmt = 0, tag = 0; uint32_t doff = 0, dlen = 0; int have_data = 0;
    for (int it = 0; it < 64 && p + 8 <= S.fsize; it++) {
        uint8_t c[8]; if (frd(p, c, 8)) return -1;
        uint32_t sz = le32(c + 4);
        if (!memcmp(c, "fmt ", 4) && sz >= 16) {
            uint8_t f[40]; uint32_t rn = sz < 40 ? sz : 40; if (frd(p + 8, f, rn)) return -1;
            tag = (int)le16(f); if (tag == 0xFFFE && rn >= 26) tag = (int)le16(f + 24);
            S.w_ch = (int)le16(f + 2); S.w_sr = le32(f + 4); S.w_ba = (int)le16(f + 12); S.w_bits = (int)le16(f + 14);
            have_fmt = 1;
        } else if (!memcmp(c, "data", 4)) {
            doff = p + 8; dlen = sz; have_data = 1; break;
        }
        p += 8 + sz + (sz & 1);
    }
    if (!have_fmt || !have_data || tag != 1) return -1;
    if (S.w_ch < 1 || S.w_ch > 2 || (S.w_bits != 8 && S.w_bits != 16 && S.w_bits != 24 && S.w_bits != 32)) return -1;
    if (S.w_ba != S.w_ch * S.w_bits / 8 || S.w_sr < 1000 || S.w_sr > 400000) return -1;
    if (doff >= S.fsize) return -1;
    if (dlen == 0 || dlen == 0xFFFFFFFFu || doff + dlen > S.fsize || doff + dlen < doff) dlen = S.fsize - doff;
    dlen -= dlen % (uint32_t)S.w_ba;
    S.w_off = doff; S.fpos = doff; S.fend = doff + dlen; S.src = 1;
    make_info(1, S.w_sr, S.w_ch, S.w_bits, (uint32_t)((uint64_t)(dlen / (uint32_t)S.w_ba) * 10 / S.w_sr), "", "");
    return 0;
}
static int open_flac(void)
{
    uint8_t h[4];
    if (frd(0, h, 4) || memcmp(h, "fLaC", 4)) return -1;
    uint32_t p = 4; int got = 0, last = 0; char title[48] = "", artist[48] = "";
    for (int it = 0; it < 256 && !last && p + 4 <= S.fsize; it++) {
        uint8_t bh[4]; if (frd(p, bh, 4)) return -1;
        last = bh[0] >> 7; int t = bh[0] & 127; uint32_t l = ((uint32_t)bh[1] << 16) | ((uint32_t)bh[2] << 8) | bh[3];
        if (t == 0 && l >= 34) { uint8_t si[34]; if (frd(p + 4, si, 34) || mvflac_info(&S.fi, si)) return -1; got = 1; }
        else if (t == 4) { uint8_t vb[2048]; uint32_t rn = l < sizeof vb ? l : (uint32_t)sizeof vb; if (!frd(p + 4, vb, rn)) flac_tags(vb, rn, title, artist); }
        p += 4 + l;
    }
    if (!got || p >= S.fsize) return -1;
    uint32_t need = S.fi.max_fs ? S.fi.max_fs : S.fi.max_bs * (uint32_t)S.fi.ch * (uint32_t)((S.fi.bps + 7) / 8) + 64;
    if (need < 64) need = 64;
    if (need > sizeof S.flin) return -1;
    S.fl_need = need; S.fl_audio = p; S.fpos = p; S.fend = S.fsize; S.src = 2;
    uint32_t len10 = S.fi.total ? (uint32_t)(S.fi.total * 10 / S.fi.sr) : 0;
    make_info(2, S.fi.sr, S.fi.ch, S.fi.bps, len10, title, artist);
    return 0;
}

/* 09h: 07h 로 받은 경로의 파일을 연다 (STOP 뒤에도 다시 09h) */
static void open_file(void)
{
    job_wait(); S.mfeed = 0; stream_clear();
    S.src = 0; S.opened = 0; S.play = 0; S.elapsed = 0; S.first = 0; S.rd_mode = 0;
    memset(S.finfo, 0, sizeof S.finfo);
    S.lfn_path[S.lfn_n] = 0;
    uint32_t sz = 0;
    if (!fops || !fops->find || !fops->read_at || !fops->find(S.lfn_path, S.hpath, sizeof S.hpath, &sz) || sz < 44) return;
    S.fsize = sz;
    uint8_t m[4]; if (frd(0, m, 4)) return;
    int r = !memcmp(m, "RIFF", 4) ? open_wav() : !memcmp(m, "fLaC", 4) ? open_flac() : -1;
    if (r) { S.src = 0; memset(S.finfo, 0, sizeof S.finfo); return; }
    S.opened = 1; S.rd_mode = 0;
    feed_file(3);                                                    /* 시작이 빠르게 미리 채움 */
}

/* ===== 8.0.81 USB 의 MP3 파일 직접 재생 =====
 * 0Ch: 지금 USB 접근(mmusb)에서 OPEN 해 둔 파일을 MP3 로 연다 (매개값 = 첫 프레임 번호, OPEN 01 과 같음). 파일을 카트리지가 FIFO 로 채우고 해독은 2Dh 로 받는 MP3 와 같은 길.
 * 길이는 머리의 Xing/Info 프레임 수, 없으면 첫 프레임 비트레이트(고정)로 어림. 시간 이동(0Bh)은 Xing 목차(TOC)가 있으면 그것, 없으면 평균 바이트율로. */
static int (*usbcur)(char *path, int max, uint32_t *size);
void mvsnd_set_usbfile(int (*cur)(char *path, int max, uint32_t *size)) { usbcur = cur; }
static int open_mp3_file(void)
{
    static const uint16_t br1[16] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 };
    static const uint8_t br2[16] = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 };
    static const uint16_t sr1[4] = { 44100, 48000, 32000, 0 };
    uint32_t sz = 0;
    S.mfeed = 0;
    MM_CRUMB("mp3 OPEN begin");
    if (!fops || !fops->read_at || !usbcur || !usbcur(S.hpath, sizeof S.hpath, &sz) || sz < 128) return -1;
    S.fsize = sz;
    uint8_t b[8192]; uint32_t off = 0;
    if (frd(0, b, 10)) return -1;
    if (b[0] == 'I' && b[1] == 'D' && b[2] == '3' && b[3] < 0xFF && b[4] < 0xFF && !((b[6] | b[7] | b[8] | b[9]) & 0x80))
        off = 10u + (((uint32_t)b[6] << 21) | ((uint32_t)b[7] << 14) | ((uint32_t)b[8] << 7) | b[9]) + ((b[5] & 0x10) ? 10u : 0u);
    if (off + 4 >= sz) return -1;
    uint32_t rn = sz - off < sizeof b ? sz - off : (uint32_t)sizeof b;
    if (frd(off, b, rn)) return -1;
    uint32_t i = 0; int ver = 0, bi = 0, si = 0, ch = 2; int found = 0;
    for (; i + 4 <= rn; i++) {                                     /* 첫 MP3 프레임 머리 (태그 뒤 여백을 건너뜀) */
        if (b[i] != 0xFF || (b[i + 1] & 0xE0) != 0xE0) continue;
        ver = (b[i + 1] >> 3) & 3; int layer = (b[i + 1] >> 1) & 3; bi = b[i + 2] >> 4; si = (b[i + 2] >> 2) & 3;
        if (ver == 1 || layer != 1 || !bi || bi == 15 || si == 3) continue;
        found = 1; break;
    }
    if (!found) return -1;
    ch = ((b[i + 3] >> 6) & 3) == 3 ? 1 : 2;
    uint32_t sr = (uint32_t)sr1[si] >> (ver == 3 ? 0 : ver == 2 ? 1 : 2);
    uint32_t kbps = ver == 3 ? br1[bi] : br2[bi];
    uint32_t spf = ver == 3 ? 1152u : 576u;
    uint32_t audio = sz - (off + i);
    uint32_t bps = kbps * 125u;                                    /* 고정 비트레이트 가정 (바이트/초) */
    uint32_t dur_ms = bps ? (uint32_t)((uint64_t)audio * 1000 / bps) : 0;
    S.m_toc = 0;
    uint32_t xo = i + 4 + (ver == 3 ? (ch == 1 ? 17u : 32u) : (ch == 1 ? 9u : 17u));
    if (xo + 120 <= rn && (!memcmp(b + xo, "Xing", 4) || !memcmp(b + xo, "Info", 4))) {
        uint32_t fl = ((uint32_t)b[xo + 4] << 24) | ((uint32_t)b[xo + 5] << 16) | ((uint32_t)b[xo + 6] << 8) | b[xo + 7], p = xo + 8, frames = 0;
        if (fl & 1) { frames = ((uint32_t)b[p] << 24) | ((uint32_t)b[p + 1] << 16) | ((uint32_t)b[p + 2] << 8) | b[p + 3]; p += 4; }
        if (fl & 2) p += 4;
        if ((fl & 4) && p + 100 <= rn) { memcpy(S.m_tocv, b + p, 100); S.m_toc = 1; }
        if (frames && sr) { dur_ms = (uint32_t)((uint64_t)frames * spf * 1000 / sr); if (dur_ms) bps = (uint32_t)((uint64_t)audio * 1000 / dur_ms); }
    }
    S.m_off = off + i; S.m_bps = bps ? bps : 16000u; S.m_len10 = dur_ms / 100;
    job_wait(); stream_clear();
    S.src = 0; S.rd_mode = 0; S.play = 0; S.elapsed = 0; S.first = S.param; S.opened = 1;
    S.fpos = 0; S.fend = sz; S.mfeed = 1;                          /* 처음(ID3 포함)부터 먹임: 해독 쪽이 ID3 를 건너뜀 */
    MM_CRUMB("mp3 OPEN header parsed, first feed");
    make_info(3, sr, ch, 0, S.m_len10, "", "");
    feed_file(3);
    MM_CRUMB("mp3 OPEN done");
    return 0;
}
static void seek_mp3(uint32_t t10)
{
    job_wait();
    uint32_t len = S.m_len10 ? S.m_len10 : t10 + 1, audio = S.fsize - S.m_off, pos;
    if (t10 >= len) t10 = len - 1;
    if (S.m_toc) {                                                 /* Xing 목차: 100칸, 값 = 바이트 위치 × 256 / 전체 */
        uint32_t x = (uint32_t)((uint64_t)t10 * 10000 / len), k = x / 100, f = x % 100;
        uint32_t a = S.m_tocv[k > 99 ? 99 : k], c = k >= 99 ? 256u : S.m_tocv[k + 1];
        uint32_t v = a * 100 + (c > a ? (c - a) * f : 0);          /* ×100 배 */
        pos = S.m_off + (uint32_t)((uint64_t)audio * v / 25600u);
    } else pos = S.m_off + (uint32_t)((uint64_t)t10 * S.m_bps / 10);
    if (pos >= S.fsize) pos = S.fsize - 1;
    stream_clear();
    S.id3_chk = 1;                                                 /* 중간부터라 ID3 확인 안 함 */
    S.fpos = pos;
    S.first = (uint32_t)((uint64_t)t10 * (uint32_t)S.fps10 / 100u); S.elapsed = 0; S.underrun = 0;
    feed_file(3);
}

/* FLAC 의 시간 이동: 목표 샘플 ts 를 품은 프레임의 바이트 위치를 이분 탐색 (프레임 머리의 샘플/프레임 번호 사용) */
static int flac_find(uint32_t from, uint32_t limit, uint32_t *off, uint64_t *samp, uint32_t *bs)
{
    uint32_t p = from;
    while (p + 8 < limit) {
        uint32_t n = limit - p; if (n > 16384) n = 16384;
        if (frd(p, S.flin, n)) return 0;
        for (uint32_t i = 0; i + 16 <= n || (n < 16384 && i + 8 < n); i++) {
            if (S.flin[i] != 0xFF || (S.flin[i + 1] & 0xFE) != 0xF8) continue;
            FlacHdr h; int r = mvflac_header(&S.fi, S.flin + i, n - i, &h);
            if (r <= 0 || h.nch != S.fi.ch) continue;
            uint64_t sm = h.var ? h.num : h.num * S.fi.max_bs;
            if (S.fi.total && sm > S.fi.total) continue;
            *off = p + i; *samp = sm; *bs = h.bs; return 1;
        }
        if (n < 16384) break;
        p += n - 16;
    }
    return 0;
}
static void seek_file(uint32_t t10)
{
    if (S.mfeed) { seek_mp3(t10); return; }
    if (!S.src) return;
    job_wait();
    uint64_t sr = S.src == 1 ? S.w_sr : S.fi.sr;
    uint64_t ts = (uint64_t)t10 * sr / 10;
    uint32_t skip = 0, pos;
    if (S.src == 1) {
        uint64_t frames = (S.fend - S.w_off) / (uint32_t)S.w_ba;
        if (ts > frames) ts = frames;
        pos = S.w_off + (uint32_t)ts * (uint32_t)S.w_ba;
    } else {
        pos = S.fl_audio;
        if (ts && S.fi.total && ts < S.fi.total) {
            uint32_t lo_o = S.fl_audio, hi_o = S.fend; uint64_t lo_s = 0, hi_s = S.fi.total;
            for (int it = 0; it < 40 && hi_o > lo_o + 1; it++) {
                uint64_t g = lo_o + (hi_s > lo_s ? (uint64_t)(hi_o - lo_o) * (ts - lo_s) / (hi_s - lo_s) : 0);
                if (g <= lo_o) g = (uint64_t)lo_o + 1;
                if (g >= hi_o) g = (uint64_t)hi_o - 1;
                uint32_t o, bs; uint64_t sm;
                if (!flac_find((uint32_t)g, hi_o, &o, &sm, &bs)) { hi_o = (uint32_t)g; continue; }
                if (sm > ts) { hi_o = o; hi_s = sm; }
                else { lo_o = o; lo_s = sm; if (sm + bs > ts) break; }
            }
            pos = lo_o; skip = (uint32_t)(ts - lo_s);
        } else if (ts && S.fi.total && ts >= S.fi.total) pos = S.fend;
    }
    stream_clear();
    S.fpos = pos; S.fl_skip = skip;
    S.first = (uint32_t)((uint64_t)t10 * (uint32_t)S.fps10 / 100u); S.elapsed = 0;
    S.underrun = 0;
    feed_file(3);
}
#endif

#ifdef MV_FILE
#define FILE_FEATURE_BIT (fops ? 0x02 : 0)
#define FILE_FORMATS (fops ? 0x03 : 0)
#else
#define FILE_FEATURE_BIT 0
#define FILE_FORMATS 0
#endif
static void command(uint8_t c)
{
    mm_crumb_cmd("snd cmd", c, c == 0x01 || c == 0x02 || c == 0x03 || c == 0x04 || c == 0x05 || c == 0x0B || c == 0x0C);
    if (c != 0x0D) S.u8_on = 0;                              /* 8.0.89: 0Dh 결과 읽기는 다른 명령이 오면 끝 */
    if (c != 0x00) S.ident_pos = 6;                          /* 8.0.66: IDENT 읽기는 다른 명령이 오면 끝 (5바이트만 읽던 옛 프로그램이 긴 이름·정보를 읽을 때 6번째 바이트가 끼지 않게) */
    if (S.lfn_mode && c != 0x07) S.lfn_mode = 0;           /* 경로를 받던 중 다른 명령이 오면 받기를 끝냄 (소리 쪽이 막히지 않게) */
    switch (c) {
    case 0x0D: lfn_utf8(); break;                                                 /* 8.0.89 LFN_READ_UTF8: 07h 로 준 경로의 긴 이름 (길이 1 + UTF-8) */
    case 0x07: S.lfn_mode = 1; S.lfn_n = 0; break;                                /* LFN_PATH: 이후 2Dh 쓰기는 경로 글자 (00h 로 끝) */
    case 0x08:
#ifdef MV_FILE
        S.rd_mode = 0;
#endif
        lfn_lookup(); break;                                               /* LFN_READ: 긴 이름 준비, 읽는 위치 처음으로 */
    case 0x00: S.ident_pos = 0;
#ifdef MV_FILE
        S.rd_mode = 0;
#endif
        break;                                                                    /* IDENT */
    case 0x01: job_wait();
#ifdef MV_FILE
        S.src = 0; S.mfeed = 0;
#endif
        stream_clear(); S.first = S.param; S.elapsed = 0; S.play = 0; S.opened = 1; break;   /* OPEN (매개값 = 첫 프레임 번호) */
    case 0x02: if (S.opened) S.play = 1; break;                                   /* PLAY */
    case 0x03: S.play = 0; break;                                                 /* PAUSE (FIFO·해독 상태 유지) */
    case 0x04: job_wait();
#ifdef MV_FILE
        S.src = 0; S.mfeed = 0;
#endif
        S.play = 0; S.opened = 0; S.elapsed = 0; stream_clear(); break;    /* STOP (모두 버림) */
    case 0x05: S.fps10 = S.param ? S.param : 150; break;                          /* SETFPS (×10) */
    case 0x06: S.vol = S.param > 100 ? 100 : S.param; break;                      /* SETVOL 0~100 */
#ifdef MV_FILE
    case 0x09: if (fops) open_file(); break;                                      /* OPEN_FILE (8.0.66) */
    case 0x0A: S.rd_mode = 1; S.finfo_pos = 0; break;                             /* FILE_INFO: 이후 2Dh 읽기는 정보 블록 */
    case 0x0B: if (fops) seek_file(S.param); break;                               /* SEEK (매개값 = 0.1초) */
    case 0x0C: if (fops && open_mp3_file()) { job_wait(); stream_clear(); S.src = 0; S.mfeed = 0; S.opened = 0; S.play = 0; S.rd_mode = 0; memset(S.finfo, 0, sizeof S.finfo); } break;   /* 열기 실패면 멈춘 상태 + 정보 0 */                                  /* 8.0.81 OPEN_USB_MP3: USB 접근에서 연 파일을 MP3 로 */
#endif
    default: break;                                                               /* 모르는 명령은 무시 */
    }
}

uint8_t mvsnd_read(int port)
{
    switch (port & 3) {
    case 0: {                                                                     /* 상태 */
        uint32_t freeb = MVSND_FIFO_SIZE - 1 - fifo_used();
        uint8_t v = (uint8_t)(0x80 | (S.play ? 0x08 : 0) | (freeb >= 2048 ? 0x02 : 0) | (S.underrun ? 0x01 : 0));
        S.underrun = 0;
        return v;
    }
    case 1: {                                                                     /* 식별 */
        if (S.ident_pos < 6) {
            int i = S.ident_pos++;
            if (i < 3) return (uint8_t)"MVS"[i];
            if (i == 3) return 0x01;
            if (i == 4) return (uint8_t)((lfn_hook ? 0x09 : 0) | FILE_FEATURE_BIT | (fops && usbcur ? 0x04 : 0));   /* 5번째 = 기능 비트 (bit0 = 긴 이름 조회, bit1 = 파일 직접 재생, bit2 = MP3 직접 재생, bit3 = 0Dh 긴 이름 UTF-8) */
            return (uint8_t)FILE_FORMATS;                                             /* 6번째 = 파일 형식 (bit0 WAV, bit1 FLAC) */
        }
#ifdef MV_FILE
        if (S.rd_mode == 1) { uint8_t b = S.finfo_pos < (int)sizeof S.finfo ? S.finfo[S.finfo_pos] : 0; if (S.finfo_pos < (int)sizeof S.finfo) S.finfo_pos++; return b; }
#endif
        if (S.u8_on) return S.u8_pos <= S.u8_n ? S.u8[S.u8_pos++] : 0;           /* 8.0.89: 0Dh 결과 (길이, 글자들) */
        if (S.lfn_pos >= 0 && S.lfn_out[0] != 0 && S.lfn_pos <= 40 && S.lfn_out[S.lfn_pos]) return (uint8_t)S.lfn_out[S.lfn_pos++];
        return 0x00;
    }
    case 2: S.pos_latch = (uint16_t)mvsnd_position(); return (uint8_t)(S.pos_latch & 0xFF);
    default: return (uint8_t)(S.pos_latch >> 8);
    }
}

void mvsnd_write(int port, uint8_t v)
{
    switch (port & 3) {
    case 0: command(v); break;
    case 1:                                                                       /* MP3 바이트 */
        if (S.lfn_mode) {                                                         /* 8.0.62: 경로 글자 (소리 FIFO 로는 안 들어감) */
            if (v == 0 || S.lfn_n >= 79) S.lfn_mode = 0; else S.lfn_path[S.lfn_n++] = (char)v;
            break;
        }
        if (S.mfeed) break;                                                       /* 8.0.81 카트리지가 파일로 채우는 중이면 2Dh 바이트는 무시 */
        if (fifo_used() < MVSND_FIFO_SIZE - 1) { S.fifo[S.fw] = v; ST(S.fw, (S.fw + 1) & (MVSND_FIFO_SIZE - 1)); }
        if (job_h < 0 && !async_ok()) pump(2, MV_MIN);   /* 일꾼이 있으면 일꾼이(프레임 끝 tick), 없을 때만 메인이 */                                                          /* 들어올 때 조금씩 미리 해독 */
        break;
    case 2: S.param = (uint16_t)((S.param & 0xFF00) | v); break;
    default: S.param = (uint16_t)((S.param & 0x00FF) | ((uint16_t)v << 8)); break;
    }
}

int mvsnd_render_stereo(int16_t *out, int16_t *outL, int16_t *outR, int n)
{
#ifdef MV_FILE
    feed_file(2);
#endif
    if (job_h < 0 && !async_ok()) { mvsnd_main_n++; pump(8, MV_MIN); if (S.play && pcm_ready() < 2400) pump(4, 4); }   /* 일꾼 없을 때만 메인이 해독 (소리가 거의 바닥나면 스트림 끝도 모인 바이트만으로) */
    if (!S.play) { memset(out, 0, (size_t)n * sizeof *out); if (outL) memset(outL, 0, (size_t)n * sizeof *outL); if (outR) memset(outR, 0, (size_t)n * sizeof *outR); return 0; }
    uint32_t got = 0;
    for (int i = 0; i < n; i++) {
        int s = 0, sl = 0;
        if (S.pr != LD(S.pw)) { s = S.pcm[S.pr]; sl = S.pcm_l[S.pr]; ST(S.pr, (S.pr + 1) & (MVSND_PCM_SIZE - 1)); got++; }
        else {
#ifdef MV_FILE
            if (S.src && LD(S.dec_done)) { } else
#endif
            S.underrun = 1;                                                       /* 해독분이 모자람 → 조용히 + 비트 0 */
        }
        out[i] = (int16_t)(s * S.vol / 100);
        if (outL) {                                                               /* 왼쪽/오른쪽: (왼쪽+오른쪽)/2 가 모노와 같도록 오른쪽 = 2 × 모노 − 왼쪽 */
            int l = sl * S.vol / 100, r = 2 * (s * S.vol / 100) - l;
            outL[i] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l); outR[i] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
        }
    }
#ifdef MV_FILE
    if (S.src && LD(S.dec_done) && S.pr == LD(S.pw)) S.play = 0;                  /* 8.0.66 파일 끝: 소리가 다 나간 뒤 상태 비트 3 = 0 */
    if (S.mfeed && LD(S.feof) && S.pr == LD(S.pw) && fifo_used() < 2048) S.play = 0;   /* 8.0.81 MP3 파일 끝: 다 먹였고 소리가 다 나갔고 남은 것이 프레임 하나 안쪽 */
#endif
    if (S.ext && LD(S.ext_done) && S.pr == LD(S.pw)) S.play = 0;                 /* 8.0.93 밖에서 넣은 PCM 이 다 나감 */
    S.elapsed += got;                                                             /* 실제로 내보낸 샘플만 (끊긴 동안은 위치가 안 감 → 영상이 기다림) */
    return 1;
}


int mvsnd_render(int16_t *out, int n) { return mvsnd_render_stereo(out, 0, 0, n); }

int mvsnd_playing(void) { return S.play; }
void mvsnd_state_get(uint32_t *first, uint32_t *elapsed, int *play) { if (first) *first = S.first; if (elapsed) *elapsed = S.elapsed; if (play) *play = S.play; }
void mvsnd_state_set(uint32_t first, uint32_t elapsed) { mvsnd_reset(); S.first = first; S.elapsed = elapsed; }   /* 그 위치에서 STOP (FIFO 는 버림) */
uint32_t mvsnd_fifo_used(void) { return fifo_used(); }
uint32_t mvsnd_pcm_ready(void) { return pcm_ready(); }

/* ---- 8.0.93 외부 PCM 입력 (X86 Multimedia 가 MP4 의 AAC 를 해독해 넣음). 시계 = 내보낸 48 kHz 샘플 수 (mvsnd_ext_elapsed) ---- */
void mvsnd_ext_open(void) { int vol = S.vol; mvsnd_reset(); if (vol > 0) S.vol = vol; S.ext = 1; }
void mvsnd_ext_clear(void) { S.play = 0; S.pr = S.pw = 0; S.last = 0; S.last_l = 0; S.phase = 0; S.underrun = 0; S.elapsed = 0; S.ext_done = 0; }
void mvsnd_ext_close(void) { if (!S.ext) return; mvsnd_ext_clear(); S.ext = 0; }
int mvsnd_ext_active(void) { return S.ext; }
uint32_t mvsnd_ext_free(void) { return S.ext ? MVSND_PCM_SIZE - 1 - pcm_ready() : 0; }
void mvsnd_ext_push(const int16_t *x, int n, int ch, int hz) { if (S.ext) push_resampled(x, n, ch, hz); }
void mvsnd_ext_play(int on) { if (S.ext) S.play = on ? 1 : 0; }
void mvsnd_ext_finish(void) { ST(S.ext_done, 1); }
int mvsnd_ext_ended(void) { return S.ext && LD(S.ext_done) && S.pr == LD(S.pw); }
uint32_t mvsnd_ext_elapsed(void) { return S.elapsed; }
void mvsnd_ext_volume(int v) { if (v < 0) v = 0; if (v > 100) v = 100; S.vol = v; }
uint32_t mvsnd_frames_decoded(void) { return S.frames; }
