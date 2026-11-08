/* 만든이: KimTaeWoo */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * mvflac.h (8.0.66) - FLAC 프레임 해독기 (직접 만듦, 외부 코드 없음). X86MV 소리 카트리지의 파일 재생(mvsnd.c)이 씀.
 * 스트림을 프레임 하나씩 풀기 위한 것: 메타데이터는 호출하는 쪽이 STREAMINFO 만 읽어 mvflac_info 에 넣고, 프레임 시작부터 buf 로 준다.
 * 지원: 1~2채널, 4~24비트(풀어 낸 값은 비트 수 그대로 int32), 블록 크기 ≤ MVFLAC_MAXBS. 프레임 CRC-16 은 검사하지 않음(머리 CRC-8 만).
 */
#ifndef MSX_MVFLAC_H
#define MSX_MVFLAC_H
#include <stdint.h>

#define MVFLAC_MAXBS 8192

typedef struct {
    uint32_t min_bs, max_bs, min_fs, max_fs;    /* STREAMINFO: 블록 크기·프레임 크기 (0 = 모름) */
    uint32_t sr; int ch, bps;
    uint64_t total;                             /* 전체 샘플 수 (채널당, 0 = 모름) */
} FlacInfo;

typedef struct { uint32_t bs, sr; int nch, assign, bps; int var; uint64_t num; uint32_t len; } FlacHdr;

/* STREAMINFO 블록 본문 34바이트 → fi. 쓸 수 있으면 0, 아니면 -1 (채널 > 2, 블록 > MVFLAC_MAXBS 등) */
int mvflac_info(FlacInfo *fi, const uint8_t *b34);
/* 프레임 머리: 길이(>0), 데이터가 모자라면 0, 잘못이면 -1 */
int mvflac_header(const FlacInfo *fi, const uint8_t *b, uint32_t n, FlacHdr *h);
/* 프레임 하나: 쓴 바이트 수(>0), 모자라면 0, 잘못이면 -1. out0/out1 = 채널별 샘플(MVFLAC_MAXBS 개 이상), *ns = 샘플 수, h 에 머리 */
int mvflac_frame(const FlacInfo *fi, const uint8_t *b, uint32_t n, int32_t *out0, int32_t *out1, FlacHdr *h);
#endif
