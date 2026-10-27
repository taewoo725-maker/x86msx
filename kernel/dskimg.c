/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* dskimg.c - 3.0.2 빈 디스크·하드 디스크 이미지 (dskimg.h 참고) */
#include "dskimg.h"
#include <string.h>

/* MSX-DOS 1·2가 아는 매체 (Disk BASIC FORMAT과 같은 BPB) */
const DskFormat dsk_formats[DSK_FORMATS] = {
    { "3.5\" 2DD 720 KB  (2-side 80 trk 9 sec, F9)",  0xF9, 1440,  9, 2, 80, 2, 3, 2, 112 },
    { "3.5\" 1DD 360 KB  (1-side 80 trk 9 sec, F8)",  0xF8,  720,  9, 1, 80, 2, 2, 2, 112 },
    { "3.5\" 2DD 640 KB  (2-side 80 trk 8 sec, FB)",  0xFB, 1280,  8, 2, 80, 2, 2, 2, 112 },
    { "3.5\" 1DD 320 KB  (1-side 80 trk 8 sec, FA)",  0xFA,  640,  8, 1, 80, 2, 1, 2, 112 },
    { "5.25\" 2D 360 KB  (2-side 40 trk 9 sec, FD)",  0xFD,  720,  9, 2, 40, 2, 2, 2, 112 },
    { "5.25\" 2D 320 KB  (2-side 40 trk 8 sec, FF)",  0xFF,  640,  8, 2, 40, 2, 1, 2, 112 },
    { "5.25\" 1D 180 KB  (1-side 40 trk 9 sec, FC)",  0xFC,  360,  9, 1, 40, 1, 2, 2,  64 },
    { "5.25\" 1D 160 KB  (1-side 40 trk 8 sec, FE)",  0xFE,  320,  8, 1, 40, 1, 1, 2,  64 },
    { "3.5\" 2HD 1.44 MB (2-side 80 trk 18 sec, F0)", 0xF0, 2880, 18, 2, 80, 1, 9, 2, 224 },
};

const uint16_t hd_sizes_mb[HD_SIZES] = { 4, 16, 32, 64, 128, 256, 512, 1024, 2047 };

static void w16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void w32(uint8_t *p, uint32_t v) { w16(p, v); w16(p + 2, v >> 16); }
static void put_label(uint8_t *p, const char *label, const char *def)
{
    const char *s = label && label[0] ? label : def;
    for (int i = 0; i < 11; i++) {
        char c = *s ? *s++ : ' ';
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        p[i] = (uint8_t)c;
    }
}
/* 루트 디렉터리 첫 항목 = 볼륨 이름 */
static void put_volume_entry(uint8_t *root, const char *label)
{
    if (!label || !label[0]) return;
    put_label(root, label, "");
    root[11] = 0x08;
    w16(root + 22, 0); w16(root + 24, (2026 - 1980) << 9 | 1 << 5 | 1);
}

uint32_t dsk_build(int fmt, uint8_t *out, const char *label)
{
    if (fmt < 0 || fmt >= DSK_FORMATS) return 0;
    const DskFormat *f = &dsk_formats[fmt];
    uint32_t size = (uint32_t)f->sectors * 512;
    memset(out, 0, size);
    uint8_t *b = out;
    b[0] = 0xEB; b[1] = 0xFE; b[2] = 0x90;                  /* MSX 부트 섹터 (EB FE 90) */
    memcpy(b + 3, "X86MSX  ", 8);
    w16(b + 0x0B, 512);
    b[0x0D] = f->spc;
    w16(b + 0x0E, 1);
    b[0x10] = f->nfats;
    w16(b + 0x11, f->root_entries);
    w16(b + 0x13, f->sectors);
    b[0x15] = f->media;
    w16(b + 0x16, f->fat_secs);
    w16(b + 0x18, f->spt);
    w16(b + 0x1A, f->heads);
    w16(b + 0x1C, 0);
    b[0x1E] = 0xC9;                                         /* 부트 코드: 시스템 없음 → Disk BASIC */
    b[510] = 0x55; b[511] = 0xAA;
    for (int k = 0; k < f->nfats; k++) {                    /* FAT: 매체 바이트 + FF FF */
        uint8_t *fat = out + (1 + (uint32_t)k * f->fat_secs) * 512;
        fat[0] = f->media; fat[1] = 0xFF; fat[2] = 0xFF;
    }
    put_volume_entry(out + (1 + (uint32_t)f->nfats * f->fat_secs) * 512, label);
    return size;
}

/* 3.6.1: 디렉토리 삽입용 부트 코드 (blueMSX "Insert Directory"처럼 MSXDOS.SYS가 있으면 MSX-DOS로 부팅).
 * 디스크 ROM이 $C01E를 CY=1로 부르면 BDOS($F37D)로 MSXDOS.SYS를 열어 $0100에 읽고 실행.
 * 없으면(또는 디스크 오류면: HL이 가리키는 자리에 그리로 가게 적음) SP를 되돌려 그냥 돌아감 → Disk BASIC (AUTOEXEC.BAS).
 * X86MSX가 새로 짠 코드 */
void dsk_boot_dos1(uint8_t *out)
{
    static const uint8_t code[] = {
        0xD0,                               /* C01E RET NC           (첫 번째 부름은 그냥 돌아감) */
        0xED, 0x73, 0x57, 0xC0,             /*      LD (C057h),SP */
        0x36, 0x52, 0x23, 0x36, 0xC0,       /*      LD (HL),C052h    디스크 오류 때 갈 곳 */
        0x31, 0x1F, 0xF5,                   /*      LD SP,F51Fh */
        0x11, 0x59, 0xC0, 0x0E, 0x0F, 0xCD, 0x7D, 0xF3,   /* FCB 열기 (BDOS 0Fh) */
        0x3C, 0x28, 0x1C,                   /*      INC A / JR Z,C052h  (없음) */
        0x11, 0x00, 0x01, 0x0E, 0x1A, 0xCD, 0x7D, 0xF3,   /* DTA = 0100h (BDOS 1Ah) */
        0x21, 0x01, 0x00, 0x22, 0x67, 0xC0, /*      레코드 크기 1 */
        0x21, 0x00, 0x3F, 0x11, 0x59, 0xC0, 0x0E, 0x27, 0xCD, 0x7D, 0xF3,   /* 3F00h 바이트 읽기 (BDOS 27h) */
        0xC3, 0x00, 0x01,                   /*      JP 0100h */
        0xED, 0x7B, 0x57, 0xC0, 0xC9,       /* C052 LD SP,(C057h) / RET  → Disk BASIC */
        0x00, 0x00,                         /* C057 SP 저장 */
        0x00, 'M', 'S', 'X', 'D', 'O', 'S', ' ', ' ', 'S', 'Y', 'S'   /* C059 FCB (나머지 0) */
    };
    memset(out + 0x1E, 0, 0x1FE - 0x1E);
    memcpy(out + 0x1E, code, sizeof code);
}

int hd_size_ok(int kind, uint32_t mb)
{
    if (kind == HDI_DOS2_FAT12) return mb >= 4 && mb <= 32;
    if (kind == HDI_NEXTOR_FAT16) return mb >= 16 && mb <= 2047;
    return mb >= 4 && mb <= 2047;
}

uint32_t hd_build_prefix(int kind, uint32_t total, uint8_t *out, uint32_t max, const char *label)
{
    if (kind == HDI_BLANK) { if (max < 512) return 0; memset(out, 0, 512); return 512; }   /* MBR 자리도 비움 → MSX에서 FDISK */
    uint32_t start = 1, part = total - start, spc, clusters, fatsz, root = 512, rootsec = root * 32 / 512;
    int fat16 = kind == HDI_NEXTOR_FAT16;
    for (spc = 1; spc <= 128; spc <<= 1) {                  /* 클러스터 수가 형식 범위에 들어가는 가장 작은 크기 */
        uint32_t c = part / spc;
        fatsz = fat16 ? (c * 2 + 4 + 511) / 512 : ((c * 3 + 1) / 2 + 3 + 511) / 512;
        clusters = (part - 1 - 2 * fatsz - rootsec) / spc;
        if (fat16 ? clusters < 65525 : clusters < 4085) break;
    }
    if (spc > 128 || (fat16 && clusters < 4085)) return 0;
    fatsz = fat16 ? ((clusters + 2) * 2 + 511) / 512 : (((clusters + 2) * 3 / 2 + 1) + 511) / 512;   /* 실제 클러스터 수에 딱 맞게 (vfat.c와 같음; Nextor가 큰 FAT를 거절) */
    if (!fat16 && spc > 64) return 0;
    uint32_t need = (start + 1 + 2 * fatsz + rootsec) * 512;
    if (need > max) return 0;
    memset(out, 0, need);
    /* MBR: 파티션 하나 */
    uint8_t *e = out + 446;
    e[0] = 0x00;
    e[1] = 0xFE; e[2] = 0xFF; e[3] = 0xFF;                  /* CHS: 쓰지 않음 (LBA) */
    e[4] = fat16 ? (part >= 65536 ? 0x06 : 0x04) : 0x01;
    e[5] = 0xFE; e[6] = 0xFF; e[7] = 0xFF;
    w32(e + 8, start); w32(e + 12, part);
    out[510] = 0x55; out[511] = 0xAA;
    /* 파티션 부트 섹터 */
    uint8_t *b = out + start * 512;
    b[0] = 0xEB; b[1] = fat16 ? 0x3C : 0xFE; b[2] = 0x90;
    memcpy(b + 3, fat16 ? "NEXTOR20" : "MSXDOS20", 8);
    w16(b + 0x0B, 512);
    b[0x0D] = (uint8_t)spc;
    w16(b + 0x0E, 1);
    b[0x10] = 2;
    w16(b + 0x11, root);
    if (part < 65536) w16(b + 0x13, part); else w32(b + 0x20, part);
    b[0x15] = 0xF8;
    w16(b + 0x16, fatsz);
    w16(b + 0x18, 32); w16(b + 0x1A, 16);
    if (fat16) {                                            /* Nextor: PC와 같은 확장 BPB */
        w32(b + 0x1C, start);
        b[0x24] = 0x80; b[0x26] = 0x29;
        w32(b + 0x27, 0x58384D53u);                         /* 볼륨 번호 */
        put_label(b + 0x2B, label, "NO NAME");
        memcpy(b + 0x36, "FAT16   ", 8);
    } else {                                                /* 3.4.4: MSX-DOS2 부트 섹터 — 커널이 C01E를 부르므로 1Eh에 코드(JR 3Eh).
                                                               전에는 PC 모양(1Ch~1Fh 숨은 섹터 32비트, 26h 29h)이라 1Eh부터 엉뚱한 코드가 돌아 DOS2 커널이 멈췄음 */
        w16(b + 0x1C, (uint16_t)start);
        b[0x1E] = 0x18; b[0x1F] = 0x1E;
        memcpy(b + 0x20, "VOL_ID", 6);
        b[0x26] = 0x00;                                     /* 되살리기 정보 없음 */
        w32(b + 0x27, 0x58384D53u);
    }
    b[0x3E] = 0xC9;
    b[510] = 0x55; b[511] = 0xAA;
    for (int k = 0; k < 2; k++) {
        uint8_t *fat = b + (1 + (uint32_t)k * fatsz) * 512;
        fat[0] = 0xF8; fat[1] = 0xFF; fat[2] = 0xFF;
        if (fat16) fat[3] = 0xFF;
    }
    put_volume_entry(b + (1 + 2 * fatsz) * 512, label);
    return need;
}
