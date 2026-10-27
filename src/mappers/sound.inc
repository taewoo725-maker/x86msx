/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * mappers/sound.inc - 카트리지 매퍼 부품: SFG·FS-CA1·Game Master 2  (5.2.4 구조 개편 3단계)
 * msx.c 가 한 번역 단위로 끼워 넣음 (msx.c 의 공용 도움 함수·블록을 그대로 씀, 인라인 가능).
 * 매퍼 하나 = cw_(쓰기, 1 = 뱅크 바뀜) + cm_(창 맞추기, 없으면 cm_default) + cr_(리셋, 없으면 cr_default). 표는 mappers/table.inc
 */

static int cw_sfg(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    {                                  /* 3.4.1: 롬 0x3FF0~0x3FF7 */
    int off = (int)a - c->plain_base * 8192;
    if (off >= 0x3FF0 && off < 0x3FF8) sfg_write(m, off & 7, v);
    return 0;
}      /* 3.4.1: I/O 포트로만 */
    return 0;   /* (끝) */
}

static void cm_sfg(MSX *m, Cartridge *c, int s, int q)
{
    (void)m; (void)c; (void)s; (void)q;
    /* 3.4.1: 롬 16·32 KiB를 시작 블록부터 */
    for (uint32_t k = 0; k < c->banks8 && c->plain_base + (int)k < 8; k++) m->slot_rd[s][q][c->plain_base + k] = cart_block(c, k);
    return;
}

static int cw_fsca1(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    {                                /* 3.4.1 */
    uint16_t o = a & 0x7FFF;
    if (o == 0x7FFE) { c->bank[0] = v & 3; return 1; }
    if (o == 0x7FFF) { c->scc_mode = v; return 0; }  /* 비트 0: 포트 C0h·C1h */
    if (!(c->bank[0] & 3) && (o & 0x3FFF) >= 0x3000) {
        uint16_t r = (uint16_t)((o & 0x3FFF) - 0x3000);
        c->sram[r] = v; m->scr.ca1_mix[0][4096 + r] = v; m->scr.ca1_mix[1][4096 + r] = v; c->sram_dirty = 1;
    }
    return 0;
}
}

static void cm_fsca1(MSX *m, Cartridge *c, int s, int q)
{
    (void)m; (void)c; (void)s; (void)q;
    /* 3.4.1: 32 KiB 창을 슬롯 전체에 반복, 뱅크 0이면 x3000~x3FFF = SRAM */
    uint32_t bank = c->bank[0] & 3;
    if (!bank) for (int h = 0; h < 2; h++) { memcpy(m->scr.ca1_mix[h], c->data + h * 0x4000 + 0x2000, 4096); memcpy(m->scr.ca1_mix[h] + 4096, c->sram, 4096); }
    for (int b = 0; b < 8; b++) {
        int k = b & 3;
        m->slot_rd[s][q][b] = (!bank && (k & 1)) ? m->scr.ca1_mix[k >> 1] : cart_block(c, bank * 4 + (uint32_t)k);
    }
    return;
}

static void cr_fsca1(Cartridge *c) { cr_default(c); c->scc_mode = 0; }   /* 3.4.1 */

static int cw_gamemaster2(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    {                          /* 5.0.38 */
    int pg = (a & 0xF000) == 0x6000 ? 1 : (a & 0xF000) == 0x8000 ? 2 : (a & 0xF000) == 0xA000 ? 3 : 0;
    if (pg) { c->bank[pg] = v & 0x3F; return 1; }
    if (a >= 0xB000 && a < 0xC000 && (c->bank[3] & 0x10)) {     /* SRAM 쓰기는 0xB000~0xBFFF만 */
        int sg = (c->bank[3] >> 5) & 1;
        c->sram[sg * 4096 + (a & 0xFFF)] = v; c->sram_dirty = 1;
        for (int i = 1; i < 4; i++) if ((c->bank[i] & 0x10) && ((c->bank[i] >> 5) & 1) == sg) { m->scr.gm2_mix[i - 1][a & 0xFFF] = v; m->scr.gm2_mix[i - 1][4096 + (a & 0xFFF)] = v; }
    }
    return 0;
}
}

static void cm_gamemaster2(MSX *m, Cartridge *c, int s, int q)
{
    (void)m; (void)c; (void)s; (void)q;
    /* 뱅크 값 비트 4 = SRAM (6000·8000·A000 창), 비트 5 = 4 KiB 칸 */
    m->slot_rd[s][q][2] = cart_block(c, 0);
    for (int i = 1; i < 4; i++) {
        uint8_t r = c->bank[i];
        if (r & 0x10) { const uint8_t *sg = c->sram + ((r >> 5) & 1) * 4096; memcpy(m->scr.gm2_mix[i - 1], sg, 4096); memcpy(m->scr.gm2_mix[i - 1] + 4096, sg, 4096); m->slot_rd[s][q][2 + i] = m->scr.gm2_mix[i - 1]; }
        else m->slot_rd[s][q][2 + i] = cart_block(c, r & 0x0F);
    }
    return;
}
