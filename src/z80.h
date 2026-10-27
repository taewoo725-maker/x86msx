/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * z80.h - MSX1 에뮬레이터 Z80 코어 (C 인터프리터)
 *
 * 설계서 §4.1 구현:
 *   - 문서화·비문서화 명령 전체, X/Y 플래그, MEMPTR(WZ), Q 레지스터
 *   - MSX M1 대기 상태 (m1_wait = 1이면 M1 사이클마다 1 T-state 추가)
 *   - IM 0/1/2, EI 지연, HALT, 레벨 방식 인터럽트 선
 *
 * 메모리/I/O는 콜백으로만 접근한다. 슬롯 구조는 코어에 들어오지 않는다.
 * 호스트는 리틀 엔디언(x86)을 가정한다.
 */
#ifndef MSX_Z80_H
#define MSX_Z80_H

#include <stdint.h>

typedef union {
    struct { uint8_t l, h; } b;
    uint16_t w;
} Z80Pair;

typedef struct Z80 {
    Z80Pair af, bc, de, hl, ix, iy, sp, wz;
    Z80Pair af_, bc_, de_, hl_;     /* 대체 레지스터 */
    uint16_t pc;
    uint8_t i, r;                   /* r: 증가는 하위 7비트만, 비트 7 보존 */
    uint8_t iff1, iff2, im;
    uint8_t halted;
    uint8_t halt_ei;            /* 1 = 인터럽트를 끈 채 HALT하면 켠 것으로 (실제 CPU는 영원히 멈춤. MSX1 한글 롬 훅 뒤 Nextor 드라이버가 걸림) */
    uint8_t ei_delay;               /* EI 직후 1명령 동안 인터럽트 수락 금지 */
    uint8_t q, q_prev;              /* Q: 직전 명령이 쓴 플래그 (SCF/CCF X/Y) */
    uint8_t r800;                   /* 1이면 R800 (turbo R): MULUB/MULUW 명령 */
    uint8_t int_line;               /* 레벨 방식 /INT 입력 (1 = 요청) */
    uint8_t int_vec;                /* 3.4.1: 인터럽트 응답 때 데이터 버스 값 (보통 0xFF, SFG = 0x3FF4에 쓴 값) */
    uint8_t m1_wait;                /* M1당 추가 대기 T-state (MSX = 1) */
    /* 3.4.2 ACC-01: R800 시간 (r800 = 1일 때 명령마다 R800 클럭을 r8c에 더함: 접근 1 + 페이지가 바뀌면 1 + 슬롯 대기) */
    uint32_t r8c;
    int r8_page;                    /* 마지막 접근의 위 8비트 (-1 = 강제 페이지 넘김) */
    uint8_t r8_delay[4];            /* 16 KB 페이지마다 접근 대기: 내장 RAM 0, 내장 롬 1(DRAM 모드는 0), 바깥 슬롯 2 */
    int extra;                      /* 명령 실행 중 누적되는 추가 T-state */
    uint64_t cycles;                /* 누적 T-state */

    /* 4.0.138: 메모리 빠른 길 — 8KiB 칸마다 1이면 함수 호출 없이 표(rdp/wrp)로 곧장 읽고 씀 (0이면 콜백) */
    const uint8_t *const *rdp;
    uint8_t *const *wrp;
    uint8_t frd[8], fwr[8];

    void *ctx;
    uint8_t (*mem_read)(void *ctx, uint16_t addr);
    void    (*mem_write)(void *ctx, uint16_t addr, uint8_t value);
    uint8_t (*io_read)(void *ctx, uint16_t port);
    void    (*io_write)(void *ctx, uint16_t port, uint8_t value);
} Z80;

/* 구조체를 0으로 만들고 내부 표를 준비한다. 콜백은 호출 후 설정한다. */
void z80_init(Z80 *z);

/* 전원 투입/리셋 상태 (설계서 §3.4) */
void z80_reset(Z80 *z);

/* 명령 1개(또는 인터럽트 수락, HALT 중 1사이클)를 실행하고 소비 T-state를 반환 */
int z80_step(Z80 *z);

/* 명령을 여러 개 이어 실행 — 적어도 1개를 실행하고, z->cycles 가 end 이상이거나 *next(가장 이른 예약 시각) 이상이 되면 돌아옴.
 * 명령 하나씩 부르는 것(z80_step 반복)과 똑같은 결과 (tests/corefuzz 가 견줌). 어셈블러 코어가 들어올 자리. */
int z80_run(Z80 *z, uint64_t end, const uint64_t *next);   /* 반환: 마지막 명령의 클럭 수 (도중 R800 전환이 있으면 거기서 멈춤) */

/* /INT 선 상태 설정 (1 = 요청 유지, 0 = 해제) */
static inline void z80_set_int(Z80 *z, int level) { z->int_line = (uint8_t)(level != 0); }

#endif
