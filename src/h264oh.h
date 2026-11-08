/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* h264oh.h - 8.0.96 MP4 2c단계: Main/High 프로파일 H.264 (CABAC·B 프레임·8x8 변환) 해독기 = Cisco OpenH264 (BSD-2, third_party/openh264) 를 C 에서 쓰는 얇은 껍데기.
 * 메모리는 mp4play 의 고정 영역에서 한 덩어리를 받아 자체 힙으로 나눠 씀 (해독기가 free 도 부르므로 bump 만으로는 부족). 스레드 없음 (한 코어). */
#ifndef H264OH_H
#define H264OH_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { const uint8_t *y, *u, *v; int w, h, sy, suv; uint32_t ts; } Oh264Pic;   /* 해독기 안 버퍼를 가리킴 — 다음 해독 전에 복사할 것 */
int oh264_open(size_t heap_bytes);                       /* 힙을 mp4h_alloc 으로 잡고 해독기 생성. 0 = 성공 */
void oh264_close(void);                                  /* 해독기 해제 (힙 영역은 mp4p_close 가 영역 전체를 되돌릴 때 같이) */
int oh264_decode(const uint8_t *nal, int len, uint32_t ts, Oh264Pic *pic);   /* 시작 코드(00 00 00 01) 붙은 NAL 하나. 1 = 그림 나옴, 0 = 안 나옴, -1 = 오류 */
int oh264_flush(Oh264Pic *pic);                          /* 해독기 안에 남은 그림 하나 꺼냄 (1 = 있음). 건너뛰기·닫을 때 */
uint32_t oh264_heap_peak(void);                          /* 진단: 힙 최대 사용 바이트 */
#ifdef __cplusplus
}
#endif
#endif
