/* 만든이: KimTaeWoo */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * mmvid.h (8.0.75) - X86 Multimedia 카트리지의 영상 쪽 (MAPPER_MMEDIA). 소리 쪽은 mvsnd (포트 2Ch~2Fh, 규격 그대로).
 * 영상: I/O 24h~27h. 카트리지가 USB 의 영상 폴더(\Video) 에서 MV2 파일을 직접 읽어 VRAM 에 쓴다 (Z80 은 이름과 명령만).
 * 규격: 「X86 Video 카트리지 요청서 v3」 (2026-11-01) 에서 포트만 28h~2Bh → 24h~27h 로 바꿈 (28h~2Bh 는 네트워크 카트리지).
 *
 *   쓰기 24h 명령          읽기 24h 상태 (카트리지 없으면 FFh):
 *        25h 글자 줄 자료 1바이트      bit0 바쁨 1 글자 줄 받기 가능 2 끝 3 오류 4 목록 끝 5 재생 중 6 일시정지
 *        26h 인자 P0, P1, … (명령을 쓰면 번호 0)        25h 결과 바이트 (차례로)
 *        27h (예비)                                    26h/27h 위치(1/15초) 낮은/높은 바이트
 *   명령 01 IDENT('X','V','D',01h) 10 LIST 11 OPEN 03 PLAY 04 PAUSE 05 STOP 06 SEEK 07 VOL 08 INFO 09 POS 0D OVL 0E OVLON
 * 1단계: MV2 만 (SCREEN 2). 8.0.90: MP4(.mp4 .m4v .mov)는 상자 구조만 읽어 INFO·오류 코드(25h 1바이트)를 줌, 해독기 전이라 OPEN 은 항상 오류(bit3, 코드 5).
 *   IDENT 5번째 바이트 bit3 = MP4 재생 가능 (해독기가 생기기 전에는 0). USB LIST(24h) 걸러내기 bit3(8) = 영상(.mv2 .mp4 .m4v .mov). 위치·소리 시계는 소리 쪽(mvsnd_position)을 따름.
 */
#ifndef MSX_MMVID_H
#define MSX_MMVID_H
#include <stdint.h>
#include "vdp.h"

#define MMVID_PORT_BASE 0x24

typedef struct { char name[64]; uint8_t is_dir; uint32_t size; } MmEnt;
typedef struct {
    int (*list)(const char *dir, MmEnt *out, int max);                                     /* 항목 수, 실패 -1 */
    int (*read_at)(const char *path, uint32_t off, uint8_t *buf, uint32_t len);            /* 성공 0 */
    const char *root;                                                                      /* 영상 폴더 (맨 위) */
    char sep;                                                                              /* 경로 구분 글자 (0 = 역슬래시) */
} MmHost;

/* 8.0.109 MSX 디스크 파일: 드라이브 번호(0=A: …) + DOS 경로 → 호스트 파일 경로·크기. 찾으면 1, 없으면 0, 읽을 수 없는 드라이브(디스크 없음·FAT12/32 하드 이미지·없는 드라이브)면 -1 (오류 9). 넣으면 IDENT 5번째 바이트 bit5 = 1 */
typedef struct {
    int (*find)(int drive, const char *dospath, char *host, int max, uint32_t *size);   /* 1 = 호스트 파일(host 에 경로), 2 = 하드 이미지 안 파일(host 에 첫 글자 01h 인 표식, 읽기는 read_at) */
    int (*read_at)(const char *token, uint32_t off, uint8_t *buf, uint32_t len);        /* 표식 파일 읽기, 성공 0 (없어도 됨) */
} MmDisk;
void mmvid_set_disk(const MmDisk *d);          /* 8.0.109: 2Ah SETPATH + OPEN P0P1=FFFDh */
void mmvid_set_host(const MmHost *h);          /* 커널이 넣어 줌. 없으면 LIST 는 비고 OPEN 은 오류 */
#include "v9990.h"
void mmvid_set_v9990(V9990 *p);                 /* 8.0.131 msx.c 가 프레임마다 알려 줌 (Gfx9000 이 없으면 0) */
void mmvid_reset(void);                        /* 꽂을 때·리셋·상태 불러온 뒤: 모두 멈춤 */
uint8_t mmvid_read(int port);                  /* 24h~27h */
void mmvid_write(int port, uint8_t v);
void mmvid_tick(VDP *v);                       /* 프레임 끝마다: 소리 먹이기·영상 갱신 */
void mmvid_attach(VDP *v);                     /* 화면 장치 (꽂을 때) */
int mmvid_playing(void);
uint32_t mmvid_frame(void);                    /* 지금 보이는 프레임 번호 (진단·시험) */
/* 8.0.155 점검: 한 프레임 걸린 시간(밀리초) 모음. dec = 해독+색 변환, show = 화면(VRAM)에 올리기(V9990 은 오차 확산 포함). slow = 33 ms 넘은 프레임 수 */
typedef struct { uint32_t n, sum, max, slow; } MmVidTimes;
extern MmVidTimes mmvid_t_dec, mmvid_t_show;
#endif
