/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * cart.h - ROM 카트리지 (설계서 §4.2, FR-42)
 *
 * 일반 ROM: 8/16/32/48/64 KiB (헤더 "AB" 위치로 배치 결정)
 * 메가ROM 매퍼 (8 KiB 블록 단위, 0x4000-0xBFFF):
 *   Konami      0x6000/0x8000/0xA000 영역 쓰기 → 해당 8 KiB 뱅크 (0x4000은 뱅크 0 고정)
 *   Konami SCC  0x5000/0x7000/0x9000/0xB000 (각 2 KiB 창) → 0x4000/0x6000/0x8000/0xA000 뱅크
 *               (SCC 음원은 src/scc.c)
 *   ASCII8      0x6000/0x6800/0x7000/0x7800 → 0x4000/0x6000/0x8000/0xA000 뱅크
 *   ASCII16     0x6000/0x7000 → 0x4000-0x7FFF / 0x8000-0xBFFF 16 KiB 뱅크
 */
#ifndef MSX_CART_H
#define MSX_CART_H

#include "flash.h"
#include <stdint.h>
#include <stddef.h>
#include "scc.h"

typedef enum {
    MAPPER_PLAIN = 0,
    MAPPER_KONAMI,
    MAPPER_KONAMI_SCC,
    MAPPER_ASCII8,
    MAPPER_ASCII16,
    MAPPER_FMPAC,               /* FM-PAC: 16 KiB 뱅크(0x7FF7), FM 레지스터(0x7FF4/5), SRAM 8 KiB */
    MAPPER_DOS2,                /* MSX-DOS2 카트리지: 16 KiB 뱅크(0x6000~0x67FF, 0x7FFE) */
    MAPPER_SCCPLUS,             /* SCC-I(SCC+) 사운드 카트리지: SCC 뱅크 + 모드 레지스터(0xBFFE), RAM */
    MAPPER_SUNRISE,             /* Sunrise IDE: 16 KiB 뱅크(제어 0x4104, 비트 뒤집기), IDE 레지스터 0x7C00~0x7EFF */
    MAPPER_ASCII8_SRAM,         /* 2.11: ASCII8 + SRAM 8 KiB (뱅크 값의 '뱅크 수' 비트 = SRAM, 0x8000~0xBFFF에서 쓰기) */
    MAPPER_ASCII16_SRAM,        /* 2.11: ASCII16 + SRAM 2 KiB (16 KiB 창에 거울) */
    MAPPER_MEGARAM,             /* 2.16: MegaRAM - 8 KiB 뱅크 RAM. IN 8Eh = 쓰기 모드, OUT 8Eh = 뱅크 선택 모드 (blueMSX와 같음) */
    MAPPER_RAM,                 /* 2.16: 일반 RAM 카트리지 16~64 KiB (0xFFFF 쪽으로 붙임, blueMSX "External RAM 16~64kB") */
    MAPPER_PAC,                 /* 2.16: PAC - SRAM 8 KiB만 (0x5FFE/0x5FFF에 4D 69를 쓰면 0x4000~0x5FFD에 보임) */
    MAPPER_RTYPE,               /* 3.0.9: R-Type - 0x4000 = 16 KiB 뱅크 0x17 고정, 0x4000~0x7FFF 쓰기 → 0x8000 뱅크 (값 & (비트4 ? 0x17 : 0x1F)) */
    MAPPER_HARRYFOX,            /* 3.0.9: Harry Fox - 0x6000~0x6FFF → 0x4000 16 KiB 뱅크 2*(v&1), 0x7000~0x7FFF → 0x8000 뱅크 2*(v&1)+1 */
    MAPPER_NETTOU,              /* 3.0.10: 열투야구 '88 - ASCII8, 뱅크 값 비트 7 = 그 창을 음성 칩(uPD7759)으로 (읽으면 0xFF = 쉬는 중, 음성은 안 냄) */
    MAPPER_ESERAM,              /* 3.1.1: Ese-RAM - ASCII8 모양 8 KiB 뱅크 SRAM. 레지스터 0x6000/0x6800/0x7000/0x7800, 값 비트 7 = 그 창 쓰기 허용 */
    MAPPER_MFRSCC,              /* 3.1.1: Mega Flash ROM SCC - 코나미 SCC 매퍼 + AMD 29F040 플래시 512 KiB (쓰기 명령) */
    MAPPER_BEERIDE,             /* 3.4.1: Beer IDE - 롬 16 KiB(0x4000), 8255 포트 30h~33h (A·B = 자료 16비트, C 아래 = 레지스터, C 위 = 읽기·쓰기 펄스) */
    MAPPER_GIDE,                /* 3.4.1: GIDE - 롬 없음, 포트 64h~6Fh (68h = 자료, 69h~6Fh = 레지스터 1~7, 66h = 제어·보조 상태) */
    MAPPER_ESESCC,
    MAPPER_GOUDA,
    MAPPER_FSCA1,               /* 3.4.1: Panasonic FS-CA1 (MSX-AUDIO) - 롬 128 KiB 32 KiB 뱅크 4개(0x7FFE), 뱅크 0의 x3000~x3FFF = SRAM 4 KiB, 0x7FFF = 포트 C0h 켜기 */
    MAPPER_SFG,                 /* 3.4.1: Yamaha SFG-01(16 KiB)·SFG-05(32 KiB) - 롬 + 롬 0x3FF0~0x3FF7 = YM2151·YM2148·키보드 */               /* 3.4.1: Gouda SCSI - 롬 16 KiB(0x4000, Novaxis BIOS), WD33C93 포트 34h~36h */              /* 3.4.1: Ese-SCC - SRAM 128~512 KiB에 코나미 SCC 매퍼. 0x7FFE/F 비트 4 = 0x4000~0x7FFF 쓰기 허용 */
    MAPPER_ZEMINA80,            /* 3.8.0: Zemina 80 in 1 (Super Game World 30/64/80) - 8 KiB 뱅크, 0x4000~0x4003 쓰기 = 4000/6000/8000/A000 창 */
    MAPPER_ZEMINA90,            /* 3.8.0: Zemina 90 in 1 (Super Game 90) - 포트 77h: 비트 0~5 = 16 KiB 세그먼트, 비트 6 = 8000 창 안 8 KiB 둘 바꿈, 비트 7 = 8000 창이 다음 세그먼트(0이면 거울) */
    MAPPER_ZEMINA126,           /* 3.8.0: Zemina 126 in 1 (Super Game World 126) - 16 KiB 뱅크, 0x4000 = 4000 창, 0x4001 = 8000 창 */
    MAPPER_CROSSBLAIM,          /* 3.8.0: Cross Blaim - 롬 64 KiB, 4000~7FFF = 16 KiB 뱅크 0 고정, 0x4045 쓰기 = 8000~BFFF 뱅크 */
    MAPPER_LODERUNNER,          /* 3.8.0: Super Lode Runner - 롬 128 KiB, 0x0000~0x3FFF 쓰기(슬롯 상관없이) = 8000~BFFF 16 KiB 뱅크 */
    MAPPER_QURAN,               /* 3.8.0: Holy Quran(Al Quran) - 롬 1 MiB, 0x5000/0x5400/0x5800/0x5C00 (+ 1 KiB 거울) = 4000/6000/8000/A000 8 KiB 뱅크 */
    MAPPER_VHD,                 /* 3.7.1: X86MSX 가상 디스크 (Nextor 커널 + X86MSX 드라이버) - 16 KiB 뱅크 0x6000~0x67FF, 하이퍼콜 포트 3Ah~3Fh */
    MAPPER_MEGASCSI,            /* 3.9.14: MEGA-SCSI - ASCII8 모양 SRAM(128~1024 KiB) + MB89352. 레지스터 0x6000/6800/7000/7800: 비트 7 = 쓰기, 0x40~0x7F = SPC(0x7F 뱅크) */
    MAPPER_WAVESCSI,            /* 3.9.14: WAVE-SCSI - Ese-SCC(코나미 SCC 매퍼 SRAM 128~1024 KiB) + MB89352 (0x4000 창 = SPC, 0x7FFE/F 비트 6 = SPC 켜기·비트 4 = 쓰기 허용) */
    MAPPER_MATRAINK,            /* 3.9.14: Matra INK - AMD 29F040 플래시 512 KiB, 읽기 = 0x0000~0xFFFF에 첫 64 KiB, 쓰기 명령 주소 = 주소 + 0x10000 */
    MAPPER_ASCII16X,            /* 4.0.129: ASCII16-X (Grauw) - 16 KiB 뱅크 12비트, 레지스터 6000/7000 + 거울, 플래시(S29GL064S) */
    MAPPER_YAMANOOTO,           /* 4.0.129: SCC Alliance Yamanooto - 플래시 2·8 MB, 8 KiB 뱅크 10비트(Konami-SCC/Konami4), SCC·SCC+, 보조 PSG 10h~11h */
    MAPPER_GAMEMASTER2,         /* Konami Game Master 2 - 롬 128 KiB 8 KiB 뱅크(0x4000 고정, 0x6000·0x8000·0xA000 레지스터), 값 비트 4 = SRAM 4 KiB, 비트 5 = SRAM 칸, 0xB000~0xBFFF 쓰기 */
    MAPPER_NEO8,                /* NEO-8 - 플래시 롬 8 KiB 뱅크 6개(0000~BFFF), 14비트 (짝수 주소 = 하위, 홀수 = 상위), 서명 ROM_NEO8 */
    MAPPER_NEO16,               /* NEO-16 - 16 KiB 뱅크 3개, 서명 ROM_NE16 */
    MAPPER_NET,                 /* 네트워크 카트리지 - UNAPI ROM 16 KiB(0x4000), I/O 28h~2Bh (netcard) */
    MAPPER_MFRSCCP,             /* Mega Flash ROM SCC+ (Manuel Pazos 2010) - M29W800DB 1 MiB, SCC-I, PSG(10h~12h), 매퍼 Konami SCC/Konami/ASCII8/ASCII16/64K, 서브슬롯 4x256 KiB, 설정 7FFEh/7FFFh. 상태는 mp_* (상태 파일 CART 판 2) */
    MAPPER_FRSCC,               /* popolon Flash-ROM SCC (Developer Edition) - M29W128 16 MiB(128 KiB 섹터), Konami SCC 매퍼 + 오프셋(3800h/3801h) + 16비트 레지스터 모드(3804h), SCC + 8비트 DAC(98FBh/98FCh, 코나미 창 4000h). 상태: xbank[0..3] 뱅크, xbank[4] 오프셋, y_en 모드, y_cfg DAC 제어 */
    MAPPER_RS232,               /* Philips NMS1210 RS-232C 인터페이스 - 8251 + 8253 포트 80h~87h, 롬 없음(BIOS 는 디스크에서 RAM 으로). 상태 rs */
    MAPPER_HBI55,               /* Sony HBI-55 데이터 카트리지 - 배터리 SRAM 4 KiB, 자기 i8255 (포트 B0h~B3h). 메모리 창 없음, sram[0..4095], ppi[4] */
    MAPPER_COUNT
} MapperType;

/* IDE 하드 디스크를 쓰는 매퍼 (3.4.1) */
#define CART_IS_IDE(mp) ((mp) == MAPPER_SUNRISE || (mp) == MAPPER_BEERIDE || (mp) == MAPPER_GIDE || (mp) == MAPPER_GOUDA || (mp) == MAPPER_MEGASCSI || (mp) == MAPPER_WAVESCSI)
/* SCC 음원이 있는 매퍼 (3.4.1) */
#define CART_HAS_SCC(mp) ((mp) == MAPPER_KONAMI_SCC || (mp) == MAPPER_MFRSCCP || (mp) == MAPPER_FRSCC || (mp) == MAPPER_SCCPLUS || (mp) == MAPPER_MFRSCC || (mp) == MAPPER_ESESCC || (mp) == MAPPER_WAVESCSI || (mp) == MAPPER_YAMANOOTO)

typedef struct { uint16_t reload, latch; uint8_t mode, rw, wlow, rlow, latched, armed; uint64_t start, edge; } PitCounter;   /* 8253 카운터 하나 (msx.c pit_*, MSX-MIDI 와 RS-232C 가 같이 씀) */
enum { UART_NONE, UART_I8251, UART_YM2148, UART_MC6850 };   /* Rs232Dev 를 쓰는 UART 종류 (SFG 는 매퍼로, Music Module 은 이 표시로) */
typedef struct {                        /* MSX 표준 RS-232C (8251 + 8253, 포트 80h~87h) — Philips NMS1210 */
    uint8_t cmd, mode_next, mode, intmask;   /* 8251 명령·모드 단계·모드 바이트, 82h 인터럽트 마스크(1 = 막음: 비트 0 RxRDY, 3 타이머 OUT2) */
    uint8_t rxrdy, rx_data, tflag, uart;   /* uart = 이 카트에 든 MIDI UART (UART_*) — 같은 칸을 YM2148(SFG)·MC6850(Music Module) 도 씀 */
    PitCounter pit[3];                  /* 0·1 = 보내기·받기 시계, 2 = 타이머 (82h 비트 6, IRQ) */
    uint64_t rx_next;                   /* 다음 받은 바이트를 8251 에 넣을 수 있는 시각 */
    uint32_t tx_count, rx_count;        /* 보낸·받은 바이트 수 (진단) */
    uint8_t rx_q[256]; uint16_t rx_r, rx_w;   /* 프런트엔드(나중에 TCP)가 넣는 받을 바이트 */
} Rs232Dev;
typedef struct {
    const uint8_t *data;
    uint32_t size;
    int present;
    MapperType mapper;
    uint32_t banks8;            /* 8 KiB 뱅크 수 */
    uint8_t bank[4];            /* 0x4000/0x6000/0x8000/0xA000 블록의 8 KiB 뱅크 번호 */
    int plain_base;             /* 일반 ROM 시작 블록 (0 = 0x0000, 2 = 0x4000, 4 = 0x8000) */
    SCC scc;                    /* 코나미 SCC 매퍼의 음원 */
    uint8_t sram[32768];        /* FM-PAC SRAM, ASCII8/16 SRAM 게임의 배터리 백업 RAM (3.8.0: ASCII8 SRAM 32 KiB까지) */
    uint8_t sram_kb;            /* 3.8.0: ASCII8 SRAM 크기 2·8·32 (0 = 8) */
    uint8_t sram_key[2], sram_on;
    uint8_t sram_dirty;         /* SRAM이 바뀜 (프런트엔드가 파일로 저장하고 지움, 2.11) */
    uint8_t scc_mode;           /* SCC-I 모드 레지스터 (0xBFFE/0xBFFF) */
    uint8_t ide_ctl, ide_hi;    /* Sunrise IDE: 제어 레지스터, 데이터 포트 위 바이트 */
    uint8_t mram_we;            /* MegaRAM: 1 = 쓰기 모드 (IN 8Eh), 0 = 뱅크 선택 모드 (OUT 8Eh) */
    uint8_t ppi[4];             /* 3.4.1 Beer IDE: 8255 레지스터 (A, B, C, 모드) */
    uint16_t ide_word;          /* 3.4.1 Beer IDE: 자료 래치 */
    uint8_t gide_alt;           /* 3.4.1 GIDE: 보조 상태 */
    uint8_t ese_we;             /* 3.4.1 Ese-SCC: 0x4000~0x7FFF SRAM 쓰기 허용 */
    uint16_t xbank[6];          /* 4.0.129: ASCII16-X 12비트 뱅크(0·1) / Yamanooto 10비트 8 KiB 뱅크 4개 */
    uint8_t fl_dirty[256];      /* 4.0.131: 플래시 8 KiB 조각마다 바뀜 표시 (저장용, 롬 이름.flash) */
    FlashChip flash;            /* 플래시 칩 상태 (MFR SCC·Matra INK·ASCII16-X·Yamanooto, 칩 부품 flash.h) */
    uint8_t y_en, y_cfg, y_off, y_raw[4], y_fsm;   /* 4.0.129: Yamanooto 7FFF·7FFD·7FFE 레지스터, 오프셋 더하기 전 뱅크 값, FPGA 상태 */
    uint8_t mp_cfg, mp_off, mp_sub, mp_psg;        /* MFR SCC+ 설정 레지스터(7FFE/7FFF)·뱅크 오프셋·서브슬롯 레지스터(FFFF)·PSG 선택 */
    uint8_t mp_bank[4][4];                         /* MFR SCC+ 서브슬롯별 뱅크 레지스터 4개 (CART 상태 판 1 은 여기 앞까지 = CART_ST_V1_SIZE) */
    uint8_t mp_sccb[4];                            /* MFR SCC+ 코나미 뱅크 레지스터 원값 (SCC 켜짐 판단: bank2 = 3Fh / bank3 비트 7) */
    Rs232Dev rs;                                   /* RS-232C (CART 상태 판 2 는 여기 앞까지 = CART_ST_V2_SIZE) */
} Cartridge;
#define CART_ST_V1_SIZE offsetof(Cartridge, mp_cfg)   /* 상태 파일 CART 판 1 (MFR SCC+ 필드 전) 의 카트 하나 크기 */
#define CART_ST_V2_SIZE offsetof(Cartridge, rs)       /* 판 2 (RS-232C 필드 전) */

/* 크기와 코드로 매퍼 추정 */
extern int cart_flash_auto;   /* 비트 1 = Yamanooto, 2 = ASCII16-X 자동 인식 */
MapperType cart_guess_mapper(const uint8_t *data, uint32_t size);
/* 파일 앞부분(data_len 바이트)만으로 추정. full_size는 실제 파일 크기 (목록 표시용) */
MapperType cart_guess_mapper_partial(const uint8_t *data, uint32_t data_len, uint32_t full_size);
const char *cart_mapper_name(MapperType m);

/* 일반 ROM 배치 블록 결정 */
int cart_plain_base(const uint8_t *data, uint32_t size);

#endif
