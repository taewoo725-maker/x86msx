/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * mappers/ram.inc - 카트리지 매퍼 부품: RAM·MegaRAM  (5.2.4 구조 개편 3단계)
 * msx.c 가 한 번역 단위로 끼워 넣음 (msx.c 의 공용 도움 함수·블록을 그대로 씀, 인라인 가능).
 * 매퍼 하나 = cw_(쓰기, 1 = 뱅크 바뀜) + cm_(창 맞추기, 없으면 cm_default) + cr_(리셋, 없으면 cr_default). 표는 mappers/table.inc
 */

static int cw_ram(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    {
    int s = m->cart_slot[n], q = m->cart_ss[n];
    if (s >= 0) ((uint8_t *)m->slot_wr[s][q][a >> 13])[a & 0x1FFF] = v;
    return 0;
}
}

static void cm_ram(MSX *m, Cartridge *c, int s, int q)
{
    (void)m; (void)c; (void)s; (void)q;
    /* 2.16: 일반 RAM (위쪽에 붙임) */
    uint32_t blocks = c->size / 8192;
    if (blocks > 8) blocks = 8;                         /* 64 KiB 넘는 크기를 주면 표 밖을 쓰던 것 (시험에서 찾음) */
    for (uint32_t k = 0; k < blocks; k++) {
        uint8_t *p = (uint8_t *)(uintptr_t)c->data + k * 8192;
        m->slot_rd[s][q][8 - blocks + k] = p;
        m->slot_wr[s][q][8 - blocks + k] = p;
    }
    return;
}

static int cw_megaram(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    if (a < 0x4000 || a >= 0xC000) return 0;
    if (c->mram_we) { ((uint8_t *)(uintptr_t)cart_block(c, c->bank[(a >> 13) - 2]))[a & 0x1FFF] = v; return 0; }
    c->bank[(a >> 13) - 2] = (uint8_t)(v % c->banks8);
    return 1;
}

static void cm_megaram(MSX *m, Cartridge *c, int s, int q)
{
    (void)m; (void)c; (void)s; (void)q;
    /* 2.16: 0x4000~0xBFFF 8 KiB 뱅크 4개 (읽기는 늘 RAM) */
    for (int i = 0; i < 4; i++) m->slot_rd[s][q][2 + i] = cart_block(c, c->bank[i]);
    return;
}
