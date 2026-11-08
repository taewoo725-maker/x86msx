/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* avi.h (8.0.120) - AVI(RIFF) 분리기: 영상(MJPEG)·소리(PCM) 조각의 위치 표를 만든다. 해독은 안 함. */
#ifndef AVI_H
#define AVI_H
#include <stdint.h>
#include "mp4info.h"

typedef struct {
    uint32_t vfcc;                 /* 영상 코덱 4글자 (MJPG 등, 소문자는 대문자로) */
    int w, h;
    uint32_t nv;                   /* 영상 조각 수 */
    uint32_t scale, rate;          /* 초당 프레임 = rate / scale */
    int has_audio; int atag, ach, abits; uint32_t arate;    /* WAVEFORMATEX: 형식(1 = PCM), 채널, 비트, 샘플율 */
    uint32_t na;                   /* 소리 조각 수 */
} AviInfo;

/* 호출한 쪽 배열에 채움 (용량 넘으면 거기까지). voff/vsize/vkey: 영상, aoff/asize: 소리 */
typedef struct {
    uint32_t *voff, *vsize; uint8_t *vkey; uint32_t vcap;
    uint32_t *aoff, *asize; uint32_t acap;
} AviTabs;

int avi_is(Mp4Read rd, void *ctx, uint32_t size);                              /* RIFF....AVI  인가 */
int avi_parse(Mp4Read rd, void *ctx, uint32_t size, AviInfo *out, AviTabs *t); /* 성공 MP4E_NONE, t 가 0 이면 정보만 */
#endif
