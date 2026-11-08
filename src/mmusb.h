/* 만든이: KimTaeWoo */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * mmusb.h (8.0.80) - X86 Multimedia 카트리지의 USB 직접 접근 (읽기 전용). 영상 포트 24h~27h 의 명령 20h~29h (mmvid.c 가 넘겨 줌).
 * USB 를 MSX-DOS 드라이브로 붙이지 않고, X86MP3·X86MV 가 카트리지에게 "USB 목록 → 폴더 목록 → 파일 열기·읽기" 를 시킨다.
 * 명령은 24h 에 쓰고, 인자는 26h 로 차례로(작은 바이트부터), 결과는 25h 에서 차례로 읽는다. 상태 24h: bit2 끝(파일 끝) bit3 오류 bit4 목록 끝.
 *
 *   20 CHANGE   인자 없음. 결과: 바뀜 번호(1바이트, USB 가 꽂히거나 빠질 때 +1), USB 수(1바이트)
 *   21 VOLS     인자: 시작 번호(1). 결과: 종류(0 있음, FFh 끝), 번호, 플래그(bit0 시스템 USB), 이름 길이, 이름(UTF-8, "Drive 2 라벨 28 GB")
 *   22 SEL      인자: USB 번호(1). 그 USB 의 맨 위 폴더로. 열려 있던 파일은 닫힘. 없는 번호면 오류
 *   23 CD       인자: 항목 번호(2). FFFFh = 위 폴더. 마지막 24 LIST 의 번호. 폴더가 아니거나 맨 위에서 위로면 오류
 *   24 LIST     인자: 시작(2), 개수(1, 1~8), 걸러내기(1: 0 모든 파일, 아니면 bit0 mp3·wav·flac, bit1 mv2, bit2 나머지 파일. 폴더는 항상).
 *               결과: 전체 항목 수(2), 이번 개수(1), 플래그(bit0 = 항목이 너무 많아 앞 4096개만), 항목마다 [종류(0 파일,1 폴더), 크기(4), 이름 길이(1), 이름]
 *               폴더 먼저, 이름순(대소문자 무시). 시작이 전체 수 이상이면 목록 끝(bit4)
 *   25 OPEN     인자: 항목 번호(2). 파일을 열고 결과: 크기(4). 한 번에 하나
 *   26 READ     인자: 바이트 수(2, 최대 1024). 결과: 읽은 바이트들. 파일 끝에 닿으면 끝(bit2)
 *   27 SEEK     인자: 위치(4). 파일 크기로 잘림
 *   28 CLOSE    인자 없음
 *   29 INFO     인자 없음. 결과: USB 번호(1, 없으면 FFh), 파일 열림(1), 크기(4), 위치(4), 경로(UTF-8, 00h 로 끝)
 * 항목 번호는 그 폴더의 마지막 LIST 결과를 기준으로 한다 (폴더를 옮기면 다시 LIST). 이름은 경로가 아니라 번호로만 다뤄 폴더 밖으로 못 나감.
 * 쓰기·지우기는 없음 (읽기 전용). exFAT·NTFS 등 펌웨어가 못 읽는 USB 는 목록에 안 나옴. USB 를 뽑으면 다음 명령이 오류 → CHANGE 로 다시 확인.
 */
#ifndef MSX_MMUSB_H
#define MSX_MMUSB_H
#include <stdint.h>

#define MMU_END  4
#define MMU_ERR  8
#define MMU_LEND 16
#define MMUSB_MAXE 4096

typedef struct { char name[384]; uint8_t is_dir; uint32_t size; } UsbEnt;   /* 긴 이름 (UTF-8 384바이트 미만; 더 길면 호스트가 뺌) */
typedef struct { char name[64]; char key[8]; uint8_t boot; } UsbVol;
typedef struct {
    int (*vols)(UsbVol *out, int max);                                          /* 드라이브 목록, 수 / 실패 -1 */
    int (*list)(const char *dir, UsbEnt *out, int max);                         /* 폴더 항목 (긴 이름), 수 / 실패 -1 */
    int (*read_at)(const char *path, uint32_t off, uint8_t *buf, uint32_t len); /* 성공 0 */
    char sep;                                                                   /* 경로 구분 글자 (0 = 역슬래시) */
} UsbHost;

void mmusb_set_host(const UsbHost *h);
int mmusb_available(void);                      /* 호스트가 있으면 1 (IDENT 능력 비트) */
void mmusb_reset(void);                         /* 꽂을 때·리셋: 열린 파일·선택 해제 */
int mmusb_args(uint8_t cmd);                    /* 명령 cmd 의 인자 수, USB 명령이 아니면 -1 */
uint8_t mmusb_run(uint8_t cmd, const uint8_t *arg);   /* 실행. 돌려주는 값 = 상태 비트 (MMU_*) */
int mmusb_cur_file(char *path, int max, uint32_t *size);   /* OPEN 해 둔 파일의 경로·크기 (있으면 1). mvsnd_set_usbfile 에 넣는다 */
extern uint32_t (*mmusb_ms)(void);                                    /* 8.0.87 진단: 시계 (느린 명령 기록용) */
void mmusb_flush(void);                                                    /* 8.0.108 목록 캐시 비우기 */
extern void (*mmusb_crumb)(const char *what);                      /* 8.0.83 진단용: 파일을 여는 단계 기록 (없으면 NULL) */
extern int mmusb_log_read;                                                /* 8.0.124 */
extern int mm_crumb_quiet;                                                /* 8.0.116 */
void mm_crumb_cmd(const char *tag, int c, int important);                /* 8.0.84: "tag XX" 한 줄 (important=0 이면 자주 오는 명령이라 플랫폼이 0.5초에 한 번만 남김) */
void mmusb_dbg(int *sel, int *nent, int *valid, int *filter);        /* 8.0.107 진단: 고른 USB·목록 상태 */
#define MM_CRUMB(s) do { if (mmusb_crumb) mmusb_crumb(s); } while (0)
int mmusb_entry_file(int idx, char *path, int max, uint32_t *size);   /* 마지막 LIST 의 idx 번째 파일 경로·크기 (영상 OPEN FFFFh 용) */
uint8_t mmusb_get(void);                        /* 결과 바이트를 차례로 */
#endif
