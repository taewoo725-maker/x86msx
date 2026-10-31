/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * vdp.h - TMS9918A / V9938 VDP (설계서 §6.4)
 *
 * 포트: 0x98 데이터, 0x99 제어·상태, 0x9A 팔레트(V9938), 0x9B 간접 레지스터(V9938).
 * 프레임 버퍼는 512 x 212. 256폭 모드는 픽셀을 두 번 쓴다.
 * 줄마다 line_direct[y]가 1이면 그 줄 값은 SCREEN 8 직접 색(GRB 3:3:2), 0이면 팔레트 번호(0~15).
 */
#ifndef MSX_VDP_H
#define MSX_VDP_H

#include <stdint.h>
#include <stddef.h>

#define VDP_PAL_SNAPS 48
#define VDP_W     512           /* 프레임 버퍼 폭 (256폭 모드는 2배) */
#define VDP_H     212           /* 최대 표시 줄 */
#define VDP_VRAM  131072

typedef struct {
    int v9938;                  /* 0 = TMS9918A, 1 = V9938 이상 */
    int v9958;                  /* 1 = V9958 (MSX2+: YJK, 가로 스크롤, R#25~27) */
    uint32_t vram_mask;
    uint8_t vram[VDP_VRAM];
    uint8_t reg[64];
    uint32_t cmd_cost, cmd_cost_off;
    /* 줄 단위 VRAM 명령(HMMV·HMMM·YMMM·LMMV·LMMM)을 시간에 따라 조금씩 실행 (openMSX처럼: 명령이 도는 동안 CPU가 쓴 VRAM이 아직 안 읽은 줄에는 반영됨) */
    int progressive, job_active, job_kind, job_done, job_tny;
    int job_p[12];
    uint64_t job_t0, job_t1;
    uint8_t il;                 /* 3.4.2 VID-06: VRAM 배열이 G6·G7 끼워 넣기 순서 */
    uint8_t hs25, hs26, hs27;   /* 줄 시작 때 잡아 둔 가로 스크롤 레지스터 (V9958, 2.8.1) */          /* 방금 실행한 명령이 실제 VDP에서 걸리는 시간 (VDP 클럭 21.48 MHz, 2.8.1) */
    int late_defer, pend_off;   /* 5.3.46: R800 에서 줄 끝(귀선) 뒤에 표시를 끄는 쓰기는 다음 줄 중간(Z80 위치)으로 */
    int line_t;                 /* 4.0.29: 줄 안 경과 클럭 (msx.c가 VDP 쓰기 전에 설정) */
    int de_pos; uint8_t de_start; /* 4.0.29: 이 줄에서 R#1 표시 켜짐/꺼짐이 바뀐 가로 위치(256폭 기준, -1 = 없음)와 줄 시작 때 상태 */
    uint8_t stat[10];           /* S#0 ~ S#9 */
    uint32_t addr;              /* 17비트 VRAM 주소 */
    uint8_t latch, latch_full;
    uint8_t read_buf;
    uint8_t pal_latch, pal_latch_full;
    int pal_dirty;              /* 팔레트가 바뀌면 1 (프런트엔드가 지움) */
    int hr;                     /* 수평 귀선 중 (msx.c가 상태 읽기 전에 설정) */
    int frame;
    /* 명령 엔진 (CPU 전송 명령의 진행 상태) */
    int cmd;                    /* 진행 중인 명령 (0 = 없음) */
    int c_x, c_y, c_nx, c_ny, c_dx, c_dy, c_left, c_x0;
    /* 표시 */
    int lines;                  /* 이번 프레임 표시 줄 (192 / 212) */
    uint8_t fb[VDP_H][VDP_W];
    uint8_t line_direct[VDP_H];
    /* 인터레이스 + 짝·홀 페이지 교대(R#9 IL·EO): 홀수 필드 줄 (line_eo[y]가 1일 때만 씀) */
    uint8_t fb_odd[VDP_H][VDP_W];
    uint8_t line_direct_odd[VDP_H];
    uint8_t line_eo[VDP_H];
    /* 줄 표시 line_direct: 0 팔레트 번호, 1 SCREEN 8 직접 색, 2 YJK, 3 YJK + YAE (원래 바이트, 출력 때 4개씩 해독) */
    uint8_t spr_ov[VDP_H][256];
    uint8_t line_bd[VDP_H];
    uint8_t spr_hide, spr_nolimit;
    /* 팔레트도 칩 안에 (예전 vdp_palette 등 공용 변수). 상태 덩이 VDP 판 2 — 판 1(이 앞까지)도 읽음 */
    uint8_t palette[16][3];     /* 팔레트 번호 → RGB (TMS9918 은 고정 팔레트, V9938 은 팔레트 레지스터 값) */
    uint8_t pal_snap[VDP_PAL_SNAPS][16][3];   /* 4.0.30: 줄마다 팔레트 (프레임 안에서 팔레트를 바꾸는 게임: Psycho World 아래 상태 칸 등) */
    uint8_t line_pal[VDP_H];    /* 줄 → pal_snap 번호 */
    uint8_t line_adj[VDP_H];    /* 4.0.31: 줄마다 R#18 (화면 위치 조정) — 줄 인터럽트로 중간에 바뀜 */
    int pal_nsnap, pal_cur, pal_changed;
    int pal_58;                 /* 1 = V9958: 5비트 늘림 곡선 */      /* 3.2.0 VID-03: 스프라이트 숨기기, 한 줄 4·8개 제한 풀기 (프런트엔드 설정) */             /* 줄마다 테두리 색 (줄별로 R#7을 바꾸는 효과: MSX2+ 로고 등) */         /* YJK 줄의 스프라이트 (0xFF = 없음, 값 = SCREEN 8 색) */
} VDP;

/* kind: 0 TMS9918A, 1 V9938, 2 V9958 */
void vdp_init(VDP *v, int kind);
void vdp_reset(VDP *v);
uint8_t vdp_read_data(VDP *v);
uint8_t vdp_read_status(VDP *v);
void vdp_write_data(VDP *v, uint8_t val);
void vdp_write_ctrl(VDP *v, uint8_t val);
void vdp_write_palette(VDP *v, uint8_t val);
void vdp_write_indirect(VDP *v, uint8_t val);
int vdp_hres(const VDP *v);                 /* 4.0.4: 가로 점 수 256 / 512 */
void vdp_line_start(VDP *v, int line);
void vdp_line_end(VDP *v, int line);  /* 줄 인터럽트 확인 */
void vdp_render_line(VDP *v, int y);
void vdp_job_run(VDP *v, int upto);   /* 진행 중인 줄 단위 명령을 upto줄까지 실행 */
void vdp_frame_end(VDP *v);             /* 표시 영역 끝: F 플래그, VR */
void vdp_frame_start(VDP *v);           /* 새 프레임: VR 해제, 표시 줄 수 결정 */
int  vdp_irq(const VDP *v);
uint8_t vdp_backdrop(const VDP *v);

/* 팔레트는 v->palette (칩 안). 기본 팔레트로 (TMS9918A 는 칩 고유 색, V9938 은 V9938 기본값) */
void vdp_default_palette(VDP *v);
/* SCREEN 8 직접 색 (GRB 3:3:2) → RGB (V9958 은 5비트 늘림 곡선) */
void vdp_g7_rgb(const VDP *v, uint8_t c, uint8_t *r, uint8_t *g, uint8_t *b);
/* 상태 덩이 VDP 판 1 (팔레트가 밖에 있던 때)의 크기 */
#define VDP_ST_V1_SIZE offsetof(VDP, palette)
/* YJK 한 줄 해독 (raw 256바이트): 점마다 RGB, 또는 팔레트 번호(pal_idx != 0xFF, YAE) */
void vdp_yjk_line(const uint8_t *raw, int yae, uint8_t rgb[256][3], uint8_t pal_idx[256]);

#endif
