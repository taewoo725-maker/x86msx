/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* mmbar.h - 영상 아래 파란 표시줄(재생 막대)을 C 로 그림. X86MV 의 MV5/MVC 하단바(V1.11.30)와 같은 배치 (8.0.149) */
#ifndef MMBAR_HEADER_H
#define MMBAR_HEADER_H
#include <stdint.h>
#define MMBAR_W 256                      /* 가로 점 (SCREEN 6·7 은 호출한 쪽이 2배로 늘림) */
#define MMBAR_H 20                       /* 세로 줄: 그림 192..211 */
enum { MMBAR_DARK = 0, MMBAR_LIGHT = 1, MMBAR_BAR = 2, MMBAR_TEXT = 3 };   /* 칸의 역할 */
typedef struct {
    int hidden;                          /* 1 = 글자 줄이 꺼져 있음 → 전부 어둡게 */
    int paused;                          /* 1 = 일시정지 (재생 기호) */
    int level;                           /* 볼륨 단계 0..5 (0 = 최대, 5 = 소리 없음) */
    uint32_t cur, total;                 /* 지금·전체 (1/15 초 프레임) */
} MmBarState;
void mmbar_render(const MmBarState *st, uint8_t roles[MMBAR_H][MMBAR_W]);
int  mmbar_level_of_percent(int pct);    /* 볼륨 % → 단계 (X86MV vc_pct = 100,71,50,35,25,0) */
int  mmbar_progress(uint32_t cur, uint32_t total);   /* 0..63 */
/* 8.0.152 화면 번호 이름표(32x14)와 일시정지 큰 표시(32x40): X86MV V1.11.31 의 b6_bdg·b6_pau 와 같은 모양. 칸 값 MMBAR_NONE = 영상이 그대로 보임 */
#define MMBAR_NONE 255
#define MMBAR_BADGE_W 32
#define MMBAR_BADGE_H 14
#define MMBAR_BADGE_X 216                /* 영상 위 자리 (256점 기준) */
#define MMBAR_BADGE_Y 4
#define MMBAR_SIGN_W 32
#define MMBAR_SIGN_H 40
#define MMBAR_SIGN_X 112
#define MMBAR_SIGN_Y 76
void mmbar_badge(int scr, uint8_t out[MMBAR_BADGE_H][MMBAR_BADGE_W]);   /* 칸: MMBAR_BAR(바탕) · MMBAR_TEXT(글자) · MMBAR_NONE */
void mmbar_sign(uint8_t out[MMBAR_SIGN_H][MMBAR_SIGN_W]);              /* 칸: MMBAR_LIGHT(테두리·막대) · MMBAR_DARK(안쪽) · MMBAR_NONE */
#endif
