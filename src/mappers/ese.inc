/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * mappers/ese.inc - 카트리지 매퍼 부품: ESE-SCC·ESE-RAM·MEGA-SCSI·WAVE-SCSI  (5.2.4 구조 개편 3단계)
 * msx.c 가 한 번역 단위로 끼워 넣음 (msx.c 의 공용 도움 함수·블록을 그대로 씀, 인라인 가능).
 * 매퍼 하나 = cw_(쓰기, 1 = 뱅크 바뀜) + cm_(창 맞추기, 없으면 cm_default) + cr_(리셋, 없으면 cr_default). 표는 mappers/table.inc
 */

static int cw_esescc(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    {                               /* 3.4.1: Ese-SCC (blueMSX ESE-SCC와 같은 동작, SPC 없는 형) */
    if (a < 0x4000 || a >= 0xC000) return 0;
    int w = (a >> 13) - 2;
    if (c->bank[2] == 0x3F && a >= 0x9800 && a < 0xA000) { scc_catch_up(m, c); scc_write(&c->scc, (uint8_t)a, v); return 0; }
    if ((a | 1) == 0x7FFF) { c->ese_we = (v & 0x10) ? 1 : 0; return 0; }
    if (c->ese_we && w < 2) { ((uint8_t *)(uintptr_t)cart_block(c, c->bank[w]))[a & 0x1FFF] = v; c->sram_dirty = 1; return 0; }
    if ((a & 0x1800) == 0x1000 && (w >= 2 || !c->ese_we)) { c->bank[w] = v & 0x3F; return 1; }
    return 0;
}
}

static int cw_eseram(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    /* 3.1.1 */
    if (a >= 0x6000 && a < 0x8000) { c->bank[(a >> 11) & 3] = v; return 1; }
    if (a >= 0x4000 && a < 0xC000) {
        int w = (a >> 13) - 2;
        if (c->bank[w] & 0x80) { ((uint8_t *)(uintptr_t)cart_block(c, c->bank[w] & 0x7F))[a & 0x1FFF] = v; c->sram_dirty = 1; }
    }
    return 0;
}

static void cm_eseram(MSX *m, Cartridge *c, int s, int q)
{
    (void)m; (void)c; (void)s; (void)q;
    for (int i = 0; i < 4; i++) m->slot_rd[s][q][2 + i] = cart_block(c, c->bank[i] & 0x7F);
}

static int cw_megascsi(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    {                             /* 3.9.14: MEGA-SCSI */
    if (a < 0x4000 || a >= 0xC000) return 0;
    if (a >= 0x6000 && a < 0x8000) { c->bank[(a >> 11) & 3] = v; return 1; }
    int w = (a >> 13) - 2;
    uint8_t bv = c->bank[w];
    if ((bv & 0xC0) == 0x40) { spc_mem_write(m, a, v); return 0; }
    if (bv & 0x80) { ((uint8_t *)(uintptr_t)cart_block(c, bv))[a & 0x1FFF] = v; c->sram_dirty = 1; }
    return 0;
}
}

static void cm_megascsi(MSX *m, Cartridge *c, int s, int q)
{
    (void)m; (void)c; (void)s; (void)q;
    /* 3.9.14: 뱅크 0x40~0x7F는 SPC (읽기는 mem_read가 가로챔) */
    for (int i = 0; i < 4; i++) m->slot_rd[s][q][2 + i] = (c->bank[i] & 0xC0) == 0x40 ? ff_block : cart_block(c, c->bank[i]);
    return;
}

static int cw_wavescsi(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    {                             /* 3.9.14: WAVE-SCSI (= MEGA-SCSI + SCC, openMSX ESE_SCC withSCSI) */
    if (a < 0x4000 || a >= 0xC000) return 0;
    int w = (a >> 13) - 2;
    if (w == 0 && (c->scc_mode & 0x40) && !c->ese_we) { spc_mem_write(m, a, v); return 0; }
    if (c->bank[2] == 0x3F && a >= 0x9800 && a < 0xA000) { scc_catch_up(m, c); scc_write(&c->scc, (uint8_t)a, v); return 0; }
    if ((a | 1) == 0x7FFF) {                        /* 비트 4 = 쓰기 허용, 비트 6 = 뱅크 A19 (SPC 켜기) */
        c->ese_we = (v & 0x10) ? 1 : 0;
        c->scc_mode = v & 0x40;
        c->bank[0] = (uint8_t)((c->bank[0] & 0x3F) | (v & 0x40));
        return 1;
    }
    if (c->ese_we && w < 2) { ((uint8_t *)(uintptr_t)cart_block(c, c->bank[w]))[a & 0x1FFF] = v; c->sram_dirty = 1; return 0; }
    if ((a & 0x1800) == 0x1000) { c->bank[w] = (uint8_t)((v & 0x3F) | (w == 0 ? (c->scc_mode & 0x40) : 0)); return 1; }
    return 0;
}
}
