/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* gmsynth.c - 3.4.5 SND3-07 소프트웨어 MIDI 음원 (설명은 gmsynth.h). 정수만 씀 (레거시 본체는 FPU 없이 빌드) */
#include "gmsynth.h"
#include "gmsynth_tab.h"
#include <string.h>

#define SR      48000
#define MAXV    48
#define GHOST   8                       /* 목소리를 뺏길 때 짧게(4 ms) 줄이며 끄는 유령 자리 — 갑자기 끊어 나던 '틱' 잡음 */
#define NV      (MAXV + GHOST)
#define A4INC   39370534u                       /* 440 Hz의 위상 증가 (2^32 / 48000 × 440) */

/* ------------------------------------------------------------------ */
/* 공용                                                                 */
/* ------------------------------------------------------------------ */
static uint32_t pow2_q16(int32_t cents)         /* 2^(cents/1200) Q16 (−12000 ~ +6000 쯤) */
{
    int32_t oct = cents >= 0 ? cents / 1200 : -((-cents + 1199) / 1200);
    int32_t fr = cents - oct * 1200;
    uint32_t v = gm_pow2[fr];
    if (oct >= 0) return oct > 14 ? 0xFFFFFFFFu : v << oct;
    return -oct > 31 ? 0 : v >> -oct;
}
static uint32_t cents_inc(int32_t c)            /* MIDI 음 × 100 + 센트 → 위상 증가 (2^32 = 한 바퀴) */
{
    int32_t d = c - 6900;
    if (d > 6000) d = 6000;
    if (d < -9000) d = -9000;
    return (uint32_t)(((uint64_t)A4INC * pow2_q16(d)) >> 16);
}
static uint32_t ms_samples(uint32_t ms) { uint32_t n = ms * (SR / 1000); return n ? n : 1; }
static int32_t sq7(int v) { return v * v; }     /* 0~127 → 0~16129 (볼륨 곡선) */
static void pan_gain(int p, int32_t *l, int32_t *r)   /* p −500(왼쪽) ~ +500(오른쪽) → 같은 힘 Q15 */
{
    if (p < -500) p = -500;
    if (p > 500) p = 500;
    int k = (p + 500) * 256 / 1000;             /* 0 ~ 256 = 0° ~ 90° */
    *l = gm_sin[256 - k] * 2;                   /* cos */
    *r = gm_sin[k] * 2;
    if (*l > 32767) *l = 32767;
    if (*r > 32767) *r = 32767;
}

/* ------------------------------------------------------------------ */
/* 채널·목소리                                                          */
/* ------------------------------------------------------------------ */
typedef struct {
    uint8_t prog, bank, vol, expr, pan, sustain, mod, rpn_l, rpn_h;
    int32_t bend, bend_range;                   /* bend −8192~8191, 범위(센트) */
    /* 효과 보내기·GS 파트 설정 */
    uint8_t rev, cho;                           /* CC91 리버브·CC93 코러스 보내기 0~127 (처음 40·0) */
    uint8_t nrpn_l, nrpn_h, use_nrpn;           /* CC98·99 (마지막에 고른 것이 NRPN 이면 use_nrpn) */
    uint8_t vrate, vdepth, cutoff, reso, att, dec, rel;   /* GS NRPN 01 08·09·20·21·63·64·66 (64 = 바꾸지 않음) */
    uint8_t level;                              /* GS 파트 레벨 (40 1x 19) — CC7 과 같은 자리 */
    int16_t tune;                               /* RPN 1·2 미세·거친 조율, GS 키 시프트 (센트) */
    uint32_t lph;                               /* 채널 비브라토 위상 */
} Chan;

typedef struct FmPatch FmPatch;
typedef struct {
    uint8_t on, chn, key, vel, kind, released, held;   /* kind 0 FM, 1 타악기, 2 SoundFont */
    uint32_t age;
    int32_t fade_q, fade_dec;                   /* 뺏긴 목소리 줄이기 (Q15 → 0, 한 표본마다 fade_dec). fade_dec 0 = 해당 없음 */
    int32_t gl, gr;                             /* 좌우 이득 Q15 (음량·세기·좌우) */
    int32_t cents;                              /* 기본 음높이 (센트, 휨 빼고) */
    /* FM */
    const FmPatch *p;
    uint32_t pc, pm, ic, im;
    int32_t fb1, fb2;
    int32_t envc, envm;                         /* Q16 0~65536 */
    uint8_t stc, stm;                           /* 0 어택 1 디케이 2 서스테인 3 릴리스 */
    int32_t ac, dc, rc, sc, am_, dm, sm;        /* 증가량·계수 */
    /* 타악기 */
    uint32_t dph, dinc, dinc_end; int32_t dsw;  /* 사인 위상, 증가, 끝 증가, 단계마다 줄이는 양 */
    uint32_t nz; int32_t nprev;
    int32_t tenv, tdec, nenv, ndec, nlev, tlev, hp;
    /* SoundFont */
    const int16_t *smp;
    uint32_t pos, frac, inc, end, ls, le, srate, sinc;   /* sinc = 표본률/48000 Q16 */
    uint8_t loop, est;                          /* 단계: 0 지연 1 어택 2 유지 3 디케이 4 서스테인 5 릴리스 */
    uint32_t ecnt, delay_n, att_n, hold_n, att_inc;   /* att_inc = 32767/att_n Q15 (나눗셈 없이) */
    int32_t ecb, dec_rate, sus_cb, rel_rate, eamp;   /* ecb = 감쇠 센티벨 Q8 */
    int16_t excl;
    /* 5.2.59 */
    int32_t base;                               /* 채널 음량·표현 빼고 이득 Q15 (세기·감쇠·드럼 레벨) */
    int16_t vpan;                               /* 목소리 자체 좌우 (SoundFont pan·드럼 위치) −500~500 */
    int16_t gsend_r, gsend_c;                   /* SoundFont 의 리버브·코러스 보내기 (1/1000) */
    uint8_t dsend_r, dsend_c;                   /* GS 드럼 악기별 보내기 (0~127) */
    int32_t rg, cgl, cgr;                       /* 리버브(모노)·코러스(좌우) 보내기 이득 Q15 */
    int32_t f_f, f_q, f_lo, f_bd;               /* 저역 거르개 (Chamberlin SVF): 계수 Q15, 상태. f_f 0 = 끔 */
    int32_t fc_base, q_base;                    /* SoundFont 거르개 (센트·센티벨) */
} Voice;

/* 음원 상태를 인스턴스(GmSynth)로 — 단자에 꽂힌 소프트웨어 기계마다 하나 (GM_INST_MAX). SoundFont 자료·샘플 캐시는 공용(한 파일).
 * MT-32 엔진(mt32w)은 하나 → GM_MT32 모드는 인스턴스 하나만. 아래 함수들은 "지금 인스턴스" G 를 쓰고(매크로), gmi_*(i, …) 가 G 를 고른 뒤 부름 */
typedef struct {                                /* 필드 이름은 매크로와 겹치지 않게 _s */
    Chan ch_s[16];
    Voice v_s[NV];
    uint32_t age_s;
    int mode_s;
    uint8_t mt_slot_s;                          /* GM_MT32 일 때 쓰는 MT-32 엔진 (0~MT32W_N-1) */
    uint8_t rs_s, dat_s[2], ndat_s, need_s, sysex_s;   /* MIDI 해석: 실행 상태, 자료 */
    uint8_t sx_s[16]; int sxn_s;
    uint8_t sfslot_s, gs_s, master_s;       /* SoundFont 칸(0 GM·1 GS), GS 기계(리듬 파트 SysEx), 마스터 음량 0~127 */
    uint16_t drum_s;                        /* 드럼 파트 채널 비트 (처음 = 채널 10) */
    uint64_t lv_sum; uint32_t lv_n;         /* 소리 나는 동안의 제곱합·표본 수 (진단 "midi rms") */
    uint16_t trim_s; uint8_t cal_s;         /* 음량 맞춤 (Q8, 256 = 그대로) — 표준 구절을 재서 PSG 크기에 맞춤. cal_s 0 = 다시 재야 함 */
    uint32_t lfo_ph_s, lfo_cnt_s;
    uint8_t dlev_s[128], dpan_s[128], dpit_s[128], drev_s[128], dcho_s[128];   /* GS 드럼 악기별 NRPN 18·1A·1C·1D·1E (rr = 음) */
} GmSynth;
static GmSynth gi[GM_INST_MAX + 1] = { [0 ... GM_INST_MAX] = { .master_s = 127, .drum_s = 1u << 9, .trim_s = 192 } };   /* 끝 칸(GM_INST_MAX) = 음량 재기용 빈 인스턴스 */   /* 마스터 음량 127, 드럼 = 채널 10 */
static GmSynth *G = &gi[0];
static void cal_all_dirty(void);
static uint32_t isqrt64(uint64_t x);

/* 효과 (인스턴스마다): GS 리버브(Freeverb 꼴: 빗살 8 + 올패스 4, 좌우) · 코러스(흔들리는 지연, 좌우). 큰 버퍼라 따로 (BSS) */
#define RV_NC 8
#define RV_NA 4
static const uint16_t rv_comb[RV_NC] = { 1214, 1293, 1390, 1476, 1548, 1622, 1694, 1760 };   /* Freeverb 44.1k 길이 × 48/44.1 */
static const uint16_t rv_ap[RV_NA] = { 605, 480, 371, 245 };
#define RV_SPREAD 25
#define CH_LEN 4096
typedef struct {
    int32_t cb[2][RV_NC][1800]; uint16_t ci[2][RV_NC]; int32_t cf[2][RV_NC];   /* 빗살 버퍼·자리·거르개 상태 */
    int32_t ab[2][RV_NA][640]; uint16_t ai[2][RV_NA];
    int32_t chb[2][CH_LEN]; uint16_t chw; uint32_t chph;   /* 코러스 지연 줄, 쓰는 자리, LFO 위상 */
    int32_t rfb, rdamp, rwet;                   /* Q15 */
    int32_t cwet, cfb, cbase, cdep, crev; uint32_t cinc;  /* 코러스: 기본 지연·깊이(표본 Q8), 리버브로 보냄 Q15, LFO 증가 */
    uint8_t rmac, rlev, rtime, cmac, clev, cfbk, cdly, crate, cdepth, c2r;   /* GS 값 (40 01 30~3F) */
    uint32_t idle;                              /* 보내기가 0 인 표본 수 (꼬리 끝나면 계산 안 함) */
} GmFx;
static GmFx gfx[GM_INST_MAX + 1];
#define FX (&gfx[G - gi])
#define ch      (G->ch_s)
#define v       (G->v_s)
#define age_ctr (G->age_s)
#define mode    (G->mode_s)
#define rs      (G->rs_s)
#define dat     (G->dat_s)
#define ndat    (G->ndat_s)
#define need    (G->need_s)
#define sysex   (G->sysex_s)
#define sx      (G->sx_s)
#define sxn     (G->sxn_s)
#define lfo_ph  (G->lfo_ph_s)
#define lfo_cnt (G->lfo_cnt_s)
#include "../third_party/nukedsc55/sc55w.h"
static int sc55_owner(void) { for (int i = 0; i < GM_INST_MAX; i++) if (gi[i].mode_s == GM_SC55) return i; return -1; }   /* 5.2.60 */
static int mt32_owner(int slot) { for (int i = 0; i < GM_INST_MAX; i++) if (gi[i].mode_s == GM_MT32 && gi[i].mt_slot_s == slot) return i; return -1; }   /* 슬롯마다 한 인스턴스 */
#ifdef FE_UEFI
#include "../third_party/mt32emu/mt32w.h"
int gm_mt32_open(int slot, void *arena, uint32_t arena_n, const uint8_t *ctl, uint32_t ctl_n, const uint8_t *pcm, uint32_t pcm_n)
{
    for (int i = 0; i < GM_INST_MAX; i++) if (gi[i].mode_s == GM_MT32 && gi[i].mt_slot_s == slot) gi[i].mode_s = GM_FM;   /* 다시 열기 전에 쓰던 것을 멈춤 */
    mt32w_arena(slot, arena, arena_n);
    return mt32w_open(slot, ctl, ctl_n, pcm, pcm_n);
}
void gm_mt32_close(int slot) { for (int i = 0; i < GM_INST_MAX; i++) if (gi[i].mode_s == GM_MT32 && gi[i].mt_slot_s == slot) gi[i].mode_s = GM_FM; mt32w_close(slot); }
int  gm_mt32_ready(int slot) { return mt32w_ready(slot); }
const char *gm_mt32_name(int slot) { return mt32w_name(slot); }
static void gm_fx_mt32(int rev) { mt32w_fx(rev); }
#else
int gm_mt32_open(int slot, void *arena, uint32_t arena_n, const uint8_t *ctl, uint32_t ctl_n, const uint8_t *pcm, uint32_t pcm_n) { (void)slot; (void)arena; (void)arena_n; (void)ctl; (void)ctl_n; (void)pcm; (void)pcm_n; return -9; }
void gm_mt32_close(int slot) { (void)slot; }
int  gm_mt32_ready(int slot) { (void)slot; return 0; }
const char *gm_mt32_name(int slot) { (void)slot; return ""; }
static void gm_fx_mt32(int rev) { (void)rev; }
static void mt32w_midi(int slot, uint8_t b) { (void)slot; (void)b; }
static void mt32w_reset(int slot) { (void)slot; }
static void mt32w_render(int slot, int32_t *l, int32_t *r, int n) { (void)slot; (void)l; (void)r; (void)n; }
#endif

/* ------------------------------------------------------------------ */
/* FM 음색: GM 16가족 바탕 (비율 ×4, 변조 지수 1/8 라디안 단위, 되먹임, 엔벌로프 ms, 서스테인 0~255) */
/* ------------------------------------------------------------------ */
struct FmPatch {
    uint8_t cr, mr, idx, fb;
    uint16_t ca, cd; uint8_t cs; uint16_t crl;  /* 운반파 어택·디케이·서스테인·릴리스 */
    uint16_t ma, md; uint8_t ms;                /* 변조파 (지수의 엔벌로프) */
    uint8_t lvl;
};
static const FmPatch fam[16] = {
    /* 피아노     */ { 4,  4, 14, 2,   2, 1800,   0, 300,   1,  700,  60, 200 },
    /* 반음 타악기 */ { 4, 14, 16, 0,   1, 1200,   0, 500,   1,  500,  20, 190 },
    /* 오르간     */ { 4,  8,  9, 3,   8,   80, 230, 120,   8,  100, 220, 150 },
    /* 기타       */ { 4,  4, 18, 3,   1, 1100,   0, 200,   1,  300,  30, 200 },
    /* 베이스     */ { 4,  4, 15, 4,   2,  700,  80, 120,   1,  250,  60, 230 },
    /* 현악       */ { 4,  4,  9, 4,  90,  400, 210, 350, 120,  500, 170, 170 },
    /* 합주·합창  */ { 4,  4,  7, 2, 120,  500, 210, 400, 150,  600, 150, 160 },
    /* 금관       */ { 4,  4, 13, 3,  30,  300, 200, 150,  60,  300, 170, 180 },
    /* 리드       */ { 4,  8, 11, 3,  20,  300, 210, 120,  20,  300, 180, 170 },
    /* 파이프     */ { 4,  4,  4, 1,  35,  200, 220, 150,  30,  200, 120, 180 },
    /* 신스 리드  */ { 4,  4, 14, 4,   5,  300, 220, 100,   5,  300, 200, 160 },
    /* 신스 패드  */ { 4,  8,  8, 3, 300,  900, 200, 700, 400,  900, 160, 150 },
    /* 신스 효과  */ { 4, 11, 12, 4, 100, 1500, 120, 900, 100, 1500, 100, 140 },
    /* 민속       */ { 4, 12, 15, 2,   2,  900,   0, 300,   1,  400,  40, 190 },
    /* 타악기     */ { 4, 10, 20, 1,   1,  500,   0, 200,   1,  200,   0, 200 },
    /* 효과음     */ { 4, 13, 30, 7,  20, 1000,  60, 400,  20, 1000,  80, 130 },
};
static FmPatch prog_patch[128];                 /* 가족 바탕을 프로그램마다 조금 바꾼 것 */
static int patches_made;
static void make_patches(void)
{
    for (int p = 0; p < 128; p++) {
        FmPatch q = fam[p >> 3];
        int k = p & 7;
        q.idx = (uint8_t)(q.idx + (k & 3) * 2 - 3 > 1 ? q.idx + (k & 3) * 2 - 3 : 1);
        if (k & 4) { q.mr = (uint8_t)(q.mr == 4 ? 8 : q.mr + 4); q.fb = (uint8_t)(q.fb < 6 ? q.fb + 1 : q.fb); }
        switch (p) {                            /* 몇 가지 특징 있는 음색 */
        case 8: case 9: case 10: case 11: case 12: case 13: case 14: q.mr = (uint8_t)(p == 11 ? 16 : 14); break;   /* 첼레스타·벨 계열 */
        case 16: case 17: case 18: q.mr = 8; q.idx = 6; break;                  /* 오르간 */
        case 19: case 20: q.mr = 4; q.idx = 5; q.ca = 40; break;                 /* 파이프 오르간·리드 오르간 */
        case 24: case 25: q.mr = 12; break;                                      /* 나일론·스틸 기타 */
        case 29: case 30: q.fb = 6; q.idx = 24; q.cd = 2000; q.cs = 120; break;  /* 디스토션 */
        case 32: case 33: case 34: q.mr = 4; q.idx = 12; break;
        case 38: case 39: q.fb = 6; q.idx = 20; break;                           /* 신스 베이스 */
        case 40: case 41: case 42: case 43: case 44: q.ca = 80; break;           /* 현 */
        case 45: q.ca = 1; q.cd = 400; q.cs = 0; break;                          /* 피치카토 */
        case 46: q.ca = 1; q.cd = 1500; q.cs = 0; q.mr = 8; break;               /* 하프 */
        case 47: q.ca = 1; q.cd = 600; q.cs = 0; q.mr = 6; q.idx = 25; break;    /* 팀파니 */
        case 52: case 53: case 54: q.mr = 4; q.idx = 4; q.fb = 1; break;         /* 합창 */
        case 55: q.ca = 1; q.cd = 600; q.cs = 0; q.idx = 30; q.fb = 7; break;    /* 오케스트라 히트 */
        case 56: case 57: case 58: case 59: case 60: case 61: case 62: case 63: break;
        case 71: q.mr = 8; break;                                                /* 클라리넷 (홀수 배음) */
        case 73: case 74: case 75: case 76: case 77: case 78: case 79: q.idx = 3; q.fb = 1; break;   /* 플루트 */
        case 80: q.mr = 8; q.idx = 12; q.fb = 6; break;                          /* 사각파 */
        case 81: q.mr = 4; q.idx = 14; q.fb = 4; break;                          /* 톱니파 */
        case 112: case 113: case 114: case 115: case 116: case 117: case 118: case 119: q.cd = 400; break;
        default: break;
        }
        prog_patch[p] = q;
    }
    patches_made = 1;
}

/* ------------------------------------------------------------------ */
/* SoundFont                                                            */
/* ------------------------------------------------------------------ */
/* SoundFont 자료를 칸(GM_SF_MAX)으로 — 0 = GM 음원용, 1 = GS 기계용. 인스턴스가 자기 칸을 씀 (gm_use 가 SFD 를 맞춤) */
#define SC_N 1024
typedef struct {
    const uint8_t *phdr, *pbag, *pgen, *inst, *ibag, *igen, *shdr;
    uint32_t nphdr, npbag, npgen, ninst, nibag, nigen, nshdr;
    const int16_t *smp; uint32_t nsmp;
    int ok, stream;                             /* 4.0.133: stream 1 = 샘플을 파일에서 그때그때 읽음 */
    uint32_t smp_off; gm_rd_fn rd;              /* smpl 자료의 파일 안 위치 */
    struct { uint32_t s, e, off, bytes, seq; } sc[SC_N];   /* 읽어 둔 샘플 (bytes 0 = 빈 칸) */
    uint8_t *pool; uint32_t cap, wp, seq;
} SfData;
static SfData sfd[GM_SF_MAX];
static SfData *SFD = &sfd[0];
#define sf_phdr (SFD->phdr)
#define sf_pbag (SFD->pbag)
#define sf_pgen (SFD->pgen)
#define sf_inst (SFD->inst)
#define sf_ibag (SFD->ibag)
#define sf_igen (SFD->igen)
#define sf_shdr (SFD->shdr)
#define sf_nphdr (SFD->nphdr)
#define sf_npbag (SFD->npbag)
#define sf_npgen (SFD->npgen)
#define sf_ninst (SFD->ninst)
#define sf_nibag (SFD->nibag)
#define sf_nigen (SFD->nigen)
#define sf_nshdr (SFD->nshdr)
#define sf_smp (SFD->smp)
#define sf_nsmp (SFD->nsmp)
#define sf_ok (SFD->ok)
#define sf_stream (SFD->stream)
#define sf_smp_off (SFD->smp_off)
#define sf_rd (SFD->rd)
#define sc_pool (SFD->pool)
#define sc_cap (SFD->cap)
#define sc_wp (SFD->wp)
#define sc_seq (SFD->seq)
static int gm_len_pct = 100;
static void fx_params(void)                     /* GS 값 → 계수 */
{
    GmFx *f = FX;
    static const uint8_t room[8] = { 50, 60, 70, 80, 88, 75, 45, 45 }, damp[8] = { 60, 50, 50, 40, 30, 10, 50, 50 };   /* Room1·2·3 Hall1·2 Plate Delay PanDelay (지연 둘은 가까운 방으로) */
    int m = f->rmac & 7;
    int rt = f->rtime * gm_len_pct / 100; if (rt > 127) rt = 127;   /* 잔향 길이 막대 (100 = 곡이 정한 값) */
    int32_t r = room[m] * (64 + rt) / 128; if (r > 99) r = 99;
    f->rfb = 22938 + 9175 * r / 100;            /* 0.7 + 0.28 × 방 크기 */
    if (f->rfb > 32440) f->rfb = 32440;
    f->rdamp = 13107 * damp[m] / 100;           /* × 0.4 */
    f->rwet = f->rlev * 516;                    /* 레벨 64 ≈ 1.0 */
    f->cwet = f->clev * 400;
    f->cfb = f->cfbk * 128;
    f->crev = f->c2r * 258;
    f->cbase = (int32_t)(SR / 1000) * (4 * 256 + f->cdly * 26);   /* 4 ms + 0.1 ms/단계 (Q8 표본) */
    f->cdep = (int32_t)(SR / 1000) * f->cdepth * 26;               /* 0.1 ms/단계 */
    if (f->cbase + f->cdep > (CH_LEN - 4) << 8) f->cdep = ((CH_LEN - 4) << 8) - f->cbase;
    f->cinc = (uint32_t)((uint64_t)(f->crate ? f->crate : 1) * 122 * 4294967ull / 1000 / SR * 1000);   /* 0.122 Hz/단계 */
}
static void fx_reset(void)
{
    GmFx *f = FX;
    memset(f, 0, sizeof *f);
    f->rmac = 4; f->rlev = 40; f->rtime = 64;   /* GS 처음값은 Hall2, 64, 64 인데 SoundFont·FM 에서는 과해서 5.2.148 에 레벨 40 으로 */
    f->cmac = 2; f->clev = 64; f->cfbk = 8; f->cdly = 80; f->crate = 3; f->cdepth = 19; f->c2r = 0;   /* Chorus3 */
    fx_params();
}
static void fx_macro_chorus(int m)               /* GS 코러스 매크로 0~7: Chorus1~4, FB Chorus, Flanger, Short Delay, Short Delay(FB) */
{
    static const uint8_t fb[8] = { 0, 5, 8, 16, 64, 112, 0, 80 }, dl[8] = { 112, 80, 80, 64, 127, 127, 127, 127 }, rt[8] = { 3, 9, 3, 9, 2, 1, 0, 0 }, dp[8] = { 5, 19, 19, 16, 24, 5, 127, 127 };
    GmFx *f = FX; m &= 7; f->cmac = (uint8_t)m; f->cfbk = fb[m]; f->cdly = dl[m]; f->crate = rt[m]; f->cdepth = m >= 6 ? 0 : dp[m]; fx_params();
}
/* 폭주 방지 — 효과·거르개 값이 한 번 크게 벗어나면 int32 가 넘쳐 잡음이 영원히 이어짐(리부팅 전까지 모든 소리가 묻힘). 값을 묶고, 벗어나면 그 칸만 비움 */
unsigned gm_guard_hits;
#define GCL(x, m) ((x) > (m) ? (m) : (x) < -(m) ? -(m) : (x))
#define GM_LIM (1 << 22)
static void fx_flush(GmFx *f) { memset(f->cb, 0, sizeof f->cb); memset(f->cf, 0, sizeof f->cf); memset(f->ab, 0, sizeof f->ab); memset(f->chb, 0, sizeof f->chb); gm_guard_hits++; }
static void fx_run(int32_t rin, int32_t cl, int32_t cr, int32_t *ol, int32_t *orr)   /* 표본 하나: 리버브 입력(모노)·코러스 입력(좌우) → 효과 출력 */
{
    GmFx *f = FX;
    rin = GCL(rin, GM_LIM); cl = GCL(cl, GM_LIM); cr = GCL(cr, GM_LIM);
    if (!rin && !cl && !cr) { if (f->idle > SR * 8) { *ol = *orr = 0; return; } f->idle++; } else f->idle = 0;
    /* 코러스 */
    int32_t col = 0, cor = 0;
    if (f->clev) {
        f->chph += f->cinc;
        uint32_t ph = f->chph;
        for (int s2 = 0; s2 < 2; s2++) {
            uint32_t p2 = ph + (s2 ? 0x40000000u : 0);   /* 오른쪽은 90° 어긋남 */
            int32_t tri = (int32_t)(p2 >> 16); tri = tri < 32768 ? tri : 65535 - tri;   /* 0~32767 */
            int32_t d = f->cbase + (int32_t)((int64_t)f->cdep * tri >> 15);           /* Q8 표본 */
            int32_t di = d >> 8, fr = d & 255;
            int32_t a = f->chb[s2][(f->chw - di) & (CH_LEN - 1)], b = f->chb[s2][(f->chw - di - 1) & (CH_LEN - 1)];
            int32_t y = a + (int32_t)(((int64_t)(b - a) * fr) >> 8);
            { int32_t w_ = (s2 ? cr : cl) + (int32_t)((int64_t)y * f->cfb >> 15); f->chb[s2][f->chw] = GCL(w_, GM_LIM << 2); }
            if (s2) cor = y; else col = y;
        }
        f->chw = (uint16_t)((f->chw + 1) & (CH_LEN - 1));
        rin += (int32_t)((int64_t)(col + cor) * f->crev >> 16);
        col = (int32_t)((int64_t)col * f->cwet >> 15); cor = (int32_t)((int64_t)cor * f->cwet >> 15);
    }
    /* 리버브 */
    int32_t rl = 0, rr = 0;
    if (f->rlev) {
        int32_t in = (int32_t)((int64_t)rin * 492 >> 15);   /* Freeverb 들어가는 이득 0.015 */
        for (int s2 = 0; s2 < 2; s2++) {
            int32_t acc = 0;
            for (int k = 0; k < RV_NC; k++) {
                int len = rv_comb[k] + (s2 ? RV_SPREAD : 0);
                int32_t *bf = f->cb[s2][k]; uint16_t *ix = &f->ci[s2][k];
                int32_t y = bf[*ix];
                f->cf[s2][k] = (int32_t)(((int64_t)y * (32768 - f->rdamp) + (int64_t)f->cf[s2][k] * f->rdamp) >> 15);
                { int32_t w_ = in + (int32_t)((int64_t)f->cf[s2][k] * f->rfb >> 15); bf[*ix] = GCL(w_, GM_LIM << 2); }
                if (++*ix >= len) *ix = 0;
                acc += y;
            }
            for (int k = 0; k < RV_NA; k++) {
                int len = rv_ap[k] + (s2 ? RV_SPREAD : 0);
                int32_t *bf = f->ab[s2][k]; uint16_t *ix = &f->ai[s2][k];
                int32_t b = bf[*ix];
                { int32_t w_ = acc + (b >> 1); bf[*ix] = GCL(w_, GM_LIM << 2); }
                acc = b - acc;
                if (++*ix >= len) *ix = 0;
            }
            if (s2) rr = acc; else rl = acc;
        }
        rl = (int32_t)((int64_t)rl * f->rwet >> 15); rr = (int32_t)((int64_t)rr * f->rwet >> 15);
    }
    *ol = col + rl; *orr = cor + rr;
    if (*ol > GM_LIM || *ol < -GM_LIM || *orr > GM_LIM || *orr < -GM_LIM) { fx_flush(f); *ol = *orr = 0; }
}
static void sf_kill_voices(void)                /* 지금 칸을 쓰는 모든 인스턴스의 SoundFont 목소리 끔 */
{
    int slot = (int)(SFD - sfd);
    for (int q = 0; q < GM_INST_MAX; q++) if (gi[q].sfslot_s == slot) for (int i = 0; i < NV; i++) if (gi[q].v_s[i].kind == 2) gi[q].v_s[i].on = 0;
}

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }
static int tag(const uint8_t *p, const char *t) { return p[0] == t[0] && p[1] == t[1] && p[2] == t[2] && p[3] == t[3]; }

int gm_load_sf2(const uint8_t *d, uint32_t len)
{
    cal_all_dirty();
    sf_ok = 0;
    if (!d || len < 12 || !tag(d, "RIFF") || !tag(d + 8, "sfbk")) return -1;
    uint32_t end = 8 + rd32(d + 4);
    if (end > len) end = len;
    for (uint32_t o = 12; o + 12 <= end; ) {
        uint32_t sz = rd32(d + o + 4);
        if (tag(d + o, "LIST")) {
            const uint8_t *lt = d + o + 8;
            uint32_t le = o + 8 + sz > end ? end : o + 8 + sz;
            for (uint32_t s = o + 12; s + 8 <= le; ) {
                uint32_t n = rd32(d + s + 4);
                const uint8_t *c = d + s + 8;
                if (s + 8 + n > le) break;
                if (tag(lt, "sdta") && tag(d + s, "smpl")) { sf_smp = (const int16_t *)(const void *)c; sf_nsmp = n / 2; }
                if (tag(lt, "pdta")) {
                    if (tag(d + s, "phdr")) { sf_phdr = c; sf_nphdr = n / 38; }
                    else if (tag(d + s, "pbag")) { sf_pbag = c; sf_npbag = n / 4; }
                    else if (tag(d + s, "pgen")) { sf_pgen = c; sf_npgen = n / 4; }
                    else if (tag(d + s, "inst")) { sf_inst = c; sf_ninst = n / 22; }
                    else if (tag(d + s, "ibag")) { sf_ibag = c; sf_nibag = n / 4; }
                    else if (tag(d + s, "igen")) { sf_igen = c; sf_nigen = n / 4; }
                    else if (tag(d + s, "shdr")) { sf_shdr = c; sf_nshdr = n / 46; }
                }
                s += 8 + n + (n & 1);
            }
        }
        o += 8 + sz + (sz & 1);
    }
    if (!sf_smp || sf_nphdr < 2 || sf_npbag < 2 || !sf_npgen || sf_ninst < 2 || sf_nibag < 2 || !sf_nigen || sf_nshdr < 2) return -1;
    sf_kill_voices();
    sf_ok = 1;
    return 0;
}
void gm_unload_sf2(void) { cal_all_dirty(); sf_kill_voices(); sf_ok = 0; sf_stream = 0; memset(SFD->sc, 0, sizeof SFD->sc); sc_wp = 0; }

/* 4.0.133: 샘플 자리 밀어내기 — 그 자리를 쓰는 목소리는 끔 */
static void sc_evict(int i)
{
    uintptr_t a = (uintptr_t)(sc_pool + SFD->sc[i].off), b = a + SFD->sc[i].bytes;
    int slot = (int)(SFD - sfd);
    for (int g = 0; g < GM_INST_MAX; g++) if (gi[g].sfslot_s == slot)   /* 같은 칸을 쓰는 모든 인스턴스 */
        for (int k = 0; k < NV; k++) { Voice *x = &gi[g].v_s[k]; if (x->on && x->kind == 2) { uintptr_t q = (uintptr_t)(x->smp + x->pos); if (q >= a && q < b) x->on = 0; } }
    SFD->sc[i].bytes = 0;
}
/* [*startp, *endp) 샘플을 담아 start 번 표본이 data[start]로 보이는 포인터를 돌려줌 (실패 NULL). 너무 긴 샘플은 끝을 자름 */
static const int16_t *sf_cache_get(uint32_t start, uint32_t *endp)
{
    uint32_t end = *endp;
    if (end - start > (4u << 20)) end = start + (4u << 20);
    *endp = end;
    for (int i = 0; i < SC_N; i++)
        if (SFD->sc[i].bytes && SFD->sc[i].s == start && SFD->sc[i].e == end) { SFD->sc[i].seq = ++sc_seq; return (const int16_t *)(const void *)(sc_pool + SFD->sc[i].off) - start; }
    uint32_t n = end - start, bytes = ((n + 2) * 2 + 15) & ~15u;
    if (bytes > sc_cap) return 0;
    if (sc_wp + bytes > sc_cap) sc_wp = 0;
    for (int i = 0; i < SC_N; i++) if (SFD->sc[i].bytes && SFD->sc[i].off < sc_wp + bytes && SFD->sc[i].off + SFD->sc[i].bytes > sc_wp) sc_evict(i);
    int slot = -1;
    for (int i = 0; i < SC_N; i++) if (!SFD->sc[i].bytes) { slot = i; break; }
    if (slot < 0) { slot = 0; for (int i = 1; i < SC_N; i++) if (SFD->sc[i].seq < SFD->sc[slot].seq) slot = i; sc_evict(slot); }
    uint8_t *dst = sc_pool + sc_wp;
    if (sf_rd(sf_smp_off + start * 2, dst, n * 2)) return 0;
    memset(dst + n * 2, 0, bytes - n * 2);
    SFD->sc[slot].s = start; SFD->sc[slot].e = end; SFD->sc[slot].off = sc_wp; SFD->sc[slot].bytes = bytes; SFD->sc[slot].seq = ++sc_seq;
    sc_wp += bytes;
    return (const int16_t *)(const void *)dst - start;
}

int gm_load_sf2_stream(gm_rd_fn rd, uint32_t fsize, uint8_t *pool, uint32_t cap)
{
    cal_all_dirty();
    uint8_t h[12], c8[8];
    sf_ok = 0; sf_stream = 0;
    sf_kill_voices();
    if (!rd || !pool || fsize < 12 || rd(0, h, 12) || !tag(h, "RIFF") || !tag(h + 8, "sfbk")) return -1;
    uint32_t end = 8 + rd32(h + 4);
    if (end > fsize) end = fsize;
    uint32_t sm_off = 0, sm_n = 0, pd_off = 0, pd_n = 0;
    for (uint32_t o = 12; o + 12 <= end; ) {
        if (rd(o, h, 12)) return -1;
        uint32_t sz = rd32(h + 4);
        if (tag(h, "LIST")) {
            if (tag(h + 8, "sdta")) {
                for (uint32_t s = o + 12; s + 8 <= o + 8 + sz && s + 8 <= end; ) {
                    if (rd(s, c8, 8)) return -1;
                    uint32_t n = rd32(c8 + 4);
                    if (tag(c8, "smpl")) { sm_off = s + 8; sm_n = n; }
                    s += 8 + n + (n & 1);
                }
            } else if (tag(h + 8, "pdta")) { pd_off = o + 8; pd_n = sz; }
        }
        o += 8 + sz + (sz & 1);
    }
    if (!sm_n || pd_n < 12 || pd_n + (1u << 20) > cap || sm_off + sm_n > fsize) return -1;
    if (rd(pd_off, pool, pd_n)) return -1;
    sf_nphdr = sf_npbag = sf_npgen = sf_ninst = sf_nibag = sf_nigen = sf_nshdr = 0;
    for (uint32_t s = 4; s + 8 <= pd_n; ) {
        uint32_t n = rd32(pool + s + 4);
        const uint8_t *c = pool + s + 8;
        if (s + 8 + n > pd_n) break;
        if (tag(pool + s, "phdr")) { sf_phdr = c; sf_nphdr = n / 38; }
        else if (tag(pool + s, "pbag")) { sf_pbag = c; sf_npbag = n / 4; }
        else if (tag(pool + s, "pgen")) { sf_pgen = c; sf_npgen = n / 4; }
        else if (tag(pool + s, "inst")) { sf_inst = c; sf_ninst = n / 22; }
        else if (tag(pool + s, "ibag")) { sf_ibag = c; sf_nibag = n / 4; }
        else if (tag(pool + s, "igen")) { sf_igen = c; sf_nigen = n / 4; }
        else if (tag(pool + s, "shdr")) { sf_shdr = c; sf_nshdr = n / 46; }
        s += 8 + n + (n & 1);
    }
    if (sf_nphdr < 2 || sf_npbag < 2 || !sf_npgen || sf_ninst < 2 || sf_nibag < 2 || !sf_nigen || sf_nshdr < 2) return -1;
    uint32_t hb = (pd_n + 15) & ~15u;
    sc_pool = pool + hb; sc_cap = (cap - hb) & ~15u; sc_wp = 0; sc_seq = 0;
    memset(SFD->sc, 0, sizeof SFD->sc);
    sf_rd = rd; sf_smp_off = sm_off; sf_smp = 0; sf_nsmp = sm_n / 2; sf_stream = 1;
    sf_ok = 1;
    return 0;
}
int gm_sf2_presets(void) { return sf_ok ? (int)sf_nphdr - 1 : 0; }

/* 발생기 번호 */
enum { G_START = 0, G_END = 1, G_LSTART = 2, G_LEND = 3, G_STARTC = 4, G_PAN = 17, G_DELAY = 33, G_ATTACK = 34, G_HOLD = 35, G_DECAY = 36,
       G_SUSTAIN = 37, G_RELEASE = 38, G_INST = 41, G_KEYR = 43, G_VELR = 44, G_LSTARTC = 45, G_KEYNUM = 46, G_VEL = 47, G_ATTEN = 48,
       G_LENDC = 50, G_COARSE = 51, G_FINE = 52, G_SAMPLE = 53, G_MODES = 54, G_SCALE = 56, G_EXCL = 57, G_ROOT = 58, G_ENDC = 12, G_N = 61 };
static void inst_defaults(int16_t *g)
{
    memset(g, 0, sizeof(int16_t) * G_N);
    g[8] = 13500; g[G_DELAY] = g[G_ATTACK] = g[G_HOLD] = g[G_DECAY] = g[G_RELEASE] = -12000;
    g[G_SCALE] = 100; g[G_ROOT] = -1; g[G_KEYNUM] = -1; g[G_VEL] = -1;
    g[G_KEYR] = (int16_t)(127 << 8); g[G_VELR] = (int16_t)(127 << 8);
}
static void apply_gens(const uint8_t *gen, uint32_t g0, uint32_t g1, uint32_t ng, int16_t *g)
{
    for (uint32_t k = g0; k < g1 && k < ng; k++) {
        uint32_t op = rd16(gen + k * 4);
        if (op < G_N) g[op] = (int16_t)rd16(gen + k * 4 + 2);
    }
}
static int in_range(int16_t r, int x) { int lo = r & 0xFF, hi = (r >> 8) & 0xFF; return x >= lo && x <= hi; }
static uint32_t tc_samples(int32_t tc)          /* 타임센트 → 표본 수 */
{
    if (tc <= -12000) return 0;
    if (tc > 8000) tc = 8000;
    return (uint32_t)(((uint64_t)SR * pow2_q16(tc)) >> 16);
}

/* ------------------------------------------------------------------ */
/* 목소리 고르기·켜기                                                    */
/* ------------------------------------------------------------------ */
static Voice *alloc_voice(void)
{
    Voice *best = 0;
    for (int i = 0; i < MAXV; i++) if (!v[i].on) return &v[i];
    for (int i = 0; i < MAXV; i++) if (v[i].released && (!best || v[i].age < best->age)) best = &v[i];   /* 놓은 것 중 가장 오래된 것 */
    if (!best) for (int i = 0; i < MAXV; i++) if (!best || v[i].age < best->age) best = &v[i];
    if (best && best->on) {                     /* 소리 나는 목소리를 뺏을 땐 유령으로 옮겨 4 ms 동안 줄임 (바로 끊으면 '틱') */
        for (int g = MAXV; g < NV; g++) if (!v[g].on) {
            v[g] = *best; v[g].chn = 255; v[g].key = 255; v[g].released = 1; v[g].held = 0; v[g].fade_q = 32767; v[g].fade_dec = 32768 * 1000 / (SR * 4);
            break;
        }
    }
    return best;
}
static int32_t bend_cents(const Chan *c) { return c->bend * c->bend_range / 8192 + c->tune; }
/* 목소리 이득을 채널 값(음량·표현·파트 레벨·좌우·보내기)으로 다시 셈 — 소리 나는 중에도 CC7·10·11·91·93 이 바로 먹음 */
static void voice_gains(Voice *x)
{
    const Chan *c = &ch[x->chn];
    int32_t g = (int32_t)((int64_t)x->base * sq7(c->vol) / 16129 * sq7(c->expr) / 16129 * sq7(c->level) / 16129);
    int32_t l, r;
    pan_gain(x->vpan + (c->pan - 64) * 500 / 64, &l, &r);
    x->gl = (int32_t)((int64_t)g * l >> 15);
    x->gr = (int32_t)((int64_t)g * r >> 15);
    int32_t sr_ = c->rev * 1000 / 127 + x->gsend_r, sc_ = c->cho * 1000 / 127 + x->gsend_c;   /* 1/1000 */
    if (sr_ > 1000) sr_ = 1000;
    if (sc_ > 1000) sc_ = 1000;
    sr_ = sr_ * x->dsend_r / 127; sc_ = sc_ * x->dsend_c / 127;
    x->rg = (int32_t)((int64_t)g * sr_ / 1000);
    x->cgl = (int32_t)((int64_t)x->gl * sc_ / 1000);
    x->cgr = (int32_t)((int64_t)x->gr * sc_ / 1000);
}
static void chan_gains(int c) { for (int i = 0; i < MAXV; i++) if (v[i].on && v[i].chn == c) voice_gains(&v[i]); }
static int32_t vel_base(int vel) { return (int32_t)((int64_t)32767 * sq7(vel) / 16129); }
/* 거르개 계수: 차단 센트 → Chamberlin f = 2 sin(π fc / SR) (Q15), 공명 센티벨 → 감쇠 q = 1/Q (Q15) */
static void voice_filter(Voice *x)
{
    const Chan *c = &ch[x->chn];
    int32_t fc = x->fc_base + (c->cutoff - 64) * 60, qcb = x->q_base + (c->reso - 64) * 15;
    if (fc >= 13000) { x->f_f = 0; return; }      /* 13000 센트 ≈ 14.5 kHz 넘으면 거르지 않음 */
    if (fc < 1500) fc = 1500;
    uint32_t hz = (uint32_t)(((uint64_t)8176 * pow2_q16(fc)) >> 16) / 1000;   /* 8.176 Hz × 2^(fc/1200) */
    if (hz > SR / 7) hz = SR / 7;
    int32_t ph = (int32_t)((uint64_t)hz * 512 / SR);                         /* gm_sin 1024 칸 = 2π → π fc/SR = hz×512/SR 칸 */
    int32_t f = gm_sin[ph & 1023] * 4;                                         /* 2 sin (Q15, gm_sin 크기 16383) */
    if (f < 64) f = 64;
    if (qcb < 0) qcb = 0;
    if (qcb > 960) qcb = 960;
    int32_t q = gm_cb[qcb] * 2; if (q < 3000) q = 3000;                        /* 1/Q = 10^(−cB/200) */
    x->f_f = f; x->f_q = q;
}

static void fm_pitch(Voice *x)
{
    const Chan *c = &ch[x->chn];
    int32_t cc = x->cents + bend_cents(c);
    { int dep = c->mod * 50 / 127 + (c->vdepth > 64 ? (c->vdepth - 64) * 50 / 63 : 0); if (dep) cc += dep * gm_sin[(c->lph >> 22) & 1023] / 16383; }   /* 모듈레이션 휠·GS 비브라토 깊이 */
    uint32_t base = cents_inc(cc);
    x->ic = (uint32_t)(((uint64_t)base * x->p->cr) >> 2);
    x->im = (uint32_t)(((uint64_t)base * x->p->mr) >> 2);
}
static void sf_pitch(Voice *x)
{
    const Chan *c = &ch[x->chn];
    int32_t cc = x->cents + bend_cents(c);
    { int dep = c->mod * 50 / 127 + (c->vdepth > 64 ? (c->vdepth - 64) * 50 / 63 : 0); if (dep) cc += dep * gm_sin[(c->lph >> 22) & 1023] / 16383; }
    if (cc > 4800) cc = 4800;
    if (!x->sinc) x->sinc = (uint32_t)(((uint64_t)x->srate << 16) / SR);
    x->inc = (uint32_t)(((uint64_t)x->sinc * pow2_q16(cc)) >> 16);
}
static void set_pan(Voice *x, int p, int32_t base)    /* base = 채널 값 빼고 이득 */
{
    x->base = base; x->vpan = (int16_t)p;
    if (!x->dsend_r && !x->dsend_c) x->dsend_r = x->dsend_c = 127;
    voice_gains(x);
}

static void fm_note_on(int c, int key, int vel)
{
    if (!patches_made) make_patches();
    Voice *x = alloc_voice();
    memset(x, 0, sizeof *x);
    x->on = 1; x->chn = (uint8_t)c; x->key = (uint8_t)key; x->vel = (uint8_t)vel; x->kind = 0; x->age = ++age_ctr;
    x->p = &prog_patch[ch[c].prog & 127];
    x->cents = key * 100;
    fm_pitch(x);
    const FmPatch *p = x->p;
    x->ac = 65536 / (int32_t)ms_samples(p->ca ? p->ca : 1) + 1;
    x->dc = 65536 / (int32_t)ms_samples(p->cd / 3 + 1) + 1;
    x->rc = 65536 / (int32_t)ms_samples(p->crl / 3 + 1) + 1;
    x->sc = p->cs * 257;
    x->am_ = 65536 / (int32_t)ms_samples(p->ma ? p->ma : 1) + 1;
    x->dm = 65536 / (int32_t)ms_samples(p->md / 3 + 1) + 1;
    x->sm = p->ms * 257;
    set_pan(x, 0, vel_base(vel) * p->lvl / 255);
}

/* 타악기 (채널 10): 사인 음높이 미끄럼 + 잡음 */
static void drum_on(int c, int key, int vel)
{
    Voice *x = alloc_voice();
    memset(x, 0, sizeof *x);
    x->on = 1; x->chn = (uint8_t)c; x->key = (uint8_t)key; x->vel = (uint8_t)vel; x->kind = 1; x->age = ++age_ctr;
    x->nz = 0x12345u + (uint32_t)key * 7919u;
    int f0 = 200, f1 = 200, sweep = 30, td = 150, nd = 60, nl = 60, tl = 200, hp = 0, pan = 0;
    switch (key) {
    case 35: case 36: f0 = 160; f1 = 45; sweep = 50; td = 350; nd = 8; nl = 60; tl = 255; break;                /* 큰북 */
    case 37: f0 = 900; f1 = 700; sweep = 5; td = 40; nd = 30; nl = 120; tl = 120; hp = 1; break;                  /* 사이드 스틱 */
    case 38: case 40: f0 = 240; f1 = 180; sweep = 15; td = 120; nd = 200; nl = 210; tl = 150; hp = 1; break;       /* 작은북 */
    case 39: tl = 0; nd = 140; nl = 220; hp = 1; break;                                                             /* 박수 */
    case 41: case 43: case 45: case 47: case 48: case 50:                                                           /* 탐 */
        f0 = 70 + (key - 41) * 18; f1 = f0 * 3 / 4; sweep = 120; td = 380; nd = 20; nl = 40; tl = 230; pan = (key - 45) * 60; break;
    case 42: tl = 0; nd = 45; nl = 170; hp = 2; pan = 150; break;                                                   /* 닫힌 하이햇 */
    case 44: tl = 0; nd = 70; nl = 150; hp = 2; pan = 150; break;                                                   /* 페달 하이햇 */
    case 46: tl = 0; nd = 450; nl = 160; hp = 2; pan = 150; break;                                                  /* 열린 하이햇 */
    case 49: case 57: tl = 0; nd = 1400; nl = 170; hp = 2; pan = key == 49 ? -200 : 200; break;                      /* 크래시 */
    case 51: case 59: f0 = f1 = 3200; td = 600; tl = 40; nd = 700; nl = 110; hp = 2; pan = -150; break;             /* 라이드 */
    case 52: tl = 0; nd = 1000; nl = 180; hp = 1; break;
    case 53: f0 = f1 = 950; td = 500; tl = 140; nd = 300; nl = 60; hp = 2; break;                                   /* 라이드 벨 */
    case 54: tl = 0; nd = 160; nl = 150; hp = 2; pan = 200; break;                                                  /* 탬버린 */
    case 55: tl = 0; nd = 700; nl = 160; hp = 2; break;
    case 56: f0 = f1 = 560; td = 220; tl = 180; nd = 10; nl = 20; break;                                            /* 카우벨 */
    case 75: f0 = f1 = 2500; td = 60; tl = 200; nd = 5; nl = 10; break;                                             /* 클라베스 */
    case 76: case 77: f0 = f1 = key == 76 ? 1300 : 1000; td = 80; tl = 200; nd = 10; nl = 30; break;               /* 우드블록 */
    case 80: case 81: f0 = f1 = 5000; td = key == 80 ? 80 : 600; tl = 120; nd = 5; nl = 10; break;                  /* 트라이앵글 */
    default: f0 = 120 + key * 12; f1 = f0 * 4 / 5; td = 150; nd = 80; nl = 90; tl = 150; hp = 1; break;
    }
    if (G->dpit_s[key & 127] != 64) { uint32_t m = pow2_q16((G->dpit_s[key & 127] - 64) * 100); f0 = (int)((uint64_t)f0 * m >> 16); f1 = (int)((uint64_t)f1 * m >> 16); }   /* GS 드럼 음높이 */
    x->dinc = (uint32_t)((uint64_t)f0 * 0x100000000ull / SR);
    x->dinc_end = (uint32_t)((uint64_t)f1 * 0x100000000ull / SR);
    x->dsw = (int32_t)((x->dinc - x->dinc_end) / ms_samples((uint32_t)sweep));
    if (f0 <= f1) x->dsw = 0;
    x->tenv = tl * 256; x->tdec = 65536 / (int32_t)ms_samples((uint32_t)td / 3 + 1) + 1;
    x->nenv = nl * 256; x->ndec = 65536 / (int32_t)ms_samples((uint32_t)nd / 3 + 1) + 1;
    x->hp = hp;
    x->dsend_r = G->drev_s[key & 127]; x->dsend_c = G->dcho_s[key & 127]; if (!x->dsend_r && !x->dsend_c) x->dsend_r = 1;
    if (G->dpan_s[key & 127] != 64) pan = G->dpan_s[key & 127] ? (G->dpan_s[key & 127] - 64) * 500 / 63 : (int)(x->nz % 1001) - 500;   /* 0 = 아무렇게 */
    set_pan(x, pan, (int32_t)((int64_t)vel_base(vel) * sq7(G->dlev_s[key & 127]) / 16129));
    x->released = 1;                            /* 타악기는 놓기를 기다리지 않음 */
    if (key == 42 || key == 44 || key == 46) {  /* 하이햇은 서로 끊음 */
        for (int i = 0; i < MAXV; i++)
            if (&v[i] != x && v[i].on && v[i].kind == 1 && v[i].chn == c && (v[i].key == 42 || v[i].key == 44 || v[i].key == 46)) { v[i].nenv >>= 4; v[i].tenv >>= 4; }
    }
}

static int find_preset(int bank, int prog)
{
    for (uint32_t i = 0; i + 1 < sf_nphdr; i++) if ((int)rd16(sf_phdr + i * 38 + 22) == bank && (int)rd16(sf_phdr + i * 38 + 20) == prog) return (int)i;
    return -1;
}
static void sf_voice(int c, int key, int vel, const int16_t *g)
{
    uint32_t si = (uint16_t)g[G_SAMPLE];
    if (si + 1 >= sf_nshdr) return;
    const uint8_t *sh = sf_shdr + si * 46;
    if (rd16(sh + 44) & 0x8000) return;         /* 롬 샘플 */
    uint32_t start = rd32(sh + 20) + (uint32_t)(g[G_START] + 32768 * g[G_STARTC]);
    uint32_t end = rd32(sh + 24) + (uint32_t)(g[G_END] + 32768 * g[G_ENDC]);
    uint32_t ls = rd32(sh + 28) + (uint32_t)(g[G_LSTART] + 32768 * g[G_LSTARTC]);
    uint32_t le = rd32(sh + 32) + (uint32_t)(g[G_LEND] + 32768 * g[G_LENDC]);
    if (end > sf_nsmp) end = sf_nsmp;
    if (start + 2 >= end) return;
    const int16_t *sbase = sf_smp;
    if (sf_stream) { sbase = sf_cache_get(start, &end); if (!sbase) return; }   /* 4.0.133: 파일에서 읽어 담기 (끝이 잘릴 수 있음) */
    Voice *x = alloc_voice();
    memset(x, 0, sizeof *x);
    x->on = 1; x->chn = (uint8_t)c; x->key = (uint8_t)key; x->vel = (uint8_t)vel; x->kind = 2; x->age = ++age_ctr;
    x->smp = sbase; x->pos = start; x->end = end;
    x->loop = (uint8_t)(g[G_MODES] & 3);
    if (x->loop == 2) x->loop = 0;
    if (ls < start || le > end || le <= ls + 1) x->loop = 0;
    x->ls = ls; x->le = le;
    x->srate = rd32(sh + 36) ? rd32(sh + 36) : SR;
    int root = g[G_ROOT] >= 0 ? g[G_ROOT] : sh[40];
    int kk = g[G_KEYNUM] >= 0 ? g[G_KEYNUM] : key;
    x->cents = (kk - root) * g[G_SCALE] + g[G_COARSE] * 100 + g[G_FINE] + (int8_t)sh[41];
    int dr = (G->drum_s >> c) & 1;
    if (dr && G->dpit_s[key & 127] != 64) x->cents += (G->dpit_s[key & 127] - 64) * 100;   /* GS 드럼 음높이 */
    sf_pitch(x);
    int vv = g[G_VEL] >= 0 ? g[G_VEL] : vel;
    int32_t att = g[G_ATTEN] * 4 / 10;          /* E-mu 방식 0.4배 (흔한 SoundFont가 이것에 맞춤) */
    if (att < 0) att = 0;
    if (att > 1440) att = 1440;
    int32_t gain = (int32_t)((int64_t)gm_cb[att] * vel_base(vv) >> 15);
    int vp = g[G_PAN];
    x->gsend_r = g[16] < 0 ? 0 : g[16] > 1000 ? 1000 : g[16]; x->gsend_c = g[15] < 0 ? 0 : g[15] > 1000 ? 1000 : g[15];   /* 리버브·코러스 보내기 생성자 */
    if (dr) {                                   /* GS 드럼 악기별 레벨·좌우·보내기 */
        gain = (int32_t)((int64_t)gain * sq7(G->dlev_s[key & 127]) / 16129);
        if (G->dpan_s[key & 127] != 64) vp = G->dpan_s[key & 127] ? (G->dpan_s[key & 127] - 64) * 500 / 63 : (int)(age_ctr * 2654435761u % 1001) - 500;
        x->dsend_r = G->drev_s[key & 127]; x->dsend_c = G->dcho_s[key & 127]; if (!x->dsend_r && !x->dsend_c) x->dsend_r = 1;
    }
    set_pan(x, vp, gain);
    x->fc_base = g[8]; x->q_base = g[9]; voice_filter(x);   /* SoundFont 거르개 + GS NRPN 차단·공명 */
    const Chan *cn = &ch[c];
    int32_t ta = (cn->att - 64) * 75, td = (cn->dec - 64) * 75, tr = (cn->rel - 64) * 75;   /* GS NRPN 어택·디케이·릴리스 (타임센트) */
    x->delay_n = tc_samples(g[G_DELAY]); x->att_n = tc_samples(g[G_ATTACK] + ta); x->hold_n = tc_samples(g[G_HOLD]);
    x->att_inc = x->att_n ? (32767u << 15) / x->att_n : 0; x->eamp = 0;
    uint32_t dn = tc_samples(g[G_DECAY] + td), rn = tc_samples(g[G_RELEASE] + tr);
    x->sus_cb = g[G_SUSTAIN] < 0 ? 0 : g[G_SUSTAIN] > 1440 ? 1440 : g[G_SUSTAIN];
    x->dec_rate = (int32_t)((1000u << 8) / (dn ? dn : 1)) + 1;
    x->rel_rate = (int32_t)((1000u << 8) / (rn ? rn : 1)) + 1;
    if (x->rel_rate < 6) x->rel_rate = 6;
    x->est = 0; x->ecnt = 0; x->ecb = 0;
    x->excl = g[G_EXCL];
    if (x->excl)                                /* 같은 배타 묶음은 끊음 (하이햇 등) */
        for (int i = 0; i < MAXV; i++)
            if (&v[i] != x && v[i].on && v[i].kind == 2 && v[i].chn == c && v[i].excl == x->excl) { v[i].est = 5; v[i].released = 1; v[i].rel_rate = 2000 << 8 >> 7; }
}
static void sf_note_on(int c, int key, int vel)
{
    int dr = (G->drum_s >> c) & 1;                      /* 드럼 파트 (GS 리듬 파트 SysEx) */
    int bank = dr ? 128 : ch[c].bank, prog = ch[c].prog;
    int pi = find_preset(bank, prog);
    if (pi < 0 && !dr && G->gs_s && (bank & 7)) pi = find_preset(bank & ~7, prog);   /* GS 변형 음색이 없으면 같은 무리의 윗 음색(sub-capital) → 아래에서 capital(뱅크 0) */
    if (pi < 0) pi = find_preset(dr ? 128 : 0, dr ? 0 : prog);
    if (pi < 0 && !dr) pi = find_preset(0, 0);
    if (pi < 0) return;
    uint32_t pb0 = rd16(sf_phdr + (uint32_t)pi * 38 + 24), pb1 = rd16(sf_phdr + (uint32_t)(pi + 1) * 38 + 24);
    int16_t pglob[G_N]; memset(pglob, 0, sizeof pglob); pglob[G_KEYR] = pglob[G_VELR] = (int16_t)(127 << 8);
    for (uint32_t z = pb0; z < pb1 && z + 1 < sf_npbag; z++) {
        uint32_t g0 = rd16(sf_pbag + z * 4), g1 = rd16(sf_pbag + (z + 1) * 4);
        int16_t pz[G_N]; memcpy(pz, pglob, sizeof pz);
        int has_inst = g1 > g0 && rd16(sf_pgen + (g1 - 1) * 4) == G_INST;
        apply_gens(sf_pgen, g0, g1, sf_npgen, pz);
        if (!has_inst) { if (z == pb0) memcpy(pglob, pz, sizeof pz); continue; }
        if (!in_range(pz[G_KEYR], key) || !in_range(pz[G_VELR], vel)) continue;
        uint32_t ii = (uint16_t)pz[G_INST];
        if (ii + 1 >= sf_ninst) continue;
        uint32_t ib0 = rd16(sf_inst + ii * 22 + 20), ib1 = rd16(sf_inst + (ii + 1) * 22 + 20);
        int16_t iglob[G_N]; inst_defaults(iglob);
        for (uint32_t iz = ib0; iz < ib1 && iz + 1 < sf_nibag; iz++) {
            uint32_t h0 = rd16(sf_ibag + iz * 4), h1 = rd16(sf_ibag + (iz + 1) * 4);
            int16_t g[G_N]; memcpy(g, iglob, sizeof g);
            int has_s = h1 > h0 && rd16(sf_igen + (h1 - 1) * 4) == G_SAMPLE;
            apply_gens(sf_igen, h0, h1, sf_nigen, g);
            if (!has_s) { if (iz == ib0) memcpy(iglob, g, sizeof g); continue; }
            if (!in_range(g[G_KEYR], key) || !in_range(g[G_VELR], vel)) continue;
            for (int k = 0; k < G_N; k++) {         /* 프리셋 단계는 더함 (범위·번호류 빼고) */
                if (k == G_KEYR || k == G_VELR || k == G_INST || k == G_SAMPLE || k == G_KEYNUM || k == G_VEL || k == G_ROOT || k == G_MODES || k == G_EXCL
                    || k == G_START || k == G_END || k == G_LSTART || k == G_LEND || k == G_STARTC || k == G_ENDC || k == G_LSTARTC || k == G_LENDC) continue;
                g[k] = (int16_t)(g[k] + pz[k]);
            }
            sf_voice(c, key, vel, g);
        }
    }
}

static void note_off(int c, int key)
{
    for (int i = 0; i < MAXV; i++) {
        Voice *x = &v[i];
        if (!x->on || x->chn != c || x->key != key || x->released || x->kind == 1) continue;
        if (ch[c].sustain) { x->held = 1; continue; }
        x->released = 1;
        if (x->kind == 0) { x->stc = 3; x->stm = 3; }
        else if (x->kind == 2) {
            if (x->est == 1) {                  /* 어택 중: 지금 크기를 센티벨로 */
                int32_t a = x->att_n ? (x->eamp >> 15) : 32767, cb = 0;
                while (cb < 1440 && gm_cb[cb] > a) cb++;
                x->ecb = cb << 8;
            }
            x->est = 5;
        }
    }
}
static void note_on(int c, int key, int vel)
{
    if (!vel) { note_off(c, key); return; }
    for (int i = 0; i < MAXV; i++)              /* 같은 음이 울리고 있으면 놓음 (다시 치기) */
        if (v[i].on && v[i].chn == c && v[i].key == key && !v[i].released && v[i].kind != 1) {
            int s = ch[c].sustain; ch[c].sustain = 0; note_off(c, key); ch[c].sustain = (uint8_t)s; break;
        }
    if (mode == GM_SF2 && sf_ok) sf_note_on(c, key, vel);
    else if ((G->drum_s >> c) & 1) drum_on(c, key, vel);
    else fm_note_on(c, key, vel);
}
static void all_off(int c, int hard)
{
    for (int i = 0; i < MAXV; i++)
        if (v[i].on && (c < 0 || v[i].chn == c)) {
            if (hard) v[i].on = 0;
            else { int k = v[i].key; v[i].held = 0; int s = ch[v[i].chn].sustain; ch[v[i].chn].sustain = 0; note_off(v[i].chn, k); ch[v[i].chn].sustain = (uint8_t)s; }
        }
}
static void reset_chan(int c)
{
    Chan *k = &ch[c];
    k->vol = 100; k->expr = 127; k->pan = 64; k->sustain = 0; k->mod = 0; k->bend = 0; k->bend_range = 200;
    k->rpn_l = k->rpn_h = 127;
    k->nrpn_l = k->nrpn_h = 127; k->use_nrpn = 0;
    k->rev = 28; k->cho = 0;                    /* 5.2.59 GM/GS 처음값 (5.2.148: 40 → 28, 리버브가 과하다는 말씀) */
    k->vrate = k->vdepth = k->cutoff = k->reso = k->att = k->dec = k->rel = 64;
}
static void reset_part(int c) { Chan *k = &ch[c]; reset_chan(c); k->level = 127; k->tune = 0; }
void gm_reset(void)
{
    if (mode == GM_MT32) { mt32w_reset(G->mt_slot_s); return; }
    if (mode == GM_SC55) { sc55w_reset(); return; }       /* GS 리셋을 보냄 */
    memset(v, 0, sizeof v);
    for (int c = 0; c < 16; c++) { reset_part(c); ch[c].prog = 0; ch[c].bank = 0; }
    rs = 0; ndat = 0; need = 0; sysex = 0; sxn = 0;
    G->drum_s = 1u << 9; G->master_s = 127;             /* 5.2.57 */
    memset(G->dlev_s, 127, 128); memset(G->dpan_s, 64, 128); memset(G->dpit_s, 64, 128); memset(G->drev_s, 127, 128); memset(G->dcho_s, 127, 128);   /* 5.2.59 */
    fx_reset();
}
void gm_set_mode(int m)
{
    if (m == GM_MT32 && !gm_mt32_ready(G->mt_slot_s)) m = sf_ok ? GM_SF2 : GM_FM;   /* 롬이 없으면 SoundFont, 그것도 없으면 FM */
    if (m == GM_MT32 && mt32_owner(G->mt_slot_s) >= 0 && &gi[mt32_owner(G->mt_slot_s)] != G) m = sf_ok ? GM_SF2 : GM_FM;   /* 엔진 슬롯 하나는 한 인스턴스만 (5.2.173: 슬롯 둘) */
    if (m == GM_SC55 && (!sc55w_ready() || (sc55_owner() >= 0 && &gi[sc55_owner()] != G))) m = sf_ok ? GM_SF2 : GM_FM;   /* 롬이 없거나 다른 기계가 쓰면 */
    if (m == GM_SF2 && !sf_ok) m = GM_FM;
    if (m != mode) { mode = m; G->cal_s = 0; G->lv_sum = 0; G->lv_n = 0; gm_reset(); }
}
int gm_mode(void) { return mode; }

static void retune(int c)
{
    for (int i = 0; i < MAXV; i++) {
        if (!v[i].on || v[i].chn != c) continue;
        if (v[i].kind == 0) fm_pitch(&v[i]);
        else if (v[i].kind == 2) sf_pitch(&v[i]);
    }
}
/* GS NRPN — 파트(01 xx)·드럼 악기(18·1A·1C·1D·1E rr) */
static void nrpn(int c, int h, int l, int val)
{
    Chan *k = &ch[c];
    if (h == 0x01) {
        switch (l) {
        case 0x08: k->vrate = (uint8_t)val; break;
        case 0x09: k->vdepth = (uint8_t)val; break;
        case 0x20: k->cutoff = (uint8_t)val; for (int i = 0; i < MAXV; i++) if (v[i].on && v[i].chn == c && v[i].kind == 2) voice_filter(&v[i]); break;
        case 0x21: k->reso = (uint8_t)val; for (int i = 0; i < MAXV; i++) if (v[i].on && v[i].chn == c && v[i].kind == 2) voice_filter(&v[i]); break;
        case 0x63: k->att = (uint8_t)val; break;
        case 0x64: k->dec = (uint8_t)val; break;
        case 0x66: k->rel = (uint8_t)val; break;
        default: break;
        }
        return;
    }
    if (!((G->drum_s >> c) & 1) && h != 0x18 && h != 0x1A && h != 0x1C && h != 0x1D && h != 0x1E) return;
    l &= 127;
    switch (h) {
    case 0x18: G->dpit_s[l] = (uint8_t)val; break;
    case 0x1A: G->dlev_s[l] = (uint8_t)val; break;
    case 0x1C: G->dpan_s[l] = (uint8_t)val; break;
    case 0x1D: G->drev_s[l] = (uint8_t)val; break;
    case 0x1E: G->dcho_s[l] = (uint8_t)val; break;
    default: break;
    }
}
static void control(int c, int n, int val)
{
    Chan *k = &ch[c];
    switch (n) {
    case 0: k->bank = (uint8_t)val; break;
    case 1: k->mod = (uint8_t)val; if (!val) retune(c); break;
    case 6:
        if (k->use_nrpn) { nrpn(c, k->nrpn_h, k->nrpn_l, val); break; }   /* GS NRPN */
        if (k->rpn_h == 0 && k->rpn_l == 0) k->bend_range = val * 100;   /* 휨 범위 (반음) */
        else if (k->rpn_h == 0 && k->rpn_l == 2) { k->tune = (int16_t)(k->tune % 100 + (val - 64) * 100); retune(c); }   /* 거친 조율 */
        else if (k->rpn_h == 0 && k->rpn_l == 1) { k->tune = (int16_t)(k->tune / 100 * 100 + (val - 64) * 100 / 64); retune(c); }   /* 미세 조율 (MSB 만) */
        break;
    case 38: if (!k->use_nrpn && k->rpn_h == 0 && k->rpn_l == 0) k->bend_range = k->bend_range / 100 * 100 + val; break;
    case 7: k->vol = (uint8_t)val; chan_gains(c); break;
    case 10: k->pan = (uint8_t)val; chan_gains(c); break;
    case 11: k->expr = (uint8_t)val; chan_gains(c); break;
    case 91: k->rev = (uint8_t)val; chan_gains(c); break;   /* 5.2.59 리버브 보내기 */
    case 93: k->cho = (uint8_t)val; chan_gains(c); break;   /* 코러스 보내기 */
    case 98: k->nrpn_l = (uint8_t)val; k->use_nrpn = 1; break;
    case 99: k->nrpn_h = (uint8_t)val; k->use_nrpn = 1; break;
    case 64:
        k->sustain = val >= 64;
        if (!k->sustain)
            for (int i = 0; i < MAXV; i++) if (v[i].on && v[i].chn == c && v[i].held) { v[i].held = 0; note_off(c, v[i].key); }
        break;
    case 100: k->rpn_l = (uint8_t)val; k->use_nrpn = 0; break;
    case 101: k->rpn_h = (uint8_t)val; k->use_nrpn = 0; break;
    case 120: all_off(c, 1); break;
    case 121: reset_chan(c); retune(c); break;
    case 123: case 124: case 125: case 126: case 127: all_off(c, 0); break;
    default: break;
    }
}
static void message(uint8_t st, uint8_t a, uint8_t b)
{
    int c = st & 15;
    switch (st & 0xF0) {
    case 0x80: note_off(c, a); break;
    case 0x90: note_on(c, a, b); break;
    case 0xB0: control(c, a, b); break;
    case 0xC0: ch[c].prog = a; break;
    case 0xE0: ch[c].bend = (int32_t)(a | b << 7) - 8192; retune(c); break;
    default: break;
    }
}
void gm_midi(uint8_t b)
{
    if (mode == GM_MT32) { mt32w_midi(G->mt_slot_s, b); return; }
    if (mode == GM_SC55) { sc55w_midi(b); return; }
    if (b >= 0xF8) return;                      /* 실시간 메시지 */
    if (sysex) {
        if (b == 0xF7 || b & 0x80) {
            sysex = 0;                          /* GM 켜기 F0 7E 7F 09 01 F7, GS 리셋 F0 41 10 42 12 40 00 7F 00 41 F7 */
            if ((sxn >= 4 && sx[0] == 0x7E && sx[2] == 0x09 && sx[3] == 0x01) || (sxn >= 8 && sx[0] == 0x41 && sx[3] == 0x12 && sx[4] == 0x40 && sx[5] == 0x00 && sx[6] == 0x7F)) gm_reset();
            else if (sxn >= 6 && sx[0] == 0x7F && sx[2] == 0x04 && sx[3] == 0x01) G->master_s = sx[5] & 0x7F;   /* 유니버설 마스터 음량 F0 7F id 04 01 ll mm F7 */
            else if (sxn >= 8 && sx[0] == 0x41 && sx[2] == 0x42 && sx[3] == 0x12 && sx[4] == 0x40) {        /* Roland GS DT1 (주소 40 xx yy) */
                if (sx[5] == 0x00 && sx[6] == 0x04) G->master_s = sx[7] & 0x7F;                            /* 마스터 음량 40 00 04 */
                else if (sx[5] == 0x00 && sx[6] == 0x05) { for (int c = 0; c < 16; c++) { ch[c].tune = (int16_t)((sx[7] - 64) * 100); retune(c); } }   /* 마스터 키 시프트 */
                else if (sx[5] == 0x01 && sx[6] >= 0x30 && sx[6] <= 0x3F) {                                /* 리버브·코러스 (40 01 30~3F) */
                    GmFx *f = FX; int val = sx[7] & 0x7F;
                    switch (sx[6]) {
                    case 0x30: f->rmac = (uint8_t)(val & 7); f->rtime = 64; break;
                    case 0x33: f->rlev = (uint8_t)val; break;
                    case 0x34: f->rtime = (uint8_t)val; break;
                    case 0x38: fx_macro_chorus(val); break;
                    case 0x3A: f->clev = (uint8_t)val; break;
                    case 0x3B: f->cfbk = (uint8_t)val; break;
                    case 0x3C: f->cdly = (uint8_t)val; break;
                    case 0x3D: f->crate = (uint8_t)val; break;
                    case 0x3E: f->cdepth = (uint8_t)val; break;
                    case 0x3F: f->c2r = (uint8_t)val; break;
                    default: break;
                    }
                    fx_params();
                }
                else if (G->gs_s && (sx[5] & 0xF0) == 0x10 && sx[6] != 0x15) {                              /* 파트 설정 40 1x yy */
                    int part = sx[5] & 0x0F, c = part == 0 ? 9 : part <= 9 ? part - 1 : part, val = sx[7] & 0x7F;
                    Chan *k = &ch[c];
                    switch (sx[6]) {
                    case 0x00: k->bank = (uint8_t)val; if (sxn >= 10) k->prog = sx[8] & 0x7F; break;   /* 음색 (뱅크, 번호) */
                    case 0x16: k->tune = (int16_t)(k->tune % 100 + (val - 64) * 100); retune(c); break;   /* 키 시프트 */
                    case 0x19: k->level = (uint8_t)val; chan_gains(c); break;                           /* 파트 레벨 */
                    case 0x1C: k->pan = (uint8_t)(val ? val : 64); chan_gains(c); break;               /* 좌우 (0 = 아무렇게 → 가운데) */
                    case 0x21: k->cho = (uint8_t)val; chan_gains(c); break;
                    case 0x22: k->rev = (uint8_t)val; chan_gains(c); break;
                    case 0x30: k->vrate = (uint8_t)val; break;
                    case 0x31: k->vdepth = (uint8_t)val; break;
                    case 0x32: k->cutoff = (uint8_t)val; break;
                    case 0x33: k->reso = (uint8_t)val; break;
                    case 0x34: k->att = (uint8_t)val; break;
                    case 0x35: k->dec = (uint8_t)val; break;
                    case 0x36: k->rel = (uint8_t)val; break;
                    default: break;
                    }
                }
                else if (G->gs_s && (sx[5] & 0xF0) == 0x10 && sx[6] == 0x15) {                              /* 파트 x 리듬 파트 40 1x 15: 0 = 보통, 1·2 = 드럼 */
                    int part = sx[5] & 0x0F, chn = part == 0 ? 9 : part <= 9 ? part - 1 : part;           /* 블록 1~9 = 파트 1~9, 0 = 파트 10, A~F = 11~16 (기본 채널 = 파트) */
                    all_off(chn, 1);
                    if (sx[7]) G->drum_s |= (uint16_t)(1u << chn); else G->drum_s &= (uint16_t)~(1u << chn);
                }
            }
            if (b == 0xF7) return;
        } else { if (sxn < (int)sizeof sx) sx[sxn++] = b; return; }
    }
    if (b & 0x80) {
        if (b == 0xF0) { sysex = 1; sxn = 0; rs = 0; return; }
        if (b >= 0xF0) { rs = 0; need = b == 0xF2 ? 2 : (b == 0xF1 || b == 0xF3) ? 1 : 0; ndat = 0; return; }
        rs = b; ndat = 0;
        need = (uint8_t)(((b & 0xF0) == 0xC0 || (b & 0xF0) == 0xD0) ? 1 : 2);
        return;
    }
    if (!rs) { if (need && ++ndat >= need) { need = 0; ndat = 0; } return; }   /* 시스템 공통 자료는 버림 */
    dat[ndat++] = b;
    if (ndat >= need) { message(rs, dat[0], need > 1 ? dat[1] : 0); ndat = 0; }
}

/* ------------------------------------------------------------------ */
/* 그리기 (표본 만들기)                                                  */
/* ------------------------------------------------------------------ */
static inline int32_t sinq(uint32_t ph) { return gm_sin[ph >> 22]; }

static int32_t fm_sample(Voice *x)
{
    const FmPatch *p = x->p;
    /* 엔벌로프 */
    switch (x->stc) {
    case 0: x->envc += x->ac; if (x->envc >= 65536) { x->envc = 65536; x->stc = 1; } break;
    case 1: x->envc -= (int32_t)(((int64_t)(x->envc - x->sc) * x->dc) >> 16) + 1; if (x->envc <= x->sc) { x->envc = x->sc; x->stc = 2; } break;
    case 2: break;
    default: x->envc -= (int32_t)(((int64_t)x->envc * x->rc) >> 16) + 1; if (x->envc <= 16) { x->on = 0; return 0; } break;
    }
    switch (x->stm) {
    case 0: x->envm += x->am_; if (x->envm >= 65536) { x->envm = 65536; x->stm = 1; } break;
    case 1: x->envm -= (int32_t)(((int64_t)(x->envm - x->sm) * x->dm) >> 16); if (x->envm <= x->sm) { x->envm = x->sm; x->stm = 2; } break;
    case 2: break;
    default: x->envm -= (int32_t)(((int64_t)x->envm * x->rc) >> 16); if (x->envm < 0) x->envm = 0; break;
    }
    if (p->cs == 0 && x->stc == 2 && x->envc < 16) { x->on = 0; return 0; }
    int32_t fbv = p->fb ? (x->fb1 + x->fb2) >> (12 - p->fb) : 0;
    x->pm += x->im;
    int32_t mo = gm_sin[((x->pm >> 22) + (uint32_t)fbv) & 1023];
    x->fb2 = x->fb1; x->fb1 = mo;
    int32_t mod = (int32_t)(((int64_t)mo * x->envm) >> 16) * p->idx >> 9;
    x->pc += x->ic;
    int32_t car = gm_sin[((x->pc >> 22) + (uint32_t)mod) & 1023];
    return (int32_t)(((int64_t)car * x->envc) >> 16);
}
static int32_t drum_sample(Voice *x)
{
    int32_t out = 0;
    if (x->tenv > 64) {
        x->dph += x->dinc;
        if (x->dsw && x->dinc > x->dinc_end) { uint32_t d = x->dinc - x->dinc_end; x->dinc -= (uint32_t)x->dsw < d ? (uint32_t)x->dsw : d; }
        out += sinq(x->dph) * (x->tenv >> 8) >> 8;
        x->tenv -= (int32_t)(((int64_t)x->tenv * x->tdec) >> 16) + 1;
    }
    if (x->nenv > 64) {
        x->nz = x->nz * 1103515245u + 12345u;
        int32_t n = (int32_t)(x->nz >> 17) - 16384;
        int32_t h = x->hp ? n - x->nprev : n;
        if (x->hp == 2) h = n - x->nprev - (x->nprev >> 1);
        x->nprev = n;
        if (x->hp) h >>= 1;
        out += h * (x->nenv >> 8) >> 8;
        x->nenv -= (int32_t)(((int64_t)x->nenv * x->ndec) >> 16) + 1;
    }
    if (x->tenv <= 64 && x->nenv <= 64) x->on = 0;
    return out;
}
static int32_t sf_sample(Voice *x)
{
    /* 엔벌로프 (감쇠 센티벨 Q8, 어택은 크기로) */
    int32_t amp;
    switch (x->est) {
    case 0: if (x->ecnt++ >= x->delay_n) { x->est = 1; x->ecnt = 0; } return 0;
    case 1:
        if (x->ecnt >= x->att_n) { x->est = 2; x->ecnt = 0; amp = 32767; break; }
        x->ecnt++; x->eamp += (int32_t)x->att_inc; amp = x->eamp >> 15; if (amp > 32767) amp = 32767;
        break;
    case 2: if (x->ecnt++ >= x->hold_n) { x->est = 3; } amp = 32767; break;
    case 3: x->ecb += x->dec_rate; if (x->ecb >= x->sus_cb << 8) { x->ecb = x->sus_cb << 8; x->est = 4; } amp = gm_cb[x->ecb >> 8]; break;
    case 4: amp = gm_cb[x->ecb >> 8]; if (x->sus_cb >= 1000) { x->on = 0; return 0; } break;
    default: x->ecb += x->rel_rate; if (x->ecb >= 1000 << 8) { x->on = 0; return 0; } amp = gm_cb[x->ecb >> 8]; break;
    }
    /* 표본: 선형 보간 */
    uint32_t p = x->pos, q = p + 1;
    int lp = x->loop == 1 || (x->loop == 3 && !x->released);
    if (lp && q >= x->le) q = x->ls;
    if (q >= x->end) q = p;
    int32_t s0 = x->smp[p], s1 = x->smp[q];
    int32_t s = s0 + (int32_t)(((int64_t)(s1 - s0) * (int32_t)x->frac) >> 16);
    x->frac += x->inc;
    x->pos += x->frac >> 16;
    x->frac &= 0xFFFF;
    if (lp) { if (x->pos >= x->le) { uint32_t len = x->le - x->ls; x->pos = x->ls + (x->pos - x->ls) % len; } }
    else if (x->pos + 1 >= x->end) { x->on = 0; return 0; }
    return s * amp >> 15;
}

/* 공간 음향 — 곡이 보낸 리버브·코러스 양에 곱함 (%, 100 = 곡 그대로, 0 = 끔, 200 = 두 배). MT-32 는 엔진의 리버브 출력 이득 */
int gm_rev_pct = 100, gm_cho_pct = 100;
void gm_fx_set(int rev, int cho, int len)
{
    gm_rev_pct = rev < 0 ? 0 : rev > 200 ? 200 : rev; gm_cho_pct = cho < 0 ? 0 : cho > 200 ? 200 : cho; gm_len_pct = len < 0 ? 0 : len > 200 ? 200 : len;
    GmSynth *sv = G; for (int q = 0; q <= GM_INST_MAX; q++) { G = &gi[q]; fx_params(); } G = sv;   /* 잔향 길이는 계수로 */
    gm_fx_mt32(gm_rev_pct);
}
static void cal_run(void);
void gm_render(int32_t *l, int32_t *r, int n)
{
    for (int i = 0; i < n; i++) { l[i] = 0; r[i] = 0; }
    if (mode == GM_OFF) return;
    if ((mode == GM_FM || mode == GM_SF2) && !G->cal_s) cal_run();
    if (mode == GM_MT32) { mt32w_render(G->mt_slot_s, l, r, n); for (int i = 0; i < n; i++) { if (l[i] > GM_LIM || l[i] < -GM_LIM || r[i] > GM_LIM || r[i] < -GM_LIM) gm_guard_hits++; l[i] = GCL(l[i], GM_LIM); r[i] = GCL(r[i], GM_LIM); } return; }
    if (mode == GM_SC55) { sc55w_render(l, r, n); for (int i = 0; i < n; i++) { if (l[i] > GM_LIM || l[i] < -GM_LIM || r[i] > GM_LIM || r[i] < -GM_LIM) gm_guard_hits++; l[i] = GCL(l[i], GM_LIM); r[i] = GCL(r[i], GM_LIM); } if (G->master_s < 127) for (int i = 0; i < n; i++) { l[i] = l[i] * G->master_s / 127; r[i] = r[i] * G->master_s / 127; } return; }
    for (int i = 0; i < n; i++) {
        if (++lfo_cnt >= 64) {                  /* 비브라토: 64표본마다, 채널마다 5.6 Hz (GS 비브라토 빠르기로 바뀜) */
            lfo_cnt = 0;
            lfo_ph += 64u * 501079u;
            for (int k = 0; k < 16; k++) {
                Chan *q = &ch[k];
                q->lph += q->vrate == 64 ? 64u * 501079u : (uint32_t)(((uint64_t)64u * 501079u * pow2_q16((q->vrate - 64) * 1200 / 32)) >> 16);
                if (q->mod || q->vdepth > 64) retune(k);
            }
        }
        int32_t al = 0, ar = 0, rin = 0, cl = 0, cr = 0;
        for (int k = 0; k < NV; k++) {
            Voice *x = &v[k];
            if (!x->on) continue;
            int32_t s = x->kind == 0 ? fm_sample(x) : x->kind == 1 ? drum_sample(x) : sf_sample(x);
            if (x->fade_dec) { x->fade_q -= x->fade_dec; if (x->fade_q <= 0) { x->on = 0; continue; } s = (int32_t)(((int64_t)s * x->fade_q) >> 15); }   /* 5.2.77 */
            if (x->f_f) {                       /* 저역 거르개 (Chamberlin) */
                x->f_lo += (int32_t)(((int64_t)x->f_f * x->f_bd) >> 15);
                int32_t hi = s - x->f_lo - (int32_t)(((int64_t)x->f_q * x->f_bd) >> 15);
                x->f_bd += (int32_t)(((int64_t)x->f_f * hi) >> 15);
                if (x->f_lo > GM_LIM || x->f_lo < -GM_LIM || x->f_bd > GM_LIM || x->f_bd < -GM_LIM) { x->f_lo = x->f_bd = 0; x->on = 0; gm_guard_hits++; continue; }
                s = x->f_lo;
            }
            s = GCL(s, GM_LIM);
            al += (int32_t)(((int64_t)s * x->gl) >> 15);
            ar += (int32_t)(((int64_t)s * x->gr) >> 15);
            if (x->rg) rin += (int32_t)(((int64_t)s * x->rg) >> 15);
            if (x->cgl | x->cgr) { cl += (int32_t)(((int64_t)s * x->cgl) >> 15); cr += (int32_t)(((int64_t)s * x->cgr) >> 15); }
        }
        if (gm_rev_pct != 100) rin = (int32_t)((int64_t)rin * gm_rev_pct / 100);
        if (gm_cho_pct != 100) { cl = (int32_t)((int64_t)cl * gm_cho_pct / 100); cr = (int32_t)((int64_t)cr * gm_cho_pct / 100); }
        { int32_t el, er; fx_run(rin, cl, cr, &el, &er); al += el; ar += er; }   /* 리버브·코러스 */
        al = GCL(al, GM_LIM); ar = GCL(ar, GM_LIM);
        if (G->trim_s != 256) { al = (int32_t)((int64_t)al * G->trim_s >> 8); ar = (int32_t)((int64_t)ar * G->trim_s >> 8); }   /* 음량 맞춤 */
        l[i] = G->master_s >= 127 ? al : (int32_t)((int64_t)al * G->master_s / 127);   /* 마스터 음량 SysEx */
        r[i] = G->master_s >= 127 ? ar : (int32_t)((int64_t)ar * G->master_s / 127);
    }
}

/* 음량 맞춤 — FM·SoundFont 은 음색마다 크기가 달라 PSG(3음 화음 음량 13 ≈ 3600)와 안 맞았음.
 * 빈 인스턴스에 같은 설정(모드·SoundFont 칸·GS)을 주고 표준 구절(피아노·현 화음, 베이스, 드럼 8박)을 한 번 울려 RMS 를 재고,
 * GM_CAL_TARGET 이 되도록 trim_s 를 정함. 모드·SoundFont 가 바뀔 때만 다시 잼 (cal_s). 지금 인스턴스 G 를 건드리지 않음 */
#define GM_CAL_TARGET 4000
static GmSynth *cal_for; static int cal_beat; static uint64_t cal_sum;   /* 제곱합 (정수만 — 커널 빌드는 부동소수점 금지) */
static uint32_t isqrt64(uint64_t x) { uint64_t r = 0, b = 1ull << 62; while (b > x) b >>= 2; while (b) { if (x >= r + b) { x -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; } return (uint32_t)r; }
static void cal_all_dirty(void) { for (int q = 0; q <= GM_INST_MAX; q++) gi[q].cal_s = 0; cal_for = 0; }
static void cal_run(void)                       /* 프레임마다 한 박씩 (한 번에 다 재면 첫 소리에서 끊김) — 한 번에 인스턴스 하나만 */
{
    GmSynth *src = G, *sv = G; SfData *ssfd = SFD;
    GmSynth *c = &gi[GM_INST_MAX];
    static int32_t cl[6000], cr[6000];
    if (cal_for && cal_for != src) return;       /* 다른 인스턴스를 재는 중 — 차례를 기다림 (그동안 지금 값) */
    G = c; SFD = &sfd[src->sfslot_s < GM_SF_MAX ? src->sfslot_s : 0];
    if (!cal_for) {
        cal_for = src; cal_beat = 0; cal_sum = 0;
        c->mode_s = src->mode_s; c->sfslot_s = src->sfslot_s; c->gs_s = src->gs_s; c->trim_s = 256; c->cal_s = 1;
        gm_reset();
        static const uint8_t pg[] = { 0xB0, 91, 0, 0xB1, 91, 0, 0xB2, 91, 0, 0xC0, 0, 0xC1, 48, 0xC2, 33 };
        for (unsigned i = 0; i < sizeof pg; i++) gm_midi(pg[i]);
    }
    static const uint8_t on[] = { 0x90, 60, 100, 0x90, 64, 100, 0x90, 67, 100, 0x91, 55, 90, 0x91, 59, 90, 0x91, 62, 90, 0x92, 36, 100, 0x99, 36, 110, 0x99, 42, 80 };
    static const uint8_t sn[] = { 0x99, 38, 110 };
    static const uint8_t of[] = { 0x80, 60, 0, 0x80, 64, 0, 0x80, 67, 0, 0x81, 55, 0, 0x81, 59, 0, 0x81, 62, 0, 0x82, 36, 0 };
    for (unsigned i = 0; i < sizeof on; i++) gm_midi(on[i]);
    if (cal_beat & 1) for (unsigned i = 0; i < sizeof sn; i++) gm_midi(sn[i]);
    gm_render(cl, cr, 6000);
    int64_t m = 0; uint64_t e = 0;
    for (int i = 0; i < 6000; i++) m += cl[i];
    m /= 6000;
    for (int i = 0; i < 6000; i++) { int64_t d = (int64_t)cl[i] - m; e += (uint64_t)(d * d); }
    cal_sum += e / 6000;
    for (unsigned i = 0; i < sizeof of; i++) gm_midi(of[i]);
    if (++cal_beat >= 8) {
        uint32_t rms = isqrt64(cal_sum / 8);
        uint32_t t = 256;
        if (rms >= 100) { uint32_t q = (uint32_t)((uint64_t)GM_CAL_TARGET * 256 / rms); t = q < 51 ? 51 : q > 1024 ? 1024 : q; }   /* 소리가 아주 작으면(음색 없음) 그대로 */
        gm_reset();
        src->trim_s = (uint16_t)t; src->cal_s = 1; cal_for = 0;
    }
    G = sv; SFD = ssfd;
}
int gm_voices(void) { if (mode == GM_MT32 || mode == GM_SC55) return 0; int c = 0; for (int i = 0; i < MAXV; i++) c += v[i].on; return c; }   /* 유령은 안 셈 */

/* 인스턴스 API — i 번째 음원 (단자에 꽂힌 소프트웨어 기계 번호). gm_*() 는 지금 고른 인스턴스(처음엔 0) */
void gm_use(int i) { G = &gi[i >= 0 && i < GM_INST_MAX ? i : 0]; SFD = &sfd[G->sfslot_s < GM_SF_MAX ? G->sfslot_s : 0]; }
void gm_sf_slot(int s) { SFD = &sfd[s >= 0 && s < GM_SF_MAX ? s : 0]; }   /* gm_load_sf2·gm_load_sf2_stream·gm_unload_sf2·gm_sf2_presets 가 다룰 칸 */
void gmi_set_sf(int i, int slot, int gs) { if (i < 0 || i >= GM_INST_MAX) return; GmSynth *q = &gi[i]; uint8_t ns = (uint8_t)(slot >= 0 && slot < GM_SF_MAX ? slot : 0); if (q->sfslot_s != ns || q->gs_s != (gs != 0)) { q->sfslot_s = ns; q->gs_s = (uint8_t)(gs != 0); q->cal_s = 0; gm_use(i); gm_reset(); } }
int  gmi_trim(int i) { return i >= 0 && i < GM_INST_MAX ? gi[i].trim_s : 256; }
int  gmi_drums(int i) { return i >= 0 && i < GM_INST_MAX ? gi[i].drum_s : 0; }
int  gmi_master(int i) { return i >= 0 && i < GM_INST_MAX ? gi[i].master_s : 0; }
void gmi_reset(int i) { gm_use(i); gm_reset(); }
void gmi_set_mode(int i, int m) { gm_use(i); gm_set_mode(m); }
void gmi_set_mt32(int i, int slot) { gm_use(i); if (G->mt_slot_s != (uint8_t)slot) { if (mode == GM_MT32) mode = GM_OFF; G->mt_slot_s = (uint8_t)slot; } }   /* 이 인스턴스가 쓸 MT-32 엔진 (모드를 바꾸기 전에) */
int  gmi_mode(int i) { return i >= 0 && i < GM_INST_MAX ? gi[i].mode_s : GM_OFF; }
void gmi_midi(int i, uint8_t b) { gm_use(i); gm_midi(b); }
void gmi_render(int i, int32_t *l, int32_t *r, int n)
{
    gm_use(i); gm_render(l, r, n);
    if (n > 0 && G->mode_s != GM_OFF) {                 /* 소리 나는 동안 평균 크기 (진단용) */
        uint64_t e = 0; for (int k = 0; k < n; k++) { int64_t d = l[k]; e += (uint64_t)(d * d); }
        if (e / (uint64_t)n >= 900) { G->lv_sum += e; G->lv_n += (uint32_t)n; if (G->lv_n > 4800000) { G->lv_sum /= 2; G->lv_n /= 2; } }
    }
}
int gmi_level(int i) { return i >= 0 && i < GM_INST_MAX && gi[i].lv_n ? (int)isqrt64(gi[i].lv_sum / gi[i].lv_n) : 0; }
int  gmi_voices(int i) { gm_use(i); return gm_voices(); }
int  gm_any_on(void) { for (int i = 0; i < GM_INST_MAX; i++) if (gi[i].mode_s != GM_OFF) return 1; return 0; }
