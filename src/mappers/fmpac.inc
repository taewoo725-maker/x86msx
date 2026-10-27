/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * mappers/fmpac.inc - 카트리지 매퍼 부품: FM-PAC·PAC  (5.2.4 구조 개편 3단계)
 * msx.c 가 한 번역 단위로 끼워 넣음 (msx.c 의 공용 도움 함수·블록을 그대로 씀, 인라인 가능).
 * 매퍼 하나 = cw_(쓰기, 1 = 뱅크 바뀜) + cm_(창 맞추기, 없으면 cm_default) + cr_(리셋, 없으면 cr_default). 표는 mappers/table.inc
 */

static int cw_fmpac(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    if (a == 0x5FFE || a == 0x5FFF) {               /* SRAM 켜기: 4D 69 */
        c->sram_key[a & 1] = v;
        c->sram_on = c->sram_key[0] == 0x4D && c->sram_key[1] == 0x69;
        if (!c->sram_on) return 1;
        return 0;
    }
    if (a >= 0x4000 && a < 0x5FFE && c->sram_on) { c->sram[a & 0x1FFF] = v; c->sram_dirty = 1; return 0; }
    if (a == 0x7FF4 || a == 0x7FF5) { if (m->opll_on) { OPLL_writeIO(m->opll, a & 1, v); m->opll_quiet = 0; } return 0; }
    if (a == 0x7FF7) {                              /* 16 KiB 뱅크 */
        c->bank[0] = (uint8_t)((v & 3) * 2);
        c->bank[1] = (uint8_t)((v & 3) * 2 + 1);
        return 1;
    }
    return 0;
}

static void cm_fmpac(MSX *m, Cartridge *c, int s, int q)
{
    (void)m; (void)c; (void)s; (void)q;
    /* 0x4000~0x7FFF만, 16 KiB 뱅크 */
    m->slot_rd[s][q][2] = cart_block(c, c->bank[0]);
    m->slot_rd[s][q][3] = cart_block(c, c->bank[1]);
    if ((c->mapper == MAPPER_FMPAC || c->mapper == MAPPER_PAC) && c->sram_on) m->slot_rd[s][q][2] = c->sram;
    return;
}

static int cw_pac(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    if (a == 0x5FFE || a == 0x5FFF) {
        c->sram_key[a & 1] = v;
        c->sram_on = c->sram_key[0] == 0x4D && c->sram_key[1] == 0x69;
        return 1;
    }
    if (a >= 0x4000 && a < 0x5FFE && c->sram_on) { c->sram[a & 0x1FFF] = v; c->sram_dirty = 1; }
    return 0;
}
