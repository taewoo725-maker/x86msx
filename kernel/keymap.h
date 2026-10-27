/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* keymap.h - PC 키 → MSX 키 매트릭스 변환 */
#ifndef KEYMAP_H
#define KEYMAP_H
#include <stdint.h>

typedef struct {
    int8_t row, bit;            /* row < 0 이면 대응 키 없음 */
    int8_t shift;               /* -1 PC Shift 그대로, 0 강제 해제, 1 강제 */
    int8_t mods;                /* 함께 누를 MSX 키: 1 GRAPH, 2 CODE (2.13) */
} MsxKey;

typedef struct {
    uint8_t held[256];
    MsxKey target[256];         /* 누를 때 정해진 MSX 키 */
    uint32_t stamp[256];
    uint32_t clock;
    uint8_t shift_held[2];
    uint8_t altgr_held;                     /* 3.2.12 */
} KeyState;

/* PC 키보드 배열: 0 한국, 1 미국 (기호 위치 같음), 2 일본 106키, 3 독일, 4 프랑스, 5 영국 (3.2.12). 다음 keymap_init부터 적용 */
enum { KL_KR = 0, KL_US = 1, KL_JP = 2, KL_DE = 3, KL_FR = 4, KL_UK = 5, KL_COUNT = 6 };
void keymap_layout(int layout);
int keymap_altgr_layout(void);               /* 오른쪽 Alt를 AltGr로 쓰는 배열이면 1 (독일·프랑스) */
/* 3.2.12 KBD-02: 사용자 키 배치. code 0 = 기본, 0xFF = 안 보냄, 1 + 행*8 + 비트 = 그 MSX 키 */
void keymap_remap_clear(void);
void keymap_remap_set(int key, int code);
int keymap_remap_get(int key);
int keymap_default_get(int key);            /* 3.7.0: 기본 배치에서 이 PC 키가 누르는 MSX 키 (1 + 행*8 + 비트, 0 없음) */
/* MSX 기종 키보드 (2.13). BIOS 0x002C 아래 4비트와 같은 번호 + 러시아·아랍·폴란드.
 * 다음 keymap_init부터 적용 */
enum { KBD_JP = 0, KBD_INT = 1, KBD_FR = 2, KBD_UK = 3, KBD_DE = 4, KBD_ES = 6, KBD_SE = 7, KBD_DK = 9,
       KBD_RU = 16, KBD_AR = 17, KBD_PL = 18 };
void keymap_machine(int kbd);
int  keymap_machine_get(void);
/* 기종 판별: KeyLayout 값(config.ini)과 BIOS 0x002C 바이트 (bios_2c < 0 = 모름) */
int  keymap_legend(int kbd, int code);        /* 5.2.143 */
int  keymap_detect(const char *key_layout, int bios_2c);
const char *keymap_name(int kbd);
void keymap_init(KeyState *ks);
void keymap_event(KeyState *ks, int key, int pressed);   /* key: 스캔 코드 & 0x7F, E0이면 | 0x80 */
void keymap_matrix(const KeyState *ks, uint8_t matrix[16]);
int  keymap_press_row(const KeyState *ks, int key);      /* 5.0.183 */
#endif
