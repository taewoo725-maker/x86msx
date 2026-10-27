/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* 소리 속도 맞춤 (HD Audio 링 버퍼용). 전에는 지연이 목표에서 600샘플 넘게 벗어나면 64샘플마다 하나를
 * 빼거나 두 번 써서(음높이 약 1.5%) 5 Hz 안팎으로 떨리고 딱딱 소리가 났음. 이제 선형 보간으로 한 샘플 안에서
 * 매끄럽게 속도를 바꾸고, 바꾸는 폭은 지연이 목표에서 벗어난 만큼을 4초에 걸쳐 천천히 따라가며 +-0.5% (약 9센트) 안.
 * 순수 정수 함수라 PC 시험(tests/sndrstest.c)에서 그대로 시험함. */
#ifndef SNDRS_H
#define SNDRS_H
#include <stdint.h>

typedef struct { uint32_t p; int32_t prev_l, prev_r, d_avg; int init; } SndRs;

static inline void sndrs_reset(SndRs *r, int target) { r->p = 0; r->prev_l = r->prev_r = 0; r->d_avg = target; r->init = 0; }

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
