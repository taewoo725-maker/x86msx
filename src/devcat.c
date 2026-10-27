/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * devcat.c - 장치 목록(카탈로그) 표 (5.2.8 구조 개편 5단계). 설명은 devcat.h
 * 예전 profile.c 의 형식 switch 3개(블록 없는 장치, 영역, 카트리지 매퍼·버퍼)·지원 목록과 medit.c 의 편집기 형식 표를 한 표로 합침.
 * 줄 순서 = 머신 편집기 "형식" 목록 순서 (blueMSX 2.8.2), 그 뒤에 편집기에 안 나오는 줄.
 */
#include "devcat.h"
#include "profile.h"
#include "cart.h"

#define IO_NONE          DC_IO_NONE, 0
#define IO_F(f)          DC_IO_FEATURE, f
#define IO_SILENT        DC_IO_SILENT, 0       /* 인식만 (경고 없음): 디지타이저·모뎀·프린터 */
#define IO_KANJI         DC_IO_KANJI, 0        /* 한자/한글 글꼴 롬 (파일 있을 때) */
#define IO_JISYO         DC_IO_JISYO, 0        /* 사전 롬 (파일 있을 때) */
#define BUF0             DC_BUF_FIXED, 0, 0xFF
#define BUF(kb, fill)    DC_BUF_FIXED, kb, fill
#define BUF_EXTRAM       DC_BUF_EXTRAM, 0, 0   /* 블록 수 2~8 × 8 KiB (아니면 64 KiB) */
#define BUF_MEGARAM      DC_BUF_MEGARAM, 0, 0xFF   /* 블록 수 × 8 KiB (8 이하면 512 KiB, 1 MiB 까지) */
#define MEM_NONE         DC_MEM_NONE, 0, 0, BUF0
#define MEM_R(r)         DC_MEM_REGION, r, 0, BUF0
#define MEM_RF(r, f)     DC_MEM_REGION, r, f, BUF0
#define MEM_MAPRAM       DC_MEM_MAPRAM, 0, 0, BUF0
#define MEM_AUDIO        DC_MEM_AUDIO, REGION_ROM_NORMAL, DF_Y8950, BUF0   /* MSX-AUDIO: Y8950 + (파일 있으면) 롬 */
#define MEM_C(mp, buf)   DC_MEM_CART, mp, 0, buf

static const DevInfo cat[] = {
    {  23, "Normal RAM", DC_ED_RAM, 0, IO_NONE, MEM_R(REGION_RAM), 0 },
    {  93, "1kB Mirrored RAM", DC_ED_RAMFIX, 1, IO_NONE, MEM_R(REGION_RAM), 0 },
    { 111, "2kB Mirrored RAM", DC_ED_RAMFIX, 2, IO_NONE, MEM_R(REGION_RAM), 0 },
    {  22, "Mapped RAM", DC_ED_MAPRAM, 0, IO_NONE, MEM_MAPRAM, 0 | DC_FULLSLOT },
    {  69, "External RAM", DC_ED_MAPRAM, 0, IO_NONE, MEM_C(MAPPER_RAM, BUF_EXTRAM), 0 | DC_FULLSLOT },
    {  44, "MegaRAM", DC_ED_MAPRAM, 0, IO_NONE, MEM_C(MAPPER_MEGARAM, BUF_MEGARAM), 0 | DC_FULLSLOT },
    {  20, "Normal ROM", DC_ED_ROM, 0, IO_NONE, MEM_R(REGION_ROM_NORMAL), 0 },
    {  66, "Normal + Cassette Patch", DC_ED_ROM, 0, IO_NONE, MEM_R(REGION_ROM_NORMAL), 0 },
    {  21, "Normal + Disk Patch", DC_ED_ROM, 0, IO_NONE, MEM_R(REGION_ROM_NORMAL), 0 },
    {  57, "TC8566AF Disk Controller", DC_ED_WIN, 4, IO_NONE, MEM_R(REGION_DISK_ROM), 0 },
    { 143, "TC8566AF Turbo-R Disk Controller", DC_ED_WIN, 4, IO_NONE, MEM_R(REGION_DISK_ROM), 0 },
    {  63, "Microsol Disk Controller", DC_ED_WIN, 4, IO_NONE, MEM_R(REGION_DISK_ROM), 0 },
    {  64, "National Disk Controller", DC_ED_WIN, 4, IO_NONE, MEM_R(REGION_DISK_ROM), 0 },
    {  65, "Philips Disk Controller", DC_ED_WIN, 4, IO_NONE, MEM_R(REGION_DISK_ROM), 0 },
    {  67, "SVI-738 Disk Controller", DC_ED_WIN, 4, IO_NONE, MEM_R(REGION_DISK_ROM), 0 },
    {   2, "MSXDOS 2", DC_ED_WIN, 2, IO_NONE, MEM_R(REGION_ROM_DOS2), 0 },
    {  98, "Beer IDE", DC_ED_WIN, 2, IO_NONE, MEM_C(MAPPER_BEERIDE, BUF(0, 0xFF)), 0 },
    {  97, "GIDE", DC_ED_DEV, 0, IO_NONE, MEM_C(MAPPER_GIDE, BUF(16, 0xFF)), 0 },
    {  96, "Sunrise IDE", DC_ED_FULL, 8, IO_NONE, MEM_R(REGION_SUNRISE), 0 },
    { 135, "Gouda SCSI", DC_ED_WIN, 2, IO_NONE, MEM_C(MAPPER_GOUDA, BUF(0, 0xFF)), 0 },
    { 115, "MEGA-SCSI", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_MEGASCSI, BUF(512, 0xFF)), 0 },
    { 129, "WAVE-SCSI", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_WAVESCSI, BUF(512, 0xFF)), 0 },
    { 153, "Philips NMS1210 Serial Interface", DC_ED_DEV, 0, IO_NONE, MEM_NONE, 0 },
    { 101, "Philips NMS-8280 Digitizer", DC_ED_DEV, 0, IO_SILENT, MEM_NONE, 0 },
    {  32, "S1985", DC_ED_DEV, 0, IO_F(DF_S1985), MEM_NONE, 0 },
    {  58, "S1990", DC_ED_DEV, 0, IO_F(DF_S1990), MEM_NONE, 0 },
    {  33, "F4 Device Normal", DC_ED_DEV, 0, IO_F(DF_F4N), MEM_NONE, 0 },
    {  34, "F4 Device Inverted", DC_ED_DEV, 0, IO_F(DF_F4I), MEM_NONE, 0 },
    {  59, "Turbo-R Timer", DC_ED_DEV, 0, IO_F(DF_TRTIMER), MEM_NONE, 0 },
    {  90, "Turbo-R Pause", DC_ED_DEV, 0, IO_F(DF_TRPAUSE), MEM_NONE, 0 },
    {  84, "MSX Printer", DC_ED_DEV, 0, IO_SILENT, MEM_NONE, 0 },
    {  43, "PAC (SRAM)", DC_ED_NOFILE, 2, IO_NONE, MEM_R(REGION_PAC), 0 },
    {  16, "FMPAC (SRAM)", DC_ED_WIN, 2, IO_NONE, MEM_C(MAPPER_FMPAC, BUF(0, 0xFF)), 0 },
    {  87, "FMPAK", DC_ED_WIN, 2, IO_NONE, MEM_C(MAPPER_FMPAC, BUF(0, 0xFF)), 0 },
    {  88, "MSX-MIDI", DC_ED_DEV, 0, IO_F(DF_MIDI), MEM_NONE, 0 },
    {  78, "MSX Music", DC_ED_WIN, 2, IO_NONE, MEM_RF(REGION_ROM_NORMAL, DF_OPLL), 0 },
    {  50, "MSX Audio", DC_ED_WIN, 2, IO_NONE, MEM_AUDIO, 0 },
    {  80, "Y8950", DC_ED_DEV, 0, IO_F(DF_Y8950), MEM_NONE, 0 },
    {  79, "Moonsound", DC_ED_DEVFILE, 0, IO_F(DF_MOON), MEM_NONE, 0 },
    {  60, "Turbo-R PCM Chip", DC_ED_DEV, 0, IO_F(DF_PCM), MEM_NONE, 0 },
    { 106, "Yamaha SFG-01", DC_ED_ROM, 0, IO_NONE, MEM_R(REGION_SFG), 0 },
    { 105, "Yamaha SFG-05", DC_ED_ROM, 0, IO_NONE, MEM_R(REGION_SFG), 0 },
    { 147, "Joyrex PSG", DC_ED_DEV, 0, IO_F(DF_JOYREX), MEM_NONE, 0 },
    { 148, "Opcode PSG", DC_ED_DEV, 0, IO_NONE, MEM_NONE, 0 },
    {  92, "GameReader", DC_ED_DEV, 0, IO_NONE, MEM_NONE, 0 },
    { 134, "NoWind USB", DC_ED_WIN, 2, IO_NONE, MEM_NONE, 0 },
    { 110, "Obsonet", DC_ED_WIN, 2, IO_NONE, MEM_NONE, 0 },
    { 146, "Yamaha Net", DC_ED_DEV, 0, IO_NONE, MEM_NONE, 0 },
    {  26, "Matsushita SRAM", DC_ED_DEV, 0, IO_F(DF_MATSU), MEM_NONE, 0 },
    { 141, "Panasonic DRAM", DC_ED_ROM, 0, IO_NONE, MEM_R(REGION_ROM_NORMAL), 0 },
    { 139, "Panasonic FM 8kB SRAM", DC_ED_ROM, 0, IO_NONE, MEM_R(REGION_FW), 0 },
    { 142, "Panasonic WX 16kB SRAM", DC_ED_ROM, 0, IO_NONE, MEM_R(REGION_FW), 0 },
    {  27, "Panasonic 16kB SRAM", DC_ED_ROM, 0, IO_NONE, MEM_R(REGION_FW), 0 },
    {  68, "Panasonic 32kB SRAM", DC_ED_ROM, 0, IO_NONE, MEM_R(REGION_FW), 0 },
    { 140, "Panasonic Modem", DC_ED_ROM, 0, IO_SILENT, MEM_NONE, 0 },
    {  28, "Bunsetsu", DC_ED_ROM, 0, IO_NONE, MEM_R(REGION_BUNSETSU), 0 },
    {  29, "Jisyo", DC_ED_DEVFILE, 0, IO_JISYO, MEM_NONE, 0 },
    {  24, "Kanji", DC_ED_DEVFILE, 0, IO_KANJI, MEM_NONE, 0 },
    {  30, "Kanji12", DC_ED_DEVFILE, 0, IO_KANJI, MEM_NONE, 0 },
    {  31, "National (SRAM)", DC_ED_ROM, 0, IO_NONE, MEM_R(REGION_FW), 0 },
    {  77, "Sony HBI-55", DC_ED_DEV, 0, IO_NONE, MEM_NONE, 0 },
    { 102, "Sony HBI-V1 Digitizer", DC_ED_DEV, 0, IO_NONE, MEM_NONE, 0 },
    { 103, "SVI-727 80 Column Card", DC_ED_DEV, 0, IO_NONE, MEM_NONE, 0 },
    { 100, "Microsol VMX-80", DC_ED_ROM, 0, IO_NONE, MEM_NONE, 0 },
    { 104, "F&M Direct Assembler System", DC_ED_ROM, 0, IO_NONE, MEM_NONE, 0 },
    { 138, "Forte II", DC_ED_ROM, 0, IO_NONE, MEM_R(REGION_FORTE), 0 },
    {  19, "Mirrored ROM", DC_ED_FULL, 8, IO_NONE, MEM_R(REGION_ROM_NORMAL), 0 },
    {  42, "Normal 4000h", DC_ED_ROM, 0, IO_NONE, MEM_R(REGION_ROM_NORMAL), 0 },
    {  39, "Basic ROM", DC_ED_ROM, 0, IO_NONE, MEM_R(REGION_ROM_BASIC), 0 },
    {  86, "Normal C000h", DC_ED_ROM, 0, IO_NONE, MEM_R(REGION_ROM_NORMAL), 0 },
    {   5, "ASCII 8", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_ASCII8, BUF(0, 0xFF)), 0 },
    {   8, "ASCII 8 (SRAM)", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_ASCII8_SRAM, BUF(0, 0xFF)), 0 },
    {   6, "ASCII 16", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_ASCII16, BUF(0, 0xFF)), 0 },
    {   9, "ASCII 16 (SRAM)", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_ASCII16_SRAM, BUF(0, 0xFF)), 0 },
    {  38, "Koei (SRAM)", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_ASCII8_SRAM, BUF(0, 0xFF)), 0 },
    {   7, "Game Master 2 (SRAM)", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_GAMEMASTER2, BUF(0, 0xFF)), 0 },
    {  17, "Konami Generic", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_KONAMI, BUF(0, 0xFF)), 0 },
    {  91, "Konami Keyboard Master", DC_ED_WIN, 4, IO_NONE, MEM_R(REGION_ROM_NORMAL), 0 },
    {  62, "Konami Majutsushi", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_KONAMI, BUF(0, 0xFF)), 0 },
    {  61, "Konami Synthesizer", DC_ED_WIN, 4, IO_NONE, MEM_R(REGION_ROM_NORMAL), 0 },
    {  99, "Konami Word Pro", DC_ED_WIN, 4, IO_NONE, MEM_NONE, 0 },
    {   4, "Konami", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_KONAMI, BUF(0, 0xFF)), 0 },
    {   3, "Konami SCC", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_KONAMI_SCC, BUF(0, 0xFF)), 0 },
    {  55, "SCC", DC_ED_NOFILE, 4, IO_NONE, MEM_C(MAPPER_KONAMI_SCC, BUF(128, 0xFF)), 0 },
    {  54, "SCC mirrored", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_KONAMI_SCC, BUF(0, 0xFF)), 0 },
    {  15, "SCC extended", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_SCCPLUS, BUF(128, 0)), 0 },
    {  56, "SCC-I", DC_ED_NOFILE, 4, IO_NONE, MEM_C(MAPPER_SCCPLUS, BUF(128, 0)), 0 },
    {  52, "The Snatcher", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_SCCPLUS, BUF(128, 0)), 0 },
    {  53, "SD Snatcher", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_SCCPLUS, BUF(128, 0)), 0 },
    { 136, "Manbow 2", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_MFRSCC, BUF(512, 0xFF)), 0 },
    { 137, "Mega Flash Rom SCC", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_MFRSCC, BUF(512, 0xFF)), 0 },
    { 125, "Ese-SCC", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_ESESCC, BUF(512, 0xFF)), 0 },
    { 120, "Ese-RAM", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_ESERAM, BUF(512, 0xFF)), 0 },
    {  11, "Cross Blaim", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_CROSSBLAIM, BUF(0, 0xFF)), 0 },
    {  40, "Halnote", DC_ED_ROM, 0, IO_NONE, MEM_R(REGION_FW), 0 },
    {  12, "Harry Fox", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_HARRYFOX, BUF(0, 0xFF)), 0 },
    {  25, "Holy Quran", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_QURAN, BUF(0, 0xFF)), 0 },
    { 145, "Jaleco Moero!! Nettou Yakyuu '88", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_NETTOU, BUF(0, 0xFF)), 0 },
    {  41, "Lode Runner", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_LODERUNNER, BUF(0, 0xFF)), 0 },
    { 144, "Matra INK", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_MATRAINK, BUF(512, 0xFF)), 0 },
    {  10, "R-Type", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_RTYPE, BUF(0, 0xFF)), 0 },
    { 109, "Sony Playball", DC_ED_WIN, 4, IO_NONE, MEM_R(REGION_ROM_NORMAL), 0 },
    {  18, "Super Pierrot", DC_ED_WIN, 4, IO_NONE, MEM_NONE, 0 },
    {  13, "Zemina 80 in 1", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_ZEMINA80, BUF(0, 0xFF)), 0 },
    {  51, "Zemina 90 in 1", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_ZEMINA90, BUF(0, 0xFF)), 0 },
    {  14, "Zemina 126 in 1", DC_ED_WIN, 4, IO_NONE, MEM_C(MAPPER_ZEMINA126, BUF(0, 0xFF)), 0 },
    {  75, "SVI-328 Disk Controller", DC_ED_DEV, 0, IO_NONE, MEM_NONE, 0 },
    {  83, "SVI-328 Printer", DC_ED_DEV, 0, IO_NONE, MEM_NONE, 0 },
    {  85, "SVI-328 Serial Port", DC_ED_DEV, 0, IO_NONE, MEM_NONE, 0 },
    {  82, "SVI-328 80 Column Card", DC_ED_DEV, 0, IO_NONE, MEM_NONE, 0 },
    { 107, "SF-7000 IPL", DC_ED_ROM, 0, IO_NONE, MEM_NONE, 0 },
    { 159, "Matsushita SRAM (Turbo)", DC_ED_DEV, 0, IO_F(DF_MATSU_TURBO), MEM_NONE, DC_HIDDEN },
};
#define CAT_N ((int)(sizeof cat / sizeof cat[0]))

const DevInfo *devcat_find(int type) { for (int i = 0; i < CAT_N; i++) if (cat[i].type == type) return &cat[i]; return 0; }
int devcat_count(void) { return CAT_N; }
int devcat_editor_count(void) { int n = 0; while (n < CAT_N && !(cat[n].flags & DC_HIDDEN)) n++; return n; }
const DevInfo *devcat_at(int i) { return i >= 0 && i < CAT_N ? &cat[i] : 0; }
int devcat_supported(int type) { const DevInfo *d = devcat_find(type); return d && (d->io != DC_IO_NONE || d->mem != DC_MEM_NONE); }
