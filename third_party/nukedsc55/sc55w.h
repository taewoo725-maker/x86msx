/* X86MSX 5.2.60 - Nuked-SC55 (Roland SC-55 실기 롬 에뮬레이션) 의 C 인터페이스. GPL-2.0-or-later (Nuked-SC55) + X86MSX GPL-3.0-or-later */
#ifndef SC55W_H
#define SC55W_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { SC55_MK2 = 0, SC55_MK1 = 1 };                     /* SC-55mkII (rom1·rom2·waverom1·waverom2·rom_sm), SC-55 (sc55_rom1·2·waverom1·2·3) */
#define SC55_NFILE 5
extern const char *const sc55w_files[2][SC55_NFILE];    /* 롬 파일 이름 (Nuked-SC55 와 같음) */
uint8_t *sc55w_tmp(void);                                /* 파일 읽을 자리 2 MiB (웨이브 롬 섞인 것을 풂) */
#define SC55_TMP_SIZE 0x200000u
int  sc55w_load(int model, int idx, uint32_t n);         /* sc55w_tmp() 에 읽은 idx 번째 파일 n 바이트를 넣음. 0 성공, -1 크기가 틀림 */
int  sc55w_start(int model);                             /* 파일 다 넣은 뒤 켬. 0 성공 */
void sc55w_close(void);
int  sc55w_ready(void);
int  sc55w_model(void);
void sc55w_midi(uint8_t b);
void sc55w_reset(void);                                  /* GS 리셋 SysEx 를 보냄 */
void sc55w_render(int32_t *l, int32_t *r, int n);        /* 48 kHz 스테레오 n 표본 (±32767) */
void sc55w_lcd_text(char *out17);                        /* LCD 윗줄 16 글자 (악기 이름·SysEx 글자) */
void sc55w_lcd_part(char *out4);                         /* 파트 번호 칸 3 글자 */
void sc55w_lcd_dots(uint16_t dot[16]);                   /* 16x16 막대·점 그림 (비트 15 = 왼쪽) */
uint32_t sc55w_steps(void);                              /* 지금까지 돌린 MCU 명령 수 (진단) */
#ifdef __cplusplus
}
#endif
#endif
