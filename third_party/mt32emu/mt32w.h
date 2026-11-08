/* X86MSX 5.0.180 - mt32w.cpp 의 C 인터페이스 */
#ifndef MT32W_H
#define MT32W_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MT32W_N 2                                           /* 5.2.173: 엔진 둘 (MT-32 두 대를 서로 다른 롬으로) */
void mt32w_arena(int slot, void *mem, uint32_t size);       /* 슬롯이 쓸 메모리 덩어리 (4 MiB 이상) */
int  mt32w_open(int slot, const uint8_t *ctl, uint32_t ctl_n, const uint8_t *pcm, uint32_t pcm_n);   /* 0 성공, -1 롬이 아님, -2 열기 실패, -4 메모리 */
void mt32w_close(int slot);
int  mt32w_ready(int slot);
const char *mt32w_name(int slot);                           /* 롬 이름 (예: ctrl_mt32_1_07) */
void mt32w_midi(int slot, uint8_t b);
void mt32w_reset(int slot);
void mt32w_real(int on);                                    /* 8.0.68: 1 = 실기 DAC 방식 (롬 종류로 세대 선택), 0 = 깨끗하게 */
void mt32w_reverb(int rv);                                /* 8.0.68: 0 롬 기본, 1 룸 2 홀 3 플레이트 4 탭딜레이 를 시작·리셋 때 깔기 */
void mt32w_fx(int rev_pct);                             /* 5.2.159: 리버브 출력 이득 (%, 100 = 그대로) — 모든 슬롯 */
void mt32w_render(int slot, int32_t *l, int32_t *r, int n); /* 48 kHz 스테레오 n표본 (±32767 안팎) */
uint32_t mt32w_peak(int slot);
#ifdef __cplusplus
}
#endif
#endif
