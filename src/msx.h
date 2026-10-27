/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * msx.h - MSX1 기계 (설계서 §4.2, §4.3, §4.6, §5)
 */
#ifndef MSX_MACHINE_H
#define MSX_MACHINE_H

#include "z80.h"
#include "netcard.h"
#include "vdp.h"
#include "profile.h"
#include "fdc.h"
#include "fdc765.h"
#include "ide.h"
#include "wd33c93.h"
#include "mb89352.h"
#include "ym64.h"
#include "opl4.h"
#include "sched.h"
#include "irqline.h"
#include "cart.h"
#include "psg.h"
#include "fw.h"
#include "v9990.h"

#define MSX_LINES_NTSC   262
#define MSX_LINES_PAL    313
#define MSX_TPL          228            /* 라인당 T-state */
#define MSX_ROM_BIG      (16u << 20)  /* 4.0.129: ASCII16-X·Yamanooto 롬 상한 */
#define MSX_IO_CHAIN     6            /* 포트 하나에 걸 수 있는 장치 수 */
#define MSX_ROM_MAX      (2u << 20)   /* 3.4.4: 2 MiB (Game World 126 Games) */
enum { MSX_CH_PSG, MSX_CH_PSG2, MSX_CH_SCC, MSX_CH_MM, MSX_CH_MA, MSX_CH_PCM, MSX_CH_MS, MSX_CH_SFG, MSX_CH_MIDI, MSX_CH_KEY, MSX_CH_IO, MSX_CH_N };   /* 차례 = 믹서 띠 차례 (칩 → 고정 KBD·I-O). 새 칩은 MSX_CH_MIDI 앞에 */   /* KEY = 키 클릭 (PSG 와 같은 소리줄, 음량만 따로) */   /* 3.6.7: PSG2 = Joyrex */   /* 3.4.5: MIDI = 소프트웨어 음원 */   /* 3.3.3 pan·lvl 번호 */
#define MSX_CART_SLOTS   5          /* 3.6.7: 5칸 (슬롯 하나를 넷으로 나누면 1-0~1-3 + 다른 슬롯) */
#define MSX_INT_CART     MSX_CART_SLOTS      /* 3.1.0: 기종 내장 장치를 카트리지처럼 (내장 PAC) */
#define MSX_INT_N        3                   /* 3.6.8: 기종 내장 카트리지 자리 수 (PAC·SFG·IDE·SCC·FM-PAC …) */
#define MSX_CART_ALL     (MSX_CART_SLOTS + MSX_INT_N)
#define MSX_MRAM_MAX     (4096u * 1024u)

typedef struct {
    uint8_t *data;
    uint32_t size;
} RomImage;

extern const NetHost *msx_net_host;                 /* UEFI 호스트(없으면 NULL = 네트워크 없음) */
typedef struct MSX {
    Z80 cpu;
    VDP vdp;
    MachineProfile prof;

    uint8_t ram[65536];
    uint8_t mram[MSX_MRAM_MAX];         /* 메모리 매퍼 RAM */
    struct {                            /* 기계 안 작업 메모리 (예전 msx.c 공용 변수). 상태 저장 CORE 에서 뺌 (내용은 카트 표가 다시 만듦) */
        uint8_t int_pool[1u << 20];     /* 파일 없는·쓰는 내장 카트리지 버퍼 (SCC·MegaRAM·Ese …) */
        uint32_t int_pool_used;
        uint8_t gm2_mix[3][8192];       /* 5.0.38 Game Master 2: SRAM 4 KiB를 8 KiB 창에 두 번 거울 */
        uint8_t ca1_mix[2][8192];       /* 3.4.1 FS-CA1: 롬 4 KiB + SRAM 4 KiB (뱅크 0의 x2000·x6000 블록) */
    } scr;
    uint32_t mram_size;                 /* 0이면 매퍼 없음 */
    int mapper_ps, mapper_ss;
    uint8_t mapper_reg[4];
    /* 외장 메모리 매퍼 RAM (2.12): 기종에 내장 매퍼가 있어도 카트리지로 하나 더. 레지스터 FC~FF는 같이 씀 */
    uint8_t *xram;                      /* 프런트엔드가 주는 버퍼 (최대 MSX_MRAM_MAX) */
    uint32_t xram_size;
    int xram_ps, xram_ss;
    /* RTC RP5C01 (포트 0xB4/0xB5) */
    int rtc_on;
    uint8_t rtc_sel, rtc_mode, rtc[4][13];
    /* RTC 시각 (2.14): 1980-01-01 0시부터 초. rtc_now는 프런트엔드가 PC 시계로 알려 준 값(rtc_ref 사이클 때),
     * rtc_delta는 MSX에서 SET TIME·SET DATE로 바꾼 만큼 */
    uint32_t rtc_now;
    uint64_t rtc_ref;
    int32_t rtc_delta;
    uint64_t rtc_wlast;
    int vsync_force;
    /* MSX-AUDIO (Y8950, 포트 C0h·C1h) 2.14: 특수 카트리지로 켬. 칩 고유 49,716 Hz로 만들어 두고 프레임 끝에 48 kHz로 */
    int audio_on;
    uint16_t y_pos;
    int16_t y_buf[1200];                    /* 2.14: 0 자동, 1 60 Hz, 2 50 Hz (프런트엔드 설정) */                 /* 블록 0을 마지막으로 쓴 때 (rtc[0]은 쓰는 중의 값) */
    /* 한자/한글 글꼴 ROM (포트 0xD8~0xDB) */
    const uint8_t *kanji;
    uint32_t kanji_size, kanji_a1, kanji_a2;
    uint8_t kanji_hi_mask;                         /* 행 주소 비트 (보통 0x3F, 대우 CPC-400·400S 한글 폰트 롬 256KB는 0x7F) */
    /* S1985 (스위치드 I/O 0xFE) */
    int s1985;
    uint8_t sw_id, s1985_addr, s1985_ram[16];
    FwDev fw;                           /* 3.4.0 기종 내장 펌웨어·Bunsetsu·Matsushita (fw.h) */
    /* 디스크: FDC와 그 레지스터가 보이는 슬롯 */
    FDC fdc;
    int fdc_kind;                       /* 0 없음, 64 대우·내셔널(0x7FB8~), 65 필립스·소니(0x7FF8~) */
    int fdc_ps, fdc_ss;
    uint8_t io_chain[256][MSX_IO_CHAIN];  /* 포트마다 있는 장치 사슬 (0 = 끝, 기계마다) */
    uint8_t io_built;                   /* 0 = 다시 만들어야 함 (msx_io_rebuild) */
    uint8_t io_sig;                     /* 프레임 시작의 카트 장치 상태 (가상 디스크·네트워크·IDE 카트) — 바뀌면 사슬 다시 */
    uint8_t rd_sp[8];                   /* 4.0.14: 이 블록 읽기가 특수 장치를 거치는가 (0이면 곧장 rd[] 읽기) */
    /* 칸(8 KiB)마다 특수 메모리 장치 사슬 (src/memdev.inc). 번호 = enum MemDev, 값 = 장치가 받는 인자(카트 번호+1 등) */
    uint8_t mrd_n[8], mrd_dev[8][8], mrd_arg[8][8];
    uint8_t mwr_n[8], mwr_dev[8][8], mwr_arg[8][8];
    /* MSX-DOS2 ROM (종류 2) */
    const uint8_t *dos2;
    uint32_t dos2_size;
    int dos2_ps, dos2_ss, dos2_bank;
    int ch_vol[MSX_CH_N];               /* 소리 채널마다 음량 0~100 (예전 vol_scc·vol_mm·… 와 psg.vol_tone·vol_click·msx_psg2_vol) */
    struct __OPLL *opll;                /* 이 기계의 YM2413 (처음 필요할 때 만듦, 예전 공용 g_opll) */
    struct __OPL *y8950;                /* 이 기계의 Y8950 (예전 공용 g_y8950) */
    int opll_on;                        /* MSX-MUSIC (YM2413) */
    uint32_t opll_acc;
    uint32_t opll_quiet;                /* 4.0.14: 연속 무음 샘플 수 (MSX-MUSIC 건너뛰기) */
    int16_t opll_last, opll_prev;      /* 49,716→48,000 Hz 선형 보간용 앞뒤 표본 */
    /* 하드 디스크 (Sunrise IDE 카트리지) */
    IDE ide;
    IDE ide_slave;                      /* 3.4.3 DEV-15: IDE 슬레이브 (ide.slave가 가리킴) */
    MB89352 spc;                        /* 3.9.14 MEGA-SCSI·WAVE-SCSI (MB89352 + SCSI 하드 디스크) */
    WD33C93 scsi;                       /* 3.4.1 Gouda SCSI (WD33C93, 포트 34h~36h) */
    /* turbo R */
    FDC765 fdc765;                      /* TC8566AF (fdc_kind 57·143) */
    int dos2_tr;                        /* 1이면 DOS2 뱅크 레지스터가 0x7FF0 (turbo R 디스크 ROM) */
    uint8_t s1990_reg, s1990_cpu;
    uint64_t s1990_switch;
    int r800_pct;                       /* R800 속도 % (Z80과 따로, 상태 저장에 함께 들어감) */              /* R800으로 바뀐 시간 (같은 OTIR의 다음 쓰기 무시) */       /* S1990: 레지스터 번호, 레지스터 6 (비트 5 = Z80, 0이면 R800) */
    uint64_t trtimer_base;              /* turbo R 타이머 (255.7 kHz) 시작 시간 */
    uint8_t f4_status, trpause_reg;
    int pause_sw;                       /* turbo R 일시 정지 키 (프런트엔드의 PAUSE 스위치) */
    uint8_t pcm_ctrl, pcm_da;           /* PCM: A5h 제어, 마지막 DA 값 */
    uint16_t pcm_n;                     /* 이번 프레임 DA 쓰기 수 */
    uint32_t pcm_t[2048];               /* 프레임 안 시간 (T-state) */
    uint8_t pcm_v[2048];
    uint64_t frame_start;
    /* 3.1.0 MoonSound (OPL4, 포트 7Eh·7Fh·C4h~C7h): 48 kHz로 바로 만들어 둔다 */
    int ms_on;
    int ds_head[2], ds_pending, ds_click, ds_motor;   /* 지난 헤드 자리, 남은 걸음 소리, 소리 안 위치, 모터 소리 크기 */
    uint32_t ds_rng, ds_phase;
    int sfg_on;                         /* 3.4.1 SFG-01·05 (3.7.0 YM64 = YM2164(OPP)·YM2151(OPM)): 꽂힘 */
    YM64 opm;
    uint64_t opm_t;                     /* 타이머를 마지막으로 돌린 CPU 시각 */
    uint32_t opm_acc;                   /* 55.9 kHz → 48 kHz */
    int32_t opm_l, opm_r, opm_pl, opm_pr;   /* 앞 표본 (보간) */
    uint8_t sfg_kbd, sfg_vec;
    uint16_t ms_pos;
    int16_t ms_buf[1200];
    Opl4 opl4;
    uint8_t pcm_last;                   /* 앞 프레임 끝의 DA 값 */
    /* 카세트 테이프 (.cas, BIOS 테이프 루틴 가로채기) */
    uint8_t *tape;                      /* 버퍼 (프런트엔드가 줌, NULL = 테이프 없음) */
    uint32_t tape_size, tape_cap, tape_pos;
    int tape_dirty, tape_motor;
    uint8_t joy[2];                     /* 조이스틱 포트 1·2: 비트 0 위 1 아래 2 왼쪽 3 오른쪽 4 버튼A 5 버튼B (1 = 누름) */
    /* 3.0 MSX 마우스 (openMSX MSXMouse와 같은 규약): 포트의 8번 핀(PSG R#15 비트 4/5)을 바꿀 때마다
     * X 위 4비트 → X 아래 → Y 위 → Y 아래. 3 ms 넘게 그대로면 처음부터. 이동량은 왼쪽·위가 +(PC와 반대) */
    int8_t mouse_port;                  /* -1 없음, 0 포트 1, 1 포트 2 */
    uint8_t mouse_btn;                  /* 비트 0 왼쪽(버튼 A), 1 오른쪽(버튼 B) */
    uint8_t mouse_phase, mouse_pin;
    int mouse_cx, mouse_cy;             /* 쌓이는 중인 이동량 (MSX 방향) */
    int mouse_lx, mouse_ly;             /* 읽는 중인 값 */
    uint64_t mouse_t;
    int cur_line;                       /* 지금 줄 번호 (VDP 명령 시간용) */
    uint64_t line_start;                /* 지금 줄이 시작된 T-state (HR 플래그용) */
    RomImage rom[MAX_REGIONS];

    /* 슬롯별 블록 표 [기본 슬롯][서브슬롯][8 KiB 블록] */
    const uint8_t *slot_rd[4][4][8];
    uint8_t *slot_wr[4][4][8];
    /* 현재 CPU에서 보이는 블록 */
    const uint8_t *rd[8];
    uint8_t *wr[8];
    uint8_t *sh_orig; uint16_t sh_lo, sh_hi; uint8_t sh_blk; uint8_t sh_buf[8192];   /* 4.0.34: 스택이 있는 칸이 롬으로 바뀔 때 스택 둘레를 살려 두는 그림자 */

    /* 카트리지 (외부 슬롯 A = 프로필 ext_slot[0]) */
    Cartridge cart[MSX_CART_ALL];     /* 카트리지 슬롯 1·2 (2.4에서 4개로) */
    int cart_slot[MSX_CART_ALL];      /* 연결된 기본 슬롯 (-1 = 없는 슬롯) */
    int cart_ss[MSX_CART_ALL];        /* 서브슬롯 (슬롯 확장기를 켰을 때) */
    int expand;                         /* 카트리지 슬롯 확장: 0 없음, 1 슬롯 1, 2 슬롯 2 */

    uint8_t primary;                    /* 포트 0xA8 */
    uint8_t subslot[4];                 /* 확장 슬롯의 0xFFFF 레지스터 */
    uint8_t ppi_c;                      /* 포트 0xAA */
    uint8_t kbd[16];                    /* 키 매트릭스, 눌림 = 0 */
    /* 3.3.0 MENU3-01 프린터 포트 (90h 상태·스트로브, 91h 자료). prn_on이면 늘 준비됨, 스트로브 1→0에서 prn_q에 쌓음 (프런트엔드가 비움) */
    uint8_t prn_on, prn_strobe, prn_data, prn_poked;   /* 4.0.8: 꺼져 있는데 프로그램이 프린터를 부름 */
    uint16_t prn_n;
    uint8_t prn_q[2048];
    /* 3.3.3 SND3-01·02 스테레오·음량 계기: 음원별 좌우(pan −100 왼쪽 ~ 0 가운데 ~ 100 오른쪽), stereo가 0이면 out_l = out_r = 모노 */
    int stereo;
    int8_t pan[MSX_CH_N];               /* MSX_CH_PSG, SCC, MM, PCM, MA, MS, SFG */
    int16_t out_l[PSG_FRAME_MAX], out_r[PSG_FRAME_MAX];
    uint16_t lvl[MSX_CH_N];                    /* 이번 프레임 음원별 최대 진폭 (믹서 계기) */
    /* 3.3.3 DEV-35 MSX-MIDI (turbo R GT, 종류 88): 8251 E8h 자료·E9h 상태/명령. 보낸 바이트를 시간과 함께 쌓음 (프런트엔드가 비움) */
    uint8_t midi_cmd, midi_mode_next;
    uint16_t midi_n;
    uint8_t midi_q[1024];
    uint32_t midi_t[1024];              /* 프레임 시작부터 T-state */
    /* 3.4.4 DEV-35 받기·타이머: 8251 받기(RxRDY, 자료), 8253 카운터 0·1·2 (4 MHz, 2번 출력 = 타이머 IRQ).
     * 8251 DTR(명령 비트 1) = 타이머 IRQ 허용, RTS(비트 5) = 받기 IRQ 허용, DSR(상태 비트 7) = 타이머 IRQ 깃발, EAh 쓰기 = 깃발 지움 (openMSX와 같음) */
    uint8_t midi_rx_q[512];             /* 프런트엔드가 넣은 받을 바이트 (31250 baud, 320 µs마다 하나씩 8251로) */
    uint16_t midi_rx_r, midi_rx_w;
    uint8_t midi_rx_data, midi_rxrdy, midi_tflag;
    uint32_t kbd_row_reads[11];           /* 키보드 행을 Z80 이 읽은 횟수 (입력 층이 키를 한 번은 읽힐 때까지 유지) — 상태 파일에는 안 넣음 */
    uint64_t midi_tx_done, midi_tx_rdy;   /* 8251 보내기 시간 (31250 baud 한 바이트 ≈ 1146 클럭) — 몰아 보내는 SysEx 가 큐에서 잘리지 않게 */
    uint64_t midi_rx_next;              /* 다음 바이트가 8251에 들어올 수 있는 CPU 시각 */
    PitCounter pit[3];                  /* 이름 붙임 (cart.h PitCounter, 배치 같음) — MSX-MIDI 8253 */
    PSG psg;                            /* 이번 프레임 소리: psg.buf[0..psg.buf_n) */
    PSG psg2;                           /* 2.16: Joyrex PSG 카트리지 (포트 10h·11h 쓰기, 12h 읽기) */
    uint8_t joyrex;
    V9990 *v9;                              /* 4.0.29: Gfx9000 (V9990). 메모리는 프런트엔드가 줌, 0이면 없음 */

    uint8_t chip_act;
    uint16_t scc1_lvl, scc2_lvl;        /* SCC 가 둘일 때 상태 아이콘 계기를 칩마다 따로 (상태 파일에는 안 넣음) */
    uint8_t scc2_act;                   /* 두 번째 SCC 카트리지가 소리를 내는 중 (SCC 가 둘일 때 상태 아이콘 SCC1·SCC2 를 따로 켜려고, 상태 파일에는 안 넣음) */
    uint64_t vdp_busy_until;
    uint32_t r800_acc;                  /* R800 시간 환산 나머지 */
    uint32_t r800_ref;                  /* 3.4.2: DRAM 새로 고침까지 센 R800 클럭 */
    uint64_t vdp_io_last;               /* turbo R: 마지막 VDP 입출력 시간 */
    int io_stall;                       /* 이번 명령에서 기다린 시간 (줄 예산에서 뺌) */            /* VDP 명령이 끝나는 CPU 시간 (그때까지 S#2 CE = 1) */
    int vhd_hide_fdc;                               /* 3.7.1: 가상 디스크가 플로피도 맡음 → 기종 디스크 롬의 "AB" 머리를 숨김 */
    int vhd_on;                                     /* 3.7.1: X86MSX 가상 디스크 카트리지가 꽂힘 (포트 3Ah~3Fh) */
    void (*vhd_io)(void *ctx, struct MSX *m, int port);   /* 3.7.1: 하이퍼콜 (프런트엔드가 레지스터·메모리를 직접 읽고 씀) */
    void *vhd_ctx;
    NetCard net;                                    /* 네트워크 카트리지 장치 */
    int ide_cart_io;                                /* 3.4.1: Beer IDE·GIDE가 꽂힘 (I/O 포트 확인) */
    int ide_compat, chput_n;                        /* Sunrise IDE 호환: CHPUT 동안 IX·IY 보존 (한글 기종 훅) */
    uint16_t chput_ret[4], chput_sp[4], chput_ix[4], chput_iy[4];                   /* 이번 프레임에 소리를 내는 음원: 비트 0 PSG, 1 SCC, 2 MSX-MUSIC, 3 PCM */

    uint64_t frame;
    int line_budget;                    /* 라인 경계를 넘은 T-state 이월 */
    Sched sched;                        /* 시간표 (sched.h) */
    uint32_t irq_lines;                 /* 인터럽트선 (irqline.h, 비트 = IrqLine) */
    uint32_t sched_test_hits;           /* EV_TEST 처리 횟수 (tests/schedtest.c) */
} MSX;
void msx_net_event(MSX *m);                                /* 5.0.57 */

#define MSX_ERR_ROM_MISSING  (-1)
#define MSX_ERR_ROM_SIZE     (-2)
#define MSX_ERR_PROFILE      (-3)

/* 프로필과 ROM 이미지(영역 번호별, RAM 영역은 비워 둠)로 기계를 구성. 성공 0 */
int  msx_setup(MSX *m, const MachineProfile *prof, const RomImage roms[MAX_REGIONS]);
const char *msx_error(int code);

/* 카트리지 슬롯 n(0부터)에 꽂기/빼기 (다음 리셋부터 BIOS가 인식). 성공 0 */
int  msx_insert_cart(MSX *m, int n, const uint8_t *data, uint32_t size, MapperType mapper);
void msx_eject_cart(MSX *m, int n);
/* 일반 ROM 카트리지의 시작 블록을 정한다 (0 = 0x0000, 2 = 0x4000, 4 = 0x8000; 롬 DB의 <start>) */
void msx_cart_set_base(MSX *m, int n, int block);
void msx_cart_set_sram(MSX *m, int n, int kb);        /* 3.8.0: ASCII8 SRAM 크기 2·8·32 */
/* 카트리지 슬롯 확장(0 없음, 1 슬롯 1, 2 슬롯 2). 성공 0, 나눌 수 없는 슬롯이면 -1. 다음 리셋부터 */
int  msx_set_expand(MSX *m, int which);
/* 카트리지 슬롯 n에 메모리 매퍼 RAM(kb KiB). 기종에 매퍼가 이미 있으면 -1 */
int  msx_insert_mapper_ram(MSX *m, int n, int kb);
/* 카트리지 슬롯 n에 디스크 인터페이스(디스크 ROM + FDC, kind 64/65)를 꽂는다. 성공 0 */
int  msx_insert_disk_interface(MSX *m, int n, const uint8_t *rom, uint32_t size, int kind);
/* 카세트 테이프 넣기 (buf 크기 cap, 지금 내용 size). 빼기는 buf = NULL */
void msx_tape_insert(MSX *m, uint8_t *buf, uint32_t size, uint32_t cap);
void msx_reset(MSX *m);                 /* 하드 리셋: RAM 지움 */
void msx_soft_reset(MSX *m);            /* 소프트 리셋: RAM 유지 */
void msx_run_frame(MSX *m);
/* 상태 저장 (2.10): YM2413 레지스터 읽기, 덮어쓴 상태의 슬롯 표·YM2413 되살리기 */
void msx_opll_regs(const MSX *m, uint8_t out[64]);
void msx_state_fixup(MSX *m, const uint8_t opll_regs[64]);
void msx_state_fixup_audio(MSX *m, const uint8_t regs[256]);
void msx_reset_maps(MSX *m);
/* PC 시계를 RTC에 (1980-01-01 0시부터 초). 2.14 */
void msx_rtc_set(MSX *m, uint32_t secs);
/* MSX-AUDIO 켜기·끄기 (2.14) */
void msx_audio_enable(MSX *m, int on);
void msx_moonsound_enable(MSX *m, int on);       /* 3.1.0: MoonSound (웨이브 롬이 opl4_set_memory로 있어야 켜짐) */
void msx_irq_set(MSX *m, int line, int on);      /* 인터럽트선 올리고 내림 (CPU INT = OR) */
void msx_sched_test(MSX *m, uint64_t when);      /* 시간표 시험 사건 예약 */
int msx_io_owners(MSX *m, int port, uint8_t out[MSX_IO_CHAIN]);   /* I/O 버스 — 이 포트를 가진 장치 번호 사슬 (앞이 먼저). 5.2.9: 기계마다 */
/* 소리 채널 표 (본체가 가진 음원). 프런트엔드 믹서는 이 표로 이름·기본 음량·좌우 가능을 그림 */
typedef struct {
    const char *key;                    /* 설정 파일 이름 (예전 믹서 ini 키 그대로) */
    const char *name, *short_name;      /* 띠 이름 (긴 → 짧은) */
    const char *full;                   /* 전체 이름 (설명 줄·속성 ▶ 사운드) */
    uint8_t def_vol, pan_ok, fixed;     /* 기본 음량, 좌우 조절 가능, 늘 보이는 채널 */
    int8_t act_bit;                     /* chip_act 비트 (-1 = 없음) */
} MsxChInfo;
extern const MsxChInfo msx_ch_info[MSX_CH_N];
int msx_scc_count(const MSX *m);                        /* 꽂힌 SCC 카트리지 수 (상태 아이콘 SCC1·SCC2) */
int msx_ch_present(const MSX *m, int ch);               /* 그 칩이 이 기계에 있음 (꽂힘·켜짐) */
int msx_ch_active(const MSX *m, int ch);                /* 이번 프레임 칩 레지스터가 소리를 내는 상태 (음량과 무관) */
extern uint32_t msx_snd_epoch;                      /* 리셋·전원·카트리지 빼기 횟수 (믹서가 "쓰인 채널"을 지우는 때) */
void msx_io_rebuild(MSX *m);                        /* 장치 구성(기종 깃발·꽂은 장치)을 바꾼 뒤 부름 — 다음 I/O 접근 때 사슬을 다시 만듦 */
uint32_t msx_cart_alloc_size(MapperType mt, uint32_t file_size);   /* 롬 버퍼 크기 = 플래시 칩 크기 이상 (나머지 FFh 로 채워 넣을 것) */
int msx_mem_owners(const MSX *m, int b, int write, uint8_t *dev, int max);   /* 메모리 칸 b 의 특수 장치 사슬 (enum MemDev 번호, 앞이 먼저) */
int  msx_rs232_in(MSX *m, const uint8_t *b, int n);
int  msx_midi_jacks(const MSX *m);                       /* MIDI 단자가 있는 곳 비트 (1 << MDP_*): 0 MSX-MIDI(본체·카트리지), 1 SFG, 2 Music Module */
int  msx_midi_in_to(MSX *m, int jack, const uint8_t *b, int n);   /* MIDI IN 단자(MDP_*)에 바이트 넣기 (넣은 수) */
void msx_cart_set_uart(MSX *m, int n, int uart);         /* 카트에 MIDI UART 표시 (Music Module = UART_MC6850) */     /* RS-232C 카트리지(NMS1210) 받기에 바이트 넣기 (넣은 수, 카트 없으면 0) */
int  msx_midi_in(MSX *m, const uint8_t *b, int n);      /* 3.4.4: MSX-MIDI 받기에 바이트 넣기 (넣은 수) */
extern int msx_midi_int_vol;                 /* 3.7.0: 믹서 내장 MIDI 음량 0~100 (Note On 세기) */
extern uint32_t msx_midi_int_bytes;           /* 3.7.0: 내장 MIDI로 보낸 바이트 수 */
extern unsigned long msx_midi_drop;               /* 5.2.158 진단: 가득 차서 버린 MIDI 바이트 */
void msx_v9990_enable(MSX *m, V9990 *mem);        /* 4.0.29: Gfx9000 카트리지 (mem = 0이면 뺌) */
void msx_joyrex_enable(MSX *m, int on);              /* 2.16: Joyrex PSG 카트리지 */
void msx_y8950_regs(const MSX *m, uint8_t out[256]);
uint8_t *msx_y8950_ram(MSX *m, uint32_t *size);
extern int msx_fw_turbor;                           /* 3.4.0: 1 = turbo R 펌웨어 롬(종류 27·68)을 붙임 (기종을 불러올 때 봄) */     /* 3.3.4: ADPCM 샘플 RAM (없으면 NULL) */
uint32_t msx_rtc_secs(int year, int mon, int day, int hour, int min, int sec);
/* PAL(50 Hz)이면 1 (2.14): TMS9929A 기종, 또는 V99x8 R#9 NT 비트. force: 0 자동, 1 60 Hz, 2 50 Hz */
int msx_is_pal(const MSX *m);
/* 기종(config.ini)이 정한 내장 장치가 이 기계에 있나 — 실행 중 코드(본체·프런트엔드)는 m->prof.* 를 직접 보지 않고 이것만 묻는다 (MDEV_*).
 * 기종 깃발을 읽는 곳은 msx.c 의 dev_on() 한 곳. 새 내장 장치 = 여기 번호 하나 + dev_on 에 한 줄. (슬롯 배치 subslotted·ext_slot·region 은 기종 데이터라 그대로) */
enum { MDEV_S1990, MDEV_TRTIMER, MDEV_F4, MDEV_F4_INV, MDEV_TRPAUSE, MDEV_PCM, MDEV_MIDI, MDEV_MIDI_HW, MDEV_MATSU, MDEV_OPLL, MDEV_PAL_VDP, MDEV_Y8950, MDEV_MOON, MDEV_CMOS_BAT, MDEV_N };
int msx_dev(const MSX *m, int d);
/* MSX-MIDI 있음을 프런트엔드가 정함 (기종 GT 의 "내장 MIDI" 설정·외장 MSX-MIDI 카트). I/O 사슬은 부른 쪽이 msx_io_rebuild */
void msx_set_midi(MSX *m, int on);
int msx_frame_lines(const MSX *m);
void msx_key(MSX *m, int row, int bit, int pressed);
extern int msx_vdp_cmd_timing;                 /* 3.2.15: 1 = VDP 명령 시간 흉내 (기본), 0 = 바로 끝 */

/* 0x0000-0xFFFF 읽기 (디버그용, 부작용 없음) */
static inline uint8_t msx_peek(const MSX *m, uint16_t a) { return m->rd[a >> 13][a & 0x1FFF]; }

uint8_t msx_io_read(MSX *m, uint16_t port);              /* 시험용 (3.0) */
uint8_t msx_mem_read(MSX *m, uint16_t a);                /* 3.7.1: CPU가 보는 메모리 */
void msx_mem_write(MSX *m, uint16_t a, uint8_t v);
void msx_io_write(MSX *m, uint16_t port, uint8_t v);

#endif
