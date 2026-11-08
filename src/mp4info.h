/* 만든이: KimTaeWoo */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* mp4info.h (8.0.90) - MP4/M4V/MOV 상자 구조만 읽어 길이·크기·코덱을 알아냄 (해독은 안 함). 파일은 읽기 함수로만 접근. */
#ifndef MP4INFO_H
#define MP4INFO_H
#include <stdint.h>

/* 오류 코드 (OPEN 실패 때 25h 로 1바이트로 읽힘). 0 = 오류 없음 */
enum {
    MP4E_NONE = 0,
    MP4E_READ = 1,        /* 파일을 못 읽음 / MP4 가 아님 / 깨짐 */
    MP4E_NOVIDEO = 2,     /* 영상 트랙이 없음 */
    MP4E_VCODEC = 3,      /* 지원하지 않는 영상 코덱 */
    MP4E_PROFILE = 4,     /* 코덱은 맞으나 프로파일·크기가 범위 밖 */
    MP4E_NODECODER = 5,   /* 구조는 읽었으나 해독기가 아직 없음 (8.0.90 은 항상 이 값) */
    MP4E_SCREEN = 6,      /* 이 SCREEN 값은 MP4 에 못 씀 */
    MP4E_NOMOOV = 7,      /* moov 상자를 못 찾음 (스트리밍용 조각 MP4 등) */
    MP4E_TOOBIG = 13,     /* 8.0.141 가로·세로가 한도(1920x1088)를 넘음 (조각 MP4 검사보다 먼저 판정) */
    MP4E_DECODE = 14,     /* 8.0.141 해독기가 첫 화면을 만들지 못함 (메모리 부족·깨진 스트림 등) */
    MP4E_TOOLONG = 12     /* 8.0.122 영상이 너무 길어 샘플 표를 못 잡음 (샘플 수 400만 초과 또는 메모리 부족) */
};

typedef struct {
    uint32_t vcodec;      /* 영상 샘플 항목 4글자 ('avc1' 등), 없으면 0 */
    uint32_t acodec;      /* 소리 샘플 항목 4글자 ('mp4a' 등), 없으면 0 */
    uint16_t width, height;
    uint32_t nframes;     /* 영상 샘플 수 */
    uint32_t dur_ms;      /* 영상 길이 (밀리초) */
    uint32_t fps100;      /* 평균 초당 프레임 ×100 */
    uint8_t avc_profile, avc_level;   /* avcC 가 있으면 */
    uint8_t aac_obj;      /* esds 의 오디오 객체 종류 (2 = AAC-LC), 모르면 0 */
    uint16_t channels; uint32_t rate;
    uint8_t has_audio;
    uint8_t fragmented;   /* moof 가 보임 */
    uint8_t avi;          /* 8.0.120 AVI 파일 (vcodec = MJPG) */
    uint32_t dbg_last, dbg_top, dbg_mvts; uint16_t dbg_boxes;   /* 8.0.115 진단: 마지막 최상위 상자 4글자, 최상위 상자 수, mvhd 시간 단위, 읽은 상자 수 */
    /* 8.0.91 영상 트랙의 표 위치 (상자 안쪽 = 버전·플래그 시작. 없으면 0) */
    uint32_t timescale;
    uint32_t tb_stsz, tb_stco, tb_co64, tb_stsc, tb_stts, tb_stss, tb_avcc, tb_avcc_len;
    /* 8.0.93 소리 트랙의 표 위치와 AudioSpecificConfig (파일 안 위치·길이) */
    uint32_t ta_stsz, ta_stco, ta_co64, ta_stsc, ta_asc, ta_asc_len, a_timescale;
} Mp4Info;

typedef int (*Mp4Read)(void *ctx, uint32_t off, uint8_t *buf, uint32_t len);   /* 성공 0 */

/* 성공 시 MP4E_NONE, 아니면 오류 코드 (info 는 읽은 만큼 채워 둠) */
int mp4_probe(Mp4Read rd, void *ctx, uint32_t size, Mp4Info *out);
int mp4_name_is(const char *name);   /* .mp4 .m4v .mov .avi 이면 1 */
#endif
