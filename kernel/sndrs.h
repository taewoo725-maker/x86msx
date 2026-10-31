/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* 소리 속도 맞춤 (HD Audio 링 버퍼용). 전에는 지연이 목표에서 600샘플 넘게 벗어나면 64샘플마다 하나를
 * 빼거나 두 번 써서(음높이 약 1.5%) 5 Hz 안팎으로 떨리고 딱딱 소리가 났음. 이제 선형 보간으로 한 샘플 안에서
 * 매끄럽게 속도를 바꾸고, 바꾸는 폭은 지연이 목표에서 벗어난 만큼을 4초에 걸쳐 천천히 따라가며 +-0.5% (약 9센트) 안.
 * 순수 정수 함수라 PC 시험(tests/sndrstest.c)에서 그대로 시험함. */
#ifndef SNDRS_H
#define SNDRS_H
#include <stdint.h>

typedef struct { uint32_t p; int32_t prev_l, prev_r, d_avg; int init; int32_t base_acc; int32_t last; } SndRs;

static inline void sndrs_reset(SndRs *r, int target) { r->p = 0; r->prev_l = r->prev_r = 0; r->d_avg = target; r->init = 0; r->base_acc = 0; r->last = 65536; }

/* ---- 기본 (5.3.14 까지 실기에서 검증된 루틴, 그대로 둠 — 5.3.17: 기본은 흔들리지 않는다) ---- */
/* 지연 d(샘플)와 목표 target 으로 한 프레임의 걸음 (16.16, 65536 = 같은 속도). 지연이 많으면(>1) 빨리 먹음 */
static inline int32_t sndrs_step(SndRs *r, int d, int target)
{
    r->d_avg += (d - r->d_avg) / 16;                    /* 프레임 시간 흔들림은 평균으로 */
    int32_t err = r->d_avg - target;
    int32_t adj = (int32_t)((int64_t)err * 65536 / (48000 * 4));     /* 4초 */
    if (adj > 327) adj = 327;                           /* +0.5% */
    if (adj < -327) adj = -327;
    return 65536 + adj;
}


/* ---- 시험용 2번 루틴 (x86msx.ini SndSync=2 일 때만). 5.3.15: 음높이가 "늘어지는" 문제. 전에는 지연이 목표에서 벗어난 만큼 4초에 걸쳐 +-0.5%(9센트)까지 속도를 바꿨는데,
 * 프레임이 한 번 늦어 버퍼가 비면 몇 초 동안 0.5% 느리게 틀어서 PSG 같은 순음은 음이 처졌다 돌아오는 게 들렸음.
 * 이제:  ① 목표 +-SNDRS_DEAD 안에서는 속도를 전혀 안 바꿈 (평소엔 음높이 고정)
 *        ② 벗어나면 5초에 걸쳐 따라가되 최대 +-0.25% (4센트; 5.3.16)
 *        ③ PC 시계와 MSX 시계의 꾸준한 차이(60.00 대 59.92 Hz 등)는 몇 분에 걸쳐 base 에 배워 두고 늘 그만큼만 보정 (+-0.3%)
 *        ④ 속도는 프레임마다 아주 조금씩만 바꿈 (미끄러지는 느낌 없음)
 *        지연이 크게 틀어진 경우(끊김)는 hda.c 가 한 번에 자리를 맞춤 (몇 초 늘어지는 것보다 짧은 한 번이 덜 거슬림) */
#define SNDRS_DEAD   400                                /* 8 ms */
#define SNDRS_MAXADJ 164                                /* 0.25% (5.3.16: 0.1% 는 화면 60.00 대 MSX 59.92 Hz 차이 0.13% 보다 작아 버퍼가 차고 프레임을 버려 버벅였음) */
#define SNDRS_MAXBASE 197                               /* 0.3% */
static inline int32_t sndrs_step2(SndRs *r, int d, int target)
{
    r->d_avg += (d - r->d_avg) / 16;                    /* 프레임 시간 흔들림은 평균으로 */
    int32_t err = r->d_avg - target;
    int32_t adj = 0;
    if (err > SNDRS_DEAD) adj = (int32_t)((int64_t)(err - SNDRS_DEAD) * 65536 / (48000 * 5));
    else if (err < -SNDRS_DEAD) adj = (int32_t)((int64_t)(err + SNDRS_DEAD) * 65536 / (48000 * 5));
    if (adj > SNDRS_MAXADJ) adj = SNDRS_MAXADJ;
    if (adj < -SNDRS_MAXADJ) adj = -SNDRS_MAXADJ;
    /* 꾸준한 차이 배우기: 띠 밖에서 보정하는 동안 그 양을 아주 천천히 base 에 누적 (0.1% 모자라면 약 1분 뒤 base 가 0.1%) */
    r->base_acc += adj;                                 /* 5.3.16: 1/2048 (약 30초에 0.1%) — 처음 켠 뒤 빨리 자리 잡게 */
    if (r->base_acc > SNDRS_MAXBASE << 11) r->base_acc = SNDRS_MAXBASE << 11;
    if (r->base_acc < -(SNDRS_MAXBASE << 11)) r->base_acc = -(SNDRS_MAXBASE << 11);
    int32_t want = 65536 + (r->base_acc >> 11) + adj;
    if (want > r->last + 4) want = r->last + 4;         /* 프레임마다 0.006% 까지만 */
    if (want < r->last - 4) want = r->last - 4;
    r->last = want;
    return want;
}

/* n 샘플(l, r(없으면 0))을 step 걸음으로 읽어 ol/or 에 씀. 쓴 개수를 돌려줌 (maxout 넘지 않음) */
static inline int sndrs_run(SndRs *r, const int16_t *l, const int16_t *rt, int n, int32_t step, int16_t *ol, int16_t *orr, int maxout)
{
    int o = 0;
    if (!r->init) { r->init = 1; r->prev_l = n ? l[0] : 0; r->prev_r = (rt && n) ? rt[0] : 0; }
    while (o < maxout) {
        uint32_t i = r->p >> 16, f = r->p & 0xFFFFu;
        if ((int)i >= n) break;
        int32_t al = i ? l[i - 1] : r->prev_l, bl = l[i];
        ol[o] = (int16_t)(al + (int32_t)(((int64_t)(bl - al) * (int32_t)f) >> 16));
        if (rt) {
            int32_t ar = i ? rt[i - 1] : r->prev_r, br = rt[i];
            orr[o] = (int16_t)(ar + (int32_t)(((int64_t)(br - ar) * (int32_t)f) >> 16));
        }
        o++;
        r->p += (uint32_t)step;
    }
    if ((int)(r->p >> 16) >= n) {                       /* 이번 덩어리를 다 읽음: 다음 덩어리로 넘어감 */
        r->p -= (uint32_t)n << 16;
        if (n) { r->prev_l = l[n - 1]; r->prev_r = rt ? rt[n - 1] : 0; }
    }
    return o;
}
#endif
