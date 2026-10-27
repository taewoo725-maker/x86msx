/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * mappers/ascii.inc - 카트리지 매퍼 부품: ASCII8·ASCII16·Nettou·ASCII SRAM 형  (5.2.4 구조 개편 3단계)
 * msx.c 가 한 번역 단위로 끼워 넣음 (msx.c 의 공용 도움 함수·블록을 그대로 씀, 인라인 가능).
 * 매퍼 하나 = cw_(쓰기, 1 = 뱅크 바뀜) + cm_(창 맞추기, 없으면 cm_default) + cr_(리셋, 없으면 cr_default). 표는 mappers/table.inc
 */

static int cw_ascii8(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    if (a >= 0x6000 && a < 0x8000) c->bank[(a >> 11) & 3] = v;
    else return 0;
    return 1;
}

static int cw_ascii16(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    if (a >= 0x6000 && a < 0x8000 && !(a & 0x0800)) {
        int i = ((a >> 12) & 1) * 2;
        c->bank[i] = (uint8_t)(v * 2);
        c->bank[i + 1] = (uint8_t)(v * 2 + 1);
    } else return 0;
    return 1;
}

static int cw_ascii8_sram(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    if (a >= 0x6000 && a < 0x8000) { c->bank[(a >> 11) & 3] = v; return 1; }
    if (a >= 0x8000 && a < 0xC000 && (c->bank[(a >> 13) - 2] & cart_sram_bit(c))) {
        if (c->sram_kb == 2) { for (int k = 0; k < 4; k++) c->sram[(a & 0x7FF) + k * 0x800] = v; }     /* 3.8.0: 2 KiB는 8 KiB 안에 네 번 거울 */
        else c->sram[sram_off8(c, c->bank[(a >> 13) - 2]) + (a & 0x1FFF)] = v;
        c->sram_dirty = 1; return 0;
    }
    return 0;
}

static int cw_ascii16_sram(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    if (a >= 0x6000 && a < 0x8000 && !(a & 0x0800)) {
        int i = ((a >> 12) & 1) * 2;
        c->bank[i] = (uint8_t)(v * 2); c->bank[i + 1] = (uint8_t)(v * 2 + 1);
        return 1;
    }
    if (a >= 0x8000 && a < 0xC000 && ((c->bank[2] >> 1) & cart_sram_bit(c))) {   /* 2 KiB를 8 KiB 안에 네 번 거울 */
        for (int k = 0; k < 4; k++) c->sram[(a & 0x7FF) + k * 0x800] = v;
        c->sram_dirty = 1;
        return 0;
    }
    return 0;
}

static void cm_ascii8_sram(MSX *m, Cartridge *c, int s, int q)
{
    (void)m; (void)c; (void)s; (void)q;
    /* 2.11: SRAM 비트가 선 뱅크는 SRAM */
    uint32_t bit = cart_sram_bit(c);
    for (int i = 0; i < 4; i++) {
        uint32_t b = c->mapper == MAPPER_ASCII16_SRAM ? (uint32_t)(c->bank[i & 2] >> 1) : c->bank[i];
        m->slot_rd[s][q][2 + i] = (b & bit) ? c->sram + (c->mapper == MAPPER_ASCII8_SRAM ? sram_off8(c, b) : 0) : cart_block(c, c->bank[i]);
    }
    return;
}
