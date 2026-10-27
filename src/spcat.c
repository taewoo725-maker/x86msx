/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* spcat.c - 특수 카트리지(외부장치) 목록 표 (5.2.44). 설명은 spcat.h. 예전 fe.c sp_defs + SPECIAL_MENU 매크로를 표 하나로 */
#include "spcat.h"
#include "cart.h"

/* 묶음: blueMSX+ Insert Special 차례. pos 는 부모 안에서 줄과 같은 번호 공간 (맨 위 묶음은 10 단위) */
const SpGroup sp_groups[SPG_N] = {
    [SPG_ROOT]      = { SPG_ROOT,      SPG_ROOT, "Insert Special",   0, 0 },
    [SPG_ESESCC]    = { SPG_ESESCC,    SPG_ROOT, "Ese-SCC",          13, 1 },
    [SPG_IDE]       = { SPG_IDE,       SPG_ROOT, "IDE",              20, 0 },
    [SPG_SCSI]      = { SPG_SCSI,      SPG_ROOT, "SCSI",             21, 0 },
    [SPG_MEGASCSI]  = { SPG_MEGASCSI,  SPG_SCSI, "MEGA-SCSI",        0, 0 },
    [SPG_WAVESCSI]  = { SPG_WAVESCSI,  SPG_SCSI, "WAVE-SCSI",        1, 0 },
    [SPG_NOWIND]    = { SPG_NOWIND,    SPG_ROOT, "Nowind USB Disk Controller", 22, 1 },
    [SPG_EXTRAM]    = { SPG_EXTRAM,    SPG_ROOT, "External RAM",     50, 0 },
    [SPG_MEGARAM]   = { SPG_MEGARAM,   SPG_ROOT, "MegaRAM",          51, 0 },
    [SPG_ESERAM]    = { SPG_ESERAM,    SPG_ROOT, "Ese-RAM",          52, 0 },
    [SPG_FLASHROM]  = { SPG_FLASHROM,  SPG_ROOT, "Mega Flash ROM",   53, 0 },
    [SPG_FLASHCART] = { SPG_FLASHCART, SPG_ROOT, "Flash cartridge",  54, 1 },
    [SPG_DISK]      = { SPG_DISK,      SPG_ROOT, "Disk Interface",   60, 0 },
    [SPG_AUDIO]     = { SPG_AUDIO,     SPG_ROOT, "MSX-AUDIO",        62, 1 },
    [SPG_NET]       = { SPG_NET,       SPG_ROOT, "Network",          71, 0 },
};

#define SR "Shared Roms\\"
static const SpCart cat[] = {
    /* kind, 묶음, 차례, 구분선, 이름, how, arg, 롬 후보, 롬 DB 종류 */
    /* ---- 맨 위 (blueMSX+ 차례) ---- */
    { 14, SPG_ROOT, 10, 0, "Joyrex PSG", SPK_JOYREX, 0, 0, 0 },
    {  2, SPG_ROOT, 11, 0, "SCC", SPK_SCC, 0, 0, 0 },
    { 10, SPG_ROOT, 12, 0, "SCC-I", SPK_SCCI, 0, 0, 0 },
    /* 13 Ese-SCC ▶, 20 IDE ▶, 21 SCSI ▶, 22 Nowind ▶ */
    {  3, SPG_ROOT, 30, 0, "FM-PAC", SPK_FMPAC, 0, SR "FMPAC.rom|" SR "PAC.rom|" SR "fmpac_en.rom", "FMPAC|FMPAK" },
    { 15, SPG_ROOT, 31, 1, "PAC (SRAM)", SPK_PAC, 0, 0, 0 },
    { 62, SPG_ROOT, 40, 0, "Sony HBI-55", SPK_HBI55, 0, 0, 0 },                        /* 5.2.45 보강 1단계: SRAM 4 KiB, i8255 B0h~B3h */
    { 65, SPG_ROOT, 41, 0, "Philips NMS1210 Serial Interface", SPK_RS232, 0, 0, 0 },
    { 66, SPG_ROOT, 42, 1, "MSX-MIDI Interface", SPK_MSXMIDI, 0, 0, 0 },              /* 외장 MSX-MIDI (8251+8253 E8h~EFh, GT 내장과 같음) */
    { 67, SPG_ROOT, 43, 0, "X86MSX MIDI", SPK_MSXMIDI, 0, 0, 0 },                     /* X86MSX 자체 MIDI 카트리지 (MSX-MIDI 호환 8251+8253, E8h~EFh). GT 가 아닌 기종에 MIDI 기계를 꽂으면 자동으로 */
    /* 50 외장 RAM ▶, 51 MegaRAM ▶, 52 Ese-RAM ▶, 53 Mega Flash ROM ▶, 54 플래시 카트리지 ▶ */
    /* ---- X86MSX 만 (blueMSX 에 없음) ---- */
    /* 60 디스크 인터페이스 ▶ */
    {  4, SPG_ROOT, 61, 0, "MSX-DOS2", SPK_DOS2, 0, SR "MSXDOS2.ROM|" SR "MSXDOS22.ROM|" SR "MSXDOS21.ROM|" SR "DOS22ASCII.ROM|" SR "ascii_msxdos22.rom|MSX2 - MSX-DOS2\\MSXDOS22.ROM|MSX2+ - MSX-DOS2\\MSXDOS22.ROM", "MSXDOS2" },
    /* 62 MSX-AUDIO ▶ */
    { 58, SPG_ROOT, 70, 0, "Sunrise GFX9000 (V9990)", SPK_V9990, 0, 0, 0 },
    /* 71 네트워크 ▶ */

    /* ---- Ese-SCC ▶ ---- */
    { 42, SPG_ESESCC, 0, 0, "Ese-SCC 128 KB", SPK_ESESCC, 128, 0, 0 },
    { 43, SPG_ESESCC, 1, 0, "Ese-SCC 256 KB", SPK_ESESCC, 256, 0, 0 },
    { 44, SPG_ESESCC, 2, 0, "Ese-SCC 512 KB", SPK_ESESCC, 512, 0, 0 },
    /* ---- IDE ▶ ---- */
    { 11, SPG_IDE, 0, 0, "IDE (Sunrise)", SPK_IDE, 0, SR "SUNRISEIDE.rom|" SR "ide240.dat", "SunriseIDE" },
    { 13, SPG_IDE, 1, 0, "IDE (Sunrise, Nextor)", SPK_IDE, 0, SR "Nextor-2.1.1.SunriseIDE.ROM|" SR "Nextor-2.1.0.SunriseIDE.ROM|" SR "Nextor-2.1.0.SunriseIDE.emulators.ROM|" SR "Nextor-2.0.1.SunriseIDE.rom|" SR "Nextor-2.0.SunriseIDE.rom", 0 },
    { 40, SPG_IDE, 2, 0, "IDE (Beer)", SPK_BEER, 0, SR "BEERIDE.ROM|" SR "beer-ide.rom", "BeerIDE" },
    { 41, SPG_IDE, 3, 1, "IDE (GIDE)", SPK_GIDE, 0, 0, 0 },
    { 49, SPG_IDE, 4, 0, "X86MSX VHD (Nextor)", SPK_VHD, 0, SR "Nextor-2.1.3.base.dat|" SR "Nextor-2.1.3.StandaloneASCII16.ROM|" SR "Nextor-2.1.3.SunriseIDE.ROM|" SR "Nextor-2.0.1.SunriseIDE.rom", 0 },
    /* ---- SCSI ▶ (MEGA ▶ / WAVE ▶ / Gouda) ---- */
    { 50, SPG_MEGASCSI, 0, 0, "MEGA-SCSI 128 KB", SPK_MEGASCSI, 128, 0, 0 },
    { 51, SPG_MEGASCSI, 1, 0, "MEGA-SCSI 256 KB", SPK_MEGASCSI, 256, 0, 0 },
    { 52, SPG_MEGASCSI, 2, 0, "MEGA-SCSI 512 KB", SPK_MEGASCSI, 512, 0, 0 },
    { 53, SPG_MEGASCSI, 3, 0, "MEGA-SCSI 1 MB", SPK_MEGASCSI, 1024, 0, 0 },
    { 54, SPG_WAVESCSI, 0, 0, "WAVE-SCSI 128 KB", SPK_WAVESCSI, 128, 0, 0 },
    { 55, SPG_WAVESCSI, 1, 0, "WAVE-SCSI 256 KB", SPK_WAVESCSI, 256, 0, 0 },
    { 56, SPG_WAVESCSI, 2, 0, "WAVE-SCSI 512 KB", SPK_WAVESCSI, 512, 0, 0 },
    { 57, SPG_WAVESCSI, 3, 0, "WAVE-SCSI 1 MB", SPK_WAVESCSI, 1024, 0, 0 },
    { 45, SPG_SCSI, 2, 0, "SCSI (Gouda)", SPK_GOUDA, 0, SR "NOVAXIS.rom|" SR "novaxis.rom", "GoudaSCSI" },
    /* ---- Nowind ▶ (안 함: PC USB 장치) ---- */
    { -1, SPG_NOWIND, 0, 0, "Nowind MSXDOS1", SPK_NOT_YET, 0, 0, 0 },
    { -1, SPG_NOWIND, 1, 0, "Nowind MSXDOS2", SPK_NOT_YET, 0, 0, 0 },
    /* ---- 외장 RAM ▶ ---- */
    { 21, SPG_EXTRAM, 0, 0, "RAM 16 KB", SPK_RAM, 16, 0, 0 },
    { 22, SPG_EXTRAM, 1, 0, "RAM 32 KB", SPK_RAM, 32, 0, 0 },
    { 23, SPG_EXTRAM, 2, 0, "RAM 48 KB", SPK_RAM, 48, 0, 0 },
    { 24, SPG_EXTRAM, 3, 1, "RAM 64 KB", SPK_RAM, 64, 0, 0 },
    {  5, SPG_EXTRAM, 4, 0, "RAM 256 KB", SPK_MAPRAM, 256, 0, 0 },
    {  6, SPG_EXTRAM, 5, 0, "RAM 512 KB", SPK_MAPRAM, 512, 0, 0 },
    {  7, SPG_EXTRAM, 6, 0, "RAM 1 MB", SPK_MAPRAM, 1024, 0, 0 },
    {  8, SPG_EXTRAM, 7, 0, "RAM 2 MB", SPK_MAPRAM, 2048, 0, 0 },
    {  9, SPG_EXTRAM, 8, 0, "RAM 4 MB", SPK_MAPRAM, 4096, 0, 0 },
    /* ---- MegaRAM ▶ ---- */
    { 16, SPG_MEGARAM, 0, 0, "MegaRAM 128 KB", SPK_MEGARAM, 128, 0, 0 },
    { 17, SPG_MEGARAM, 1, 0, "MegaRAM 256 KB", SPK_MEGARAM, 256, 0, 0 },
    { 18, SPG_MEGARAM, 2, 0, "MegaRAM 512 KB", SPK_MEGARAM, 512, 0, 0 },
    { 19, SPG_MEGARAM, 3, 0, "MegaRAM 768 KB", SPK_MEGARAM, 768, 0, 0 },
    { 20, SPG_MEGARAM, 4, 0, "MegaRAM 2 MB", SPK_MEGARAM, 2048, 0, 0 },
    /* ---- Ese-RAM ▶ ---- */
    { 35, SPG_ESERAM, 0, 0, "Ese-RAM 128 KB", SPK_ESERAM, 128, 0, 0 },
    { 36, SPG_ESERAM, 1, 0, "Ese-RAM 256 KB", SPK_ESERAM, 256, 0, 0 },
    { 37, SPG_ESERAM, 2, 0, "Ese-RAM 512 KB", SPK_ESERAM, 512, 0, 0 },
    { 38, SPG_ESERAM, 3, 0, "Ese-RAM 1 MB", SPK_ESERAM, 1024, 0, 0 },
    /* ---- Mega Flash ROM ▶ ---- */
    { 39, SPG_FLASHROM, 0, 0, "Mega Flash ROM SCC", SPK_MFR, 512, 0, 0 },
    { 63, SPG_FLASHROM, 1, 0, "Mega Flash ROM SCC+", SPK_MFRP, 0, 0, 0 },          /* 5.2.46 보강 2단계: M29W800DB 1 MiB + SCC-I + PSG */
    /* ---- 플래시 카트리지 ▶ (5.2.43) ---- */
    { 60, SPG_FLASHCART, 0, 0, "ASCII16-X", SPK_FLASH, MAPPER_ASCII16X, 0, 0 },
    { 61, SPG_FLASHCART, 1, 0, "Yamanooto", SPK_FLASH, MAPPER_YAMANOOTO, 0, 0 },
    { 64, SPG_FLASHCART, 2, 0, "Flash-ROM SCC", SPK_FLASH, MAPPER_FRSCC, 0, 0 },     /* 5.2.47 보강 3단계: popolon Developer Edition 16 MiB */
    /* ---- 디스크 인터페이스 ▶ (X86MSX) ---- */
    {  0, SPG_DISK, 0, 0, "Disk (National)", SPK_DISK, 64, SR "NATIONALDISK.rom|" SR "PANASONICDISK.rom", "National" },
    {  1, SPG_DISK, 1, 1, "Disk (Philips)", SPK_DISK, 65, SR "PHILIPSDISK.rom", "WD2793" },
    { 27, SPG_DISK, 2, 0, "Disk (FS-FD1A)", SPK_DISK, 57, SR "fs-fd1a.rom", "TC8566AF" },
    { 28, SPG_DISK, 3, 0, "Disk (HBD-F1)", SPK_DISK, 65, SR "hbd-f1.rom", "WD2793" },
    { 29, SPG_DISK, 4, 0, "Disk (VY-0010)", SPK_DISK, 65, SR "vy0010_marked_VG8245_v1.05.rom|" SR "vy0010_JVC.rom", "WD2793" },
    { 30, SPG_DISK, 5, 0, "Disk (NMS-1200)", SPK_DISK, 65, SR "nms1200_4.rom|" SR "nms1200_6.rom", "WD2793" },
    { 31, SPG_DISK, 6, 0, "Disk (ML-30DC)", SPK_DISK, 65, SR "ml-30dc.rom", "WD2793" },
    { 32, SPG_DISK, 7, 0, "Disk (HB-3600)", SPK_DISK, 65, SR "hb3600_disk.rom", "WD2793" },
    { 33, SPG_DISK, 8, 0, "Disk (DPF-550)", SPK_DISK, 64, SR "avt_dpf-550_disk.rom", "MB8877A" },
    /* ---- MSX-AUDIO ▶ ---- */
    { 12, SPG_AUDIO, 0, 0, "MSX-AUDIO", SPK_AUDIO, 0, 0, 0 },
    { 25, SPG_AUDIO, 1, 0, "MSX-AUDIO (Philips)", SPK_AUDIOROM, 0, SR "nms1205.rom|" SR "MusicModulePH12.ROM|" SR "MusicModulePH11.ROM", "MSX-AUDIO" },
    { 26, SPG_AUDIO, 2, 0, "MSX-AUDIO (Toshiba)", SPK_AUDIOROM, 0, SR "hx-mu900.rom", "MSX-AUDIO" },
    { 46, SPG_AUDIO, 3, 1, "MSX-AUDIO (FS-CA1)", SPK_CA1, 0, SR "fs-ca1.rom|" SR "FS-CA1.rom|" SR "fs-ca1_CLEAN.rom|" SR "Panasonic FS-CA1 Jorito.rom", 0 },
    { 34, SPG_AUDIO, 4, 1, "MoonSound", SPK_MOON, 0, 0, 0 },
    { 47, SPG_AUDIO, 5, 0, "Yamaha SFG-01", SPK_SFG, 0, SR "SFG01.ROM|" SR "sfg01.rom|" SR "yamaha_sfg-01.rom", 0 },
    { 48, SPG_AUDIO, 6, 0, "Yamaha SFG-05", SPK_SFG, 0, SR "SFG05.rom|" SR "sfg05.rom|" SR "yamaha_sfg-05.rom", 0 },
    /* ---- 네트워크 ▶ ---- */
    { 59, SPG_NET, 0, 0, "X86Network", SPK_NET, 0, SR "UNAPI.ROM|" SR "unapi.rom|" SR "X86MSX_UNAPI.ROM", 0 },
};
#define N ((int)(sizeof cat / sizeof cat[0]))

const SpCart *spcat_at(int i) { return i >= 0 && i < N ? &cat[i] : 0; }
int spcat_count(void) { return N; }
const SpCart *spcat_kind(int kind)
{
    if (kind < 0) return 0;
    for (int i = 0; i < N; i++) if (cat[i].kind == kind) return &cat[i];
    return 0;
}
int spcat_max_kind(void)
{
    int m = 0;
    for (int i = 0; i < N; i++) if (cat[i].kind + 1 > m) m = cat[i].kind + 1;
    return m;
}
