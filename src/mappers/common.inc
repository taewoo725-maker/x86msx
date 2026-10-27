/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * mappers/common.inc - 카트리지 매퍼 부품: 공용 (기본 창·기본 리셋·일반 롬 창)  (5.2.4 구조 개편 3단계)
 * msx.c 가 한 번역 단위로 끼워 넣음 (msx.c 의 공용 도움 함수·블록을 그대로 씀, 인라인 가능).
 * 매퍼 하나 = cw_(쓰기, 1 = 뱅크 바뀜) + cm_(창 맞추기, 없으면 cm_default) + cr_(리셋, 없으면 cr_default). 표는 mappers/table.inc
 */

static void cm_default(MSX *m, Cartridge *c, int s, int q)
{
    (void)m;
    for (int i = 0; i < 4; i++)
        m->slot_rd[s][q][2 + i] = (c->mapper == MAPPER_NETTOU && (c->bank[i] & 0x80)) ? ff_block : cart_block(c, c->bank[i] & (c->mapper == MAPPER_NETTOU ? 0x7F : 0xFF));
}

static void cr_default(Cartridge *c)                   /* 뱅크 0, 리셋 상태 지움 — 아래 매퍼별 함수가 이어서 고침 */
{
    for (int i = 0; i < 4; i++) c->bank[i] = 0;
    c->ide_ctl = 0;
    c->mram_we = 0;
    c->sram_on = 0; c->sram_key[0] = c->sram_key[1] = 0;
    flash_reset(&c->flash);                             /* 플래시 칩 읽기 모드로 */
}

static void cr_16k_01(Cartridge *c) { cr_default(c); c->bank[1] = 1; c->bank[3] = 1; }   /* 16 KiB 뱅크: 0·1 */

static void cr_0123(Cartridge *c) { cr_default(c); for (int i = 0; i < 4; i++) c->bank[i] = (uint8_t)i; }   /* Harry Fox·Game Master 2 (5.0.38) */

static void cr_scc_family(Cartridge *c)               /* Konami·Konami SCC·SCC-I·MFR SCC·ESE-SCC·WAVE-SCSI */
{
    for (int i = 0; i < 4; i++) c->bank[i] = (uint8_t)i;
    c->scc_mode = 0; c->ese_we = 0;
    c->sram_on = 0; c->sram_key[0] = 0;
    flash_reset(&c->flash);                             /* 3.1.1: 플래시 명령 상태 (5.2.7: 칩 부품) */
}

static void cm_plain(MSX *m, Cartridge *c, int s, int q)
{
    (void)m; (void)c; (void)s; (void)q;
    uint32_t blocks = (c->size + 8191) / 8192;
    int base = c->plain_base;
    if (blocks == 1 && base == 2) {                     /* 8 KiB: 0x0000~0x7FFF에 반복 (3.0.9: INIT가 0x0000 쪽인 Crazy Train) */
        for (int b = 0; b < 4; b++) m->slot_rd[s][q][b] = c->data;
        return;
    }
    for (uint32_t k = 0; k < blocks && base + (int)k < 8; k++)
        m->slot_rd[s][q][base + k] = c->data + k * 8192;
    return;
}
