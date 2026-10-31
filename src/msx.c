/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * msx.c - MSX 본체 (MSX1 · MSX2 · MSX2+ · turbo R 공용, 기종 차이는 config.ini 의 부품 목록)
 *
 * [메모리 버스] 64 KiB = 16 KiB 페이지 4개 = 8 KiB 칸 8개.
 *   - 기본 슬롯 레지스터(PPI 포트 A8h, m->primary)가 페이지마다 기본 슬롯 0~3 을,
 *     확장 슬롯 레지스터(그 기본 슬롯의 FFFFh, m->subslot[ps])가 확장 슬롯 0~3 을 고른다.
 *   - update_page(): 고른 (기본, 확장) 슬롯의 칸 표 slot_rd/slot_wr[ps][ss][칸] → 지금 표 m->rd/wr[칸].
 *     8 KiB 칸 단위인 것은 ASCII8·Konami 같은 8 KiB 뱅크 매퍼 때문 (16 KiB 페이지는 칸 두 개).
 *   - 없는 곳 읽기 = 0xFF 칸(ff_block), 쓰기 금지 = 버림 칸(discard_block) — 분기 없이 포인터로 끝남.
 *   - 메모리에 레지스터가 보이는 장치(매퍼 뱅크 레지스터·SCC·플래시 명령·FDC·FFFFh)는
 *     칸마다 "특수 장치 사슬"(memdev.inc, m->mrd/mwr_*)에 등록되고, 그런 칸만 느린 길(mem_read/mem_write)로 간다.
 * [I/O 버스] 포트 256 개마다 "그 기계에 있는 장치" 사슬(io_chain, 5.2.9). 처음 "내 것"이라 답한 장치가 처리, 없으면 읽기 0xFF.
 * [시간] Z80 3.579545 MHz 기준 T-state. 한 줄 228 T (MSX_TPL), 262 줄(NTSC) / 313 줄(PAL).
 *   msx_run_frame_inner(): 줄 시작 → CPU 를 그 줄 예산만큼 → 줄 끝(수평 귀선) → VDP 가 그 줄을 그림.
 *   그 사이 예약 사건은 시간표(sched.h), 인터럽트는 인터럽트선(irqline.h)으로.
 */
#include "msx.h"
#include "device.h"

/* 소프트웨어 MIDI 음원 렌더에 쓴 시간(TSC 틱) 합계 — 프런트엔드가 프레임마다 가져가 진단 "synth" 칸에 더함 */
uint64_t msx_synth_cyc;
uint64_t msx_synth_wcyc;                         /* 8.0.32: 일꾼 코어가 MIDI 합성에 쓴 시간 (메인 시간과 따로 셈) */
#if defined(__i386__) || defined(__x86_64__)
static inline uint64_t synth_tsc(void) { uint32_t lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }
#else
static inline uint64_t synth_tsc(void) { return 0; }
#endif
/* OUT 명령의 쓰기는 명령 시작보다 약 10 T-state 뒤(M1 4+1 · 주소 읽기 3 · I/O 끝). cpu.cycles 는 명령 시작 시각이라 그만큼 더해 줄 안 위치(line_t)를 구함.
 * 이 값이 없으면 Aleste 2 가 상태 표시줄 오른쪽(LEV2 의 2)을 가려 프레임마다 글자 아랫줄이 깜박였음 (실측: 9 이상이면 24프레임 모두 온전, 7 은 일부 잘림) */
#define VDP_IO_DELAY 10
/* 5.3.45: R800은 같은 일을 줄 앞쪽에서 끝내므로 VDP 쓰기의 줄 안 위치는 Z80 시간으로 환산 (Aleste 2 상태 표시줄) */
#define VDP_LINE_T(m) ((int)(m->cpu.cycles - m->line_start) * (m->cpu.r800 ? 3 : 1) + VDP_IO_DELAY)
int msx_vdp_cmd_timing = 1;                  /* 3.2.15 VID-04: VDP 명령 시간 흉내 (0 = 명령이 바로 끝남) */
#if defined(IO_TRACE) || defined(PC_TRACE) || defined(IO_TRACE2) || defined(VDP_TRACE)
#include <stdio.h>
#include <stdlib.h>
#endif
#include "../third_party/emu2413/emu2413.h"
#include "gmsynth.h"
#include "mididev.h"
#include "midipkt.h"

static void mapper_map(MSX *m);
static void cart_map(MSX *m, int n);               /* 매퍼 부품 (mappers/table.inc) */
static void cart_reset(MSX *m, int n);
static void cart_write(MSX *m, int n, uint16_t a, uint8_t v);
static void dos2_map(MSX *m);
static void md_build(MSX *m, int b, int ps, int ss);   /* memdev.inc */
static int md_wr_owned(const MSX *m, int b);
/* YM2413: 기계마다 칩 하나를 처음 필요할 때 만들고 계속 쓴다 (칩 고유 주파수 49,716 Hz, 변환은 정수로). 5.2.40: m->opll (예전 공용 m->opll) */
/* 내장 장치 표 (msx.h MDEV_*) — 본체가 기종 깃발(m->prof.*)을 읽는 유일한 곳 (슬롯 배치 데이터는 제외).
 * MDEV_MIDI 는 기종(midi_hw)과 프런트엔드의 "내장 MIDI" 설정·외장 MSX-MIDI 카트로 정해진 값 */
static inline int dev_on(const MSX *m, int d)
{
    switch (d) {
    case MDEV_S1990:   return m->prof.s1990 != 0;          /* turbo R 시스템 제어 (R800·DRAM 모드) */
    case MDEV_TRTIMER: return m->prof.trtimer != 0;        /* turbo R 타이머 */
    case MDEV_F4:      return m->prof.f4 != 0;             /* MSX2+·turbo R F4 장치 */
    case MDEV_F4_INV:  return m->prof.f4 == 2;             /* F4 반전형 (종류 34) */
    case MDEV_TRPAUSE: return m->prof.trpause != 0;        /* turbo R 일시 정지 */
    case MDEV_PCM:     return m->prof.pcm != 0;            /* turbo R PCM */
    case MDEV_MIDI:    return m->prof.midi != 0;           /* MSX-MIDI (8251·8253, E8h~EFh) */
    case MDEV_MIDI_HW: return m->prof.midi_hw != 0;        /* 기종 파일에 MSX-MIDI 가 있음 (GT) */
    case MDEV_MATSU:   return m->prof.matsushita != 0;     /* Matsushita 교환 I/O·SRAM */
    case MDEV_OPLL:    return m->prof.opll != 0;           /* 내장 MSX-MUSIC */
    case MDEV_Y8950:   return m->prof.y8950 != 0;          /* 내장 MSX-AUDIO (Y8950) */
    case MDEV_MOON:    return m->prof.moonsound != 0;      /* 내장 MoonSound (OPL4) */
    case MDEV_CMOS_BAT: return m->prof.cmos_battery != 0;  /* RTC 배터리 백업 (꺼도 RTC 메모리가 남음) */
    case MDEV_PAL_VDP:                                     /* TMS9929A (PAL) */
        for (const char *p = m->prof.vdp; *p; p++) if (p[0] == '9' && p[1] == '9' && p[2] == '2' && p[3] == '9') return 1;
        return 0;
    default:         return 0;
    }
}
int msx_dev(const MSX *m, int d) { return dev_on(m, d); }
void msx_set_midi(MSX *m, int on) { m->prof.midi = on ? 1 : 0; }

static OPLL *opll(MSX *m)
{
    if (!m->opll) { m->opll = OPLL_new(3579545, 3579545 / 72); if (m->opll) OPLL_reset(m->opll); }
    return m->opll;
}
static void opll_update_on(MSX *m)
{
    int on = dev_on(m, MDEV_OPLL);
    for (int n = 0; n < MSX_CART_ALL; n++) if (m->cart[n].present && m->cart[n].mapper == MAPPER_FMPAC) on = 1;
    if (on && opll(m)) { OPLL_reset(m->opll); m->opll_quiet = 0; }    /* 4.0.29: 리셋마다 지움 (켜진 채 리부팅해도 소리가 남던 것) */
    m->opll_on = on && m->opll;
}
#include <string.h>

/* Y8950 (MSX-AUDIO, 2.14): 기계마다 칩 하나를 처음 켤 때 만든다. 5.2.40: m->y8950 (예전 공용 m->y8950) */
#include "../third_party/emu8950/emu8950.h"
void msx_audio_enable(MSX *m, int on)
{
    if (on && !m->y8950) m->y8950 = OPL_new(3579545, 3579545 / 72);
    if (on && m->y8950) OPL_reset(m->y8950);
    m->audio_on = on && m->y8950;
    m->y_pos = 0;
}
int msx_midi_int_vol = 100;                            /* 3.7.0: 믹서 내장 MIDI (MSX-MIDI 인터페이스로 보낸 Note On 세기 비율) */
uint32_t msx_midi_int_bytes;                            /* 3.7.0: 내장 MIDI로 보낸 바이트 수 (음원 아이콘) */

void msx_joyrex_enable(MSX *m, int on)
{
    m->joyrex = (uint8_t)(on != 0);
    psg_reset(&m->psg2, m->cpu.cycles);
    m->psg2.buf_n = 0;
}
_Static_assert(sizeof(V9990) + sizeof(uint32_t) * V9_FBH * V9_FBW + 4096 * 6 <= (2u << 20), "V9990 상태 + 화면 그림자가 2 MiB 안에 들어야 함");
void msx_v9990_enable(MSX *m, V9990 *mem)
{
    if (mem && m->v9 != mem) v9990_init(mem);
    m->v9 = mem;
    msx_io_rebuild(m);                                  /* V9990 포트 */
}
void msx_y8950_regs(const MSX *m, uint8_t out[256])
{
    memset(out, 0, 256);
    if (m->y8950) memcpy(out, m->y8950->reg, 256);
}
static void y8950_sync(MSX *m)                          /* 지금 시각까지 소리를 만들어 둠 (타이머·상태가 맞게) */
{
    if (!m->audio_on) return;
    uint64_t el = m->cpu.cycles - m->frame_start;
    uint32_t target = (uint32_t)(el / 72u);
    if (target > sizeof m->y_buf / sizeof m->y_buf[0]) target = sizeof m->y_buf / sizeof m->y_buf[0];
    while (m->y_pos < target) m->y_buf[m->y_pos++] = OPL_calc(m->y8950);
}

/* 3.1.0 MoonSound: 지금 시각까지 48 kHz 소리를 만들어 둠 (쓰기 전에 불러 시간이 맞게) */
static void ms_sync(MSX *m)
{
    if (!m->ms_on) return;
    uint64_t el = m->cpu.cycles - m->frame_start;
    uint32_t target = (uint32_t)(el * 48000u / 3579545u);
    if (target > sizeof m->ms_buf / sizeof m->ms_buf[0]) target = sizeof m->ms_buf / sizeof m->ms_buf[0];
    while (m->ms_pos < target) { int v = opl4_calc48(&m->opl4); m->ms_buf[m->ms_pos++] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
}
void msx_moonsound_enable(MSX *m, int on)
{
    m->ms_on = on && opl4_has_memory();
    opl4_reset(&m->opl4);
    m->ms_pos = 0;
}

int msx_fw_turbor;                                   /* 3.4.0: turbo R 내장 소프트웨어(MSX-View·메뉴) 롬을 붙임 */
static uint8_t ff_block[8192];
static uint8_t discard_block[8192];

/* ------------------------------------------------------------------ */
/* 슬롯                                                                 */
/* ------------------------------------------------------------------ */
/* 3.4.2 ACC-01·02: R800 접근 대기 (openMSX): 바깥 슬롯(1·2) 2, 내장 RAM(3-0) 0, 내장 롬 1.
 * DRAM 모드(S1990 레지스터 6 비트 6 = 0)면 BIOS·BASIC(0-0)·서브롬·한자 드라이버(3-1)는 RAM에서 돌아 0 */
static void r800_delay(MSX *m, int page)
{
    int ps = (m->primary >> (page * 2)) & 3, ss = m->prof.subslotted[ps] ? (m->subslot[ps] >> (page * 2)) & 3 : 0;
    int d = (ps == 1 || ps == 2) ? 2 : (ps == 3 && ss == 0) ? 0 : 1;
    if (!(m->s1990_cpu & 0x40) && page <= 1 && ((ps == 0 && ss == 0) || (ps == 3 && ss == 1))) d = 0;
    if (!dev_on(m, MDEV_S1990)) d = 0;
    m->cpu.r8_delay[page] = (uint8_t)d;
}

static void update_page(MSX *m, int page)
{
    if (m->sh_blk && (m->sh_blk - 1) / 2 == page) {         /* 4.0.34: 스택 그림자가 걸린 칸을 따로 바꾸면 먼저 되돌림 */
        memcpy(m->sh_orig + m->sh_lo, m->sh_buf + m->sh_lo, (size_t)(m->sh_hi - m->sh_lo));
        m->sh_blk = 0;
    }
    int ps = (m->primary >> (page * 2)) & 3;
    int ss = 0;
    if (m->prof.subslotted[ps])
        ss = (m->subslot[ps] >> (page * 2)) & 3;
    for (int b = page * 2; b < page * 2 + 2; b++) {
        m->rd[b] = m->slot_rd[ps][ss][b];
        m->wr[b] = m->slot_wr[ps][ss][b];
        md_build(m, b, ps, ss);                         /* 특수 메모리 장치 사슬 (memdev.inc) */
    }
    r800_delay(m, page);                            /* 3.4.2 ACC-01·02 */
#ifdef PC_TRACE
    { static int w = -1; if (w < 0) w = getenv("WATCHPG") != 0; if (w && page >= 1 && page <= 2 && ps == 1) fprintf(stderr, "PG%d slot 1-%d pc %04X f %llu\n", page, ss, m->cpu.pc, (unsigned long long)m->frame); }
#endif
}

/* 4.0.138: CPU 메모리 빠른 길 표 갱신 — 특수 장치·매퍼 레지스터가 없는 칸만 곧장 접근 */
static void fast_refresh(MSX *m)
{
    m->cpu.rdp = m->rd;
    m->cpu.wrp = m->wr;
    for (int b = 0; b < 8; b++) {
        m->cpu.frd[b] = (uint8_t)(!m->rd_sp[b] && !m->vhd_hide_fdc);
        m->cpu.fwr[b] = (uint8_t)!m->mwr_n[b];
    }
}

static void update_all_pages(MSX *m)
{
    /* 4.0.34: 스택이 있는 8 KiB 칸이 슬롯 바꿈으로 롬이 되면(예: 한글 롬 CALSLT, 스택이 0x8000대인 BASIC) 스택이 사라져 되돌아갈 곳을 잃는다.
     * 그 칸을 롬 내용의 복사본으로 바꾸고 SP 둘레 256바이트만 원래 RAM 내용으로 덮어 스택을 살려 둔다. 다시 바뀔 때 그 둘레를 RAM으로 되돌린다. */
    uint8_t *ow[8];
    for (int b = 0; b < 8; b++) ow[b] = m->wr[b];
    if (m->sh_blk) {
        int sb = m->sh_blk - 1;
        memcpy(m->sh_orig + m->sh_lo, m->sh_buf + m->sh_lo, (size_t)(m->sh_hi - m->sh_lo));
        ow[sb] = m->sh_orig;
        m->sh_blk = 0;
    }
    for (int p = 0; p < 4; p++) update_page(m, p);
    int sb = m->cpu.sp.w >> 13;
    if (m->wr[sb] == discard_block && ow[sb] && ow[sb] != discard_block && ow[sb] != m->sh_buf && !m->rd_sp[sb] && !md_wr_owned(m, sb)) {
        unsigned off = m->cpu.sp.w & 0x1FFF;
        m->sh_lo = (uint16_t)(off > 128 ? off - 128 : 0);
        m->sh_hi = (uint16_t)(off + 128 < 8192 ? off + 128 : 8192);
        memcpy(m->sh_buf, m->rd[sb], 8192);
        memcpy(m->sh_buf + m->sh_lo, ow[sb] + m->sh_lo, (size_t)(m->sh_hi - m->sh_lo));
        m->sh_orig = ow[sb];
        m->sh_blk = (uint8_t)(sb + 1);
        m->rd[sb] = m->sh_buf;
        m->wr[sb] = m->sh_buf;
    }
    fast_refresh(m);
}

/* ------------------------------------------------------------------ */
/* 카트리지                                                             */
/* ------------------------------------------------------------------ */
/* SRAM 고르는 비트: ASCII8 = 8 KiB 뱅크 수, ASCII16 = 16 KiB 뱅크 수 (blueMSX와 같음) */
static uint32_t cart_sram_bit(const Cartridge *c)
{
    uint32_t n = c->mapper == MAPPER_ASCII16_SRAM ? (c->size + 16383) / 16384 : c->banks8, b = 1;
    while (b < n) b <<= 1;
    return b;
}

/* 3.8.0: ASCII8 SRAM이 32 KiB면 뱅크 값의 아래 두 비트가 8 KiB 칸을 고름 (MSX-Write II 등). 2·8 KiB는 한 칸 */
static uint32_t sram_off8(const Cartridge *c, uint32_t bank) { return c->sram_kb == 32 ? (bank & 3u) * 8192u : 0; }

static const uint8_t *cart_block(const Cartridge *c, uint32_t bank8)
{
    return c->data + (bank8 % c->banks8) * 8192;
}


/* 카트리지 슬롯의 블록 표를 현재 뱅크 상태로 다시 만든다 */

/* 5.2.3 구조 개편 3단계(나): 카트리지 창 맞추기(슬롯 표에 뱅크 반영)를 매퍼마다 함수 하나로 (예전 cart_map if 사슬 120줄).
 * 표에 없는 매퍼(Konami·ASCII8·ASCII16·SCC 등 8 KiB 뱅크 4개)는 cm_default. 새 매퍼 = 함수 하나 + cart_map_fn 표 한 줄 */
typedef void (*CartMapFn)(MSX *m, Cartridge *c, int s, int q);
/* 5.2.4 구조 개편 3단계(다): 카트리지 리셋도 매퍼마다 (예전 switch). 공통: SCC 끔, 네트워크 카트 RAM 사본 → 매퍼 리셋 → 창 맞추기 */
typedef void (*CartResetFn)(Cartridge *c);
/* ==================================================================
 * 5.2.2 구조 개편 3단계(가): 카트리지 매퍼 쓰기를 매퍼마다 함수 하나로 (예전 cart_write switch 283줄).
 * 돌려주는 값 1 = 뱅크가 바뀜 (창을 다시 맞춤: cart_map + update_all_pages), 0 = 그대로.
 * 새 매퍼 = 함수 하나 + 아래 cart_wr_fn 표 한 줄. 다른 매퍼 코드는 안 고침.
 * ================================================================== */
typedef int (*CartWrFn)(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v);

/* SCC 레지스터를 바꾸기 전에 지금 시각까지의 소리를 만들어 둔다 (2.11) */
static void scc_catch_up(MSX *m, Cartridge *c)
{
    psg_sync(&m->psg, m->cpu.cycles);
    scc_render(&c->scc, m->psg.buf_n);
}

/* ---- 3.4.1 SFG-01·05: YM2151 + YM2148(MIDI 보내기·받기 5.2.56) + 키보드(YK-01, 없음) ---- */
const NetHost *msx_net_host;                          /* 5.0.57 */
static void sync_int(MSX *m);
static void sfgm_write(MSX *m, int r, uint8_t v);   /* 5.2.56 YM2148 (아래 UART 쪽) */
static uint8_t sfgm_read(MSX *m, int r);
static void sfg_write(MSX *m, int r, uint8_t v)
{
    switch (r) {
    case 0: case 1:
        opm_advance(&m->opm, (uint32_t)(m->cpu.cycles - m->opm_t)); m->opm_t = m->cpu.cycles;
        opm_write(&m->opm, r, v);
        sync_int(m);
        break;
    case 2: m->sfg_kbd = v; break;                      /* 키보드 행 */
    case 4: m->sfg_vec = v; sync_int(m); break;         /* YM2151 인터럽트 벡터 (IM 2) */
    case 3: case 5: case 6: sfgm_write(m, r, v); break;  /* YM2148 MIDI UART (벡터·보내기·명령) */
    default: break;
    }
}

static uint8_t sfg_read(MSX *m, int r)
{
    switch (r) {
    case 0: case 1:
        opm_advance(&m->opm, (uint32_t)(m->cpu.cycles - m->opm_t)); m->opm_t = m->cpu.cycles;
        return opm_status(&m->opm);
    case 2: return 0xFF;                                /* 건반 없음 */
    case 5: case 6: return sfgm_read(m, r);             /* YM2148 받은 자료·상태 */
    default: return 0xFF;
    }
}

/* 5.2.7 구조 개편 4단계: 플래시 칩은 공용 부품 (src/flash.c, 칩 표). 매퍼 → 칩 종류 표, 칩 안 주소로 쓰기 */
static const uint8_t cart_flash_type[MAPPER_COUNT] = {
    [MAPPER_MFRSCC] = FLASH_AM29F040, [MAPPER_MATRAINK] = FLASH_AM29F040,      /* 3.1.1·3.9.14 */
    [MAPPER_YAMANOOTO] = FLASH_S29GL064N, [MAPPER_ASCII16X] = FLASH_S29GL064S, /* 4.0.129·4.0.131 */
    [MAPPER_MFRSCCP] = FLASH_M29W800DB,                                           /* 5.2.46 */
    [MAPPER_FRSCC] = FLASH_M29W128,                                               /* 5.2.47 */
};
static void dac_push(MSX *m, uint8_t v);                       /* 카트리지 DAC (아래 PCM 쪽) */
static int cart_flash(Cartridge *c, uint32_t fa, uint8_t v)    /* 1 = 읽기 모드(자동 선택·CFI)가 바뀜 → 창 다시 맞춤 */
{
    c->flash.type = cart_flash_type[c->mapper];
    FlashMem fm = { (uint8_t *)(uintptr_t)c->data, c->size, &c->sram_dirty, c->fl_dirty, sizeof c->fl_dirty };
    return flash_write(&c->flash, &fm, fa, v);
}
static const uint8_t *cart_flash_id(const Cartridge *c) { return flash_id_page(&c->flash); }   /* 자동 선택·CFI 중 창 */

uint32_t msx_cart_alloc_size(MapperType mt, uint32_t file_size)   /* 롬 버퍼 크기 = 플래시 칩 크기 (모자란 곳은 FFh = 지운 상태) */
{
    uint32_t chip = (unsigned)mt < MAPPER_COUNT ? flash_chip_size((FlashType)cart_flash_type[mt]) : 0;
    return chip > file_size ? chip : file_size;
}

/* 4.0.129: Yamanooto 보조 PSG·ECHO 쓰기가 있는지 */
static int yam_on(const MSX *m)
{
    for (int n = 0; n < MSX_CART_ALL; n++) if (m->cart[n].present && m->cart[n].mapper == MAPPER_YAMANOOTO) return n + 1;
    return 0;
}
/* 포트 10h~12h 의 두 번째 PSG 가 있는가 — Joyrex 카트·Yamanooto 보조 PSG·MFR SCC+ 내장 PSG */
static int psg2_on(const MSX *m)
{
    if (m->joyrex || yam_on(m)) return 1;
    for (int n = 0; n < MSX_CART_ALL; n++) if (m->cart[n].present && m->cart[n].mapper == MAPPER_MFRSCCP) return 1;
    return 0;
}

static void spc_mem_write(MSX *m, uint16_t a, uint8_t v)
{
    if ((a & 0x1FFF) < 0x1000) spc_write_dreg(&m->spc, &m->ide, v);
    else spc_write_reg(&m->spc, &m->ide, a & 0x0F, v);
}

/* 5.2.4 구조 개편 3단계: 카트리지 매퍼 부품 (src/mappers/ 의 .inc) — 매퍼마다 쓰기·창·리셋 함수, 표·진입점은 table.inc */
#include "mappers/common.inc"
#include "mappers/konami.inc"
#include "mappers/ascii.inc"
#include "mappers/flash.inc"
#include "mappers/ese.inc"
#include "mappers/fmpac.inc"
#include "mappers/storage.inc"
#include "mappers/ram.inc"
#include "mappers/sound.inc"
#include "mappers/misc.inc"
#include "mappers/table.inc"

int msx_set_expand(MSX *m, int which)
{
    if (which < 0 || which > 2) return -1;
    int a = m->prof.ext_slot[0], b = m->prof.ext_slot[1];
    if (m->expand) m->prof.subslotted[m->cart_slot[m->expand - 1]] = 0;      /* 원래대로 */
    m->expand = 0;
    m->cart_slot[0] = a; m->cart_slot[1] = b;
    for (int n = 0; n < MSX_CART_SLOTS; n++) m->cart_ss[n] = 0;
    for (int n = 2; n < MSX_CART_SLOTS; n++) {              /* 3.3.1: 기종 설정의 카트리지 C·D (3.6.7: E) */
        int es = m->prof.ext_slot[n];
        m->cart_slot[n] = es >= 0 && es <= 3 ? es : -1;
        if (es >= 0 && es <= 3 && m->prof.subslotted[es]) m->cart_ss[n] = m->prof.ext_ss[n] & 3;
    }
    if (!which) { update_all_pages(m); return 0; }
    int ps = which == 1 ? a : b;
    if (ps < 0 || ps > 3 || m->prof.subslotted[ps]) { update_all_pages(m); return -1; }   /* 이미 나뉜 슬롯은 다시 못 나눔 */
    m->expand = which;
    m->prof.subslotted[ps] = 1;
    /* 확장기의 4칸 (2.7.7): 확장기를 꽂은 슬롯의 서브슬롯 0~3 = SLOT1~4, 3.6.7: 다른 카트리지 슬롯 = SLOT5 */
    for (int n = 0; n < 4; n++) { m->cart_slot[n] = ps; m->cart_ss[n] = n; }
    {
        int o = which == 1 ? b : a, ox = which == 1 ? 1 : 0;
        m->cart_slot[4] = o >= 0 && o <= 3 && o != ps ? o : -1;
        m->cart_ss[4] = m->cart_slot[4] >= 0 && m->prof.subslotted[o] ? m->prof.ext_ss[ox] & 3 : 0;
    }
    for (int n = 0; n < MSX_CART_SLOTS; n++) cart_map(m, n);
    update_all_pages(m);
    return 0;
}

int msx_insert_mapper_ram(MSX *m, int n, int kb)
{
    if (n < 0 || n >= MSX_CART_SLOTS || m->cart_slot[n] < 0) return -1;
    uint32_t sz = (uint32_t)kb * 1024;
    if (sz > MSX_MRAM_MAX) sz = MSX_MRAM_MAX;
    if (m->mram_size) {                                 /* 2.12: 기종 내장 매퍼가 있으면 외장 버퍼로 두 번째 매퍼 */
        if (!m->xram || m->xram_size) return -1;
        m->xram_size = sz;
        m->xram_ps = m->cart_slot[n];
        m->xram_ss = m->cart_ss[n];
        memset(&m->cart[n], 0, sizeof m->cart[n]);
        m->cart[n].present = 1;
        m->cart[n].mapper = MAPPER_PLAIN;
        memset(m->xram, 0, sz);
        mapper_map(m);
        update_all_pages(m);
        return 0;
    }
    m->mram_size = sz;
    m->mapper_ps = m->cart_slot[n];
    m->mapper_ss = m->cart_ss[n];
    memset(&m->cart[n], 0, sizeof m->cart[n]);
    m->cart[n].present = 1;                 /* 데이터 없는 일반 = RAM 카트리지 표시 */
    m->cart[n].mapper = MAPPER_PLAIN;
    memset(m->mram, 0, m->mram_size);
    mapper_map(m);
    update_all_pages(m);
    return 0;
}

int msx_insert_cart(MSX *m, int n, const uint8_t *data, uint32_t size, MapperType mapper)
{
    if (n < 0 || n >= MSX_CART_ALL || m->cart_slot[n] < 0) return MSX_ERR_ROM_SIZE;   /* 3.6.8: 내장 자리도 */
    if (!data || !size || (size > MSX_ROM_MAX && !(mapper == MAPPER_MEGARAM && size <= 2u * MSX_ROM_MAX) && !((mapper == MAPPER_ASCII16X || mapper == MAPPER_YAMANOOTO || mapper == MAPPER_NEO8 || mapper == MAPPER_NEO16 || mapper == MAPPER_FRSCC) && size <= MSX_ROM_BIG)) || mapper >= MAPPER_COUNT) return MSX_ERR_ROM_SIZE;
    Cartridge *c = &m->cart[n];
    memset(c, 0, sizeof *c);
    c->data = data;
    c->size = size;
    c->present = 1;
    c->mapper = mapper;
    c->banks8 = (size + 8191) / 8192;
    c->plain_base = mapper == MAPPER_SFG ? 0 : cart_plain_base(data, size);   /* 3.4.1: SFG는 0x0000부터 (blueMSX와 같음) */
    msx_io_rebuild(m);                                  /* 카트에 딸린 I/O 장치 (MegaRAM·Zemina90·IDE 카트) */
    cart_reset(m, n);
    update_all_pages(m);
    return 0;
}

int msx_insert_disk_interface(MSX *m, int n, const uint8_t *rom, uint32_t size, int kind)
{
    int e = msx_insert_cart(m, n, rom, size, MAPPER_PLAIN);
    if (e) return e;
    m->fdc_kind = kind;
    m->fdc_ps = m->cart_slot[n];
    m->fdc_ss = m->cart_ss[n];                          /* 확장기 안 서브슬롯이면 그 칸 */
    msx_io_rebuild(m);                                  /* Microsol 등 I/O 포트 FDC */
    update_all_pages(m);
    return 0;
}

void msx_cart_set_sram(MSX *m, int n, int kb)             /* 3.8.0: ASCII8 SRAM 크기 (롬 DB: 2·8·32 KB) */
{
    if (n < 0 || n >= MSX_CART_ALL || !m->cart[n].present || m->cart[n].mapper != MAPPER_ASCII8_SRAM) return;
    m->cart[n].sram_kb = (uint8_t)(kb == 2 || kb == 32 ? kb : 8);
    cart_map(m, n);
    update_all_pages(m);
}

void msx_cart_set_base(MSX *m, int n, int block)
{
    if (n < 0 || n >= MSX_CART_ALL || !m->cart[n].present || m->cart[n].mapper != MAPPER_PLAIN || !m->cart[n].data) return;
    if (block != 0 && block != 2 && block != 4) return;
    m->cart[n].plain_base = block;
    cart_map(m, n);
    update_all_pages(m);
}

void msx_eject_cart(MSX *m, int n)
{
    msx_snd_epoch++;                                    /* 5.2.11 */
    if (n >= 0 && n < MSX_CART_SLOTS && m->cart[n].present && !m->cart[n].data && m->xram_size && m->xram_ps == m->cart_slot[n] &&
        (!m->prof.subslotted[m->xram_ps] || m->xram_ss == m->cart_ss[n])) m->xram_size = 0;   /* 외장 매퍼 RAM 카트리지 */
    else if (n >= 0 && n < MSX_CART_SLOTS && m->cart[n].present && !m->cart[n].data && m->mapper_ps == m->cart_slot[n] &&
        (!m->prof.subslotted[m->mapper_ps] || m->mapper_ss == m->cart_ss[n])) {
        int builtin = 0;                    /* 메모리 매퍼 RAM 카트리지를 뺌 (기종 내장 매퍼는 그대로) */
        for (int i = 0; i < m->prof.region_count; i++) if (m->prof.region[i].type == REGION_RAM_MAPPER) builtin = 1;
        if (!builtin) m->mram_size = 0;
    }
    if (n >= 0 && n < MSX_CART_SLOTS && m->fdc_kind && m->fdc_ps == m->cart_slot[n] && m->cart[n].present &&
        (!m->prof.subslotted[m->fdc_ps] || m->fdc_ss == m->cart_ss[n])) {
        int builtin = 0;                                /* 기종 내장 디스크 ROM이면 그대로 */
        for (int i = 0; i < m->prof.region_count; i++) if (m->prof.region[i].type == REGION_DISK_ROM) builtin = 1;
        if (!builtin) m->fdc_kind = 0;
    }
    msx_io_rebuild(m);                                  /* 5.2.9 */
    if (n < 0 || n >= MSX_CART_SLOTS) return;
    memset(&m->cart[n], 0, sizeof m->cart[n]);
    cart_map(m, n);
    update_all_pages(m);
}

/* FDC 레지스터 주소면 레지스터 번호(0~3 FDC, 4 제어/상태), 아니면 -1 */
static int fdc_reg(const MSX *m, uint16_t a)
{
    uint16_t o = a & 0x3FFF;
    if (m->fdc_kind == 64 || m->fdc_kind == 67) {       /* 대우·내셔널: 0x3FB8~0x3FBF (비트 3~5 무시: 0x3FC7), 3.4.0: SVI-738도 같음 */
        if ((o & 0x3FC7) >= 0x3F80 && (o & 0x3FC7) <= 0x3F87 && (o & 0x38) == 0x38) return (o & 4) ? 4 : (o & 3);
        return -1;
    }
    if (m->fdc_kind == 57 || m->fdc_kind == 143) {       /* TC8566AF: turbo R(143) 0x7FF0~7, 파나소닉 MSX2·2+(57) 0x7FF8~F → 16 + n */
        if (m->fdc_kind == 143) return (o >= 0x3FF0 && o <= 0x3FF7) ? 16 + (o - 0x3FF0) : -1;
        return (o >= 0x3FF8 && o <= 0x3FFB) ? 16 + 2 + (o - 0x3FF8) : -1;   /* 57: DOR 0x7FF8, CCR 9, MSR A, 데이터 B */
    }
    if (m->fdc_kind == 65) {                            /* 필립스·소니: 0x3FF8~0x3FFF */
        if (o >= 0x3FF8) return o - 0x3FF8 < 4 ? (o - 0x3FF8) : 4 + (o - 0x3FFC);
        return -1;
    }
    return -1;
}

static uint8_t fdc_mem_read(MSX *m, int r, uint8_t rom)
{
    if (r >= 16) {                                      /* TC8566AF */
        int n = r - 16;
        m->fdc.now = m->cpu.cycles; fdc765_poll(&m->fdc765, m->cpu.cycles);
        if (n == 4) return fdc765_msr(&m->fdc765);
        if (n == 5) return fdc765_read_data(&m->fdc765, &m->fdc);
        return rom;
    }
    FDC *f = &m->fdc;
    if (r < 4) return fdc_read(f, r, m->cpu.cycles);
    fdc_poll(f, m->cpu.cycles);                         /* 3.3.1: 시간 흉내 */
    if (m->fdc_kind == 64 || m->fdc_kind == 67) {       /* 비트 7 = IRQ, 비트 6 = DRQ가 0일 때 1 */
        uint8_t v = 0x7F;
        if (f->irq) v |= 0x80;
        if (f->drq) v &= (uint8_t)~0x40;
        return v;
    }
    switch (r) {                                        /* 필립스: 4 측면, 5 드라이브, 7 상태 */
    case 4: return (uint8_t)(f->side | 0xFE);
    case 5: return (uint8_t)((f->drive & 3) | (f->motor ? 0x80 : 0) | 0x7C);
    case 7: { uint8_t v = 0xC0; if (f->irq) v &= (uint8_t)~0x40; if (f->drq) v &= (uint8_t)~0x80; return v; }
    default: return rom;
    }
}

#if defined(FDC_TRACE) || defined(VDP_TRACE)
#include <stdio.h>
#endif
static void fdc_mem_write(MSX *m, int r, uint8_t v)
{
    if (r >= 16) {                                      /* TC8566AF */
        int n = r - 16;
        m->fdc.now = m->cpu.cycles; fdc765_poll(&m->fdc765, m->cpu.cycles);
        if (n == 0 && m->dos2_tr) { m->dos2_bank = v & 3; dos2_map(m); update_all_pages(m); }   /* turbo R: ROM 뱅크 */
        else if (n == 2) {                              /* DOR: 비트 0~1 드라이브, 비트 4~5 모터 */
            m->fdc.drive = v & 1;
            m->fdc.motor = (v >> (4 + (v & 1))) & 1;
            if (!(v & 0x04)) fdc765_reset(&m->fdc765);  /* 리셋 (비트 2 = 0) */
        }
        else if (n == 5) fdc765_write_data(&m->fdc765, &m->fdc, v);
        return;
    }
    FDC *f = &m->fdc;
#ifdef FDC_TRACE
    if (r != 3) fprintf(stderr, "fdc kind %d w%d %02X (pc %04X)\n", m->fdc_kind, r, v, m->cpu.pc);
#endif
    if (r < 4) { fdc_write(f, r, v, m->cpu.cycles); return; }
    if (m->fdc_kind == 64 || m->fdc_kind == 67) {       /* 비트 0 = 드라이브 A, 비트 1 = 드라이브 B, 비트 2 측면, 비트 3 모터 */
        f->drive = (v & 3) == 1 ? 0 : (v & 3) == 2 ? 1 : 2;
        f->side = (v >> 2) & 1;
        f->motor = (v >> 3) & 1;
        return;
    }
    if (r == 4) f->side = v & 1;
    else if (r == 5) { f->drive = v & 1; f->motor = (v >> 7) & 1; }
}

static void dos2_map(MSX *m);
static void msx_run_frame_inner(MSX *m);
static int fw_read(MSX *m, uint16_t a);
static void fw_write(MSX *m, uint16_t a, uint8_t v);
static int bun_read(MSX *m, uint16_t a);
static void bun_write(MSX *m, uint16_t a, uint8_t v);
#include "memdev.inc"                               /* 5.2.5 */
/* 메모리 읽기 느린 길. CPU(z80.c rd)는 먼저 빠른 길(칸에 특수 장치가 없으면 m->rd[칸][주소 & 1FFF] 를 곧장)을 쓰고
 * 그 칸에 특수 장치가 있을 때만 여기로 온다:
 *   CPU rd(a) ─ 빠른 칸? ─ 예 → rd[a>>13][a&1FFF]
 *                        └ 아니오 → mem_read: ① 특수 장치 사슬(처음 답한 장치) ② FFFFh = 확장 슬롯 레지스터(반전) ③ 칸 표 */
static uint8_t mem_read(void *ctx, uint16_t a)
{
    MSX *m = ctx;
    if (!m->rd_sp[a >> 13] && a < 0xFFFF && !m->vhd_hide_fdc) return m->rd[a >> 13][a & 0x1FFF];   /* 4.0.14: 빠른 길 */
    if (m->vhd_hide_fdc && (a == 0x4000 || a == 0x4001) && (m->fdc_kind || m->dos2)) {   /* 3.7.1: 디스크 롬을 없는 것처럼 (드라이브 문자 A·B를 가상 디스크가) */
        int ps = (m->primary >> 2) & 3;
        if (ps == m->fdc_ps && (!m->prof.subslotted[ps] || ((m->subslot[ps] >> 2) & 3) == m->fdc_ss)) return 0xFF;
        if (m->dos2 && ps == m->dos2_ps && (!m->prof.subslotted[ps] || ((m->subslot[ps] >> 2) & 3) == m->dos2_ss)) return 0xFF;
    }
    {                                                   /* 칸의 특수 장치 사슬 (memdev.inc) — 처음 "내 것"이 답함 */
        int bk = a >> 13, n = m->mrd_n[bk];
        for (int k = 0; k < n; k++) { int v = mem_dev[m->mrd_dev[bk][k]].rd(m, m->mrd_arg[bk][k], a); if (v >= 0) return (uint8_t)v; }
    }
    if (a == 0xFFFF) {
        int ps = m->primary >> 6;
#ifdef PC_TRACE
        { static int w = -1; if (w < 0) w = getenv("WATCHR") != 0; if (w) fprintf(stderr, "RD FFFF ps %d sub %d val %02X pc %04X a8 %02X f %llu\n", ps, m->prof.subslotted[ps], m->prof.subslotted[ps] ? (uint8_t)~m->subslot[ps] : m->rd[7][0x1FFF], m->cpu.pc, m->primary, (unsigned long long)m->frame); }
#endif
        if (m->prof.subslotted[ps]) return (uint8_t)~m->subslot[ps];
    }
    return m->rd[a >> 13][a & 0x1FFF];
}

/* 메모리 쓰기: ① FFFFh = 확장 슬롯 레지스터 (그 기본 슬롯이 확장돼 있으면) → 페이지 표 다시 만듦
 *            ② 칸의 특수 장치 사슬 (매퍼 뱅크 바꾸기·SCC·플래시 명령·FDC …), 처음 받은 장치에서 끝
 *            ③ 칸 표 m->wr (RAM, 또는 롬이면 버림 칸) */
static void mem_write(void *ctx, uint16_t a, uint8_t v)
{
    MSX *m = ctx;
#ifdef PC_TRACE
    { static long wa = -2; if (wa == -2) { const char *e = getenv("WATCH"); wa = e ? strtol(e, 0, 16) : -1; }
      if (a == wa) fprintf(stderr, "WATCH %04X=%02X pc %04X a8 %02X f %llu\n", a, v, m->cpu.pc, m->primary, (unsigned long long)m->frame); }
#endif
    if (a == 0xFFFF) {
        int ps = m->primary >> 6;
        if (m->prof.subslotted[ps]) {
            m->subslot[ps] = v;
            update_all_pages(m);
            return;
        }
    }
    {                                                   /* 칸의 특수 장치 사슬 (memdev.inc) — FDC·DOS2·펌웨어·Bunsetsu·Lode Runner·매퍼 레지스터 */
        int bk = a >> 13, n = m->mwr_n[bk];
        for (int k = 0; k < n; k++) if (mem_dev[m->mwr_dev[bk][k]].wr(m, m->mwr_arg[bk][k], a, v)) return;
    }
    m->wr[a >> 13][a & 0x1FFF] = v;
}

/* ------------------------------------------------------------------ */
/* I/O                                                                  */
/* ------------------------------------------------------------------ */
/* ---- 3.4.4 DEV-35: MSX-MIDI 받기와 8253 타이머 ----
 * 8253 시계 4 MHz (카운터 0 = 8251 보내기·받기 시계, 1 = 쓰지 않음, 2 = 타이머 IRQ). 시간은 CPU 사이클(3.58 MHz)로 재고 읽을 때 4 MHz로 바꿈.
 * 카운터 2 출력이 올라가는 순간 타이머 깃발을 세움 (모드 2·3은 N 클록마다, 모드 0·4는 한 번) */
#define PIT_HZ 4000000u
static uint64_t pit_ticks(const MSX *m, uint64_t since) { uint64_t d = m->cpu.cycles > since ? m->cpu.cycles - since : 0; return d * PIT_HZ / 3579545u; }
static uint64_t pit_cyc(uint64_t ticks) { return (ticks * 3579545u + PIT_HZ - 1) / PIT_HZ; }
static uint32_t pit_n(const PitCounter *p) { return p->reload ? p->reload : 65536u; }

static void pit_arm(const MSX *m, PitCounter *p)        /* 값을 다 썼을 때: 세기 시작, 다음 올라감 시각 (5.2.48: 카운터 포인터 — MIDI·RS-232C 공용) */
{
    uint32_t n = pit_n(p);
    p->start = m->cpu.cycles;
    p->armed = 1;
    switch (p->mode) {
    case 2: case 3: p->edge = m->cpu.cycles + pit_cyc(n); break;
    case 0: case 4: p->edge = m->cpu.cycles + pit_cyc(n + 1); break;
    default: p->edge = 0; break;                        /* 모드 1·5는 게이트 신호가 없어 시작하지 않음 */
    }
}

static uint16_t pit_count(const MSX *m, const PitCounter *p)   /* 지금 카운터 값 */
{
    if (!p->armed) return p->reload;
    uint32_t n = pit_n(p);
    uint64_t t = pit_ticks(m, p->start);
    switch (p->mode) {
    case 2: return (uint16_t)(n - (uint32_t)(t % n));
    case 3: { uint32_t h = n / 2 ? n / 2 : 1, k = (uint32_t)(t % n); return (uint16_t)((n - 2 * (k % h)) & 0xFFFE); }   /* 반 주기마다 N에서 2씩 */
    default: return (uint16_t)(n - (uint32_t)(t > n ? n : t));
    }
}

static int pit_edge(const MSX *m, PitCounter *p)        /* 출력이 올라갔으면 1 (다음 올라감 시각으로 넘김) */
{
    if (!(p->armed && p->edge && m->cpu.cycles >= p->edge)) return 0;
    if (p->mode == 2 || p->mode == 3) {
        uint64_t per = pit_cyc(pit_n(p));
        if (per < 16) per = 16;
        while (p->edge <= m->cpu.cycles) p->edge += per;
    } else p->edge = 0;
    return 1;
}

static void midi_tick(MSX *m)                           /* 줄마다·포트 접근 때: 타이머 올라감, 받은 바이트 */
{
    if (pit_edge(m, &m->pit[2])) m->midi_tflag = 1;
    if (!m->midi_rxrdy && m->midi_rx_r != m->midi_rx_w && (m->midi_cmd & 4) && m->cpu.cycles >= m->midi_rx_next) {
        m->midi_rx_data = m->midi_rx_q[m->midi_rx_r];
        m->midi_rx_r = (uint16_t)((m->midi_rx_r + 1) % sizeof m->midi_rx_q);
        m->midi_rxrdy = 1;
        m->midi_rx_next = m->cpu.cycles + 1145;         /* 10비트 ÷ 31250 baud = 320 µs */
    }
}

static int midi_irq(const MSX *m)
{
    return dev_on(m, MDEV_MIDI) && (((m->midi_cmd & 0x02) && m->midi_tflag) || ((m->midi_cmd & 0x20) && m->midi_rxrdy));
}

int msx_midi_in(MSX *m, const uint8_t *b, int n)
{
    int k = 0;
    if (!dev_on(m, MDEV_MIDI)) return 0;
    while (k < n) {
        uint16_t w = (uint16_t)((m->midi_rx_w + 1) % sizeof m->midi_rx_q);
        if (w == m->midi_rx_r) break;                   /* 가득 참 */
        m->midi_rx_q[m->midi_rx_w] = b[k++];
        m->midi_rx_w = w;
    }
    return k;
}

static uint8_t pit_read(const MSX *m, PitCounter *p)
{
    uint16_t v = p->latched ? p->latch : pit_count(m, p);
    uint8_t rw = p->rw, r;
    if (rw == 1) r = (uint8_t)v;
    else if (rw == 2) r = (uint8_t)(v >> 8);
    else { r = p->rlow ? (uint8_t)(v >> 8) : (uint8_t)v; p->rlow ^= 1; if (p->rlow) return r; }
    p->latched = 0;
    return r;
}

static void pit_write(const MSX *m, PitCounter pit[3], int reg, uint8_t v)
{
    if (reg == 3) {                                     /* 제어 단어 */
        int c = v >> 6;
        if (c == 3) return;                             /* 8254 되읽기 명령은 없음 (8253) */
        PitCounter *p = &pit[c];
        int rw = (v >> 4) & 3;
        if (!rw) { p->latch = pit_count(m, p); p->latched = 1; return; }
        p->rw = (uint8_t)rw; p->mode = (uint8_t)((v >> 1) & 7);
        if (p->mode > 5) p->mode &= 3;
        p->wlow = p->rlow = 0; p->armed = 0; p->edge = 0;
        return;
    }
    PitCounter *p = &pit[reg];
    if (p->rw == 1) p->reload = (uint16_t)((p->reload & 0xFF00) | v);
    else if (p->rw == 2) p->reload = (uint16_t)((p->reload & 0x00FF) | (v << 8));
    else if (!p->wlow) { p->reload = (uint16_t)((p->reload & 0xFF00) | v); p->wlow = 1; if (p->mode == 0) p->armed = 0; return; }
    else { p->reload = (uint16_t)((p->reload & 0x00FF) | (v << 8)); p->wlow = 0; }
    pit_arm(m, p);
}

/* ---- 5.2.48 MSX 표준 RS-232C 카트리지 (Philips NMS1210): 8251 80h/81h, 상태·인터럽트 마스크 82h, 8253 84h~87h. 롬 없음.
 * 상대는 아직 없음(빈 단자): 보낸 바이트는 세기만, 받기는 프런트엔드가 msx_rs232_in 으로 넣으면 8251 속도대로 올라옴.
 * 82h 읽기(낮으면 켜짐): 비트 0 CD, 1 RI, 6 타이머 2 출력, 7 CTS — 상대가 없으니 CD·RI·CTS 모두 1(꺼짐). */
static int rs232_cart(const MSX *m) { for (int n = 0; n < MSX_CART_ALL; n++) if (m->cart[n].present && m->cart[n].mapper == MAPPER_RS232) return n; return -1; }
static void rs232_tick(MSX *m, Rs232Dev *r)
{
    if (pit_edge(m, &r->pit[2])) r->tflag = 1;
    if (!r->rxrdy && r->rx_r != r->rx_w && (r->cmd & 4) && m->cpu.cycles >= r->rx_next) {
        r->rx_data = r->rx_q[r->rx_r]; r->rx_r = (uint16_t)((r->rx_r + 1) % sizeof r->rx_q);
        r->rxrdy = 1; r->rx_count++;
        r->rx_next = m->cpu.cycles + 3730;              /* 10비트 ÷ 9600 baud ≈ 1.04 ms (시계는 8253 카운터 0 이 정하지만 받기 속도는 넉넉히) */
    }
}
static int rs232_irq(const MSX *m)
{
    int n = rs232_cart(m); if (n < 0) return 0;
    const Rs232Dev *r = &m->cart[n].rs;
    return (!(r->intmask & 1) && r->rxrdy) || (!(r->intmask & 8) && r->tflag);
}
int msx_rs232_in(MSX *m, const uint8_t *b, int n)
{
    int c = rs232_cart(m), k = 0; if (c < 0) return 0;
    Rs232Dev *r = &m->cart[c].rs;
    while (k < n) { uint16_t w = (uint16_t)((r->rx_w + 1) % sizeof r->rx_q); if (w == r->rx_r) break; r->rx_q[r->rx_w] = b[k++]; r->rx_w = w; }
    return k;
}
static uint8_t rs232_read(MSX *m, Rs232Dev *r, int p)
{
    rs232_tick(m, r);
    switch (p & 7) {
    case 0: { uint8_t v = r->rx_data; r->rxrdy = 0; sync_int(m); return v; }
    case 1: return (uint8_t)(0x05 | (r->rxrdy ? 0x02 : 0));              /* TxRDY·TxE, RxRDY; DSR(비트 7) = 상대 없음 = 0 */
    case 2: {                                                            /* CD·RI·CTS 꺼짐(1), 비트 6 = 타이머 2 출력(높으면 0) */
        const PitCounter *t = &r->pit[2];
        int out = (t->mode == 0 || t->mode == 4) ? (!t->armed || t->edge == 0) : 1;   /* 모드 0·4: 다 세면 높음, 모드 2·3: 거의 늘 높음 */
        return (uint8_t)(0xBF | (out ? 0 : 0x40));
    }
    case 3: return 0xFF;
    case 7: return 0xFF;
    default: return pit_read(m, &r->pit[(p & 7) - 4]);
    }
}
static void rs232_write(MSX *m, Rs232Dev *r, int p, uint8_t v)
{
    rs232_tick(m, r);
    switch (p & 7) {
    case 0: if (r->cmd & 1) r->tx_count++; break;       /* TxEN 이면 보냄 (상대 없음) */
    case 1:                                             /* 8251: 리셋 뒤 첫 바이트는 모드, 다음은 명령 (비트 6 = 내부 리셋) */
        if (!r->mode_next) { r->mode = v; r->mode_next = 1; break; }
        if (v & 0x40) { r->mode_next = 0; r->cmd = 0; r->rxrdy = 0; break; }
        r->cmd = v; if (!(v & 4)) r->rxrdy = 0;
        break;
    case 2: r->intmask = v; if (r->intmask & 8) r->tflag = 0; break;
    case 3: break;
    default: pit_write(m, r->pit, (p & 7) - 4, v); break;
    }
    sync_int(m);
}
/* ---- 5.2.56 MIDI UART 두 가지 (MIDI 단자 = MSX 쪽 MIDI OUT/IN) — 상태는 그 카트의 Rs232Dev 칸 (상태 파일 꼴 그대로)
 * YM2148 (Yamaha SFG-01/05, map.grauw.nl/resources/midi/ym2148.php · openMSX YM2148.cc): 3FF3h IRQ 벡터, 3FF5h 자료(쓰기 보내기·읽기 받기), 3FF6h 명령/상태.
 *   명령 비트 0 TxEN 1 TxIE 2 RxEN 3 RxIE 4 오류 지움 7 내부 리셋. 상태 비트 0 TxRDY 1 RxRDY 4 넘침. 칸: cmd, rxrdy, rx_data, intmask = 벡터, tflag = 넘침
 * MC6850 (Philips NMS-1205 Music Module, openMSX MusicModuleMIDI·MC6850): 쓰기 00h 제어·01h 보내기 (04h·05h 도 같음), 읽기 04h 상태·05h 받기.
 *   제어 비트 0·1 = 3 이면 마스터 리셋, 5·6 = 01 이면 보내기 IRQ, 7 받기 IRQ. 상태 비트 0 RDRF 1 TDRE 5 넘침 7 IRQ. 칸: cmd = 제어, pit[0].edge = 보내기 끝나는 시각
 * 받는 바이트는 rx_q (msx_midi_in_to), 31250 baud = 1145 사이클마다 하나씩 */
static void midiq_push(MSX *m, uint8_t v, uint32_t tag) { if (m->midi_n < sizeof m->midi_q) { m->midi_t[m->midi_n] = (uint32_t)(m->cpu.cycles - m->frame_start) | tag; m->midi_q[m->midi_n++] = v; } }
static int uart_cart(const MSX *m, int kind)
{
    for (int n = 0; n < MSX_CART_ALL; n++) {
        const Cartridge *c = &m->cart[n];
        if (!c->present) continue;
        if (kind == UART_YM2148 ? c->mapper == MAPPER_SFG : c->rs.uart == kind) return n;
    }
    return -1;
}
static void uart_rx_tick(MSX *m, Rs232Dev *r, int en)
{
    if (!r->rxrdy && en && r->rx_r != r->rx_w && m->cpu.cycles >= r->rx_next) {
        r->rx_data = r->rx_q[r->rx_r]; r->rx_r = (uint16_t)((r->rx_r + 1) % sizeof r->rx_q);
        r->rxrdy = 1; r->rx_count++; r->rx_next = m->cpu.cycles + 1145;
    }
}
static int sfgm_irq(const MSX *m)
{
    int k = uart_cart(m, UART_YM2148); if (k < 0) return 0;
    const Rs232Dev *r = &m->cart[k].rs;
    return ((r->cmd & 0x08) && r->rxrdy) || ((r->cmd & 0x02) && (r->cmd & 0x01));
}
static void sfgm_write(MSX *m, int reg, uint8_t v)
{
    int k = uart_cart(m, UART_YM2148); if (k < 0) return;
    Rs232Dev *r = &m->cart[k].rs;
    if (reg == 3) { r->intmask = v; return; }           /* MIDI IRQ 벡터 */
    if (reg == 5) { if (r->cmd & 0x01) { midiq_push(m, v, MIDI_T_SFG); r->tx_count++; } return; }   /* 보내기 (TxEN) */
    if (v & 0x80) { r->cmd = 0; r->rxrdy = 0; r->tflag = 0; sync_int(m); return; }   /* 내부 리셋 */
    if (v & 0x10) { r->tflag = 0; return; }             /* 오류 지움 */
    if (!(v & 0x04)) r->rxrdy = 0;                      /* 받기 끔 */
    r->cmd = v; sync_int(m);
}
static uint8_t sfgm_read(MSX *m, int reg)
{
    int k = uart_cart(m, UART_YM2148); if (k < 0) return 0xFF;
    Rs232Dev *r = &m->cart[k].rs;
    { int was = r->rxrdy; uart_rx_tick(m, r, r->cmd & 0x04); if (r->rxrdy != was) sync_int(m); }
    if (reg == 5) { uint8_t v = r->rx_data; r->rxrdy = 0; sync_int(m); return v; }
    return (uint8_t)((r->cmd & 0x01) | (r->rxrdy ? 0x02 : 0) | (r->tflag ? 0x10 : 0));
}
static int mmm_irq(const MSX *m)
{
    int k = uart_cart(m, UART_MC6850); if (k < 0) return 0;
    const Rs232Dev *r = &m->cart[k].rs;
    if ((r->cmd & 3) == 3) return 0;
    int tdre = m->cpu.cycles >= r->pit[0].edge;
    return ((r->cmd & 0x80) && r->rxrdy) || ((r->cmd & 0x60) == 0x20 && tdre);
}
static int ior_mmmidi(MSX *m, int p, uint8_t *v)
{
    int k = uart_cart(m, UART_MC6850); if (k < 0 || p < 4) return 0;
    Rs232Dev *r = &m->cart[k].rs;
    int live = (r->cmd & 3) != 3;
    { int was = r->rxrdy; uart_rx_tick(m, r, live); if (r->rxrdy != was) sync_int(m); }
    if (p == 5) { *v = r->rx_data; r->rxrdy = 0; sync_int(m); return 1; }
    uint8_t st = (uint8_t)((r->rxrdy ? 0x01 : 0) | (live && m->cpu.cycles >= r->pit[0].edge ? 0x02 : 0) | (r->tflag ? 0x20 : 0));
    if (mmm_irq(m)) st |= 0x80;
    *v = st; return 1;
}
static int iow_mmmidi(MSX *m, int p, uint8_t v)
{
    int k = uart_cart(m, UART_MC6850); if (k < 0) return 0;
    Rs232Dev *r = &m->cart[k].rs;
    if (!(p & 1)) {                                     /* 제어 */
        if ((v & 3) == 3) { r->rxrdy = 0; r->tflag = 0; r->pit[0].edge = 0; }   /* 마스터 리셋 */
        else if ((r->cmd & 3) == 3) r->pit[0].edge = m->cpu.cycles;            /* 리셋에서 나옴: 보내기 비어 있음 */
        r->cmd = v;
    } else if ((r->cmd & 3) != 3) {                     /* 보내기: 10비트 동안 TDRE 0 (Synthesix 검사) */
        midiq_push(m, v, MIDI_T_MM); r->tx_count++;
        r->pit[0].edge = m->cpu.cycles + 1145;
    }
    sync_int(m);
    return 1;
}
int msx_midi_jacks(const MSX *m) { return (dev_on(m, MDEV_MIDI) ? 1 << MDP_MSXMIDI : 0) | (uart_cart(m, UART_YM2148) >= 0 ? 1 << MDP_SFG : 0) | (uart_cart(m, UART_MC6850) >= 0 ? 1 << MDP_MM : 0); }
int msx_midi_in_to(MSX *m, int jack, const uint8_t *b, int n)
{
    if (jack == MDP_MSXMIDI) return msx_midi_in(m, b, n);
    int k = uart_cart(m, jack == MDP_SFG ? UART_YM2148 : UART_MC6850), i = 0;
    if (k < 0) return 0;
    Rs232Dev *r = &m->cart[k].rs;
    while (i < n) { uint16_t w = (uint16_t)((r->rx_w + 1) % sizeof r->rx_q); if (w == r->rx_r) break; r->rx_q[r->rx_w] = b[i++]; r->rx_w = w; }
    return i;
}
void msx_cart_set_uart(MSX *m, int n, int uart) { if (n >= 0 && n < MSX_CART_ALL) { m->cart[n].rs.uart = (uint8_t)uart; if (uart == UART_MC6850) m->cart[n].rs.cmd = 3; msx_io_rebuild(m); } }   /* MC6850 은 마스터 리셋 상태로 시작 */

static int ior_rs232(MSX *m, int p, uint8_t *v) { int n = rs232_cart(m); if (n < 0) return 0; *v = rs232_read(m, &m->cart[n].rs, p); return 1; }
static int iow_rs232(MSX *m, int p, uint8_t v) { int n = rs232_cart(m); if (n < 0) return 0; rs232_write(m, &m->cart[n].rs, p, v); return 1; }

static void midi_reset(MSX *m)
{
    m->midi_cmd = 0; m->midi_mode_next = 0;
    m->midi_rx_r = m->midi_rx_w = 0; m->midi_rxrdy = 0; m->midi_tflag = 0; m->midi_rx_next = 0; m->midi_tx_done = m->midi_tx_rdy = 0;
    memset(m->pit, 0, sizeof m->pit);
}

/* 5.2.0 구조 개편 1단계: 인터럽트선 — 장치는 자기 선만 올리고 내림, CPU INT = 모든 선의 OR */
void msx_irq_set(MSX *m, int line, int on)
{
    uint32_t b = 1u << line;
    if (on) m->irq_lines |= b; else m->irq_lines &= ~b;
    z80_set_int(&m->cpu, m->irq_lines != 0);
}

/* 아직 선을 직접 움직이지 않는 장치들의 상태를 선에 옮김 (장치를 부품으로 떼어 낼 때 하나씩 직접 msx_irq_set 으로 바뀜) */
static void sync_int(MSX *m)                    /* 3.4.1: SFG 타이머 IRQ, 응답 벡터는 0x3FF4에 쓴 값 */
{
    uint32_t l = m->irq_lines & ~((1u << IRQ_VDP) | (1u << IRQ_V9990) | (1u << IRQ_OPM) | (1u << IRQ_OPL4) | (1u << IRQ_MIDI) | (1u << IRQ_NET) | (1u << IRQ_RS232) | (1u << IRQ_SFGMIDI) | (1u << IRQ_MMMIDI));
    if (vdp_irq(&m->vdp)) l |= 1u << IRQ_VDP;
    if (m->v9 && v9990_irq(m->v9)) l |= 1u << IRQ_V9990;
    if (m->sfg_on && opm_irq(&m->opm)) l |= 1u << IRQ_OPM;
    if (m->ms_on && (m->opl4.status & 0x80)) l |= 1u << IRQ_OPL4;   /* 3.4.4: MoonSound(OPL4) FM 타이머 IRQ */
    if (midi_irq(m)) l |= 1u << IRQ_MIDI;                         /* 3.4.4: MSX-MIDI 타이머·받기 IRQ */
    if (rs232_irq(m)) l |= 1u << IRQ_RS232;                       /* RS-232C 카트리지 */
    int sfgm = m->sfg_on && sfgm_irq(m);
    if (sfgm) l |= 1u << IRQ_SFGMIDI;                             /* SFG MIDI */
    if (mmm_irq(m)) l |= 1u << IRQ_MMMIDI;                        /* Music Module MIDI */
    if (netcard_irq_line(&m->net)) l |= 1u << IRQ_NET;
    m->irq_lines = l;
    { int k = sfgm ? uart_cart(m, UART_YM2148) : -1;
      m->cpu.int_vec = m->sfg_on ? (k >= 0 ? m->cart[k].rs.intmask : m->sfg_vec) : 0xFF; }   /* SFG가 데이터 버스를 몬다 (blueMSX와 같음). 5.2.56: MIDI IRQ 면 3FF3h 벡터 */
    z80_set_int(&m->cpu, l != 0);
}

/* 5.2.0 시간표: 사건 번호 → 처리 (sched.h). 장치를 부품으로 옮길 때 여기에 한 줄씩 */
static void sched_dispatch(MSX *m, int ev)
{
    switch (ev) {
    case EV_TEST: m->sched_test_hits++; break;
    default: break;
    }
}
static void sched_run(MSX *m)
{
    int ev;
    while ((ev = sched_pop(&m->sched, m->cpu.cycles)) >= 0) sched_dispatch(m, ev);
}
void msx_sched_test(MSX *m, uint64_t when) { sched_set(&m->sched, EV_TEST, when); }

void msx_net_event(MSX *m) { netcard_raise_irq(&m->net); sync_int(m); }   /* 호스트가 받은 자료·망 상태 변화를 알림 */

static uint8_t psg_read(MSX *m)
{
    switch (m->psg.sel) {
    case 14: {                      /* 조이스틱 (포트는 레지스터 15 비트 6), 0 = 누름 */
        int port = (psg_read_reg(&m->psg, 15) >> 6) & 1;
        /* 4.0.2: 마우스 포트라도 마우스 프로그램이 8번 핀을 건드린 지 1초가 안 됐을 때만 마우스로 보임. 그렇지 않으면 그냥 조이스틱 (갤러그·갤럭시안 같은 게임이 포트 1 조이스틱을 직접 읽음) */
        if (port == m->mouse_port && m->cpu.cycles - m->mouse_t < 3579545u) {   /* 3.0: MSX 마우스 */
            int v = m->mouse_phase == 0 ? m->mouse_lx >> 4 : m->mouse_phase == 1 ? m->mouse_lx : m->mouse_phase == 2 ? m->mouse_ly >> 4 : m->mouse_ly;
            return (uint8_t)(0xC0 | (~m->mouse_btn & 3) << 4 | (v & 0x0F));
        }
        return (uint8_t)(0xC0 | (~m->joy[port] & 0x3F));
    }
    default: return psg_read_reg(&m->psg, m->psg.sel);
    }
}

static void set_ppi_c(MSX *m, uint8_t v)
{
    if ((v ^ m->ppi_c) & 0x80) {        /* 키 클릭 비트가 바뀌면 그 시점까지 소리를 만든 뒤 반영 */
        psg_sync(&m->psg, m->cpu.cycles);
        psg_set_click(&m->psg, v & 0x80);
    }
    m->ppi_c = v;
}

/* MSX-DOS2 ROM: 0x4000~0x7FFF에 16 KiB 뱅크 */
static void dos2_map(MSX *m)
{
    if (!m->dos2) return;
    uint32_t nb = m->dos2_size / 16384 ? m->dos2_size / 16384 : 1;
    const uint8_t *p = m->dos2 + (uint32_t)(m->dos2_bank % nb) * 16384;
    /* 읽기 표는 uint8_t *이지만 ROM 블록은 읽기만 한다 (쓰기 표에는 넣지 않음) */
    m->slot_rd[m->dos2_ps][m->dos2_ss][2] = (uint8_t *)(uintptr_t)p;
    m->slot_rd[m->dos2_ps][m->dos2_ss][3] = (uint8_t *)(uintptr_t)p + 8192;
}

/* 메모리 매퍼: 페이지마다 16 KiB 조각 */
static void mapper_map(MSX *m)
{
    if (m->mram_size) {
        uint32_t nseg = m->mram_size / 16384;
        for (int b = 0; b < 8; b++) {
            uint32_t seg = m->mapper_reg[b >> 1] & (nseg - 1);
            uint8_t *p = m->mram + seg * 16384 + (uint32_t)(b & 1) * 8192;
            m->slot_rd[m->mapper_ps][m->mapper_ss][b] = p;
            m->slot_wr[m->mapper_ps][m->mapper_ss][b] = p;
        }
    }
    if (m->xram_size && m->xram) {                      /* 외장 매퍼: 같은 레지스터, 자기 크기로 자름 */
        uint32_t nseg = m->xram_size / 16384;
        for (int b = 0; b < 8; b++) {
            uint32_t seg = m->mapper_reg[b >> 1] & (nseg - 1);
            uint8_t *p = m->xram + seg * 16384 + (uint32_t)(b & 1) * 8192;
            m->slot_rd[m->xram_ps][m->xram_ss][b] = p;
            m->slot_wr[m->xram_ps][m->xram_ss][b] = p;
        }
    }
}


/* ------------------------------------------------------------------ */
/* 3.4.0 기종 내장 펌웨어 매퍼·Bunsetsu (fw.h)                          */
/* ------------------------------------------------------------------ */
static int fw_sram_banks(const FwDev *f) { return f->sram_mirror ? 8 : (int)(f->sram_size / 8192); }
static uint32_t fw_sram_off(const FwDev *f, uint32_t b) { return ((b - 0x80) * 8192u) % (f->sram_size ? f->sram_size : 8192); }
static const uint8_t *fw_rom8(const FwDev *f, uint32_t bank)
{
    uint32_t nb = f->rom_size / 8192;
    return nb ? f->rom + (bank % nb) * 8192 : ff_block;
}
static void update_all_pages(MSX *m);
static void fw_map(MSX *m)
{
    FwDev *f = &m->fw;
    if (f->kind == FW_NONE || f->ps < 0) return;
    int s = f->ps, q = f->ss;
    if (f->kind == FW_PANASONIC) {
        for (int r = 0; r < 8; r++) {
            if (r < f->start || r >= f->start + f->count) continue;
            uint32_t b = f->bank[r];
            if (b >= 0x80 && (int)(b - 0x80) < fw_sram_banks(f)) { m->slot_rd[s][q][r] = f->sram + fw_sram_off(f, b); m->slot_wr[s][q][r] = discard_block; }
            else if (b >= 0x180 && f->ram_banks && m->mram_size) { uint32_t o = ((b - 0x180) * 8192u) & (m->mram_size - 1); m->slot_rd[s][q][r] = m->mram + o; m->slot_wr[s][q][r] = m->mram + o; }   /* 본체 RAM (넘치면 되풀이) */
            else { m->slot_rd[s][q][r] = (uint8_t *)(uintptr_t)fw_rom8(f, b); m->slot_wr[s][q][r] = discard_block; }
        }
    } else if (f->kind == FW_NATIONAL) {
        for (int r = 0; r < 8; r++) {
            if (r < f->start || r >= f->start + f->count) continue;
            uint32_t b16 = f->bank[r >> 1];
            m->slot_rd[s][q][r] = (uint8_t *)(uintptr_t)fw_rom8(f, b16 * 2 + (uint32_t)(r & 1));
            m->slot_wr[s][q][r] = discard_block;
        }
    } else if (f->kind == FW_HALNOTE) {
        for (int r = 0; r < 8; r++) {
            if (r < f->start || r >= f->start + f->count) continue;
            if (r < 2) m->slot_rd[s][q][r] = f->sram_on ? f->sram + r * 8192 : ff_block;
            else if (r < 6) m->slot_rd[s][q][r] = (uint8_t *)(uintptr_t)fw_rom8(f, f->bank[r]);
            else m->slot_rd[s][q][r] = ff_block;
            m->slot_wr[s][q][r] = discard_block;
        }
    }
}
static void fw_reset(MSX *m)
{
    FwDev *f = &m->fw;
    for (int i = 0; i < 8; i++) f->bank[i] = 0;
    f->ctrl = 0; f->sub[0] = f->sub[1] = 0; f->sram_on = f->sub_on = 0; f->sram_addr = 0;
    f->jisyo_addr = 0; f->mat_addr = 0; f->mat_pattern = 0; f->mat_c1 = f->mat_c2 = 0;
    fw_map(m);
}
/* 특별한 읽기 (0xFFFF가 아니면 그 값, 아니면 -1 = 보통 메모리) */
static int fw_read(MSX *m, uint16_t a)
{
    FwDev *f = &m->fw;
    if (f->kind == FW_PANASONIC) {
        if ((f->ctrl & 0x04) && a >= 0x7FF0 && a < 0x7FF8) return f->bank[a & 7] & 0xFF;
        if ((f->ctrl & 0x10) && a == 0x7FF8) { int v = 0; for (int i = 7; i >= 0; i--) v = (v << 1) | ((f->bank[i] >> 8) & 1); return v; }
        if ((f->ctrl & 0x08) && a == 0x7FF9) return f->ctrl;
    } else if (f->kind == FW_NATIONAL) {
        if ((f->ctrl & 0x04) && (a & 0x7FF9) == 0x7FF0) return f->bank[(a & 6) >> 1] & 0xFF;
        if ((f->ctrl & 0x02) && (a & 0x3FFF) == 0x3FFD) { int v = f->sram[f->sram_addr & 0x0FFF]; f->sram_addr++; return v; }
    } else if (f->kind == FW_HALNOTE) {
        if (f->sub_on && a >= 0x7000 && a < 0x8000) {
            uint32_t o = 0x80000u + (uint32_t)f->sub[a >= 0x7800] * 0x800u + (a & 0x7FF);
            return o < f->rom_size ? f->rom[o] : 0xFF;
        }
    }
    return -1;
}
static void fw_write(MSX *m, uint16_t a, uint8_t v)
{
    FwDev *f = &m->fw;
    int r = a >> 13;
    if (f->kind == FW_PANASONIC) {
        if (a >= 0x6000 && a < 0x7FF0) {
            int g = (a & 0x1C00) >> 10;
            if (g == 5 || g == 6) g ^= 3;
            f->bank[g] = (uint16_t)((f->bank[g] & 0x100) | v);
            fw_map(m); update_all_pages(m);
        } else if (a == 0x7FF8) {
            for (int g = 0; g < 8; g++) { f->bank[g] = (uint16_t)((f->bank[g] & 0xFF) | ((v & 1) << 8)); v >>= 1; }
            fw_map(m); update_all_pages(m);
        } else if (a == 0x7FF9) f->ctrl = v;
        else {
            uint32_t b = f->bank[r];
            if (b >= 0x80 && (int)(b - 0x80) < fw_sram_banks(f)) { f->sram[fw_sram_off(f, b) + (a & 0x1FFF)] = v; f->sram_dirty = 1; }
            else if (b >= 0x180 && f->ram_banks && m->mram_size) m->mram[(((b - 0x180) * 8192u) & (m->mram_size - 1)) + (a & 0x1FFF)] = v;
        }
    } else if (f->kind == FW_NATIONAL) {
        if (a == 0x6000) { f->bank[1] = v; fw_map(m); update_all_pages(m); }
        else if (a == 0x6400) { f->bank[0] = v; fw_map(m); update_all_pages(m); }
        else if (a == 0x7000) { f->bank[2] = v; fw_map(m); update_all_pages(m); }
        else if (a == 0x7400) { f->bank[3] = v; fw_map(m); update_all_pages(m); }
        else if (a == 0x7FF9) f->ctrl = v;
        else if (f->ctrl & 0x02) {
            uint16_t x = a & 0x3FFF;
            if (x == 0x3FFA) f->sram_addr = (f->sram_addr & 0x00FFFF) | (uint32_t)v << 16;
            else if (x == 0x3FFB) f->sram_addr = (f->sram_addr & 0xFF00FF) | (uint32_t)v << 8;
            else if (x == 0x3FFC) f->sram_addr = (f->sram_addr & 0xFFFF00) | v;
            else if (x == 0x3FFD) { f->sram[f->sram_addr & 0x0FFF] = v; f->sram_addr++; f->sram_dirty = 1; }
        }
    } else if (f->kind == FW_HALNOTE) {
        if (a < 0x4000) { if (f->sram_on) { f->sram[a] = v; f->sram_dirty = 1; } }
        else if (a < 0xC000) {
            if (a == 0x77FF || a == 0x7FFF) f->sub[a >= 0x7800] = v;
            else if ((a & 0x1FFF) == 0x0FFF) {
                f->bank[r] = v;
                if (r == 2) f->sram_on = (v & 0x80) != 0;
                if (r == 3) f->sub_on = (v & 0x80) != 0;
                fw_map(m); update_all_pages(m);
            }
        }
    }
}
static int bun_read(MSX *m, uint16_t a)
{
    FwDev *f = &m->fw;
    if (a != 0xBFFF || !f->jisyo || !f->jisyo_size) return -1;
    int v = f->jisyo[f->jisyo_addr % f->jisyo_size];
    f->jisyo_addr = (f->jisyo_addr + 1) & 0x1FFFF;
    return v;
}
static void bun_write(MSX *m, uint16_t a, uint8_t v)
{
    FwDev *f = &m->fw;
    if (a == 0xBFFC) f->jisyo_addr = (f->jisyo_addr & 0x1FF00) | v;
    else if (a == 0xBFFD) f->jisyo_addr = (f->jisyo_addr & 0x100FF) | (uint32_t)v << 8;
    else if (a == 0xBFFE) f->jisyo_addr = (f->jisyo_addr & 0x0FFFF) | (uint32_t)(v & 1) << 16;
}
/* Matsushita 교환 I/O (ID 8): 포트 41h~4Fh */
static int mat_io_read(MSX *m, int p)
{
    FwDev *f = &m->fw;
    switch (p & 0x0F) {
    case 0: return (uint8_t)~8;
    case 1: { int r = f->mat_switch ? 0x7F : 0xFF; if (f->mat_turbo) r &= ~1; return r; }
    case 3: { int r = (((f->mat_pattern & 0x80) ? f->mat_c2 : f->mat_c1) << 4) | ((f->mat_pattern & 0x40) ? f->mat_c2 : f->mat_c1);
              f->mat_pattern = (uint8_t)((f->mat_pattern << 2) | (f->mat_pattern >> 6)); return r; }
    case 9: { int r = f->mat_addr < 0x800 ? f->mat_sram[f->mat_addr] : 0xFF; f->mat_addr = (f->mat_addr + 1) & 0x1FFF; return r; }
    default: return 0xFF;
    }
}
static void mat_io_write(MSX *m, int p, uint8_t v)
{
    FwDev *f = &m->fw;
    switch (p & 0x0F) {
    case 1: f->mat_turbo = !(v & 1); break;
    case 3: f->mat_c2 = (v >> 4) & 15; f->mat_c1 = v & 15; break;
    case 4: f->mat_pattern = v; break;
    case 7: f->mat_addr = (uint16_t)((f->mat_addr & 0xFF00) | v); break;
    case 8: f->mat_addr = (uint16_t)((f->mat_addr & 0x00FF) | ((v & 0x1F) << 8)); break;
    case 9: if (f->mat_addr < 0x800) { f->mat_sram[f->mat_addr] = v; f->mat_dirty = 1; } f->mat_addr = (f->mat_addr + 1) & 0x1FFF; break;
    }
}

/* turbo R: S1990은 R800이 VDP(98h~9Bh)에 너무 빨리 연달아 접근하지 않게 기다리게 한다 (약 62 R800 클럭 = Z80 시간 31) */
static void r800_vdp_wait(MSX *m)
{
    uint64_t need = m->vdp_io_last + 31;
    if (m->cpu.cycles < need) { m->io_stall += (int)(need - m->cpu.cycles); m->cpu.cycles = need; }
    m->vdp_io_last = m->cpu.cycles;
}

/* RTC (2.14): PC 시계(msx_rtc_set) + 에뮬레이션 시간 + MSX에서 바꾼 만큼. PC 시계가 없으면 2026-10-01 0시부터 */
static const uint8_t mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
static int leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
uint32_t msx_rtc_secs(int y, int mo, int d, int h, int mi, int s)
{
    if (y < 1980) y = 1980;
    if (mo < 1) mo = 1;
    if (mo > 12) mo = 12;
    if (d < 1) d = 1;
    uint32_t days = 0;
    for (int yy = 1980; yy < y; yy++) days += leap(yy) ? 366u : 365u;
    for (int m = 1; m < mo; m++) days += mdays[m - 1] + (m == 2 && leap(y));
    days += (uint32_t)(d - 1);
    return days * 86400u + (uint32_t)h * 3600u + (uint32_t)mi * 60u + (uint32_t)s;
}
void msx_rtc_set(MSX *m, uint32_t secs)
{
    m->rtc_now = secs;
    m->rtc_ref = m->cpu.cycles;
}
static uint32_t rtc_time(const MSX *m)
{
    uint32_t base = m->rtc_now ? m->rtc_now : 1475280000u;      /* 2026-10-01 0시 */
    uint64_t el = m->cpu.cycles >= m->rtc_ref ? (m->cpu.cycles - m->rtc_ref) / 3579545u : 0;
    return base + (uint32_t)el + (uint32_t)m->rtc_delta;
}
static void rtc_fields(uint32_t t, int v[13])
{
    int sec = (int)(t % 60), min = (int)(t / 60 % 60), hour = (int)(t / 3600 % 24);
    uint32_t days = t / 86400;
    int wd = (int)((2 + days) % 7);                     /* 1980-01-01은 화요일 (일요일 = 0) */
    int y = 1980;
    while (days >= (leap(y) ? 366u : 365u)) { days -= leap(y) ? 366u : 365u; y++; }
    int mo = 0;
    while (days >= (uint32_t)(mdays[mo] + (mo == 1 && leap(y)))) { days -= (uint32_t)(mdays[mo] + (mo == 1 && leap(y))); mo++; }
    int day = (int)days + 1, yr = (y - 1980) % 100;
    mo++;
    int vals[13] = { sec % 10, sec / 10, min % 10, min / 10, hour % 10, hour / 10, wd,
                     day % 10, day / 10, mo % 10, mo / 10, yr % 10, yr / 10 };
    for (int i = 0; i < 13; i++) v[i] = vals[i];
}
static uint8_t rtc_read(MSX *m)
{
    int blk = m->rtc_mode & 3, r = m->rtc_sel;
    if (r >= 13) return (uint8_t)(0xF0 | (r == 13 ? m->rtc_mode : 0x0F));
    if (blk == 1 && r == 11) {                          /* 윤년 카운터 = 연도 % 4 */
        int v[13]; rtc_fields(rtc_time(m), v);
        return (uint8_t)(0xF0 | ((v[12] * 10 + v[11]) & 3));
    }
    if (blk != 0) return (uint8_t)(0xF0 | (m->rtc[blk][r] & 0x0F));
    int v[13];
    rtc_fields(rtc_time(m), v);
    return (uint8_t)(0xF0 | v[r]);
}

static void rtc_write(MSX *m, uint8_t v)
{
    int r = m->rtc_sel;
    if (r == 13) m->rtc_mode = v & 0x0F;
    else if (r < 13 && (m->rtc_mode & 3)) m->rtc[m->rtc_mode & 3][r] = v & 0x0F;
    else if (r < 13 && r != 6) {                        /* 블록 0: MSX가 시각·날짜를 바꿈 → 그만큼 어긋남으로 기억 */
        uint32_t t = rtc_time(m);
        int f[13];
        if (!m->rtc_wlast || m->cpu.cycles - m->rtc_wlast > 3579545u) {   /* 새로 쓰기 시작: 지금 값부터 */
            rtc_fields(t, f);
            for (int i = 0; i < 13; i++) m->rtc[0][i] = (uint8_t)f[i];
        }
        m->rtc_wlast = m->cpu.cycles;
        m->rtc[0][r] = v & 0x0F;
        for (int i = 0; i < 13; i++) f[i] = m->rtc[0][i];
        int sec = f[1] * 10 + f[0], min = f[3] * 10 + f[2], hour = f[5] * 10 + f[4];
        int day = f[8] * 10 + f[7], mon = f[10] * 10 + f[9], yr = f[12] * 10 + f[11];
        if (sec > 59 || min > 59 || hour > 23 || day < 1 || day > 31 || mon < 1 || mon > 12) return;   /* 쓰는 중간의 값 */
        uint32_t nt = msx_rtc_secs(1980 + yr, mon, day, hour, min, sec);
        m->rtc_delta += (int32_t)(nt - t);
    }
}

/* ---- 3.4.1 Beer IDE (8255 포트 30h~33h)·GIDE (포트 64h~6Fh) — blueMSX 동작을 보고 새로 작성 ---- */
static int io_cart(const MSX *m, MapperType mp)
{
    for (int n = 0; n < MSX_CART_ALL; n++) if (m->cart[n].present && m->cart[n].mapper == mp) return n;
    return -1;
}

static void beer_port_c(MSX *m, Cartridge *c)           /* C 아래 3비트 = 레지스터, PC7 0 = 읽기, PC6 0 = 쓰기 */
{
    uint8_t v = c->ppi[2], md = c->ppi[3];
    if (md & 0x08) return;                              /* C 위가 입력이면 펄스 없음 */
    int r = v & 7;
    if (!(v & 0x80)) c->ide_word = r ? ide_read_reg(&m->ide, r) : ide_read_data(&m->ide);
    if (!(v & 0x40)) { if (r) ide_write_reg(&m->ide, r, (uint8_t)c->ide_word); else ide_write_data(&m->ide, c->ide_word); }
}

static uint8_t beer_read(MSX *m, Cartridge *c, int p)
{
    switch (p & 3) {
    case 0: return (c->ppi[3] & 0x10) ? (uint8_t)c->ide_word : c->ppi[0];
    case 1: return (c->ppi[3] & 0x02) ? (uint8_t)(c->ide_word >> 8) : c->ppi[1];
    case 2: return c->ppi[2];
    default: (void)m; return c->ppi[3];
    }
}

static void beer_write(MSX *m, Cartridge *c, int p, uint8_t v)
{
    switch (p & 3) {
    case 0: c->ppi[0] = v; if (!(c->ppi[3] & 0x10)) c->ide_word = (uint16_t)((c->ide_word & 0xFF00) | v); break;
    case 1: c->ppi[1] = v; if (!(c->ppi[3] & 0x02)) c->ide_word = (uint16_t)((c->ide_word & 0x00FF) | (v << 8)); break;
    case 2: c->ppi[2] = v; beer_port_c(m, c); break;
    default:
        if (v & 0x80) { c->ppi[3] = v; beer_write(m, c, 0, c->ppi[0]); beer_write(m, c, 1, c->ppi[1]); beer_port_c(m, c); }
        else { uint8_t k = (uint8_t)(1 << ((v >> 1) & 7)); c->ppi[2] = (v & 1) ? (c->ppi[2] | k) : (c->ppi[2] & ~k); beer_port_c(m, c); }
        break;
    }
}

static uint8_t gide_read(MSX *m, Cartridge *c, int r)
{
    switch (r) {
    case 6: return c->gide_alt;
    case 7: return (uint8_t)(m->ide.head & 0x10 ? 0xFE : 0xFD);     /* 드라이브 주소: 고른 드라이브 비트 0 */
    case 8: return ide_read_data8(&m->ide);
    case 15: c->gide_alt = ide_read_reg(&m->ide, 7); return c->gide_alt;
    default: return r >= 9 ? ide_read_reg(&m->ide, r - 8) : 0xFF;
    }
}

static void gide_write(MSX *m, Cartridge *c, int r, uint8_t v)
{
    (void)c;
    if (r == 6) { if (v & 0x02) ide_reset(&m->ide); return; }
    if (r == 8) { ide_write_data8(&m->ide, v); return; }
    if (r >= 9) ide_write_reg(&m->ide, r - 8, v);
}

static int ca1_off(const MSX *m)                     /* 3.4.1 FS-CA1: 0x7FFF 비트 0이 0이면 포트 C0h·C1h 꺼짐 */
{
    for (int n = 0; n < MSX_CART_ALL; n++) if (m->cart[n].present && m->cart[n].mapper == MAPPER_FSCA1) return !(m->cart[n].scc_mode & 1);
    return 0;
}

static void vdp_sync(MSX *m);


/* VDP 명령이 끝나는 시각. 화면 안(표시 줄 + 바로 앞 줄)과 밖(수직 귀선·위아래 테두리)은 VRAM 자리 밀도가 달라
 * (openMSX VDPAccessSlots: 화면 끔 표 ↔ 스프라이트 표) 줄마다 속도를 바꿔 가며 일의 양을 소진시킴. */
static uint64_t vdp_cmd_end(MSX *m)
{
    uint32_t con = m->vdp.cmd_cost / 6, coff = m->vdp.cmd_cost_off / 6;
    if (con < 1) con = 1;
    if (coff < 1) coff = 1;
    if (coff > con) coff = con;
    int total = msx_frame_lines(m), lines = m->vdp.lines, line = m->cur_line;
    uint64_t t = m->cpu.cycles, w = 1ull << 30;              /* 남은 일 (1.0 = 2^30) */
    int64_t avail = (int64_t)MSX_TPL - (int64_t)(t - m->line_start);
    if (avail < 1) avail = 1;
    for (int it = 0; it < 1200; it++) {
        int on = (line < lines) || (line == total - 1);
        uint32_t c = on ? con : coff;
        uint64_t need = (w * c) >> 30;                       /* 이 속도로 끝내는 데 필요한 사이클 */
        if (need <= (uint64_t)avail) return t + need;
        w -= ((uint64_t)avail << 30) / c;
        t += (uint64_t)avail;
        if (++line >= total) line = 0;
        avail = MSX_TPL;
    }
    return t + ((w * con) >> 30);
}

/* 진행 중인 줄 단위 VDP 명령을 지금 시각만큼 실행 (VRAM에 닿기 전·화면 그리기 전에 부름) */
static void vdp_sync(MSX *m)
{
    VDP *v = &m->vdp;
    if (!v->job_active) return;
    uint64_t now = m->cpu.cycles;
    if (!v->job_t1 || now >= v->job_t1) vdp_job_run(v, v->job_tny);
    else if (now > v->job_t0) vdp_job_run(v, (int)(((uint64_t)v->job_tny * (now - v->job_t0)) / (v->job_t1 - v->job_t0)));
}
/* 새 명령이 줄 단위면 진행 시간을 정함 */
static void vdp_job_arm(MSX *m)
{
    VDP *v = &m->vdp;
    if (!v->job_active) return;
    if (!v->job_t1 && msx_vdp_cmd_timing && m->vdp_busy_until > m->cpu.cycles) { v->job_t0 = m->cpu.cycles; v->job_t1 = m->vdp_busy_until; }
    else if (!msx_vdp_cmd_timing || m->vdp_busy_until <= m->cpu.cycles) vdp_job_run(v, v->job_tny);
}

/* ==================================================================
 * 5.2.1 구조 개편 2단계: I/O 버스
 * 포트마다 "그 포트를 가질 수 있는 장치" 사슬(번호, 앞이 먼저). IN/OUT 은 사슬을 앞에서부터 물어
 * 처음으로 "내 것"이라고 한 장치가 처리 (예전 if 사슬과 같은 순서 = 같은 동작). 아무도 안 가지면 읽기 FFh.
 * 장치를 꽂고 빼는 것·켜고 끄는 것은 각 처리 함수가 확인 (5단계에서 꽂힌 장치만 등록하도록 바뀜).
 * 새 장치 = IoDev 번호 + 읽기/쓰기 함수 + 장치 표 io_dev[] 에 IODEV(...) 한 줄 (5.2.118: 포트·있음 조건·읽기·쓰기가 한 줄, src/device.h). 다른 장치·사슬 코드는 안 고침.
 * 사슬은 번호만 담음(함수 포인터 아님) — 상태 저장·UEFI 주소 바뀜과 무관. 번호 → 함수는 아래 const 표.
 * ================================================================== */
enum IoDev {               /* 순서 = 우선순위 (예전 io_read/io_write 의 if 순서) — 바꾸지 않음, 새 장치는 알맞은 자리에 */
    IOD_NET = 1, IOD_V9990, IOD_MAT, IOD_VHD, IOD_BEER, IOD_GOUDA, IOD_GIDE, IOD_FDC63,
    IOD_S1990, IOD_TRTIMER, IOD_F4, IOD_TRPAUSE, IOD_PCM,     /* 예전 IOD_TURBOR 를 장치마다 (포트 겹침 없음) */
    IOD_MIDI, IOD_PRN, IOD_VDP, IOD_OPLL, IOD_ZEM90, IOD_MEGARAM, IOD_PSG2, IOD_Y8950,
    IOD_OPL4, IOD_RTC, IOD_KANJI, IOD_SYSID, IOD_PSG, IOD_PPI, IOD_MAPPER, IOD_HBI55, IOD_RS232, IOD_MMMIDI, IOD_COUNT
};

/* ---- 읽기: 1 = 내 것 (*v 에 값), 0 = 아님 ---- */
static int ior_net(MSX *m, int p, uint8_t *v) { if (!netcard_owns(&m->net, (uint8_t)p)) return 0; *v = netcard_io_read(&m->net, (uint16_t)p, m->cpu.cycles); return 1; }   /* 5.0.57 */
static int ior_v9990(MSX *m, int p, uint8_t *v)                  /* 4.0.29 Gfx9000 (V9990) 포트 60h~6Fh */
{
    int tick = (int)((m->cpu.cycles - m->line_start) * 2736u / MSX_TPL);
    *v = v9990_read(m->v9, p & 15, tick > 2735 ? 2735 : tick);
    sync_int(m);
    return 1;
}
static int ior_mat(MSX *m, int p, uint8_t *v) { if (m->sw_id != 8) return 0; *v = (uint8_t)mat_io_read(m, p); return 1; }   /* 3.4.0 */
static int ior_beer(MSX *m, int p, uint8_t *v) { int n = io_cart(m, MAPPER_BEERIDE); if (n < 0) return 0; *v = beer_read(m, &m->cart[n], p); return 1; }   /* 3.4.1 */
static int ior_gouda(MSX *m, int p, uint8_t *v)
{
    *v = (p & 3) == 0 ? wd_read_aux(&m->scsi) : (p & 3) == 1 ? wd_read_ctrl(&m->scsi, &m->ide) : 0xB0;
    return 1;
}
static int ior_gide(MSX *m, int p, uint8_t *v) { int n = io_cart(m, MAPPER_GIDE); if (n < 0) return 0; *v = gide_read(m, &m->cart[n], p & 0x0F); return 1; }
static int ior_fdc63(MSX *m, int p, uint8_t *v)                  /* 3.4.0 MED-04 I/O 포트 FDC (Microsol D0h~D4h) */
{
    int r = p & 7;
    if (r < 4) *v = fdc_read(&m->fdc, r, m->cpu.cycles);
    else if (r == 4) { fdc_poll(&m->fdc, m->cpu.cycles); *v = (uint8_t)((m->fdc.irq ? 0x80 : 0) | (m->fdc.drq ? 0x3F : 0x7F)); }   /* 비트 7 IRQ, 비트 6 = DRQ가 0일 때 1 */
    else *v = 0xFF;
    return 1;
}
static int ior_s1990(MSX *m, int p, uint8_t *v)                  /* turbo R S1990 */
{
    *v = p == 0xE4 ? m->s1990_reg : m->s1990_reg == 6 ? m->s1990_cpu : m->s1990_reg == 5 ? (msx_fw_turbor ? 0x40 : 0) : 0;   /* 3.6.1: 레지스터 5 비트 6 = 내장 소프트웨어 스위치 */
    return 1;
}
static int ior_trtimer(MSX *m, int p, uint8_t *v) { uint32_t t = (uint32_t)((m->cpu.cycles - m->trtimer_base) / 14); *v = (p & 1) ? (uint8_t)(t >> 8) : (uint8_t)t; return 1; }
static int ior_f4(MSX *m, int p, uint8_t *v) { (void)p; *v = m->f4_status; return 1; }
static int ior_trpause(MSX *m, int p, uint8_t *v) { (void)p; *v = (uint8_t)(m->pause_sw ? 1 : 0); return 1; }
static int ior_pcm(MSX *m, int p, uint8_t *v) { *v = p == 0xA4 ? (uint8_t)(((m->cpu.cycles - m->pcm_ref) / 228) & 3) : (uint8_t)(m->pcm_ctrl & 0x1F); return 1; }   /* A4h: 15.7 kHz 카운터 */
static int ior_midi(MSX *m, int p, uint8_t *v)
{
    switch (p) {
    case 0xE8: midi_tick(m); *v = m->midi_rx_data; m->midi_rxrdy = 0; sync_int(m); return 1;   /* 3.4.4: 받은 자료 */
    case 0xE9: midi_tick(m); {   /* TxRDY(비트 0) 는 버퍼가 비었을 때, TxE(비트 2) 는 다 내보냈을 때 */
        uint64_t c = m->cpu.cycles; int rdy = c >= m->midi_tx_rdy || m->midi_tx_rdy > c + 4096, emp = c >= m->midi_tx_done || m->midi_tx_done > c + 8192;
        *v = (uint8_t)((rdy ? 0x01 : 0) | (emp ? 0x04 : 0) | (m->midi_rxrdy ? 0x02 : 0) | (m->midi_tflag ? 0x80 : 0)); } return 1;   /* TxRDY TxE, RxRDY, DSR = 타이머 깃발 */
    case 0xEC: case 0xED: case 0xEE: *v = pit_read(m, &m->pit[p - 0xEC]); return 1;   /* 3.4.4: 8253 카운터 */
    }
    return 0;
}
static int ior_vdp(MSX *m, int p, uint8_t *v)
{
    vdp_sync(m);
    if (m->cpu.r800) r800_vdp_wait(m);
    if (p == 0x98) { *v = vdp_read_data(&m->vdp); return 1; }
    if (p == 0x99) {
        m->vdp.hr = (m->cpu.cycles - m->line_start) > 180;     /* 줄 끝 부분이 수평 귀선 */
        int sel = m->vdp.reg[15] & 15;
        uint8_t s = vdp_read_status(&m->vdp);
        sync_int(m);
        if (sel == 2 && m->vdp.v9938 && m->cpu.cycles < m->vdp_busy_until) s |= 0x01;   /* 명령 실행 중 (CE) */
#ifdef VDP_TRACE
        { extern int vdp_trace_line; extern unsigned long long vdp_trace_frame; fprintf(stderr, "S%d f%llu l%d v=%02X pc=%04X\n", sel, vdp_trace_frame, vdp_trace_line, s, m->cpu.pc); }
#endif
        *v = s; return 1;
    }
    *v = 0xFF; return 1;                                /* 9Ah·9Bh 읽기 */
}
static int ior_rtc(MSX *m, int p, uint8_t *v) { if (p != 0xB5) return 0; *v = m->rtc_on ? rtc_read(m) : 0xFF; return 1; }
static int ior_megaram(MSX *m, int p, uint8_t *v)               /* 2.16: MegaRAM 쓰기 모드 */
{
    (void)p;
    for (int n = 0; n < MSX_CART_ALL; n++) if (m->cart[n].present && m->cart[n].mapper == MAPPER_MEGARAM) m->cart[n].mram_we = 1;
    *v = 0xFF; return 1;
}
/* 5.2.45 Sony HBI-55 데이터 카트리지: 2 KiB SRAM 2개(4 KiB) + i8255 (포트 B0h~B3h, 본체 PPI 와 별개).
 * A(B0h, 출력) = 주소 0~7, B(B1h, 출력) = 비트 0~3 주소 8~11 · 비트 6 칩 켜기(CE) · 비트 7 1=읽기(OE) 0=쓰기(WE), C(B2h) = 자료 (제어 89h 입력 / 80h 출력).
 * 읽기: C 입력 + CE + OE 면 sram[주소]. 쓰기: C 출력 + CE + !OE 일 때 C 래치가 sram[주소] 로 (B 나 C 를 쓸 때). ppi[0..3] = A, B, C 래치, 제어. (map.grauw.nl/resources/hbi55.php, openMSX MSXHBI55) */
static uint8_t hbi55_read(Cartridge *c, int p)
{
    uint8_t b = c->ppi[1];
    switch (p & 3) {
    case 0: return c->ppi[0];
    case 1: return b;
    case 2:
        if (!(c->ppi[3] & 0x09)) return c->ppi[2];                      /* C 출력 모드: 래치 그대로 */
        return (b & 0xC0) == 0xC0 ? c->sram[((b & 0x0F) << 8) | c->ppi[0]] : 0xFF;   /* CE + OE */
    default: return c->ppi[3];
    }
}
static void hbi55_strobe(Cartridge *c)                              /* CE + WE + C 출력 → 쓰기 */
{
    uint8_t b = c->ppi[1];
    if ((b & 0xC0) != 0x40 || (c->ppi[3] & 0x09)) return;
    uint16_t a = (uint16_t)(((b & 0x0F) << 8) | c->ppi[0]);
    if (c->sram[a] != c->ppi[2]) { c->sram[a] = c->ppi[2]; c->sram_dirty = 1; }
}
static void hbi55_write(Cartridge *c, int p, uint8_t v)
{
    switch (p & 3) {
    case 0: c->ppi[0] = v; hbi55_strobe(c); break;
    case 1: c->ppi[1] = v; hbi55_strobe(c); break;
    case 2: c->ppi[2] = v; hbi55_strobe(c); break;
    default:
        if (v & 0x80) { c->ppi[3] = v; c->ppi[2] = 0; }              /* 모드 설정: 8255 는 출력 래치를 지움 */
        else { uint8_t k = (uint8_t)(1 << ((v >> 1) & 7)); c->ppi[2] = (v & 1) ? (c->ppi[2] | k) : (c->ppi[2] & ~k); hbi55_strobe(c); }
        break;
    }
}
static int ior_hbi55(MSX *m, int p, uint8_t *v) { int n = io_cart(m, MAPPER_HBI55); if (n < 0) return 0; *v = hbi55_read(&m->cart[n], p); return 1; }
static int iow_hbi55(MSX *m, int p, uint8_t v) { int n = io_cart(m, MAPPER_HBI55); if (n < 0) return 0; hbi55_write(&m->cart[n], p, v); return 1; }
static int ior_psg2(MSX *m, int p, uint8_t *v) { if (p != 0x12) return 0; *v = m->joyrex ? psg_read_reg(&m->psg2, m->psg2.sel) : 0xFF; return 1; }
static int ior_y8950(MSX *m, int p, uint8_t *v)
{
    if (m->audio_on && !ca1_off(m)) { y8950_sync(m); *v = p == 0xC0 ? (uint8_t)(OPL_status(m->y8950) | 0x06) : OPL_readIO(m->y8950); }   /* MSX-AUDIO 상태 (비트 1·2는 늘 1) */
    else *v = 0xFF;
    return 1;
}
static int ior_opl4(MSX *m, int p, uint8_t *v)                   /* 3.1.0 MoonSound */
{
    if (!m->ms_on) { *v = 0xFF; return 1; }
    ms_sync(m);
    *v = (p == 0x7E || p == 0x7F) ? opl4_read_wave(&m->opl4, p & 1) : opl4_read_fm(&m->opl4, p - 0xC4);
    return 1;
}
static int ior_kanji(MSX *m, int p, uint8_t *v)
{
    if (p != 0xD9 && p != 0xDB) return 0;
#ifdef VDP_TRACE
    { static int n; if (n++ < 6) fprintf(stderr, "kanji read port %02X a1 %05X a2 %05X size %u\n", p, m->kanji_a1, m->kanji_a2, m->kanji_size); }
#endif
    if (m->kanji) {
        uint32_t *a = p == 0xD9 ? &m->kanji_a1 : &m->kanji_a2;
        uint32_t base = p == 0xD9 ? 0 : 0x20000;
        *v = (base + *a < m->kanji_size) ? m->kanji[base + *a] : 0xFF;
        *a = (*a & ~0x1Fu) | ((*a + 1) & 0x1F);
    } else *v = 0xFF;
    return 1;
}
static int ior_sysid(MSX *m, int p, uint8_t *v)
{
    if (p == 0x40) { *v = (m->s1985 && m->sw_id == 0xFE) ? (uint8_t)~0xFE : 0xFF; return 1; }
    if (p == 0x42) { *v = (m->s1985 && m->sw_id == 0xFE) ? m->s1985_ram[m->s1985_addr & 15] : 0xFF; return 1; }
    return 0;
}
static int ior_psg(MSX *m, int p, uint8_t *v) { if (p != 0xA2) return 0; *v = psg_read(m); return 1; }
static int ior_ppi(MSX *m, int p, uint8_t *v)
{
    if (p == 0xA8) { *v = m->primary; return 1; }
    if (p == 0xA9) { int row = m->ppi_c & 0x0F; if (row < 11) m->kbd_row_reads[row]++; *v = row < 11 ? m->kbd[row] : 0xFF; return 1; }
    if (p == 0xAA) { *v = m->ppi_c; return 1; }
    return 0;
}
static int ior_mapper(MSX *m, int p, uint8_t *v)
{
    if (m->mram_size || m->xram_size) {                 /* 큰 쪽 매퍼의 비트 수 */
        uint32_t big = m->mram_size > m->xram_size ? m->mram_size : m->xram_size;
        /* 2.16: MSX-ENGINE S1985 기종은 매퍼 레지스터 아래 5비트를 그대로 돌려주고 위 3비트는 1 (openMSX S1985와 같음) */
        if (m->s1985) *v = (uint8_t)((m->mapper_reg[p - 0xFC] & 0x1F) | 0xE0);
        else *v = (uint8_t)(m->mapper_reg[p - 0xFC] | ~(big / 16384 - 1));
    } else *v = 0xFF;
    return 1;
}
static int ior_prn(MSX *m, int p, uint8_t *v) { if (p != 0x90) return 0; if (!m->prn_on) m->prn_poked = 1; *v = m->prn_on ? 0xFD : 0xFF; return 1; }   /* 3.3.0 프린터: 비트 1 = 바쁨 */

/* ---- 쓰기: 1 = 내 것 (사슬 멈춤), 0 = 아님 ---- */
static int iow_net(MSX *m, int p, uint8_t v) { if (!netcard_owns(&m->net, (uint8_t)p)) return 0; netcard_io_write(&m->net, (uint16_t)p, v, m->cpu.cycles); sync_int(m); return 1; }
static int iow_v9990(MSX *m, int p, uint8_t v) { v9990_write(m->v9, p & 15, v); sync_int(m); return 1; }
static int iow_mat(MSX *m, int p, uint8_t v) { if (!((p & 0x0F) && m->sw_id == 8)) return 0; mat_io_write(m, p, v); return 1; }
static int iow_vhd(MSX *m, int p, uint8_t v) { (void)v; if (!m->vhd_io) return 0; m->vhd_io(m->vhd_ctx, m, p); return 1; }   /* 3.7.1 가상 디스크 */
static int iow_beer(MSX *m, int p, uint8_t v) { int n = io_cart(m, MAPPER_BEERIDE); if (n < 0) return 0; beer_write(m, &m->cart[n], p, v); return 1; }
static int iow_gouda(MSX *m, int p, uint8_t v)
{
    if ((p & 3) == 0) wd_write_adr(&m->scsi, v); else if ((p & 3) == 1) wd_write_ctrl(&m->scsi, &m->ide, v); else wd_reset(&m->scsi);
    return 1;
}
static int iow_gide(MSX *m, int p, uint8_t v) { int n = io_cart(m, MAPPER_GIDE); if (n < 0) return 0; gide_write(m, &m->cart[n], p & 0x0F, v); return 1; }
static int iow_fdc63(MSX *m, int p, uint8_t v)                   /* 3.4.0 MED-04 */
{
    int r = p & 0x0F;
    FDC *f = &m->fdc;
    if (r < 4) fdc_write(f, r, v, m->cpu.cycles);
    else if (r == 4) { f->drive = (v & 0x0F) == 1 ? 0 : (v & 0x0F) == 2 ? 1 : 2; f->side = (v >> 4) & 1; f->motor = (v >> 5) & 1; }   /* 비트 0~3 드라이브 A~D, 4 면, 5 모터 */
    return 1;
}
static int iow_s1990(MSX *m, int p, uint8_t v)
{
    if (p == 0xE4) { m->s1990_reg = v; return 1; }
    if (m->s1990_reg == 6) {                            /* 비트 5: 1 = Z80, 0 = R800 */
        /* Z80 → R800: 첫 쓰기에서 CPU가 바뀌고 Z80은 멈춘다. BIOS CHGCPU는 OTIR로 [목표, 60h]를 쓰는데
         * 두 번째 바이트는 바뀐 뒤라 반영되지 않는다 (같은 명령 안의 쓰기는 무시) */
        if (m->cpu.r800 && m->cpu.cycles - m->s1990_switch < 32) return 1;
        int to_r800 = !(v & 0x20);
        if (to_r800 && !m->cpu.r800) m->s1990_switch = m->cpu.cycles;
        m->s1990_cpu = v & 0x60;
        m->cpu.r800 = (uint8_t)to_r800;
        for (int pg = 0; pg < 4; pg++) r800_delay(m, pg);   /* 3.4.2: ROM/DRAM 모드 */
        m->cpu.r8c = 0; m->cpu.r8_page = -1;
    }
    return 1;
}
static int iow_trtimer(MSX *m, int p, uint8_t v) { (void)v; if (p == 0xE6) m->trtimer_base = m->cpu.cycles; return 1; }
static int iow_f4(MSX *m, int p, uint8_t v) { (void)p; m->f4_status = dev_on(m, MDEV_F4_INV) ? (uint8_t)(v | 0x7F) : (uint8_t)((m->f4_status & 0x20) | (v & 0xA0)); return 1; }   /* 2 = 반전형 */
static int iow_trpause(MSX *m, int p, uint8_t v) { (void)p; m->trpause_reg = v; return 1; }
/* 카트리지 DAC (popolon Flash-ROM SCC) — turbo R PCM 과 같은 8비트 DA 이므로 PCM 소리줄(pcm_t/pcm_v, MSX_CH_PCM)을 함께 씀 */
static int dac_cart(const MSX *m) { for (int n = 0; n < MSX_CART_ALL; n++) if (m->cart[n].present && m->cart[n].mapper == MAPPER_FRSCC) return 1; return 0; }
static void dac_push(MSX *m, uint8_t v)
{
    m->pcm_da = v;
    if (m->pcm_n < 2048) { m->pcm_t[m->pcm_n] = (uint32_t)(m->cpu.cycles - m->frame_start); m->pcm_v[m->pcm_n++] = v; }
}
static int iow_pcm(MSX *m, int p, uint8_t v)
{
    if (p == 0xA5) { m->pcm_ctrl = v; return 1; }
    m->pcm_ref = m->cpu.cycles;                         /* A4h 쓰면 15.75 kHz 카운터 0부터 다시 */
    m->pcm_da = v;                                      /* DA 값 (프레임 안 시간과 함께 기록) */
    if (m->pcm_n < 2048) { m->pcm_t[m->pcm_n] = (uint32_t)(m->cpu.cycles - m->frame_start); m->pcm_v[m->pcm_n++] = v; }
    return 1;
}
unsigned long msx_midi_drop;
static int iow_midi(MSX *m, int p, uint8_t v)
{
    switch (p) {
    case 0xE8:                                          /* 3.3.3 MSX-MIDI 보내기 (TxEN일 때) */
        if ((m->midi_cmd & 1) && m->midi_n >= sizeof m->midi_q) msx_midi_drop++;   /* 5.2.158 진단 */
        if ((m->midi_cmd & 1) && m->midi_n < sizeof m->midi_q) {
            int gt_own = dev_on(m, MDEV_MIDI_HW) && midi_bus.def_on;   /* GTMIDI 띠 계기 = GT 내장 음원이 켜졌을 때. 음량은 소리에 곱함 (모듈로 가는 바이트는 그대로) */
            { uint64_t c = m->cpu.cycles, sd = m->midi_tx_done > c + 8192 ? c : m->midi_tx_done;   /* 한 바이트 = 1146 클럭, 한 바이트는 버퍼에 쌓임 */
              if (c >= sd) { m->midi_tx_rdy = c; m->midi_tx_done = c + 1146; } else { m->midi_tx_rdy = sd; m->midi_tx_done = sd + 1146; } }
            m->midi_t[m->midi_n] = (uint32_t)(m->cpu.cycles - m->frame_start); m->midi_q[m->midi_n++] = v; if (gt_own) msx_midi_int_bytes++;
        }
        return 1;
    case 0xE9:                                          /* 8251: 리셋 뒤 첫 바이트는 모드, 그다음은 명령 (비트 6 = 내부 리셋) */
        if (!m->midi_mode_next) { m->midi_mode_next = 1; return 1; }
        if (v & 0x40) { m->midi_mode_next = 0; m->midi_cmd = 0; m->midi_rxrdy = 0; sync_int(m); return 1; }
        m->midi_cmd = v;
        if (!(v & 4)) m->midi_rxrdy = 0;                /* 3.4.4: 받기 끔 */
        sync_int(m);
        return 1;
    case 0xEA: m->midi_tflag = 0; sync_int(m); return 1;   /* 3.4.4: 타이머 IRQ 깃발 지움 */
    case 0xEC: case 0xED: case 0xEE: case 0xEF: midi_tick(m); pit_write(m, m->pit, p - 0xEC, v); return 1;   /* 3.4.4: 8253 */
    }
    return 0;
}
static int iow_prn(MSX *m, int p, uint8_t v)
{
    if (p == 0x91) { m->prn_data = v; if (!m->prn_on) m->prn_poked = 1; return 1; }   /* 3.3.0 프린터 자료 */
    /* 0x90 스트로브: 1 → 0에서 한 글자 */
    if (m->prn_on && !m->prn_strobe && !(v & 1) && m->prn_n < sizeof m->prn_q) m->prn_q[m->prn_n++] = m->prn_data;
    m->prn_strobe = !(v & 1);                           /* 1 = 스트로브 내려가 있음 */
    return 1;
}
static int iow_vdp(MSX *m, int p, uint8_t v)
{
    vdp_sync(m);
    if (m->cpu.r800) r800_vdp_wait(m);
    switch (p) {
    case 0x98: vdp_write_data(&m->vdp, v); break;
    case 0x99:
#ifdef VDP_TRACE
        if (m->frame < 100) fprintf(stderr, "OUT99 %02X pc %04X a8 %02X f %llu\n", v, m->cpu.pc, m->primary, (unsigned long long)m->frame);
#endif
        m->vdp.line_t = VDP_LINE_T(m); m->vdp.late_defer = m->cpu.r800; vdp_write_ctrl(&m->vdp, v); sync_int(m);
        if (m->vdp.cmd_cost) { if (msx_vdp_cmd_timing) m->vdp_busy_until = vdp_cmd_end(m); m->vdp.cmd_cost = 0; }
        vdp_job_arm(m);
        break;
    case 0x9A: vdp_write_palette(&m->vdp, v); break;
    case 0x9B: m->vdp.line_t = VDP_LINE_T(m); m->vdp.late_defer = m->cpu.r800; vdp_write_indirect(&m->vdp, v); sync_int(m);
        if (m->vdp.cmd_cost) { if (msx_vdp_cmd_timing) m->vdp_busy_until = vdp_cmd_end(m); m->vdp.cmd_cost = 0; }
        vdp_job_arm(m);
        break;
    }
    return 1;
}
static int iow_opll(MSX *m, int p, uint8_t v) { if (m->opll_on) { OPLL_writeIO(m->opll, p & 1, v); m->opll_quiet = 0; } return 1; }
static int iow_zem90(MSX *m, int p, uint8_t v)                   /* 3.8.0: Zemina 90 in 1 세그먼트 */
{
    (void)p;
    for (int n = 0; n < MSX_CART_ALL; n++) {
        Cartridge *z = &m->cart[n];
        if (!z->present || z->mapper != MAPPER_ZEMINA90) continue;
        uint32_t seg = v & 0x3F, nxt = (v & 0x80) ? seg + 1 : seg;
        z->bank[0] = (uint8_t)(seg * 2); z->bank[1] = (uint8_t)(seg * 2 + 1);
        z->bank[2] = (uint8_t)(nxt * 2); z->bank[3] = (uint8_t)(nxt * 2 + 1);
        if (v & 0x40) { uint8_t t = z->bank[2]; z->bank[2] = z->bank[3]; z->bank[3] = t; }
        cart_map(m, n);
    }
    update_all_pages(m);
    return 1;
}
static int iow_megaram(MSX *m, int p, uint8_t v)                 /* 2.16: MegaRAM 뱅크 선택 모드 */
{
    (void)p; (void)v;
    for (int n = 0; n < MSX_CART_ALL; n++) if (m->cart[n].present && m->cart[n].mapper == MAPPER_MEGARAM) m->cart[n].mram_we = 0;
    return 1;
}
static int iow_psg2(MSX *m, int p, uint8_t v)
{
    if (p == 0x10) { if (psg2_on(m)) m->psg2.sel = v & 0x0F; return 1; }
    if (p == 0x11) { if (psg2_on(m)) { psg_sync(&m->psg2, m->cpu.cycles); psg_write(&m->psg2, m->psg2.sel, v); } return 1; }
    return 0;
}
static int iow_y8950(MSX *m, int p, uint8_t v) { if (m->audio_on && !ca1_off(m)) { y8950_sync(m); OPL_writeIO(m->y8950, p & 1, v); } return 1; }   /* MSX-AUDIO (2.14) */
static int iow_opl4(MSX *m, int p, uint8_t v)                    /* 3.1.0 MoonSound */
{
    if (!m->ms_on) return 1;
    ms_sync(m);
    if (p == 0x7E || p == 0x7F) opl4_write_wave(&m->opl4, p & 1, v);
    else { opl4_write_fm(&m->opl4, p - 0xC4, v); sync_int(m); }
    return 1;
}
static int iow_rtc(MSX *m, int p, uint8_t v) { if (p == 0xB4) m->rtc_sel = v & 0x0F; else if (m->rtc_on) rtc_write(m, v); return 1; }
static int iow_kanji(MSX *m, int p, uint8_t v)
{
    switch (p) {
    case 0xD8: m->kanji_a1 = (m->kanji_a1 & 0x3F800) | ((uint32_t)(v & 0x3F) << 5); break;
    case 0xD9: m->kanji_a1 = (m->kanji_a1 & 0x007E0) | ((uint32_t)(v & (m->kanji_hi_mask ? m->kanji_hi_mask : 0x3F)) << 11); break;   /* 한글 롬은 7비트 */
    case 0xDA: m->kanji_a2 = (m->kanji_a2 & 0x1F800) | ((uint32_t)(v & 0x3F) << 5); break;
    case 0xDB: m->kanji_a2 = (m->kanji_a2 & 0x007E0) | ((uint32_t)(v & 0x3F) << 11); break;
    }
    return 1;
}
static int iow_sysid(MSX *m, int p, uint8_t v)
{
    if (p == 0x40) m->sw_id = v;
    else if (p == 0x41) { if (m->s1985 && m->sw_id == 0xFE) m->s1985_addr = v; }
    else if (m->s1985 && m->sw_id == 0xFE) m->s1985_ram[m->s1985_addr & 15] = v;
    return 1;
}
static int iow_psg(MSX *m, int p, uint8_t v)
{
    if (p == 0xA0) { m->psg.sel = v & 0x0F; { int yq = yam_on(m); if (yq && (m->cart[yq - 1].y_cfg & 2)) m->psg2.sel = v & 0x0F; } return 1; }   /* 4.0.129: Yamanooto ECHO */
    if (p != 0xA1) return 0;
    { int yq = yam_on(m); if (yq && (m->cart[yq - 1].y_cfg & 2)) { psg_sync(&m->psg2, m->cpu.cycles); psg_write(&m->psg2, m->psg2.sel, v); } }
    psg_sync(&m->psg, m->cpu.cycles);
    if (m->psg.sel == 15 && m->mouse_port >= 0) {       /* 3.0: 마우스 포트 8번 핀이 바뀌면 다음 4비트 */
        uint8_t pin = (uint8_t)((v >> (4 + m->mouse_port)) & 1);
        if (pin != m->mouse_pin) {
            if (m->cpu.cycles - m->mouse_t > 3579545u * 3 / 1000) m->mouse_phase = 3;   /* 3 ms 넘게 쉬면 처음부터 */
            m->mouse_t = m->cpu.cycles;
            m->mouse_phase = (uint8_t)((m->mouse_phase + 1) & 3);
            if (m->mouse_phase == 0) {                  /* 새로 읽기 시작: 쌓인 값을 넘김 (-128~127) */
                int x = m->mouse_cx, y = m->mouse_cy;
                x = x > 127 ? 127 : x < -128 ? -128 : x;
                y = y > 127 ? 127 : y < -128 ? -128 : y;
                m->mouse_lx = x; m->mouse_ly = y; m->mouse_cx -= x; m->mouse_cy -= y;
            }
            m->mouse_pin = pin;
        }
    }
    psg_write(&m->psg, m->psg.sel, v);
    return 1;
}
static int iow_ppi(MSX *m, int p, uint8_t v)
{
    if (p == 0xA8) { m->primary = v; update_all_pages(m); return 1; }
    if (p == 0xAA) { set_ppi_c(m, v); return 1; }
    if (p == 0xAB) {
        if (!(v & 0x80)) {                              /* 포트 C 비트 설정/해제 */
            int bit = (v >> 1) & 7;
            uint8_t c = m->ppi_c;
            if (v & 1) c |= (uint8_t)(1 << bit);
            else c &= (uint8_t)~(1 << bit);
            set_ppi_c(m, c);
        }
        return 1;
    }
    return 0;
}
static int iow_mapper(MSX *m, int p, uint8_t v)
{
    if (m->mram_size || m->xram_size) { m->mapper_reg[p - 0xFC] = v; mapper_map(m); update_all_pages(m); }
    return 1;
}

/* I/O 장치 표 — 장치 한 줄에 가질 수 있는 포트(ports)·이 기계에 있는 조건(present, 없으면 늘 있음)·읽기·쓰기.
 * 순서(우선순위)는 IoDev 번호 순 = 위 enum. 있는 조건은 기종(config.ini)·꽂은 장치로 정해짐; 처리 함수 안에서 기종 깃발(m->prof.*)을 보지 않는다
 * (5.2.88: 내장 장치는 dev_on 표로). 바뀌면 msx_io_rebuild (다음 접근 때 사슬을 다시 만듦) */
#define IOP(n, e) static int ip_##n(int p) { (void)p; return (e); }
#define IOS(n, e) static int is_##n(const MSX *m) { (void)m; return (e); }
IOP(net, 1)                                                                  /* 기준 포트를 설정으로 바꿀 수 있어 모든 포트에서 확인 */
IOP(v9990, (p & 0xF0) == 0x60)
IOP(mat, (p & 0xF0) == 0x40)
IOP(vhd, p >= 0x3A && p <= 0x3F)
IOP(beer, (p & 0xFC) == 0x30)
IOP(gouda, (p & 0xFC) == 0x34 && (p & 3) != 3)
IOP(gide, (p & 0xF0) == 0x60 && (p & 0x0C))
IOP(fdc63, (p & 0xF8) == 0xD0)
IOP(s1990, p == 0xE4 || p == 0xE5)
IOP(trtimer, p == 0xE6 || p == 0xE7)
IOP(f4, p == 0xF4)
IOP(trpause, p == 0xA7)
IOP(pcm, p == 0xA4 || p == 0xA5)
IOP(midi, p == 0xE8 || p == 0xE9 || p == 0xEA || (p >= 0xEC && p <= 0xEF))
IOP(prn, p == 0x90 || p == 0x91)
IOP(vdp, (p & 0xFC) == 0x98)
IOP(opll, p == 0x7C || p == 0x7D)
IOP(zem90, p == 0x77)
IOP(megaram, p == 0x8E)
IOP(psg2, p == 0x10 || p == 0x11 || p == 0x12)
IOP(y8950, p == 0xC0 || p == 0xC1)
IOP(opl4, p == 0x7E || p == 0x7F || (p >= 0xC4 && p <= 0xC7))
IOP(rtc, p == 0xB4 || p == 0xB5)
IOP(kanji, p >= 0xD8 && p <= 0xDB)
IOP(sysid, p == 0x40 || p == 0x41 || p == 0x42)
IOP(psg, p >= 0xA0 && p <= 0xA2)
IOP(ppi, p >= 0xA8 && p <= 0xAB)
IOP(mapper, p >= 0xFC)
IOP(hbi55, (p & 0xFC) == 0xB0)                                               /* 5.2.45 Sony HBI-55 (자기 i8255) */
IOP(rs232, (p & 0xF8) == 0x80)                                               /* 5.2.48 RS-232C 카트리지 (NMS1210) */
IOP(mmmidi, p <= 0x01 || p == 0x04 || p == 0x05)                             /* 5.2.56 Music Module MIDI (MC6850) */
IOS(net, m->net.enabled)                                                     /* 5.0.57 네트워크 카트 (프레임 시작에 갱신) */
IOS(v9990, m->v9 != 0)                                                       /* 4.0.29 Gfx9000 을 꽂았을 때 */
IOS(vhd, m->vhd_on)                                                          /* 3.7.1 가상 디스크 카트 */
IOS(beer, m->ide_cart_io && io_cart(m, MAPPER_BEERIDE) >= 0)                 /* 3.4.1 IDE 카트 (ide_cart_io = 프레임 시작에 갱신) */
IOS(gouda, m->ide_cart_io && io_cart(m, MAPPER_GOUDA) >= 0)
IOS(gide, m->ide_cart_io && io_cart(m, MAPPER_GIDE) >= 0)
IOS(zem90, io_cart(m, MAPPER_ZEMINA90) >= 0)                                 /* 3.8.0 */
IOS(megaram, io_cart(m, MAPPER_MEGARAM) >= 0)                                /* 2.16 */
IOS(hbi55, io_cart(m, MAPPER_HBI55) >= 0)                                    /* 5.2.45 */
IOS(rs232, io_cart(m, MAPPER_RS232) >= 0)                                    /* 5.2.48 */
IOS(mmmidi, uart_cart(m, UART_MC6850) >= 0)                                  /* 5.2.56 */
IOS(mat, dev_on(m, MDEV_MATSU))                                              /* 3.4.0 Matsushita 교환 I/O (기종) */
IOS(fdc63, m->fdc_kind == 63)                                                /* 3.4.0 Microsol I/O 포트 FDC */
IOS(s1990, dev_on(m, MDEV_S1990))                                            /* turbo R */
IOS(trtimer, dev_on(m, MDEV_TRTIMER))
IOS(f4, dev_on(m, MDEV_F4))                                                  /* MSX2+·turbo R F4 장치 */
IOS(trpause, dev_on(m, MDEV_TRPAUSE))
IOS(pcm, dev_on(m, MDEV_PCM))
IOS(midi, dev_on(m, MDEV_MIDI))                                              /* 3.3.3 MSX-MIDI (기종 + 내장 MIDI 설정) */
#define IODEV(id, nm, pt, pr, r, w) [id] = { nm, pt, pr, r, w }
static const DeviceOps io_dev[IOD_COUNT] = {
    IODEV(IOD_NET,     "Network card",   ip_net,     is_net,     ior_net,     iow_net),
    IODEV(IOD_V9990,   "V9990",          ip_v9990,   is_v9990,   ior_v9990,   iow_v9990),
    IODEV(IOD_MAT,     "Matsushita",     ip_mat,     is_mat,     ior_mat,     iow_mat),
    IODEV(IOD_VHD,     "Virtual disk",   ip_vhd,     is_vhd,     0,           iow_vhd),
    IODEV(IOD_BEER,    "Beer IDE",       ip_beer,    is_beer,    ior_beer,    iow_beer),
    IODEV(IOD_GOUDA,   "Gouda SCSI",     ip_gouda,   is_gouda,   ior_gouda,   iow_gouda),
    IODEV(IOD_GIDE,    "GIDE",           ip_gide,    is_gide,    ior_gide,    iow_gide),
    IODEV(IOD_FDC63,   "Microsol FDC",   ip_fdc63,   is_fdc63,   ior_fdc63,   iow_fdc63),
    IODEV(IOD_S1990,   "S1990",          ip_s1990,   is_s1990,   ior_s1990,   iow_s1990),
    IODEV(IOD_TRTIMER, "turbo R timer",  ip_trtimer, is_trtimer, ior_trtimer, iow_trtimer),
    IODEV(IOD_F4,      "F4 device",      ip_f4,      is_f4,      ior_f4,      iow_f4),
    IODEV(IOD_TRPAUSE, "turbo R pause",  ip_trpause, is_trpause, ior_trpause, iow_trpause),
    IODEV(IOD_PCM,     "turbo R PCM",    ip_pcm,     is_pcm,     ior_pcm,     iow_pcm),
    IODEV(IOD_MIDI,    "MSX-MIDI",       ip_midi,    is_midi,    ior_midi,    iow_midi),
    IODEV(IOD_PRN,     "Printer",        ip_prn,     0,          ior_prn,     iow_prn),
    IODEV(IOD_VDP,     "VDP",            ip_vdp,     0,          ior_vdp,     iow_vdp),
    IODEV(IOD_OPLL,    "MSX-MUSIC",      ip_opll,    0,          0,           iow_opll),
    IODEV(IOD_ZEM90,   "Zemina 90",      ip_zem90,   is_zem90,   0,           iow_zem90),
    IODEV(IOD_MEGARAM, "MegaRAM",        ip_megaram, is_megaram, ior_megaram, iow_megaram),
    IODEV(IOD_PSG2,    "PSG 2",          ip_psg2,    0,          ior_psg2,    iow_psg2),
    IODEV(IOD_Y8950,   "MSX-AUDIO",      ip_y8950,   0,          ior_y8950,   iow_y8950),
    IODEV(IOD_OPL4,    "MoonSound",      ip_opl4,    0,          ior_opl4,    iow_opl4),
    IODEV(IOD_RTC,     "RTC",            ip_rtc,     0,          ior_rtc,     iow_rtc),
    IODEV(IOD_KANJI,   "Kanji ROM",      ip_kanji,   0,          ior_kanji,   iow_kanji),
    IODEV(IOD_SYSID,   "System ID",      ip_sysid,   0,          ior_sysid,   iow_sysid),
    IODEV(IOD_PSG,     "PSG",            ip_psg,     0,          ior_psg,     iow_psg),
    IODEV(IOD_PPI,     "PPI",            ip_ppi,     0,          ior_ppi,     iow_ppi),
    IODEV(IOD_MAPPER,  "Mapper RAM",     ip_mapper,  0,          ior_mapper,  iow_mapper),
    IODEV(IOD_HBI55,   "HBI-55",         ip_hbi55,   is_hbi55,   ior_hbi55,   iow_hbi55),
    IODEV(IOD_RS232,   "RS-232C",        ip_rs232,   is_rs232,   ior_rs232,   iow_rs232),
    IODEV(IOD_MMMIDI,  "Music Module MIDI", ip_mmmidi, is_mmmidi, ior_mmmidi, iow_mmmidi),
};
int msx_io_dev_count(void) { return IOD_COUNT; }
const char *msx_io_dev_name(int d) { return d > 0 && d < IOD_COUNT && io_dev[d].name ? io_dev[d].name : ""; }

static void io_chain_build(MSX *m)                     /* 기계마다 (있는 장치만) */
{
    for (int p = 0; p < 256; p++) {
        int k = 0;
        for (int d = 1; d < IOD_COUNT && k < MSX_IO_CHAIN; d++) if (io_dev[d].ports(p) && (!io_dev[d].present || io_dev[d].present(m))) m->io_chain[p][k++] = (uint8_t)d;
        for (; k < MSX_IO_CHAIN; k++) m->io_chain[p][k] = 0;
    }
    m->io_built = 1;
}
void msx_io_rebuild(MSX *m) { m->io_built = 0; }          /* 장치가 생기거나 빠지면: 다음 I/O 접근 때 사슬을 다시 만듦 */
int msx_io_owners(MSX *m, int port, uint8_t out[MSX_IO_CHAIN])  /* 시험·진단: 이 포트의 사슬 */
{
    if (!m->io_built) io_chain_build(m);
    int n = 0;
    for (int k = 0; k < MSX_IO_CHAIN && m->io_chain[port & 0xFF][k]; k++) out[n++] = m->io_chain[port & 0xFF][k];
    return n;
}

/* I/O 읽기·쓰기: CPU IN/OUT → 포트 아래 8비트 → 그 포트의 장치 사슬(io_chain[포트]) 을 차례로 →
 * 처음 "내 것"(1) 이라 답한 장치에서 끝. 예: 98h/99h VDP, A0h~A2h PSG, A8h~ABh PPI(슬롯·키보드), FCh~FFh 매퍼 RAM.
 * 사슬은 기계를 만들거나 장치를 꽂고 뺄 때 msx_io_rebuild() 로 다시 만든다 (포트마다 if 를 훑지 않음). */
static uint8_t io_read(void *ctx, uint16_t port)
{
    MSX *m = ctx;
    int p = port & 0xFF;
#ifdef IO_TRACE2
    if (p == 0x7E || p == 0x7F || (p >= 0xC4 && p <= 0xC7)) fprintf(stderr, "R %02X pc %04X f %llu\n", p, m->cpu.pc, (unsigned long long)m->frame);
#endif
#ifdef IO_TRACE
    if (p == 0xB5 || (p >= 0x40 && p <= 0x4F)) fprintf(stderr, "R %02X sel %X mode %X pc %04X f %llu\n", p, m->rtc_sel, m->rtc_mode, m->cpu.pc, (unsigned long long)m->frame);
#endif
    if (!m->io_built) io_chain_build(m);                /* msx_setup 을 거치지 않은 시험 기계도 */
    const uint8_t *ch = m->io_chain[p];
    for (int k = 0; k < MSX_IO_CHAIN && ch[k]; k++) {
        int (*f)(MSX *, int, uint8_t *) = io_dev[ch[k]].rd;
        uint8_t v;
        if (f && f(m, p, &v)) return v;
    }
    return 0xFF;
}

static void io_write(void *ctx, uint16_t port, uint8_t v)
{
    MSX *m = ctx;
    int p = port & 0xFF;
#ifdef IO_TRACE2
    if (p == 0x7E || p == 0x7F || (p >= 0xC4 && p <= 0xC7)) fprintf(stderr, "W %02X=%02X pc %04X f %llu\n", p, v, m->cpu.pc, (unsigned long long)m->frame);
#endif
#ifdef IO_TRACE
    if (p == 0xB4 || p == 0xB5 || (p >= 0x40 && p <= 0x4F)) fprintf(stderr, "W %02X=%02X sel %X mode %X pc %04X f %llu\n", p, v, m->rtc_sel, m->rtc_mode, m->cpu.pc, (unsigned long long)m->frame);
#endif
    if (!m->io_built) io_chain_build(m);
    const uint8_t *ch = m->io_chain[p];
    for (int k = 0; k < MSX_IO_CHAIN && ch[k]; k++) {
        int (*f)(MSX *, int, uint8_t) = io_dev[ch[k]].wr;
        if (f && f(m, p, v)) return;
    }
}

/* ------------------------------------------------------------------ */
/* 초기화                                                               */
/* ------------------------------------------------------------------ */
const char *msx_error(int code)
{
    switch (code) {
    case 0: return "ok";
    case MSX_ERR_ROM_MISSING: return "ROM image missing for a ROM region";
    case MSX_ERR_ROM_SIZE: return "ROM size does not fit its region";
    case MSX_ERR_PROFILE: return "invalid machine profile";
    default: return "unknown error";
    }
}

/* 3.6.8: 기종 내장 카트리지 자리 (MSX_INT_CART부터 MSX_INT_N개) */
static int int_cart_alloc(MSX *m)
{
    for (int n = MSX_INT_CART; n < MSX_CART_ALL; n++) if (m->cart_slot[n] < 0 && !m->cart[n].present) return n;
    return -1;
}

#include "regions.inc"                                  /* 5.2.89 구조 개편 5단계: 기종 영역 종류마다 만들기 함수 (표 region_ops) */

int msx_setup(MSX *m, const MachineProfile *prof, const RomImage roms[MAX_REGIONS])
{
    memset(m, 0, sizeof *m);
    msx_snd_epoch++;                                    /* 기종 켜기 */
    sched_init(&m->sched);                              /* 5.2.0 */
    /* I/O 사슬은 io_built = 0 (memset) → 처음 I/O 때 이 기계의 장치로 만듦 */
    m->mouse_port = -1;
    memset(ff_block, 0xFF, sizeof ff_block);
    m->prof = *prof;
    for (int c = 0; c < MSX_CH_N; c++) m->ch_vol[c] = msx_ch_info[c].def_vol;   /* 채널 표의 기본 음량 (드라이브·소프트 MIDI 는 0) */
    m->ds_rng = 1;
    m->r800_pct = 100;
    int v9938 = prof->vdp[0] == 'V';
    netcard_init(&m->net, NETCARD_DEFAULT_BASE);          /* 5.0.57 */
    if (msx_net_host) netcard_set_host(&m->net, msx_net_host);
    vdp_init(&m->vdp, !strcmp(prof->vdp, "V9958") ? 2 : v9938 ? 1 : 0);
    m->vdp.progressive = msx_vdp_cmd_timing;
    m->rtc_on = v9938 || prof->cmos;
    m->s1985 = prof->s1985;
    for (int n = 0; n < MSX_CART_SLOTS; n++) {
        if (n >= 2) {                                   /* 3.3.1 MENU3-07: 카트리지 C·D (기종 설정에 있을 때) */
            int es = prof->ext_slot[n];
            m->cart_slot[n] = es >= 0 && es <= 3 ? es : -1;
            m->cart_ss[n] = es >= 0 && es <= 3 && prof->subslotted[es] ? prof->ext_ss[n] & 3 : 0;
            continue;
        }
        int es = prof->ext_slot[n];
        m->cart_slot[n] = (es >= 0 && es <= 3) ? es : (n ? 2 : 1);
    }
    for (int n = MSX_INT_CART; n < MSX_CART_ALL; n++) { m->cart_slot[n] = -1; m->cart_ss[n] = 0; memset(&m->cart[n], 0, sizeof m->cart[n]); }   /* 3.6.8: 내장 자리 여럿 */
    m->scr.int_pool_used = 0;
    memset(&m->fw, 0, sizeof m->fw); m->fw.ps = m->fw.bun_ps = m->fw.bun_ss = -1;   /* 3.4.0 */
    memset(m->fw.sram, 0xFF, sizeof m->fw.sram); memset(m->fw.mat_sram, 0xFF, sizeof m->fw.mat_sram);   /* 처음 SRAM은 FF (openMSX와 같음) */
    if (prof->region_count <= 0 || prof->region_count > MAX_REGIONS) return MSX_ERR_PROFILE;

    for (int s = 0; s < 4; s++)
        for (int ss = 0; ss < 4; ss++)
            for (int b = 0; b < 8; b++) {
                m->slot_rd[s][ss][b] = ff_block;
                m->slot_wr[s][ss][b] = discard_block;
            }

    for (int i = 0; i < prof->region_count; i++) {
        const ProfileRegion *r = &prof->region[i];
        if (r->type != REGION_KANJI && r->type != REGION_JISYO && r->type != REGION_ROM_DOS2 && (r->slot > 3 || r->subslot > 3 || r->block_count < 1 || r->start_block + r->block_count > 8))
            return MSX_ERR_PROFILE;
        int e = region_ops[r->type > 0 && r->type < REGION_TYPES ? r->type : 0].setup(m, i, r, &roms[i]);   /* 영역 부품 표 (src/regions.inc) */
        if (e) return e;
    }

    z80_init(&m->cpu);
    m->cpu.ctx = m;
    m->cpu.mem_read = mem_read;
    m->cpu.mem_write = mem_write;
    m->cpu.io_read = io_read;
    m->cpu.io_write = io_write;
    m->cpu.m1_wait = 1;                 /* MSX M1 대기 */
    m->fdc.ndrives = prof->fdc_count == 1 ? 1 : 2;      /* 3.2.0: config.ini [FDC] Count */
    msx_audio_enable(m, prof->y8950);   /* 2.16: 내장 MSX-AUDIO */
    msx_moonsound_enable(m, prof->moonsound);   /* 3.1.0: 내장 MoonSound (웨이브 롬은 machine_load가 읽어 둠) */
    msx_joyrex_enable(m, prof->joyrex);           /* 3.6.8: 내장 Joyrex PSG */
    msx_reset(m);
    return 0;
}

static void reset_common(MSX *m)
{
    m->ide.slave = &m->ide_slave;                       /* 3.4.3 */
    ide_reset(&m->ide);
    wd_reset(&m->scsi);                                 /* 3.4.1 */
    spc_reset(&m->spc, 1);                              /* 3.9.14 */
    opm_reset(&m->opm); m->opm_t = m->cpu.cycles; m->sfg_kbd = 0; m->sfg_vec = 0xFF;
    opll_update_on(m);
    fdc765_reset(&m->fdc765);
    m->s1990_reg = 0; m->s1990_cpu = 0x60; m->cpu.r800 = 0;            /* Z80, ROM 모드로 시작 */
    m->trtimer_base = m->cpu.cycles;
    m->f4_status = dev_on(m, MDEV_F4_INV) ? 0xFF : 0x00;
    m->pcm_ctrl = 0; m->pcm_da = 0x80; m->pcm_n = 0;
    fdc_reset(&m->fdc);
    m->dos2_bank = 0;
    dos2_map(m);
    m->mapper_reg[0] = 3; m->mapper_reg[1] = 2; m->mapper_reg[2] = 1; m->mapper_reg[3] = 0;
    mapper_map(m);
    fw_reset(m);                                        /* 3.4.0 */
    z80_reset(&m->cpu); m->cpu.halt_ei = 1;
    vdp_reset(&m->vdp);
    m->primary = 0;
    memset(m->subslot, 0, sizeof m->subslot);
    m->ppi_c = 0;
    memset(m->kbd, 0xFF, sizeof m->kbd);
    psg_reset(&m->psg, m->cpu.cycles);
    psg_reset(&m->psg2, m->cpu.cycles);
    m->frame = 0;
    m->line_budget = 0;
    m->chput_n = 0;
    for (int n = 0; n < MSX_CART_ALL; n++) if (m->cart[n].present) cart_reset(m, n);
    if (m->audio_on && m->y8950) { OPL_reset(m->y8950); m->y_pos = 0; }
    if (m->ms_on) { opl4_reset(&m->opl4); m->ms_pos = 0; }
    if (m->v9) v9990_reset(m->v9);
    update_all_pages(m);
}

/* 3.6.1: MSX-MIDI가 든 기종(GT)에서 프런트엔드가 내장 MIDI를 끄면 BIOS 002Eh 비트 0(MSX-MIDI 있음)도 지움.
 * 리셋 직후 페이지 0 = 슬롯 0-0 메인 롬 (롬은 파일에서 읽은 버퍼라 고칠 수 있음) */
static void midi_id_patch(MSX *m)
{
    if (!dev_on(m, MDEV_MIDI_HW) || !m->rd[0]) return;
    uint8_t *b = (uint8_t *)(uintptr_t)m->rd[0];
    b[0x2E] = dev_on(m, MDEV_MIDI) ? (uint8_t)(b[0x2E] | 1) : (uint8_t)(b[0x2E] & ~1);
}

/* 소리 채널 표 — 본체에 있는 음원마다 한 줄 (새 칩 = 줄 하나 + 섞기). 이름은 믹서 띠 폭에 따라 긴 → 짧은 */
const MsxChInfo msx_ch_info[MSX_CH_N] = {
    [MSX_CH_PSG]  = { "PSG", "PSG", "PSG", "PSG (AY-3-8910)", 100, 1, 1, 0 },
    [MSX_CH_SCC]  = { "SCC", "SCC", "SCC", "SCC (Konami)", 100, 1, 0, 1 },
    [MSX_CH_MM]   = { "M-M", "MSX-MUSIC", "M-M", "MSX-MUSIC (YM2413)", 100, 1, 0, 2 },
    [MSX_CH_PCM]  = { "PCM", "PCM", "PCM", "PCM (Turbo R)", 100, 1, 0, 3 },
    [MSX_CH_MA]   = { "M-A", "MSX-AUDIO", "M-A", "MSX-AUDIO (Y8950)", 100, 1, 0, 4 },
    [MSX_CH_MS]   = { "M-S", "MoonSound", "MOON", "MoonSound (YMF278B)", 100, 1, 0, 5 },
    [MSX_CH_SFG]  = { "OPP", "OPP", "OPP", "YAMAHA SFG-YM2151(OPM),SFG-YM2164(OPP)", 100, 1, 0, 6 },
    [MSX_CH_IO]   = { "I-O", "I-O", "I-O", "I-O", 0, 1, 1, -1 },          /* 드라이브·테이프 기계 소리 (처음엔 0, 프런트엔드가 정함) */
    [MSX_CH_MIDI] = { "MIDI", "MIDI", "MIDI", "MIDI devices (all plugged in)", 100, 1, 0, -1 },   /* 단자에 꽂힌 MIDI 기계 전체의 마스터 (기계별은 MIDI 믹서) — 기본 100, 좌우 있음 */
    [MSX_CH_PSG2] = { "PSG2", "PSG2", "PSG2", "PSG2 (Joyrex)", 100, 1, 0, 7 },
    [MSX_CH_KEY]  = { "KBD", "KBD", "KBD", "KBD (Key Click)", 100, 0, 1, -1 },        /* 키 클릭 (PSG 소리줄 안). 5.2.13: 띠 이름 KBD (표준 약어, 마스터님 결정) */
};
int msx_scc_count(const MSX *m)
{
    int k = 0;
    for (int n = 0; n < MSX_CART_ALL; n++) if (m->cart[n].present && CART_HAS_SCC(m->cart[n].mapper)) k++;
    return k;
}
int msx_ch_present(const MSX *m, int ch)
{
    switch (ch) {
    case MSX_CH_PSG: case MSX_CH_KEY: return 1;
    case MSX_CH_PSG2: return m->joyrex;                                   /* 3.6.7 Joyrex PSG */
    case MSX_CH_SCC: for (int n = 0; n < MSX_CART_ALL; n++) if (m->cart[n].present && CART_HAS_SCC(m->cart[n].mapper)) return 1; return 0;
    case MSX_CH_MM: return m->opll_on;
    case MSX_CH_MA: return m->audio_on;
    case MSX_CH_PCM: return dev_on(m, MDEV_PCM) || dac_cart(m);                /* 카트리지 DAC 도 */
    case MSX_CH_MS: return m->ms_on;                                      /* 3.1.0 */
    case MSX_CH_SFG: return m->sfg_on;                                    /* 3.4.1 */
    case MSX_CH_MIDI: return dev_on(m, MDEV_MIDI) || m->sfg_on;                   /* MIDI 를 내는 장치 (내장 MSX-MIDI·SFG) */
    case MSX_CH_IO: return m->fdc_kind != 0;                              /* 3.4.3 드라이브 */
    default: return 0;
    }
}
int msx_ch_active(const MSX *m, int ch)
{
    int b = (unsigned)ch < MSX_CH_N ? msx_ch_info[ch].act_bit : -1;
    return b >= 0 && (m->chip_act >> b & 1);
}

uint32_t msx_snd_epoch;                                 /* 리셋·전원·카트리지 빼기 때마다 +1 (믹서 "쓰인 채널" 지움) */
void msx_reset(MSX *m)
{
    msx_snd_epoch++;
    sched_init(&m->sched); m->irq_lines = 0;             /* 리셋하면 예약·인터럽트선 모두 풀림 */
    midi_reset(m);                                      /* 3.4.4 */
    netcard_reset(&m->net);                             /* 5.0.57 */
    for (int i = 0; i < GM_INST_MAX; i++) gmi_reset(i);  /* 3.4.5: 소프트웨어 음원도 모든 음 끔 (5.2.51: 인스턴스 모두) */
    memset(m->ram, 0, sizeof m->ram);
    if (m->mram_size) memset(m->mram, 0, m->mram_size);
    if (m->xram_size && m->xram) memset(m->xram, 0, m->xram_size);
    reset_common(m);
    midi_id_patch(m);
}

void msx_soft_reset(MSX *m)
{
    reset_common(m);
    midi_id_patch(m);
}

void msx_key(MSX *m, int row, int bit, int pressed)
{
    if (row < 0 || row > 10 || bit < 0 || bit > 7) return;
    if (pressed) m->kbd[row] &= (uint8_t)~(1 << bit);
    else m->kbd[row] |= (uint8_t)(1 << bit);
}

/* ------------------------------------------------------------------ */
/* 실행                                                                 */
/* ------------------------------------------------------------------ */
/* 음원 사용 여부 (상태 아이콘용): 레지스터로 판단 — 믹서 음량·소리 끔과 무관 */
static uint8_t chip_activity(MSX *m)
{
    uint8_t act = 0;
    const PSG *p = &m->psg;
    uint8_t env = (uint8_t)(p->env_step ^ p->env_attack) & 0x0F;
    for (int c = 0; c < 3; c++) {
        uint8_t a = p->reg[8 + c], amp = (a & 0x10) ? env : (a & 0x0F);
        int on = !((p->reg[7] >> c) & 1) || !((p->reg[7] >> (c + 3)) & 1);   /* 톤 또는 노이즈가 켜짐 */
        if (on && amp) act |= 1;
    }
    if (psg2_on(m)) {                                   /* 3.6.7: Joyrex PSG = 128 (PSG2 아이콘), 4.0.129: Yamanooto */
        const PSG *q = &m->psg2;
        uint8_t e2 = (uint8_t)(q->env_step ^ q->env_attack) & 0x0F;
        for (int c = 0; c < 3; c++) {
            uint8_t a = q->reg[8 + c], amp = (a & 0x10) ? e2 : (a & 0x0F);
            if ((!((q->reg[7] >> c) & 1) || !((q->reg[7] >> (c + 3)) & 1)) && amp) act |= 128;
        }
    }
    m->scc2_act = 0;
    for (int n = 0, k = 0; n < MSX_CART_ALL; n++) {       /* 첫 SCC = 비트 2 (SCC1), 둘째 이후 = scc2_act (SCC2) */
        const Cartridge *c = &m->cart[n];
        if (!c->present || !CART_HAS_SCC(c->mapper)) continue;
        int on = 0;
        for (int ch = 0; ch < 5; ch++) if (((c->scc.enable >> ch) & 1) && (c->scc.vol[ch] & 0x0F)) on = 1;
        if (on) { if (k == 0) act |= 2; else m->scc2_act = 1; }
        k++;
    }
    if (m->opll_on && m->opll) {
        const uint8_t *r = m->opll->reg;
        int nch = (r[0x0E] & 0x20) ? 6 : 9;
        for (int ch = 0; ch < nch; ch++) if ((r[0x20 + ch] & 0x10) && (r[0x30 + ch] & 0x0F) != 0x0F) act |= 4;
        if ((r[0x0E] & 0x20) && (r[0x0E] & 0x1F)) act |= 4;
    }
    if (m->audio_on && m->y8950) {                       /* MSX-AUDIO: FM 키 켜짐 또는 ADPCM 재생 */
        const uint8_t *r = m->y8950->reg;
        for (int ch = 0; ch < 9; ch++) if (r[0xB0 + ch] & 0x20) act |= 16;
        if ((r[0xBD] & 0x20) && (r[0xBD] & 0x1F)) act |= 16;
        if (r[0x07] & 0x80) act |= 16;
    }
    if (m->ms_on && opl4_active(&m->opl4)) act |= 32;   /* 3.1.0 MoonSound */
    if (m->sfg_on && opm_active(&m->opm)) act |= 64;   /* 3.4.1 SFG */
    if ((dev_on(m, MDEV_PCM) || dac_cart(m)) && m->pcm_n) {
        for (int i = 0; i < m->pcm_n; i++) if (m->pcm_v[i] != m->pcm_v[0] || m->pcm_v[i] != m->pcm_last) { act |= 8; break; }
    }
    return act;
}

/* 8.0.25 다중CPU: MIDI 소프트웨어 음원(GM·MT-32) 그리기 — 한 프레임의 MIDI 바이트를 보낸 시각에 맞춰 넣으며 합성.
 * 다른 음원(PSG·SCC·FM…)을 메인이 섞는 동안 일꾼 코어가 이것을 하고, 섞기 끝에서 메인이 기다려 더한다 (같은 프레임, 늦어짐 없음).
 * 그동안 음원(gmsynth·midibus)은 이 일만 만진다 (메인은 다른 음원만). 결과·계기는 혼자 할 때와 같다 */
static int32_t ml[PSG_FRAME_MAX], mr[PSG_FRAME_MAX], dl[PSG_FRAME_MAX], dr[PSG_FRAME_MAX];
typedef struct { MSX *m; int n, def_on, soft; uint64_t cyc; } MidiJob;
static void midi_mix_job(void *arg)
{
        MidiJob *mj = (MidiJob *)arg; MSX *m = mj->m; int n = mj->n;
        int def_on = midibus_def(); mj->def_on = def_on;                     /* GT 기본 음원은 GTMIDI 띠 (음량은 Note On 세기로 이미 들어감) — MIDI 띠 음량을 곱하지 않음 */
        if (def_on) for (int i = 0; i < n; i++) { dl[i] = 0; dr[i] = 0; }
        uint32_t frame_t = (uint32_t)(m->cpu.cycles - m->frame_start);
        int at = 0, soft = midibus_any_soft_on() && n > 0; mj->soft = soft;
        for (int d = 0; d < MIDI_DEV_MAX; d++) midi_bus.dev[d].lvl = 0;
        for (int j = 0; j <= m->midi_n; j++) {
            uint32_t tj = j < m->midi_n ? (m->midi_t[j] & ~MIDI_T_SRC) : 0;
            int to = j < m->midi_n ? (frame_t ? (int)((uint64_t)tj * (uint32_t)n / frame_t) : 0) : n;
            if (to > n) to = n;
            if (soft && to > at) {                      /* 꽂힌 소프트웨어 기계(음원 인스턴스) 모두 더함. 5.2.52: 기계마다 음량·좌우·끄기·계기 (믹서의 기계 띠) */
                static int32_t tl[PSG_FRAME_MAX], tr[PSG_FRAME_MAX];
                for (int i = at; i < to; i++) { ml[i] = 0; mr[i] = 0; }
                if (midibus_def()) {                    /* 꽂힌 기계 없음 + GT 내장 MIDI = 기본 음원 */
                    { uint64_t t0_ = synth_tsc(); gmi_render(GM_INST_GT, tl, tr, to - at); mj->cyc += synth_tsc() - t0_; }
                    for (int i = 0; i < to - at; i++) { dl[at + i] += tl[i]; dr[at + i] += tr[i]; }
                }
                for (int d = 0; d < midi_bus.n; d++) {
                    MidiDev *dv = &midi_bus.dev[d];
                    if (!dv->active || gmi_mode(d) == GM_OFF) continue;
                    { uint64_t t0_ = synth_tsc(); gmi_render(d, tl, tr, to - at); mj->cyc += synth_tsc() - t0_; }
                    int g = dv->mute ? 0 : dv->vol, p = dv->pan;
                    int gl2 = g * (100 - (p > 0 ? p : 0)) / 100, gr2 = g * (100 + (p < 0 ? p : 0)) / 100;
                    for (int i = 0; i < to - at; i++) {
                        int32_t l = tl[i] * gl2 / 100, r = tr[i] * gr2 / 100, mo = (l + r) / 2;
                        ml[at + i] += l; mr[at + i] += r;
                        uint32_t a_ = (uint32_t)(mo < 0 ? -mo : mo); if (a_ > dv->lvl) dv->lvl = (uint16_t)(a_ > 65535 ? 65535 : a_);
                    }
                }
                at = to;
            }
            if (j < m->midi_n) midibus_feed(m->midi_q[j], tj, (m->midi_t[j] & MIDI_T_SFG) ? MDP_SFG : (m->midi_t[j] & MIDI_T_MM) ? MDP_MM : MDP_MSXMIDI);
        }
}
const void *msx_midi_job_id = (const void *)midi_mix_job;   /* 8.0.34: 일꾼이 이 함수를 돌리면 MIDI 합성 시간으로 셈 (시스템 보기 > 성능) */
int (*msx_job_post)(void (*fn)(void *), void *arg);    /* 8.0.25: 일꾼 코어에 일 올리기 (없거나 -1 이면 메인이 직접) — 플랫폼이 정함 */
int (*msx_job_wait)(int h);                             /* 올린 일이 끝날 때까지 기다림. 시간 초과면 0 */
int msx_mp_midi = 1;                                    /* ini MpMidi */
uint32_t msx_mp_midi_n;                                 /* 일꾼이 MIDI 를 그린 프레임 수 (DIAG) */

void msx_run_frame(MSX *m)
{
    m->psg.vol_tone = (uint8_t)m->ch_vol[MSX_CH_PSG];   /* 채널 음량 → PSG (리셋 뒤에도 믹서 값 그대로) */
    m->psg.vol_click = (uint8_t)m->ch_vol[MSX_CH_KEY];
    m->psg.buf_n = 0;
    m->psg2.buf_n = 0;
    m->frame_start = m->cpu.cycles;
    m->pcm_n = 0;
    m->ide_compat = 0;
    for (int n = 0; n < MSX_CART_ALL; n++) if (m->cart[n].present && (m->cart[n].mapper == MAPPER_SUNRISE || m->cart[n].mapper == MAPPER_VHD)) m->ide_compat = 1;
    { int k = io_cart(m, MAPPER_SFG), sf = k >= 0; if (sf && !m->sfg_on) m->opm_t = m->cpu.cycles; m->sfg_on = sf;   /* 3.4.1 */
      if (sf) m->opm.opp = m->cart[k].size != 16384u; }   /* 3.7.0 YM64: SFG-01(16 KiB) = YM2151(OPM), 그 밖(SFG-05) = YM2164(OPP) */
    m->vhd_on = io_cart(m, MAPPER_VHD) >= 0;          /* 3.7.1 */
    m->net.enabled = io_cart(m, MAPPER_NET) >= 0;     /* 5.0.57 */
    m->ide_cart_io = io_cart(m, MAPPER_BEERIDE) >= 0 || io_cart(m, MAPPER_GIDE) >= 0 || io_cart(m, MAPPER_GOUDA) >= 0;   /* 3.4.1 */
    { uint8_t sig = (uint8_t)((m->vhd_on ? 1 : 0) | (m->net.enabled ? 2 : 0) | (m->ide_cart_io ? 4 : 0));   /* 카트 장치가 바뀌면 I/O 사슬 다시 */
      if (sig != m->io_sig) { m->io_sig = sig; msx_io_rebuild(m); } }
    if (!m->ide_compat) m->chput_n = 0;
    m->midi_n = 0;                                      /* 3.4.5: 지난 프레임 MIDI는 프런트엔드가 이미 읽었음 */
    netcard_tick(&m->net, m->cpu.cycles);                /* 5.0.57 */
    msx_run_frame_inner(m);
    m->chip_act = chip_activity(m);
    /* 3.3.3 SND3-01·02: 음원마다 모노 합(psg.buf, 녹음·영상용), 좌우(out_l·out_r), 최대 진폭(lvl) */
    static int32_t am[PSG_FRAME_MAX], al[PSG_FRAME_MAX], ar[PSG_FRAME_MAX];
    static int16_t tmp[PSG_FRAME_MAX];
    int n = m->psg.buf_n;
    if (n > PSG_FRAME_MAX) n = PSG_FRAME_MAX;
    static MidiJob mj; int mjh = -1;                    /* 8.0.25: MIDI 음원은 먼저 일꾼에게 (다른 음원을 섞는 동안) */
    mj.m = m; mj.n = n; mj.cyc = 0;
    if (msx_mp_midi && msx_job_post && msx_job_wait && midibus_any_soft_on() && n > 0) mjh = msx_job_post(midi_mix_job, &mj);
    if (mjh < 0) midi_mix_job(&mj);
    int gl[MSX_CH_N], gr[MSX_CH_N];
    for (int c = 0; c < MSX_CH_N; c++) {
        int p = m->stereo ? m->pan[c] : 0;
        if (p > 100) p = 100;
        if (p < -100) p = -100;
        gl[c] = 256 * (100 - (p > 0 ? p : 0)) / 100;
        gr[c] = 256 * (100 + (p < 0 ? p : 0)) / 100;
        m->lvl[c] = 0;
    }
#define ADDC(i, v, c) do { int32_t v_ = (v); am[i] += v_; al[i] += v_ * gl[c] >> 8; ar[i] += v_ * gr[c] >> 8; \
        uint32_t a_ = (uint32_t)(v_ < 0 ? -v_ : v_); if (a_ > m->lvl[c]) m->lvl[c] = (uint16_t)(a_ > 65535 ? 65535 : a_); } while (0)
    for (int i = 0; i < n; i++) { am[i] = al[i] = ar[i] = 0; ADDC(i, m->psg.buf[i] * 1196 / 1000, MSX_CH_PSG); }   /* openMSX 와 같은 크기 (같은 SOUND 440 Hz 시험: 교류분 3479 → 4160) */
    if (!m->psg.tone_seen) m->lvl[MSX_CH_PSG] = 0;     /* 3.7.2: 키 클릭만이면 PSG 계기·아이콘 꺼짐 */
    m->psg.tone_seen = 0;
    if ((dev_on(m, MDEV_PCM) || dac_cart(m)) && m->ch_vol[MSX_CH_PCM] > 0 && n) {   /* turbo R PCM (5.2.47: 카트리지 DAC 도): 프레임 안 DA 값을 시간대로 */
        uint32_t frame_t = (uint32_t)(m->cpu.cycles - m->frame_start), k = 0;
        uint8_t v = m->pcm_n ? m->pcm_last : m->pcm_da;
        for (int i = 0; i < n; i++) {
            uint32_t t = frame_t ? (uint32_t)((uint64_t)i * frame_t / (uint32_t)n) : 0;
            while (k < m->pcm_n && m->pcm_t[k] <= t) v = m->pcm_v[k++];
            ADDC(i, ((int)v - 128) * 55 * m->ch_vol[MSX_CH_PCM] / 100, MSX_CH_PCM);
        }
    }
    m->pcm_last = m->pcm_da;
    if (psg2_on(m)) {                                   /* 2.16: Joyrex PSG (3.6.7: 믹서 PSG2 채널), 4.0.129: Yamanooto 보조 PSG */
        psg_sync(&m->psg2, m->cpu.cycles);
        m->psg2.vol_tone = (uint8_t)m->ch_vol[MSX_CH_PSG2]; m->psg2.vol_click = 0;
        int nn = m->psg2.buf_n < n ? m->psg2.buf_n : n;
        for (int i = 0; i < nn; i++) ADDC(i, m->psg2.buf[i], MSX_CH_PSG2);
    }
    m->scc1_lvl = m->scc2_lvl = 0;
    for (int c = 0, sk = 0; c < MSX_CART_ALL; c++)
        if (m->cart[c].present && CART_HAS_SCC(m->cart[c].mapper)) {
            uint16_t lv0 = m->lvl[MSX_CH_SCC]; m->lvl[MSX_CH_SCC] = 0;   /* 이 칩만의 크기를 재고 */
            memset(tmp, 0, sizeof tmp[0] * (size_t)n);
            if (m->cart[c].scc.st && m->stereo) {           /* 4.0.131: Yamanooto 채널별 좌우 */
                static int16_t tl[PSG_FRAME_MAX], tr[PSG_FRAME_MAX];
                scc_mix_st(&m->cart[c].scc, tmp, tl, tr, n, m->ch_vol[MSX_CH_SCC]);
                for (int i = 0; i < n; i++) {
                    int32_t v_ = tmp[i]; am[i] += v_;
                    al[i] += tl[i] * gl[MSX_CH_SCC] >> 8; ar[i] += tr[i] * gr[MSX_CH_SCC] >> 8;
                    uint32_t a_ = (uint32_t)(v_ < 0 ? -v_ : v_); if (a_ > m->lvl[MSX_CH_SCC]) m->lvl[MSX_CH_SCC] = (uint16_t)(a_ > 65535 ? 65535 : a_);
                }
                { uint16_t cl_ = m->lvl[MSX_CH_SCC]; if (sk == 0) m->scc1_lvl = cl_; else if (cl_ > m->scc2_lvl) m->scc2_lvl = cl_; if (lv0 > m->lvl[MSX_CH_SCC]) m->lvl[MSX_CH_SCC] = lv0; sk++; }
                continue;
            }
            scc_mix(&m->cart[c].scc, tmp, n, m->ch_vol[MSX_CH_SCC]);
            for (int i = 0; i < n; i++) ADDC(i, tmp[i], MSX_CH_SCC);
            { uint16_t cl_ = m->lvl[MSX_CH_SCC]; if (sk == 0) m->scc1_lvl = cl_; else if (cl_ > m->scc2_lvl) m->scc2_lvl = cl_; if (lv0 > m->lvl[MSX_CH_SCC]) m->lvl[MSX_CH_SCC] = lv0; sk++; }
        }
    if (m->audio_on) {                                  /* MSX-AUDIO: 프레임 끝까지 만들고 48 kHz로 (믹서 M-A 음량) */
        y8950_sync(m);
        if (m->ch_vol[MSX_CH_MA] > 0 && m->y_pos && n)
            for (int i = 0; i < n; i++) {                /* 가까운 표본 고르기 → 선형 보간 (표본이 건너뛰어 딸깍거리던 것) */
                uint32_t num = (uint32_t)i * m->y_pos; uint32_t ix = (uint32_t)(num / (uint32_t)n), fr = (uint32_t)(num % (uint32_t)n), nx = ix + 1 < m->y_pos ? ix + 1 : ix;
                int32_t v = m->y_buf[ix] + ((m->y_buf[nx] - m->y_buf[ix]) * (int32_t)fr / n);
                ADDC(i, v * 2 * m->ch_vol[MSX_CH_MA] / 100, MSX_CH_MA);
            }
        m->y_pos = 0;
    }
    if (m->ms_on) {                                     /* 3.1.0 MoonSound: 프레임 끝까지 만들고 섞음 (믹서 M-S 음량) */
        ms_sync(m);
        while (m->ms_pos < n && m->ms_pos < sizeof m->ms_buf / sizeof m->ms_buf[0]) {
            int v = opl4_calc48(&m->opl4);
            m->ms_buf[m->ms_pos++] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        if (m->ch_vol[MSX_CH_MS] > 0 && m->ms_pos && n)
            for (int i = 0; i < n; i++) ADDC(i, m->ms_buf[(uint32_t)i * m->ms_pos / (uint32_t)n] * m->ch_vol[MSX_CH_MS] / 100, MSX_CH_MS);
        m->ms_pos = 0;
    }
    if (m->sfg_on && n) {                               /* 3.4.1 SFG (YM64: YM2164/YM2151) 55,930 Hz → 48 kHz, 좌우는 RL 비트 */
        int vs = m->ch_vol[MSX_CH_SFG];
        for (int i = 0; i < n; i++) {
            m->opm_acc += 3579545u / 64;
            while (m->opm_acc >= 48000u) { m->opm_acc -= 48000u; m->opm_pl = m->opm_l; m->opm_pr = m->opm_r; opm_calc(&m->opm, &m->opm_l, &m->opm_r); }   /* 선형 보간 */
            if (vs <= 0) continue;
            int32_t il = m->opm_pl + ((m->opm_l - m->opm_pl) * (int32_t)(m->opm_acc >> 5) / 1500), ir = m->opm_pr + ((m->opm_r - m->opm_pr) * (int32_t)(m->opm_acc >> 5) / 1500);
            int32_t l = il * vs * 3 / 1000, r = ir * vs * 3 / 1000, mo = (l + r) / 2;
            am[i] += mo; al[i] += l * gl[MSX_CH_SFG] >> 8; ar[i] += r * gr[MSX_CH_SFG] >> 8;
            uint32_t a_ = (uint32_t)(mo < 0 ? -mo : mo); if (a_ > m->lvl[MSX_CH_SFG]) m->lvl[MSX_CH_SFG] = (uint16_t)(a_ > 65535 ? 65535 : a_);
        }
    }
    if (m->opll_on && m->ch_vol[MSX_CH_MM] > 0) {                  /* YM2413 49,716 Hz → 48 kHz (정수 누산) */
        for (int i = 0; i < n; i++) {
            m->opll_acc += 3579545u / 72;                  /* 표본을 건너뛰던 방식(약 1.7 kHz 로 딸깍거려 FM 이 앵앵거림) → 앞뒤 표본 선형 보간 */
            while (m->opll_acc >= 48000u) {
                m->opll_acc -= 48000u;
                m->opll_prev = m->opll_last;
                if (m->opll_quiet < 8192u) { m->opll_last = OPLL_calc(m->opll); if (m->opll_last) m->opll_quiet = 0; else m->opll_quiet++; }   /* 4.0.14: 오래 무음이면 계산 생략 */
                else m->opll_last = 0;
            }
            int32_t os = m->opll_prev + ((m->opll_last - m->opll_prev) * (int32_t)(m->opll_acc >> 5) / 1500);
            ADDC(i, os * 363 * m->ch_vol[MSX_CH_MM] / 20000, MSX_CH_MM)   /* openMSX 시험 (443 → 536) 에 맞춰 x1.5 → x1.815 */;
        }
    }
#ifdef PC_TRACE
    if (getenv("DSDBG") && (m->frame % 30) == 0) fprintf(stderr, "f%llu kind %d vol %d motor %d head %d pend %d\n", (unsigned long long)m->frame, m->fdc_kind, m->ch_vol[MSX_CH_IO], m->fdc.motor, m->fdc.drv[0].head, m->ds_pending);
#endif
    if (m->fdc_kind && n) {                             /* 3.4.3 SND3-03: 드라이브 소리 — 모터 윙윙(120·240 Hz + 잡음), 헤드 걸음마다 딸깍 */
        for (int d = 0; d < 2; d++) {
            int h = m->fdc.drv[d].head, dl = h - m->ds_head[d];
            if (dl < 0) dl = -dl;
            if (dl && m->ch_vol[MSX_CH_IO] > 0) m->ds_pending += dl > 90 ? 90 : dl;
            m->ds_head[d] = h;
        }
        int target = m->fdc.motor ? 256 : 0;
        for (int i = 0; i < n; i++) {
            if ((i & 15) == 0) m->ds_motor += m->ds_motor < target ? 2 : m->ds_motor > target ? -2 : 0;   /* 켜고 끌 때 천천히 */
            if (m->ch_vol[MSX_CH_IO] <= 0) continue;
            m->ds_rng = m->ds_rng * 1103515245u + 12345u;
            int32_t nz = (int32_t)((m->ds_rng >> 16) & 0x7FF) - 1024;
            int32_t v = 0;
            if (m->ds_motor) {
                m->ds_phase += 120u * 65536u / 48000u * 65536u;
                uint32_t ph = m->ds_phase >> 26;               /* 64단계 */
                static const int8_t tri[64] = { 0,4,8,12,16,20,24,28,32,36,40,44,48,52,56,60,64,60,56,52,48,44,40,36,32,28,24,20,16,12,8,4,
                                                0,-4,-8,-12,-16,-20,-24,-28,-32,-36,-40,-44,-48,-52,-56,-60,-64,-60,-56,-52,-48,-44,-40,-36,-32,-28,-24,-20,-16,-12,-8,-4 };
                v += (tri[ph] * 6 + tri[(ph * 2) & 63] * 3 + nz / 8) * m->ds_motor >> 8;
            }
            if (m->ds_click > 0) {                      /* 딸깍: 2.5 ms 잡음이 빨리 줄어듦 */
                int k = m->ds_click--;
                v += nz * 5 * k / 120;
            } else if (m->ds_pending && (i % 144) == 0) { m->ds_pending--; m->ds_click = 120; }   /* 3 ms마다 한 걸음 */
            ADDC(i, v * m->ch_vol[MSX_CH_IO] / 100, MSX_CH_IO);
        }
    }
    {   /* 8.0.25: MIDI 음원 결과 섞기 (위에서 일꾼 또는 메인이 그림) */
        if (mjh >= 0) { if (msx_job_wait(mjh)) msx_mp_midi_n++; else { mj.soft = 0; msx_mp_midi = 0; } }   /* 시간 초과: 이 프레임 MIDI 는 무음, 이후 메인 혼자 */
        if (mjh >= 0) msx_synth_wcyc += mj.cyc; else msx_synth_cyc += mj.cyc;   /* 8.0.32: 일꾼이 한 건 메인 시간에서 빼지 않음 */
        int def_on = mj.def_on, soft = mj.soft;
        if (def_on && soft)
            for (int i = 0; i < n; i++) { int32_t l = dl[i] * msx_midi_int_vol / 100, r = dr[i] * msx_midi_int_vol / 100, mo = (l + r) / 2; am[i] += mo; al[i] += l; ar[i] += r; }   /* GTMIDI 음량 */
        int vm = soft && midi_bus.n ? m->ch_vol[MSX_CH_MIDI] : 0;     /* MIDI 띠 = MIDI 믹서의 모든 기계 소리 (GT 내장 단자 기계도). GTMIDI 띠는 GT 내장 단자의 Note On 세기 */
        if (vm > 0)
            for (int i = 0; i < n; i++) {
                int32_t l = ml[i] * vm / 100, r = mr[i] * vm / 100, mo = (l + r) / 2;
                am[i] += mo; al[i] += l * gl[MSX_CH_MIDI] >> 8; ar[i] += r * gr[MSX_CH_MIDI] >> 8;
                uint32_t a_ = (uint32_t)(mo < 0 ? -mo : mo); if (a_ > m->lvl[MSX_CH_MIDI]) m->lvl[MSX_CH_MIDI] = (uint16_t)(a_ > 65535 ? 65535 : a_);
            }
    }
#undef ADDC
    for (int i = 0; i < n; i++) {
        m->psg.buf[i] = (int16_t)(am[i] > 32767 ? 32767 : am[i] < -32768 ? -32768 : am[i]);
        m->out_l[i] = (int16_t)(al[i] > 32767 ? 32767 : al[i] < -32768 ? -32768 : al[i]);
        m->out_r[i] = (int16_t)(ar[i] > 32767 ? 32767 : ar[i] < -32768 ? -32768 : ar[i]);
    }
}

/* ---- 카세트: BIOS 테이프 루틴(0x00E1~0x00F3)을 대신 처리하고 RET ---- */
static const uint8_t cas_header[8] = { 0x1F, 0xA6, 0xDE, 0xBA, 0xCC, 0x13, 0x7D, 0x74 };

void msx_tape_insert(MSX *m, uint8_t *buf, uint32_t size, uint32_t cap)
{
    m->tape = buf; m->tape_size = buf ? size : 0; m->tape_cap = cap; m->tape_pos = 0;
    m->tape_dirty = 0; m->tape_motor = 0;
}

static void tape_put(MSX *m, uint8_t v)
{
    if (m->tape_pos >= m->tape_cap) return;
    m->tape[m->tape_pos++] = v;
    m->tape_size = m->tape_pos;                         /* 쓰면 그 뒤는 지워진 것으로 */
    m->tape_dirty = 1;
}

static uint32_t tape_last_frame;                          /* 4.0.34: 마지막으로 테이프를 읽고 쓴 프레임 (모터가 계속 켜진 채 남는 것 막기) */
static int tape_trap(MSX *m)
{
    uint16_t pc = m->cpu.pc;
    if (pc != 0x00E1 && pc != 0x00E4 && pc != 0x00E7 && pc != 0x00EA && pc != 0x00ED && pc != 0x00F0 && pc != 0x00F3) return 0;
    if ((m->primary & 3) != 0 || (m->prof.subslotted[0] && (m->subslot[0] & 3) != 0)) return 0;   /* 페이지 0이 BIOS일 때만 */
    int carry = 0;
    uint8_t a = m->cpu.af.b.h;
    if (pc != 0x00F3) tape_last_frame = m->frame;
    switch (pc) {
    case 0x00E1: {                                      /* TAPION: 다음 블록 머리표 찾기 */
        uint32_t p = (m->tape_pos + 7) & ~7u;
        carry = 1;
        for (; p + 8 <= m->tape_size; p += 8)
            if (!memcmp(m->tape + p, cas_header, 8)) { m->tape_pos = p + 8; carry = 0; break; }
        m->tape_motor = 1;
        break;
    }
    case 0x00E4:                                        /* TAPIN: 한 바이트 읽기 */
        if (m->tape_pos < m->tape_size) m->cpu.af.b.h = m->tape[m->tape_pos++];
        else carry = 1;
        break;
    case 0x00EA:                                        /* TAPOON: 8바이트 경계까지 채우고 머리표 쓰기 */
        while (m->tape_pos & 7) tape_put(m, 0);
        for (int i = 0; i < 8; i++) tape_put(m, cas_header[i]);
        m->tape_motor = 1;
        carry = m->tape_pos > m->tape_cap - 8;
        break;
    case 0x00ED:                                        /* TAPOUT: 한 바이트 쓰기 */
        tape_put(m, a);
        carry = m->tape_pos >= m->tape_cap;
        break;
    case 0x00E7: case 0x00F0:                           /* TAPIOF / TAPOOF */
        m->tape_motor = 0;
        break;
    case 0x00F3:                                        /* STMOTR: 0 끄기, 1 켜기, FF 바꾸기 */
        m->tape_motor = a == 0xFF ? !m->tape_motor : a != 0;
        if (m->tape_motor) tape_last_frame = m->frame;
        return 0;                                       /* BIOS 원래 코드도 실행 (PPI 모터 비트) */
    }
    m->cpu.af.b.l = (uint8_t)((m->cpu.af.b.l & ~1) | (carry ? 1 : 0));
    uint16_t sp = m->cpu.sp.w;                          /* RET */
    m->cpu.pc = (uint16_t)(mem_read(m, sp) | (mem_read(m, (uint16_t)(sp + 1)) << 8));
    m->cpu.sp.w = (uint16_t)(sp + 2);
    return 1;
}

/* Sunrise IDE BIOS 호환 (2.8): 이 BIOS는 초기화 중 IX에 작업 영역 주소를 둔 채 글자를 찍는다(RST 18h).
 * 대우 등 한글 기종은 H.CHPU 훅이 CALLF(RST 30h)로 한글 ROM을 불러 IX·IY가 바뀌고,
 * 그 결과 헤드·섹터 수를 ROM에서 읽어 드라이브를 못 쓰게 된다("Disk offline"·하드 디스크 부팅 안 됨).
 * Sunrise 카트리지가 꽂혀 있고 그 BIOS(페이지 1)가 H.CHPU 훅(FDA4h)을 부를 때만 IX·IY를 기억했다가,
 * BIOS 코드로 돌아오면(스택이 훅 들어갈 때보다 위) 되돌린다. 한글 훅은 CHPUT을 건너뛰고 바로 돌아가기도 한다. */
static int ide_page1(MSX *m)                    /* 페이지 1에 Sunrise 카트리지가 보이면 1 */
{
    for (int n = 0; n < MSX_CART_ALL; n++) {
        int ps = m->cart_slot[n];
        if (ps < 0 || !m->cart[n].present || (m->cart[n].mapper != MAPPER_SUNRISE && m->cart[n].mapper != MAPPER_VHD)) continue;
        if (((m->primary >> 2) & 3) == ps && (!m->prof.subslotted[ps] || ((m->subslot[ps] >> 2) & 3) == m->cart_ss[n])) return 1;
    }
    return 0;
}

static void ide_chput_guard(MSX *m)
{
    uint16_t pc = m->cpu.pc, sp = m->cpu.sp.w;
    if (m->chput_n) {
        /* Sunrise BIOS 코드(페이지 1)로 돌아왔고 스택이 훅 들어갈 때보다 올라갔으면 IX·IY 되돌림 */
        if (pc >= 0x4000 && pc < 0x8000 && sp > m->chput_sp[m->chput_n - 1] && ide_page1(m)) {
            while (m->chput_n > 1 && sp > m->chput_sp[m->chput_n - 2]) m->chput_n--;
            m->chput_n--;
            m->cpu.ix.w = m->chput_ix[m->chput_n];
            m->cpu.iy.w = m->chput_iy[m->chput_n];
        }
        return;
    }
    if (pc != 0xFDA4 || !ide_page1(m)) return;          /* H.CHPU 훅에 들어갈 때 (RST 18h·CHPUT 모두 여기를 지남) */
    m->chput_sp[0] = sp; m->chput_ix[0] = m->cpu.ix.w; m->chput_iy[0] = m->cpu.iy.w;
    m->chput_n = 1;
}

int msx_is_pal(const MSX *m)
{
    if (m->vsync_force) return m->vsync_force == 2;
    if (!m->vdp.v9938) return dev_on(m, MDEV_PAL_VDP);   /* TMS9929A = PAL 기종 */
    return (m->vdp.reg[9] & 0x02) != 0;                 /* V99x8 R#9 NT: BIOS가 기종에 맞게 씀 */
}
int msx_frame_lines(const MSX *m) { return msx_is_pal(m) ? MSX_LINES_PAL : MSX_LINES_NTSC; }

static void msx_run_frame_inner(MSX *m)
{
    fast_refresh(m);                                    /* 4.0.138: 프레임마다 한 번 더 (카트리지·가상 디스크 변경 반영) */
    vdp_frame_start(&m->vdp);
    sync_int(m);
    int lines = m->vdp.lines, total = msx_frame_lines(m);
    int early_f = 0;
    int vt = m->v9 ? v9990_frame_lines(m->v9) : 0, vl = 0;   /* V9990 은 자기 프레임(PAL 313줄)으로 셈 — MSX 줄에 맞춰 나눠 돌림 */
    for (int line = 0; line < total; line++) {
        if (line == lines) {
            if (early_f) { m->vdp.stat[2] |= 0x40; early_f = 0; }   /* 4.0.129: 명령 도중에 이미 F를 세웠으면 (IN 이 읽어 갔을 수 있음) 다시 세우지 않음 */
            else vdp_frame_end(&m->vdp);
            sync_int(m);
        }
        int vb = m->v9 ? (line + 1) * vt / total : 0;
        if (m->v9) { for (int k = vl; k < vb; k++) v9990_line_start(m->v9, k, vt); sync_int(m); }   /* 4.0.29, 5.2.128 */
        vdp_sync(m);
        if (line < 256) { vdp_line_start(&m->vdp, line); sync_int(m); }   /* 줄 일치(FH)는 표시 영역 밖 줄(212~255)에서도 */
        if (m->sfg_on) { opm_advance(&m->opm, (uint32_t)(m->cpu.cycles - m->opm_t)); m->opm_t = m->cpu.cycles; sync_int(m); }   /* 3.4.1 */
        if (m->ms_on && (m->opl4.fmreg[0x04] & 3)) { ms_sync(m); sync_int(m); }   /* 3.4.4: OPL4 타이머가 돌면 줄마다 IRQ 확인 */
        if (dev_on(m, MDEV_MIDI) && (m->pit[2].edge || m->midi_rx_r != m->midi_rx_w)) { midi_tick(m); sync_int(m); }   /* 3.4.4: MSX-MIDI */
        { int rn = rs232_cart(m); if (rn >= 0) { Rs232Dev *r = &m->cart[rn].rs; if (r->pit[2].edge || r->rx_r != r->rx_w) { rs232_tick(m, r); sync_int(m); } } }   /* 5.2.48 */
        for (int u = UART_YM2148; u <= UART_MC6850; u++) { int un = uart_cart(m, u); if (un >= 0) { Rs232Dev *r = &m->cart[un].rs; if (r->rx_r != r->rx_w && !r->rxrdy) { uart_rx_tick(m, r, u == UART_YM2148 ? (r->cmd & 0x04) : (r->cmd & 3) != 3); sync_int(m); } } }   /* 5.2.56 SFG·Music Module MIDI 받기 */
        m->line_start = m->cpu.cycles; m->cur_line = line;
        m->line_budget += MSX_TPL;
        while (m->line_budget > 0) {
            /* 4.0.129: 수직귀선 줄 바로 앞 줄의 마지막 명령이 IN A,(99h) 이면 그 읽기가 F 가 서는 시각 이후라 F=1 을 본다 (실기 타이밍).
             * 줄 경계에서만 F를 세우면 인터럽트 루틴이 늘 먼저 가져가 DI 없이 F를 기다리는 게임이 영영 못 빠져나옴 (Shaolin's Road V9990 등) */
            if (line == lines - 1 && !early_f && m->line_budget < 8 && (m->vdp.stat[0] & 0x80) == 0 && msx_mem_read(m, m->cpu.pc) == 0xDB && msx_mem_read(m, (uint16_t)(m->cpu.pc + 1)) == 0x99) {
                m->vdp.stat[0] |= 0x80; early_f = 1;
            }
            if (m->tape && m->cpu.pc >= 0x00E1 && m->cpu.pc <= 0x00F3 && tape_trap(m)) { m->line_budget -= 100; continue; }
            if (m->ide_compat) ide_chput_guard(m);
#ifdef PC_TRACE
            { static int wd = -2, wn; static uint16_t pd, pp; if (wd == -2) { const char *e = getenv("WATCHDE"); wd = e ? (int)strtol(e, 0, 16) : -1; } if (wd >= 0) { if (m->cpu.de.w == (uint16_t)wd && pd != (uint16_t)wd && wn < 4) { wn++; fprintf(stderr, "WDE de=%04X after pc %04X f%llu a8 %02X sp %04X af %04X hl %04X bc %04X stack:", wd, pp, (unsigned long long)m->frame, m->primary, m->cpu.sp.w, m->cpu.af.w, m->cpu.hl.w, m->cpu.bc.w); for (int r = 0; r < 16; r += 2) fprintf(stderr, " %04X", msx_peek(m, (uint16_t)(m->cpu.sp.w + r)) | msx_peek(m, (uint16_t)(m->cpu.sp.w + r + 1)) << 8); fprintf(stderr, "\n"); } pd = m->cpu.de.w; pp = m->cpu.pc; } }
            { if (getenv("FORCEB") && m->cpu.pc == 5 && (m->cpu.bc.w & 0xFF) == 0x2F) { fprintf(stderr, "FORCEB f%llu bc %04X\n", (unsigned long long)m->frame, m->cpu.bc.w); m->cpu.bc.w &= 0x00FF; } }
            { static int da = -2, dd; if (da == -2) { const char *e = getenv("DUMPPC"); da = e ? (int)strtol(e, 0, 16) : -1; } if (da >= 0 && !dd && m->cpu.pc == da && (long)m->frame >= (getenv("DUMPMINF") ? atol(getenv("DUMPMINF")) : 0)) { dd = 1; if (getenv("DUMPAT")) { int a0 = (int)strtol(getenv("DUMPAT"), 0, 16); fprintf(stderr, "DAT %04X:", a0); for (int r = 0; r < 0x1400; r++) fprintf(stderr, "%02X", msx_peek(m, (uint16_t)(a0 + r))); fprintf(stderr, "\n"); } fprintf(stderr, "DMP %04X:", da - 0x60); for (int r = -0x60; r < 0x80; r++) fprintf(stderr, " %02X", msx_peek(m, (uint16_t)(da + r))); fprintf(stderr, "\n"); } }
            { static int ja = -2, jp, jn; if (ja == -2) { const char *e = getenv("JMPAT"); ja = e ? (int)strtol(e, 0, 16) : -1; } if (ja >= 0) { if (m->cpu.pc == ja && jp != ja && jn < 6) { jn++; fprintf(stderr, "JMP to %04X from %04X f%llu a8 %02X sp %04X stack:", ja, jp, (unsigned long long)m->frame, m->primary, m->cpu.sp.w); for (int r = 0; r < 8; r += 2) fprintf(stderr, " %04X", msx_peek(m, (uint16_t)(m->cpu.sp.w + r)) | msx_peek(m, (uint16_t)(m->cpu.sp.w + r + 1)) << 8); fprintf(stderr, "\n"); } jp = m->cpu.pc; } }
            { static int sa = -2; if (sa == -2) { const char *e = getenv("STKAT"); sa = e ? (int)strtol(e, 0, 16) : -1; } if (sa >= 0 && m->cpu.pc == sa) { fprintf(stderr, "STK pc %04X f%llu a8 %02X af %04X hl %04X de %04X bc %04X:", sa, (unsigned long long)m->frame, m->primary, m->cpu.af.w, m->cpu.hl.w, m->cpu.de.w, m->cpu.bc.w); for (int r = 0; r < 12; r += 2) fprintf(stderr, " %04X", msx_peek(m, (uint16_t)(m->cpu.sp.w + r)) | msx_peek(m, (uint16_t)(m->cpu.sp.w + r + 1)) << 8); fprintf(stderr, "\n"); } }
            { static int wa = -2; static uint8_t wv; if (wa == -2) { const char *e = getenv("WATCHA"); wa = e ? (int)strtol(e, 0, 16) : -1; } if (wa >= 0) { uint8_t v = msx_peek(m, (uint16_t)wa); if (v != wv) { fprintf(stderr, "WATCH %04X %02X->%02X after pc %04X f%llu a8 %02X\n", wa, wv, v, m->cpu.pc, (unsigned long long)m->frame, m->primary); wv = v; } } }
            { static unsigned char seen[65536]; if (getenv("FIRSTPC") && !seen[m->cpu.pc]) { seen[m->cpu.pc] = 1; fprintf(stderr, "FP %04X f%llu a8 %02X\n", m->cpu.pc, (unsigned long long)m->frame, m->primary); } }
            { static long tf = -2; if (tf == -2) { const char *e = getenv("TRACEF"); tf = e ? atol(e) : -1; }
              static long tn = -2; if (tn == -2) { const char *e2 = getenv("TRACEN"); tn = e2 ? atol(e2) : 2; }
              if ((long)m->frame >= tf && (long)m->frame < tf + tn) fprintf(stderr, "%04X sp %04X a8 %02X af %04X hl %04X ix %04X\n", m->cpu.pc, m->cpu.sp.w, m->primary, m->cpu.af.w, m->cpu.hl.w, m->cpu.ix.w); }
            { static int bn; if (getenv("BPX") && m->cpu.pc >= 0xC01E && m->cpu.pc < 0xC090 && bn < 400) { bn++; fprintf(stderr, "BP %04X af %04X bc %04X de %04X hl %04X sp %04X\n", m->cpu.pc, m->cpu.af.w, m->cpu.bc.w, m->cpu.de.w, m->cpu.hl.w, m->cpu.sp.w); } }
            { static uint16_t pp[8]; static int pi; if (m->cpu.pc == 0 && getenv("RSTLOG")) fprintf(stderr, "RST f%llu prev %04X %04X %04X %04X sp %04X a8 %02X\n", (unsigned long long)m->frame, pp[(pi+4)&7], pp[(pi+5)&7], pp[(pi+6)&7], pp[(pi+7)&7], m->cpu.sp.w, m->primary); pp[pi++ & 7] = m->cpu.pc; }
#endif
            int t;
            if (!m->cpu.r800 && !m->tape && !m->ide_compat && !(line == lines - 1 && !early_f)) {
                /* Z80 일반 경로는 명령을 이어 실행 (줄 예산·예약 시각까지). 결과는 하나씩 부르는 것과 같음 (corefuzz) */
                uint64_t c0 = m->cpu.cycles;
                t = z80_run(&m->cpu, c0 + (uint64_t)m->line_budget, &m->sched.next);
                if (!m->cpu.r800) {
                    m->line_budget -= (int)(m->cpu.cycles - c0);
                    m->io_stall = 0;
                    if (m->cpu.cycles >= m->sched.next) sched_run(m);
                    continue;
                }
                m->line_budget -= (int)(m->cpu.cycles - c0) - t;   /* 도중에 R800 으로 바뀜: 마지막 명령만 아래에서 R800 시간으로 환산 */
            } else t = z80_step(&m->cpu);
            if (m->cpu.r800) {                          /* R800 (7.16 MHz): 명령마다 센 R800 클럭 → Z80 시간 (× 100 / R800 속도 %) */
                /* 2.8.1: cpu.cycles도 실제 시간(Z80 3.58 MHz 단위)으로 — 수평 귀선(HR)·PSG·타이머가 R800에서도 맞게.
                 * 3.4.2 ACC-01: Z80 시간 ÷ 4 근사 대신 접근마다 1 + 페이지 넘김 1 + 슬롯 대기, I/O 7, DRAM 새로 고침 210클럭마다 25 */
                uint32_t n = m->cpu.r8c ? m->cpu.r8c : 1;
                m->cpu.r8c = 0;
                m->r800_ref += n;
                while (m->r800_ref >= 210) { m->r800_ref -= 210; n += 25; m->cpu.r8_page = -1; }
                m->r800_acc += n * 100u;
                uint32_t div = 2u * (uint32_t)(m->r800_pct > 0 ? m->r800_pct : 100);
                int c = 0;                              /* 4.0.14: 나눗셈 대신 뺄셈 (명령마다 불리던 div 제거) */
                { uint32_t acc = m->r800_acc; while (acc >= div) { acc -= div; c++; } m->r800_acc = acc; }
                m->cpu.cycles = m->cpu.cycles - (uint64_t)t + (uint64_t)c;
                m->line_budget -= c;
            } else m->line_budget -= t;
            if (m->io_stall) { m->line_budget -= m->io_stall; m->io_stall = 0; }
            if (m->cpu.cycles >= m->sched.next) sched_run(m);   /* 5.2.0 시간표 (예약이 없으면 비교 하나) */
        }
        vdp_sync(m);
        if (line < 256) { vdp_line_end(&m->vdp, line); sync_int(m); }   /* 4.0.29: FH는 줄 끝(수평 귀선)에 */
        if (line < VDP_H)
            vdp_render_line(&m->vdp, line);
        if (m->v9) { for (int k = vl; k < vb; k++) v9990_line_end(m->v9, k); vl = vb; }
    }
    psg_sync(&m->psg, m->cpu.cycles);
    m->frame++;
    if (m->tape_motor && m->frame - tape_last_frame > 240) m->tape_motor = 0;   /* 4.0.34: 4초 넘게 테이프를 안 건드리면 모터 끔 (다 읽었는데 계속 돌던 것) */
#ifdef VDP_TRACE
    { extern unsigned long long vdp_trace_frame; vdp_trace_frame = m->frame; }
#endif
}

/* ------------------------------------------------------------------ */
/* 상태 저장 (2.10)                                                     */
/* ------------------------------------------------------------------ */
/* YM2413 레지스터 64바이트 (없으면 0) */
void msx_opll_regs(const MSX *m, uint8_t out[64])
{
    memset(out, 0, 64);
    if (m->opll) memcpy(out, m->opll->reg, 64);
}

/* 저장된 상태를 덮어쓴 뒤: 포인터가 들어 있는 슬롯 표를 지금 뱅크 상태로 다시 만들고 YM2413 레지스터를 되살린다.
 * 호출 전에 slot_rd/slot_wr는 새로 구성한 기계(같은 기종·카트리지)의 값이어야 한다 */
void msx_state_fixup(MSX *m, const uint8_t opll_regs[64])
{
    mapper_map(m);
    dos2_map(m);
    for (int n = 0; n < MSX_CART_ALL; n++) if (m->cart_slot[n] >= 0) cart_map(m, n);
    /* 저장 상태의 칸 표(rd/wr)·스택 그림자 원본 포인터(sh_orig)는 저장하던 쪽 주소 공간의 것 — 실 PC(UEFI) 는 켤 때마다 주소가 같아 우연히 맞았지만
     * 호스트(fehost, ASLR) 에서는 이어서 켜기가 죽음. 포인터는 버리고, 그림자가 걸려 있었으면 그 칸의 RAM(쓸 수 있는 슬롯 칸)을 찾아 그림자를 다시 건다 */
    int sh_sb = m->sh_blk ? m->sh_blk - 1 : -1;
    uint16_t sh_lo = m->sh_lo, sh_hi = m->sh_hi;
    for (int b = 0; b < 8; b++) { m->rd[b] = ff_block; m->wr[b] = discard_block; }
    m->sh_blk = 0; m->sh_orig = 0;
    update_all_pages(m);
    if (sh_sb >= 0 && !m->sh_blk && m->wr[sh_sb] == discard_block) {
        uint8_t *ram = 0;
        for (int ps = 0; ps < 4 && !ram; ps++) for (int ss = 0; ss < 4 && !ram; ss++) {
            uint8_t *w = m->slot_wr[ps][ss][sh_sb];
            if (w && w != discard_block && w != m->sh_buf) ram = w;
        }
        if (ram) {
            memcpy(ram + sh_lo, m->sh_buf + sh_lo, (size_t)(sh_hi - sh_lo));   /* 그림자에 있던 스택 바이트를 RAM 에 되돌리고 */
            memcpy(m->sh_buf, m->rd[sh_sb], 8192);                            /* 롬 사본 + 스택 둘레로 그림자를 다시 */
            memcpy(m->sh_buf + sh_lo, ram + sh_lo, (size_t)(sh_hi - sh_lo));
            m->sh_lo = sh_lo; m->sh_hi = sh_hi; m->sh_orig = ram; m->sh_blk = (uint8_t)(sh_sb + 1);
            m->rd[sh_sb] = m->sh_buf; m->wr[sh_sb] = m->sh_buf;
            fast_refresh(m);
        }
    }
    netcard_reset(&m->net);                              /* 저장 상태에는 호스트 함수 주소·진행 중 명령이 들어 있다 → 다시 붙이고 명령 상태를 비운다 */
    netcard_set_host(&m->net, msx_net_host);
    if (m->opll_on && opll(m)) {
        OPLL_reset(m->opll); m->opll_quiet = 0;
        for (int r = 0; r < 64; r++) OPLL_writeReg(m->opll, (uint32_t)r, opll_regs[r]);
    }
}

/* 3.3.4 DEV-34: MSX-AUDIO ADPCM 샘플 RAM (256 KiB, 켜져 있을 때만) */
uint8_t *msx_y8950_ram(MSX *m, uint32_t *size)
{
    if (!m->audio_on || !m->y8950 || !m->y8950->adpcm || !m->y8950->adpcm->memory[0]) { *size = 0; return 0; }
    *size = 256u * 1024u;
    return m->y8950->adpcm->memory[0];
}

void msx_state_fixup_audio(MSX *m, const uint8_t regs[256])
{
    int on = m->audio_on;
    msx_audio_enable(m, on);
    if (!m->audio_on) return;
    for (int r = 0x20; r < 0x100; r++) OPL_writeReg(m->y8950, (uint32_t)r, regs[r]);
    OPL_writeReg(m->y8950, 0x08, regs[0x08]);
    OPL_writeReg(m->y8950, 0x01, regs[0x01]);
}

/* 카트리지 블록 표를 지금 뱅크·SRAM 상태로 다시 (2.11: SRAM 파일을 읽은 뒤) */
void msx_reset_maps(MSX *m)
{
    for (int n = 0; n < MSX_CART_ALL; n++) if (m->cart_slot[n] >= 0) cart_map(m, n);
    update_all_pages(m);
}

/* 시험용: I/O 포트 읽기·쓰기 (3.0 tests/mousetest.c) */
uint8_t msx_io_read(MSX *m, uint16_t port) { return io_read(m, port); }
void msx_io_write(MSX *m, uint16_t port, uint8_t v) { io_write(m, port, v); }
uint8_t msx_mem_read(MSX *m, uint16_t a) { return mem_read(m, a); }   /* 3.7.1: CPU가 보는 메모리 (가상 디스크 전송) */
void msx_mem_write(MSX *m, uint16_t a, uint8_t v) { mem_write(m, a, v); }
