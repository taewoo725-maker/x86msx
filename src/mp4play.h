/* 만든이: KimTaeWoo */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* mp4play.h (8.0.91) - MP4 안의 H.264(baseline) 영상을 해독해 MSX 비트맵 화면(SCREEN 5·6·7·8)용 그림으로 바꿈.
 * 해독기: third_party/h264bsd (Apache-2.0, Android 에서 뽑은 것). 소리는 이 단계에서 없음 (8.0.92). */
#ifndef MP4PLAY_H
#define MP4PLAY_H
#include <stdint.h>
#include "mp4info.h"

typedef struct {
    int scr;                       /* 5 6 7 8 */
    int w, h;                      /* 화면 점 (5·8: 256, 6·7: 512) × 줄(192/212) */
    int stride;                    /* 한 줄 바이트 */
    uint32_t bytes;                /* 그림 바이트 수 (VRAM 0 번지부터) */
    uint8_t pal[16][3];            /* 3비트 RGB (SCREEN 8 은 안 씀) */
    int npal;                      /* 팔레트 칸 수 (5·7 = 16, 6 = 4, 8 = 0) */
    int pal_changed;               /* 이번 그림에서 팔레트가 바뀜 */
} Mp4Pic;

/* 열기: 성공이면 MP4E_NONE, 아니면 오류 코드. 열린 뒤 첫 그림은 mp4p_frame 으로 받음 */
/* fit: 0 가득 채움(넘치는 곳 자름) 1 비율 유지(검은 띠) 2 늘림 / dither: 0~100 / maxfps: 0 = 원본 */
int mp4p_open(Mp4Read rd, void *ctx, uint32_t fsize, int scr, int lines, int fit, int dither, int maxfps);
void mp4p_close(void);
uint32_t mp4p_len_ms(void);
uint32_t mp4p_fps100(void);
/* t_ms 에 보여야 할 그림까지 해독해 새 그림이 만들어지면 1, 아직이면 0, 끝에 이르러 더 없으면 -1.
 * budget_ms > 0 이고 ms 가 있으면 그 시간 안에서만 해독 (못 따라가면 키프레임으로 건너뜀) */
int mp4p_frame(uint32_t t_ms, uint32_t budget_ms, uint32_t (*ms)(void), Mp4Pic *pic);
/* 그림을 다시 만들 때 쓰는 마지막 그림 (오버레이를 얹기 전) */
const uint8_t *mp4p_image(void);
const Mp4Pic *mp4p_pic(void);
/* ms 이하의 가장 가까운 키프레임으로 이동. 그 시각(ms)을 돌려줌. 다음 mp4p_frame 이 그 키프레임을 해독 */
uint32_t mp4p_seek(uint32_t t_ms);
int mp4p_ended(void);              /* 마지막 샘플까지 해독했음 */
extern int mp4p_skip_nonref;                                 /* 8.0.139: 1 = 화면에 안 나올 비참조 프레임은 해독 생략 (ini Mp4SkipNonRef, 기본 0). Main/High 만 */
extern int mp4p_fast;                                      /* 8.0.123: 1 = 빠른 화면 처리 (ini Mp4Fast, 기본 0) */
extern uint32_t mp4p_fill;                                     /* 8.0.148: 영상 둘레 띠 색 0x00RRGGBB (기본 0) */
extern int mp4p_raw;                                        /* 8.0.131 */
int mp4p_rgb32(uint32_t *dst, int dw, int dh, int stride, int quick);  /* 8.0.131: 마지막 그림을 V9990 용 RGB 로 (1 = 그렸음) */
int mp4p_why(void);                                         /* 8.0.124 진단: 코드 4 를 돌려준 곳 번호 */
uint32_t mp4p_skipped(void);       /* 8.0.139: 해독을 생략한 비참조 프레임 수 (진단) */
uint32_t mp4p_dropped(void);       /* 건너뛴 프레임 수 (진단) */
/* ---- 8.0.93 소리: AAC-LC (third_party/faad2). mp4p_open 이 성공한 뒤에 부름 ---- */
int mp4p_audio_open(void);                                 /* 1 = 소리를 낼 수 있음, 0 = 소리 트랙 없음·못 읽음·AAC-LC 아님 (영상만 재생) */
/* 다음 AAC 프레임 하나를 해독. 성공 1 (pcm = 16비트 인터리브, frames = 채널당 샘플 수), 끝 0. 깨진 프레임은 건너뜀 */
int mp4p_audio_next(const int16_t **pcm, int *frames, int *ch, int *hz);
uint32_t mp4p_audio_seek(uint32_t t_ms);                   /* 그 시각 이하의 프레임으로 이동하고 그 시각(ms)을 돌려줌 */
uint32_t mp4p_arena_used(void);                            /* 고정 영역 사용량 (진단: 해독 중 늘지 않아야 함) */
uint32_t mp4p_audio_frames(void);                          /* 해독한 프레임 수 (진단) */
#endif
