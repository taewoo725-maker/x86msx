/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * mappers/misc.inc - 카트리지 매퍼 부품: 그 밖 (R-Type·Zemina·Harry Fox·Cross Blaim·Lode Runner·Quran)  (5.2.4 구조 개편 3단계)
 * msx.c 가 한 번역 단위로 끼워 넣음 (msx.c 의 공용 도움 함수·블록을 그대로 씀, 인라인 가능).
 * 매퍼 하나 = cw_(쓰기, 1 = 뱅크 바뀜) + cm_(창 맞추기, 없으면 cm_default) + cr_(리셋, 없으면 cr_default). 표는 mappers/table.inc
 */

static int cw_rtype(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    if (a < 0x4000 || a >= 0x8000) return 0;
    v &= (v & 0x10) ? 0x17 : 0x1F;
    c->bank[2] = (uint8_t)(v * 2); c->bank[3] = (uint8_t)(v * 2 + 1);
    return 1;
}

static void cr_rtype(Cartridge *c) { cr_default(c); c->bank[0] = 0x2E; c->bank[1] = 0x2F; c->bank[2] = 0; c->bank[3] = 1; }

static int cw_zemina80(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    /* 3.8.0: 0x4000~0x4003 = 창 0~3의 8 KiB 뱅크 */
    if (a < 0x4000 || a > 0x4003) return 0;
    c->bank[a & 3] = v;
    return 1;
}

static int cw_zemina126(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    /* 3.8.0: 0x4000 = 4000~7FFF, 0x4001 = 8000~BFFF (16 KiB) */
    if (a != 0x4000 && a != 0x4001) return 0;
    { int i = (a & 1) * 2; c->bank[i] = (uint8_t)(v * 2); c->bank[i + 1] = (uint8_t)(v * 2 + 1); }
    return 1;
}

static void cr_zemina(Cartridge *c) { cr_default(c); c->bank[0] = 0; c->bank[1] = 1; c->bank[2] = 0; c->bank[3] = 1; }   /* 3.8.0: 세그먼트 0, 8000 창은 거울 */

static int cw_harryfox(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    if (a < 0x6000 || a >= 0x8000) return 0;
    { int i = a >= 0x7000 ? 2 : 0; c->bank[i] = (uint8_t)((v & 1) * 4 + i); c->bank[i + 1] = (uint8_t)((v & 1) * 4 + i + 1); }
    return 1;
}

static int cw_crossblaim(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    if (a == 0x4045) { c->bank[2] = (uint8_t)((v & 3) * 2); c->bank[3] = (uint8_t)((v & 3) * 2 + 1); }
    else return 0;
    return 1;
}

static int cw_loderunner(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    if (a < 0x4000) { c->bank[2] = (uint8_t)(v * 2); c->bank[3] = (uint8_t)(v * 2 + 1); }
    else return 0;
    return 1;
}

static int cw_quran(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    if (a >= 0x5000 && a < 0x6000) c->bank[(a >> 10) & 3] = v;
    else return 0;
    return 1;
}
