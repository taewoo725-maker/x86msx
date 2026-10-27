/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * input.c - 키 입력 처리 (설계서 §4.6)
 *
 * DPC-200 BIOS는 키 매트릭스를 3프레임(약 50 ms)마다 읽는다 (호스트 측정, 2026-10-01).
 * 그래서 입력 계층은 다음을 보장한다 (KEY_MIN_FRAMES = 3):
 *   1. 누른 키는 최소 3프레임 동안 눌린 상태 (빠른 뗌은 그 키만 미룸)
 *   2. 같은 키를 다시 누를 때는 뗀 상태가 최소 3프레임 보인 뒤
 *   3. 서로 다른 일반 키의 새 누름 사이는 최소 3프레임
 *      (같은 읽기 구간에 두 키가 새로 눌리면 BIOS가 매트릭스 순서로 읽어 순서가 바뀜)
 *      Shift/Ctrl/Alt는 글자를 만들지 않으므로 예외. 5.0.183: MSX 8행 키(스페이스·방향키 등)도 예외 —
 *      매트릭스 마지막 행이라 같은 읽기 구간에 들어가도 앞 키보다 먼저 읽히지 않음
 * 조건이 안 되면 INPUT_DEFER를 돌려 그 바이트부터 다음 프레임에 다시 처리한다 (순서 보존).
 */
#include "input.h"
#include <string.h>

#define KEY_MIN_FRAMES 3

void input_init(Input *in)
{
    memset(in, 0, sizeof *in);
    keymap_init(&in->ks);
    in->last_new_press = (uint64_t)0 - KEY_MIN_FRAMES;
    for (int k = 0; k < 256; k++) in->release_frame[k] = (uint64_t)0 - KEY_MIN_FRAMES;
}

static int is_modifier(int key)
{
    return key == 0x2A || key == 0x36 || key == 0x1D || key == 0x9D || key == 0x38 || key == 0xB8 || key == 0xAA || key == 0xB6;
}

/* 방향키는 게임이 매 프레임 읽으므로 최소 눌림·다시 누름 간격을 1프레임으로 (대각선 이동이 3프레임씩 끌려 뚝뚝 끊기던 것). 글자 입력용 키는 그대로 3프레임 */
static int min_frames(const Input *in, int key)
{
    /* 방향키(E0 48·4B·4D·50 = 0xC8·0xCB·0xCD·0xD0)만. 스페이스 같은 글자 입력용 8행 키는 BIOS 가 3프레임마다 읽어 1프레임 눌림을 놓침 */
    return (key == 0xC8 || key == 0xCB || key == 0xCD || key == 0xD0) && keymap_press_row(&in->ks, key) == 8 ? 1 : KEY_MIN_FRAMES;
}

/* 눌린 뒤 최소 프레임이 지났고, 그 키가 있는 행을 Z80 이 한 번은 읽었어야 뗄 수 있다 (디스크를 읽는 동안처럼 인터럽트가 오래 막혀
 * BIOS 가 키보드를 못 읽으면 짧게 누른 글자가 사라지던 것). 안 읽혀도 20프레임이 지나면 뗌 */
static int release_ok(const Input *in, const MSX *m, int key)
{
    if (m->frame - in->press_frame[key] < (uint64_t)min_frames(in, key)) return 0;
    int row = in->press_row[key];
    if (row < 0 || row > 10) return 1;
    return m->kbd_row_reads[row] != in->press_reads[key] || m->frame - in->press_frame[key] >= 20;
}

int input_byte(Input *in, const MSX *m, uint8_t c)
{
    if (c == 0xE0) { in->e0 = 1; return INPUT_OK; }
    if (c == 0xE1) { in->e1 = 5; return INPUT_OK; }     /* Pause 시퀀스 무시 */
    if (in->e1) { in->e1--; return INPUT_OK; }
    if (c == 0xFA || c == 0xFE || c == 0xEE || c == 0x00 || c == 0xFF) { in->e0 = 0; return INPUT_OK; }
    int key = (c & 0x7F) | (in->e0 ? 0x80 : 0);
    int press = !(c & 0x80);
    uint64_t now = m->frame;

    if (press && in->release_pending[key])
        return INPUT_DEFER;             /* 규칙 2: 미뤄 둔 뗌이 먼저 (자동 반복으로 오인하지 않도록) */
    if (press && !in->ks.held[key]) {
        if (now - in->release_frame[key] < (uint64_t)min_frames(in, key)) return INPUT_DEFER;
        if (!is_modifier(key) && now - in->last_new_press < KEY_MIN_FRAMES && keymap_press_row(&in->ks, key) != 8)
            return INPUT_DEFER;                                           /* 규칙 3 (5.0.183: 8행 스페이스·방향키·HOME·INS·DEL은
                                                                             BIOS가 맨 마지막에 읽으므로 순서가 바뀌지 않음 — 미루지 않아 게임 반응이 빠름) */
    }
    in->e0 = 0;

    if (press) {
        if (!in->ks.held[key]) {
            in->press_frame[key] = now;
            { int r_ = keymap_press_row(&in->ks, key); in->press_row[key] = (int8_t)(r_ >= 0 && r_ <= 10 ? r_ : -1); in->press_reads[key] = r_ >= 0 && r_ <= 10 ? m->kbd_row_reads[r_] : 0; }
            if (!is_modifier(key)) in->last_new_press = now;
        }
        keymap_event(&in->ks, key, 1);                  /* 이미 눌린 키(자동 반복)는 무시됨 */
    } else if (in->ks.held[key] && !release_ok(in, m, key)) {
        in->release_pending[key] = 1;                   /* 규칙 1 */
    } else {
        keymap_event(&in->ks, key, 0);
        in->release_frame[key] = now;
    }
    return INPUT_OK;
}

void input_frame(Input *in, MSX *m)
{
    for (int k = 0; k < 256; k++)
        if (in->release_pending[k] && release_ok(in, m, k)) {
            in->release_pending[k] = 0;
            keymap_event(&in->ks, k, 0);
            in->release_frame[k] = m->frame;
        }
    keymap_matrix(&in->ks, m->kbd);
}
