/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* mpwake.h - 8.0.6: APIC(INIT-SIPI)로 다른 코어 하나를 직접 깨워 "깨어났다"만 확인하고 다시 재우는 시험 */
#ifndef X86MSX_MPWAKE_H
#define X86MSX_MPWAKE_H
#include <stdint.h>
#include "efi.h"
enum { MW_NONE = 0, MW_OK, MW_TIMEOUT, MW_NOMEM, MW_BADMEM, MW_NOTARGET, MW_IPIFAIL };
typedef struct {
    int status;            /* MW_* */
    uint32_t target;       /* 깨우려던 APIC ID */
    uint32_t woke_id;      /* 깨어난 코어가 자기 APIC ID 라고 적은 값 */
    uint32_t us;           /* 깨우기 시작 ~ 표시를 본 때까지 (대략 µs) */
    uint32_t base;         /* 시작 코드를 놓은 1 MiB 아래 주소 */
    int x2apic;
} MpWakeResult;
/* target 을 깨운다 (x2apic: ICR 을 MSR 로, 아니면 lapic_base 의 메모리로). 깬 코어는 INIT 으로 다시 재움. 읽기·시험 전용 — 코어에 일을 맡기지 않음 */
void mpwake_run(EFI_BOOT_SERVICES *bs, uint32_t target, int x2apic, uint64_t lapic_base, MpWakeResult *r);
int mpwake_text(const MpWakeResult *r, char *b, int max);

/* ---- 8.0.9 일꾼 모드: 깨운 코어가 메인 프로그램의 C 함수(entry)를 직접 돌린다 ----
 * 순서: mpworkers_open(set) → 코어마다 mpworkers_start(set, 코어, ctx) → ... → 일이 모두 끝나면(ctx->state==2) mpworkers_close(set)
 * 일꾼 함수는 펌웨어 함수·파일·USB 를 부르면 안 된다 (계산만). 메인 코어의 페이지 표·메모리 종류(PAT)를 그대로 쓴다. */
typedef struct MpWCtx {
    volatile uint32_t state;      /* 0 아직, 1 시작함, 2 함수가 끝남 */
    uint32_t apic;                /* 일꾼 코어가 적은 자기 APIC ID */
    uint64_t stack_top;           /* 16바이트 정렬된 스택 꼭대기 */
    uint64_t entry;               /* 부를 함수 (UEFI 호출 규약, 인수 1개) */
    uint64_t arg;
    uint64_t fault_cr2, fault_s0, fault_s1;   /* 일꾼 코어에서 예외가 났을 때만: state=0xDEAD, CR2, 스택 맨 위 두 값(오류 코드·실행 위치) */
} MpWCtx;
#define MPW_MAXT 64
typedef struct {
    EFI_BOOT_SERVICES *bs; uint32_t base; int open;
    int x2apic; uint64_t lapic; uint64_t cr3, pat;
    uint32_t started[MPW_MAXT]; int nstarted; int leaked;
} MpWorkerSet;
int mpworkers_open(EFI_BOOT_SERVICES *bs, MpWorkerSet *w, int x2apic, uint64_t lapic_base, uint64_t cr3, uint64_t pat);
int mpworkers_start(MpWorkerSet *w, uint32_t target, MpWCtx *ctx);   /* 0 = 시작함, -1 = 응답 없음(그 코어는 어디서 도는지 모름) */
void mpworkers_close(MpWorkerSet *w);                                  /* 모든 일꾼을 INIT 으로 재우고 쪽을 돌려줌 (응답 없던 코어가 있으면 쪽은 돌려주지 않음) */
#endif
