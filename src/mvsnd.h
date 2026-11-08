/* 만든이: KimTaeWoo */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * mvsnd.h (8.0.55) - X86 Multimedia 카트리지의 소리 쪽 (MAPPER_MMEDIA). 메모리 창 없음, I/O 2Ch~2Fh.
 * X86MV(MSX 쪽 MV2 뮤직비디오 플레이어)가 MP3 바이트를 2Dh 로 흘려 보내면 이 카트리지가 해독해 믹서(PCM 채널)로 낸다.
 * 규격: 「X86MV 소리 카트리지 요청서 V1.1.0」(2026-11-01).
 *
 *   읽기 2Ch 상태 (비트 7 = 1 항상, 3 = 재생 중, 1 = FIFO 빈자리 2 KB 이상, 0 = 끊김(읽으면 0), 나머지 0)
 *        2Dh 식별 'M','V','S',01h (IDENT 뒤부터 차례로, 5번째부터 00h)
 *        2Eh 위치 아래 8비트 (읽는 순간 16비트를 붙잡음)   2Fh 붙잡은 값의 위 8비트
 *   쓰기 2Ch 명령 (00 IDENT 01 OPEN 02 PLAY 03 PAUSE 04 STOP 05 SETFPS 06 SETVOL 07 LFN_PATH 08 LFN_READ 09 OPEN_FILE 0A FILE_INFO 0B SEEK 0C OPEN_USB_MP3, 모르는 명령은 무시)
 *        2Dh MP3 바이트 → FIFO   2Eh 매개값 아래 8비트   2Fh 매개값 위 8비트
 *   위치 = OPEN 의 첫 프레임 번호 + 믹서로 실제 내보낸 48 kHz 샘플 수 × fps / 48000 (내림). PAUSE 중 안 변함.
 *
 * 카트리지는 하나만 꽂히는 것으로 보고 상태는 이 파일 안의 정적 변수 하나 (MSX 구조체 밖 — 64 KB FIFO + PCM 예비가 커서).
 * 상태 파일에는 첫 프레임·흐른 샘플·PLAY 만 (msx.c 가 카트의 빈 칸 rs.tx_count/rx_count/tflag 에 비춰 둠), 불러오면 STOP.
 */
#ifndef MSX_MVSND_H
#define MSX_MVSND_H
#include <stdint.h>

#define MVSND_PORT_BASE 0x2C
#define MVSND_FIFO_SIZE 65536u               /* MP3 바이트 고리 (16 KB/s → 약 4초) */
#define MVSND_PCM_SIZE  65536u               /* 48 kHz 해독 결과 고리 (샘플, 약 1.4초) */
#define MVSND_PRE_SAMPLES 24000u             /* 미리 해독해 둘 양 (0.5초) */

void mvsnd_reset(void);                      /* 전원·리셋·꽂을 때: STOP, FIFO 비움, fps 150, 음량 100 */
uint8_t mvsnd_read(int port);                /* port = 2Ch~2Fh */
void mvsnd_write(int port, uint8_t v);
int mvsnd_render_stereo(int16_t *out, int16_t *outL, int16_t *outR, int n);   /* 8.0.232 모노 + 좌우 (outL/outR 은 NULL 가능, (L+R)/2 == 모노) */
int mvsnd_render(int16_t *out, int n);       /* 믹서용 48 kHz 모노 n 샘플 (음량 반영). 재생 중이 아니면 0 을 채우고 0 반환, 재생 중이면 1 */
void mvsnd_tick(int (*post)(void (*)(void *), void *), int (*waitf)(int));   /* 8.0.56: 프레임 끝에. 해독을 일꾼 코어에 올림 (NULL 이면 메인이 직접) */
extern uint32_t mvsnd_job_n, mvsnd_post_n, mvsnd_rej_n, mvsnd_main_n; extern uint64_t mvsnd_job_tsc;                 /* 일꾼이 해독한 횟수 (DIAG) */
/* 8.0.66 파일 직접 재생 (WAV·FLAC): 커널이 넣어 주는 USB 파일 읽기. find = DOS 짧은 경로 → USB 경로·크기 (찾으면 1), read_at = 성공 0, now_ms 는 없어도 됨.
 * 넣으면 IDENT 5번째 바이트 bit1 = 1, 6번째 바이트 bit0 = WAV, bit1 = FLAC (bit2 = APE 는 항상 0). */
typedef struct {
    int (*find)(const char *dospath, char *host, int max, uint32_t *size);
    int (*read_at)(const char *host, uint32_t off, uint8_t *buf, uint32_t len);
    uint32_t (*now_ms)(void);
} MvFileOps;
void mvsnd_set_file(const MvFileOps *ops);
void mvsnd_set_usbfile(int (*cur)(char *path, int max, uint32_t *size));   /* 8.0.81: USB 접근에서 열어 둔 파일(경로·크기). 넣으면 IDENT 5번째 바이트 bit2 = 1 (0Ch 로 MP3 직접 재생) */
void mvsnd_set_lfn(int (*fn)(const char *path, char *out, int max));   /* 8.0.62: 긴 이름 조회 함수 (찾으면 1, 못 찾으면 0). 넣으면 IDENT 5번째 바이트 bit0 = 1 */
int mvsnd_playing(void);                     /* 재생 중(소리 아이콘·계기용) */
uint32_t mvsnd_position(void);               /* 지금 위치(프레임 번호, 16비트로 잘라 쓰기 전 값) */
void mvsnd_state_get(uint32_t *first, uint32_t *elapsed, int *play);   /* 상태 저장용 */
void mvsnd_state_set(uint32_t first, uint32_t elapsed);                /* 상태 불러오기: 그 위치에서 STOP */
/* 진단 */
uint32_t mvsnd_fifo_used(void);
uint32_t mvsnd_pcm_ready(void);
uint32_t mvsnd_frames_decoded(void);
/* 8.0.93 외부 PCM 입력: 카트리지(mmvid)가 MP4 의 AAC 를 해독해 믹서 고리에 직접 넣음. 시계는 내보낸 48 kHz 샘플 수 */
void mvsnd_ext_open(void);                   /* 소리 상태 초기화(음량은 유지) 후 외부 입력 모드 */
void mvsnd_ext_clear(void);                  /* 멈추고 고리를 비움, 시계 0 (이동할 때) */
void mvsnd_ext_close(void);
int mvsnd_ext_active(void);
uint32_t mvsnd_ext_free(void);               /* 고리에 더 넣을 수 있는 48 kHz 샘플 수 */
void mvsnd_ext_push(const int16_t *x, int n, int ch, int hz);   /* 입력 주파수 hz, 인터리브 ch 채널 n 샘플 → 48 kHz 모노 */
void mvsnd_ext_play(int on);
void mvsnd_ext_finish(void);                 /* 더 넣을 것 없음 */
int mvsnd_ext_ended(void);                   /* 다 넣었고 다 나갔음 */
uint32_t mvsnd_ext_elapsed(void);            /* 지금까지 내보낸 48 kHz 샘플 수 */
void mvsnd_ext_volume(int v);                /* 0~100 */
#endif
