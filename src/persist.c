/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* persist.c - 5.2.33: 영속 데이터 선언 (persist.h 설명). 이름·꼴은 4.0.131 까지의 fe_state.inc sram_kind/sram_path 와 같음 */
#include "persist.h"
#include <string.h>

static void put(char *d, int max, const char *s) { int i = 0; while (s[i] && i < max - 1) { d[i] = s[i]; i++; } d[i] = 0; }
static void add(char *d, int max, const char *s) { int i = 0; while (d[i]) i++; while (*s && i < max - 1) d[i++] = *s++; d[i] = 0; }
static void addn(char *d, int max, uint32_t v) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v); char r[12]; int k = 0; while (n) r[k++] = t[--n]; r[k] = 0; add(d, max, r); }

int msx_cart_persist(MSX *m, int n, int has_rom, int special, MsxPersist *p)
{
    memset(p, 0, sizeof *p);
    if (n < 0 || n >= MSX_CART_ALL) return 0;
    Cartridge *c = &m->cart[n];
    if (!c->present) return 0;
    int builtin = n >= MSX_INT_CART;                    /* 기종 내장 자리 (내장 FM-PAC·PAC 는 따로) */
    switch (c->mapper) {
    case MAPPER_FMPAC: case MAPPER_PAC:                 /* 2.11·2.16: 머리 + 8190 */
        p->fmt = PS_PAC; p->data = c->sram; p->size = 8190; p->load_max = 8192;
        put(p->name, sizeof p->name, c->mapper == MAPPER_FMPAC ? (builtin ? "FMPAC_builtin.pac" : "FMPAC.pac") : (builtin ? "PAC_builtin.pac" : "PAC.pac"));
        return 1;
    case MAPPER_ASCII8_SRAM: case MAPPER_ASCII16_SRAM: case MAPPER_GAMEMASTER2:   /* 게임 배터리 SRAM: 롬 이름.sram (내장 자리는 안 남김) */
        if (!has_rom) return 0;
        p->fmt = PS_RAW; p->from_rom = 1; put(p->name, sizeof p->name, ".sram"); p->data = c->sram;
        p->size = p->load_max = c->mapper == MAPPER_ASCII8_SRAM && c->sram_kb == 32 ? 32768u : 8192u;   /* 3.8.0 */
        return 1;
    case MAPPER_ASCII16X: case MAPPER_YAMANOOTO: case MAPPER_FRSCC:   /* 4.0.131 (5.2.47 Flash-ROM SCC): 플래시 바뀐 조각. 5.2.43: 빈 카트리지(특수 카트리지 메뉴)는 고정 이름 */
        p->fmt = PS_FLASH; p->data = (uint8_t *)(uintptr_t)c->data; p->size = p->load_max = c->size;
        if (has_rom) { p->from_rom = 1; put(p->name, sizeof p->name, ".flash"); return 1; }
        if (special) { put(p->name, sizeof p->name, c->mapper == MAPPER_ASCII16X ? "ASCII16-X.flash" : c->mapper == MAPPER_FRSCC ? "FlashROM_SCC.flash" : "Yamanooto.flash"); return 1; }
        return 0;
    case MAPPER_HBI55:                                  /* Sony HBI-55 배터리 SRAM 4 KiB */
        p->fmt = PS_RAW; put(p->name, sizeof p->name, builtin ? "HBI-55_builtin.sram" : "HBI-55.sram"); p->data = c->sram; p->size = p->load_max = 4096;
        return 1;
    case MAPPER_FSCA1:                                  /* 3.4.1 */
        p->fmt = PS_RAW; put(p->name, sizeof p->name, "FS-CA1.sram"); p->data = c->sram; p->size = p->load_max = 8192;
        return 1;
    case MAPPER_ESERAM: case MAPPER_MFRSCC: case MAPPER_MFRSCCP: case MAPPER_ESESCC: case MAPPER_MEGASCSI: case MAPPER_WAVESCSI: case MAPPER_MATRAINK:   /* 3.1.1: 카트 메모리 전체 (5.2.46 MFR SCC+) */
        p->fmt = PS_RAW; p->data = (uint8_t *)(uintptr_t)c->data; p->size = p->load_max = c->size;
        if (has_rom && !special) { p->from_rom = 1; put(p->name, sizeof p->name, ".sram"); return 1; }   /* 롬 파일을 이 매퍼로 넣음 */
        if (c->mapper == MAPPER_MFRSCC) { put(p->name, sizeof p->name, "MegaFlashROM_SCC.flash"); return 1; }
        if (c->mapper == MAPPER_MFRSCCP) { put(p->name, sizeof p->name, "MegaFlashROM_SCCplus.flash"); return 1; }   /* 1 MiB, CPU 주소 순서 */
        put(p->name, sizeof p->name, c->mapper == MAPPER_ESESCC ? "EseSCC_" : c->mapper == MAPPER_MEGASCSI ? "MegaSCSI_" : c->mapper == MAPPER_WAVESCSI ? "WaveSCSI_" : "EseRAM_");
        addn(p->name, sizeof p->name, c->size / 1024); add(p->name, sizeof p->name, "KB.sram");
        return 1;
    default:
        return 0;
    }
}

uint32_t msx_fw_persist(MSX *m, MsxFwPersist *p)
{
    memset(p, 0, sizeof *p);
    if (!((m->fw.kind && m->fw.sram_size) || msx_dev(m, MDEV_MATSU))) return 0;
    p->data[0] = m->fw.sram; p->size[0] = m->fw.kind ? m->fw.sram_size : 0; p->dirty[0] = &m->fw.sram_dirty;
    p->data[1] = m->fw.mat_sram; p->size[1] = 2048; p->dirty[1] = &m->fw.mat_dirty;
    return p->size[0] + p->size[1];
}
