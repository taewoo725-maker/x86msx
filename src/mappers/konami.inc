/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * mappers/konami.inc - 카트리지 매퍼 부품: Konami·Konami SCC·SCC-I(SCC+)  (5.2.4 구조 개편 3단계)
 * msx.c 가 한 번역 단위로 끼워 넣음 (msx.c 의 공용 도움 함수·블록을 그대로 씀, 인라인 가능).
 * 매퍼 하나 = cw_(쓰기, 1 = 뱅크 바뀜) + cm_(창 맞추기, 없으면 cm_default) + cr_(리셋, 없으면 cr_default). 표는 mappers/table.inc
 */

static int cw_konami(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    if (a >= 0x6000 && a < 0xC000) c->bank[(a >> 13) - 2] = v;
    else return 0;
    return 1;
}

static int cw_konami_scc(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    if (a >= 0x9800 && a < 0xA000 && (c->bank[2] & 0x3F) == 0x3F) { scc_catch_up(m, c); scc_write(&c->scc, (uint8_t)a, v); return 0; }   /* SCC 레지스터 */
    if ((a & 0x1800) == 0x1000 && a >= 0x4000 && a < 0xC000) c->bank[(a >> 13) - 2] = v;
    else return 0;
    return 1;
}

static int cw_sccplus(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    {
    if (a == 0xBFFE || a == 0xBFFF) { c->scc_mode = v; return 1; }               /* 모드 레지스터 */
    int plus = (c->scc_mode & 0x20) != 0, b = (a >> 13) - 2;
    if (plus && (c->bank[3] & 0x80) && a >= 0xB800 && a < 0xBFFE) { scc_catch_up(m, c); scc_write_plus(&c->scc, (uint8_t)a, v); return 0; }
    if (!plus && (c->bank[2] & 0x3F) == 0x3F && a >= 0x9800 && a < 0xA000) { scc_catch_up(m, c); scc_write(&c->scc, (uint8_t)a, v); return 0; }
    if (b < 0 || b > 3) return 0;
    if ((c->scc_mode & 0x10) || (b < 3 && (c->scc_mode & (1 << b)))) {     /* RAM 쓰기 허용 */
        ((uint8_t *)(uintptr_t)cart_block(c, c->bank[b]))[a & 0x1FFF] = v;
        return 0;
    }
    if ((a & 0x1800) == 0x1000) { c->bank[b] = v; return 1; }                   /* 뱅크 선택 */
    return 0;
}
}
