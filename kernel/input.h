/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* input.h - PC 스캔 코드 바이트 → MSX 키 매트릭스 (플랫폼 독립, 호스트 시험 가능) */
#ifndef INPUT_H
#define INPUT_H
#include "keymap.h"
#include "../src/msx.h"

#define INPUT_OK      0
#define INPUT_DEFER   1     /* 이 바이트는 다음 프레임에 다시 넣을 것 */

typedef struct {
    KeyState ks;
    int e0, e1;
    uint64_t press_frame[256];
    uint64_t release_frame[256];
    uint64_t last_new_press;
    uint8_t release_pending[256];
    int8_t press_row[256];
    uint32_t press_reads[256];          /* 누른 순간 그 키 행의 읽힌 횟수 */
} Input;

void input_init(Input *in);
/* 스캔 코드 바이트 하나 처리 */
int  input_byte(Input *in, const MSX *m, uint8_t c);
/* 프레임 시작마다: 미뤄 둔 뗌 반영 후 키 매트릭스 갱신 */
void input_frame(Input *in, MSX *m);
#endif
