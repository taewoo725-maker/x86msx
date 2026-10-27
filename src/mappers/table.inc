/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * mappers/table.inc - 매퍼 번호 → 부품 함수 표와 공통 진입점 cart_map·cart_reset·cart_write (5.2.4 구조 개편 3단계)
 * 새 매퍼 = mappers/<종류>.inc 에 함수 + 아래 표에 줄. 공통 진입점은 바꾸지 않음.
 */

static const CartMapFn cart_map_fn[MAPPER_COUNT] = {
    [MAPPER_GIDE] = cm_gide,
    [MAPPER_FSCA1] = cm_fsca1,
    [MAPPER_SFG] = cm_sfg,
    [MAPPER_BEERIDE] = cm_beeride,
    [MAPPER_GOUDA] = cm_beeride,
    [MAPPER_NET] = cm_beeride,
    [MAPPER_FMPAC] = cm_fmpac,
    [MAPPER_DOS2] = cm_fmpac,
    [MAPPER_SUNRISE] = cm_fmpac,
    [MAPPER_PAC] = cm_fmpac,
    [MAPPER_VHD] = cm_fmpac,
    [MAPPER_RAM] = cm_ram,
    [MAPPER_MEGARAM] = cm_megaram,
    [MAPPER_PLAIN] = cm_plain,
    [MAPPER_HBI55] = cm_plain,
    [MAPPER_RS232] = cm_plain,       /* 메모리 창 없음 — I/O 만 (msx.c ior_rs232/iow_rs232) */       /* 메모리 창 없음 (0xFF 자리) — I/O 만 (msx.c ior_hbi55/iow_hbi55) */
    [MAPPER_ASCII8_SRAM] = cm_ascii8_sram,
    [MAPPER_ASCII16_SRAM] = cm_ascii8_sram,
    [MAPPER_MFRSCC] = cm_mfrscc,
    [MAPPER_MFRSCCP] = cm_mfrscp,
    [MAPPER_FRSCC] = cm_frscc,
    [MAPPER_ASCII16X] = cm_ascii16x,
    [MAPPER_NEO8] = cm_neo8,
    [MAPPER_NEO16] = cm_neo8,
    [MAPPER_YAMANOOTO] = cm_yamanooto,
    [MAPPER_GAMEMASTER2] = cm_gamemaster2,
    [MAPPER_ESERAM] = cm_eseram,
    [MAPPER_MATRAINK] = cm_matraink,
    [MAPPER_MEGASCSI] = cm_megascsi,
};

static const CartResetFn cart_reset_fn[MAPPER_COUNT] = {
    [MAPPER_KONAMI] = cr_scc_family, [MAPPER_KONAMI_SCC] = cr_scc_family, [MAPPER_SCCPLUS] = cr_scc_family,
    [MAPPER_MFRSCC] = cr_scc_family, [MAPPER_ESESCC] = cr_scc_family, [MAPPER_WAVESCSI] = cr_scc_family,
    [MAPPER_ASCII16X] = cr_ascii16x, [MAPPER_MFRSCCP] = cr_mfrscp, [MAPPER_FRSCC] = cr_frscc, [MAPPER_NEO8] = cr_neo, [MAPPER_NEO16] = cr_neo, [MAPPER_YAMANOOTO] = cr_yamanooto,
    [MAPPER_ASCII16] = cr_16k_01, [MAPPER_CROSSBLAIM] = cr_16k_01, [MAPPER_LODERUNNER] = cr_16k_01, [MAPPER_ASCII16_SRAM] = cr_16k_01,
    [MAPPER_FMPAC] = cr_16k_01, [MAPPER_DOS2] = cr_16k_01, [MAPPER_SUNRISE] = cr_16k_01, [MAPPER_PAC] = cr_16k_01, [MAPPER_VHD] = cr_16k_01,
    [MAPPER_RTYPE] = cr_rtype, [MAPPER_HARRYFOX] = cr_0123, [MAPPER_GAMEMASTER2] = cr_0123,
    [MAPPER_ZEMINA90] = cr_zemina, [MAPPER_ZEMINA126] = cr_zemina, [MAPPER_FSCA1] = cr_fsca1,
};

static const CartWrFn cart_wr_fn[MAPPER_COUNT] = {
    [MAPPER_KONAMI] = cw_konami,
    [MAPPER_KONAMI_SCC] = cw_konami_scc,
    [MAPPER_NET] = cw_net,
    [MAPPER_BEERIDE] = cw_beeride,
    [MAPPER_GIDE] = cw_beeride,
    [MAPPER_GOUDA] = cw_beeride,
    [MAPPER_GAMEMASTER2] = cw_gamemaster2,
    [MAPPER_FSCA1] = cw_fsca1,
    [MAPPER_SFG] = cw_sfg,
    [MAPPER_ESESCC] = cw_esescc,
    [MAPPER_ESERAM] = cw_eseram,
    [MAPPER_MFRSCC] = cw_mfrscc,
    [MAPPER_MFRSCCP] = cw_mfrscp,
    [MAPPER_FRSCC] = cw_frscc,
    [MAPPER_ASCII16X] = cw_ascii16x,
    [MAPPER_NEO8] = cw_neo8,
    [MAPPER_NEO16] = cw_neo8,
    [MAPPER_YAMANOOTO] = cw_yamanooto,
    [MAPPER_MATRAINK] = cw_matraink,
    [MAPPER_MEGASCSI] = cw_megascsi,
    [MAPPER_WAVESCSI] = cw_wavescsi,
    [MAPPER_ASCII8] = cw_ascii8,
    [MAPPER_NETTOU] = cw_ascii8,
    [MAPPER_ASCII8_SRAM] = cw_ascii8_sram,
    [MAPPER_ASCII16_SRAM] = cw_ascii16_sram,
    [MAPPER_FMPAC] = cw_fmpac,
    [MAPPER_PAC] = cw_pac,
    [MAPPER_MEGARAM] = cw_megaram,
    [MAPPER_RAM] = cw_ram,
    [MAPPER_SCCPLUS] = cw_sccplus,
    [MAPPER_SUNRISE] = cw_sunrise,
    [MAPPER_VHD] = cw_vhd,
    [MAPPER_DOS2] = cw_dos2,
    [MAPPER_RTYPE] = cw_rtype,
    [MAPPER_ZEMINA80] = cw_zemina80,
    [MAPPER_ZEMINA126] = cw_zemina126,
    [MAPPER_HARRYFOX] = cw_harryfox,
    [MAPPER_CROSSBLAIM] = cw_crossblaim,
    [MAPPER_LODERUNNER] = cw_loderunner,
    [MAPPER_QURAN] = cw_quran,
    [MAPPER_ASCII16] = cw_ascii16,
};

static void cart_map(MSX *m, int n)
{
    int s = m->cart_slot[n], q = m->cart_ss[n];
    Cartridge *c = &m->cart[n];
    if (s < 0) return;
    if (c->present && c->mapper == MAPPER_PLAIN && !c->data) return;     /* 메모리 매퍼 RAM 카트리지 */
    if (n >= MSX_INT_CART && c->mapper == MAPPER_PAC) {                 /* 3.1.0: 내장 PAC — 0x4000~0x7FFF만 (같은 서브슬롯의 다른 롬은 그대로) */
        if (!c->present) return;
        m->slot_rd[s][q][2] = c->sram_on ? c->sram : ff_block;
        m->slot_rd[s][q][3] = ff_block;
        return;
    }
    if (c->present && c->mapper == MAPPER_SFG) {            /* 내장 SFG는 자기 롬 칸만 — 같은 슬롯 위쪽의 RAM(CX5M-2: 3-0 에 SFG 0000~7FFF + RAM 8000~FFFF)을 지우지 않음 */
        for (int b = 0; b < 8; b++) if (b >= c->plain_base && b < c->plain_base + (int)c->banks8) { m->slot_rd[s][q][b] = ff_block; m->slot_wr[s][q][b] = discard_block; }
    } else
    for (int b = 0; b < 8; b++) {
        m->slot_rd[s][q][b] = ff_block;
        m->slot_wr[s][q][b] = discard_block;
    }
    if (!c->present) return;
    CartMapFn f = (unsigned)c->mapper < MAPPER_COUNT ? cart_map_fn[c->mapper] : 0;
    (f ? f : cm_default)(m, c, s, q);
}

static void cart_reset(MSX *m, int n)
{
    Cartridge *c = &m->cart[n];
    if (CART_HAS_SCC(c->mapper)) scc_reset(&c->scc);    /* 4.0.29: 리셋하면 SCC 소리도 끔 */
    if (c->rs.uart == UART_MC6850 || c->mapper == MAPPER_SFG) { c->rs.cmd = c->rs.uart == UART_MC6850 ? 3 : 0; c->rs.rxrdy = 0; c->rs.tflag = 0; c->rs.rx_r = c->rs.rx_w = 0; }   /* MIDI UART 리셋 (Music Module = 마스터 리셋) */
    if (c->mapper == MAPPER_NET && c->banks8 >= 2) memcpy(c->sram, cart_block(c, 1), 8192);   /* 6000~7FFF 를 RAM 사본으로 (7F00~7FFF 쓰기 가능: 이전 EXTBIO 후크 사본) */
    CartResetFn f = (unsigned)c->mapper < MAPPER_COUNT ? cart_reset_fn[c->mapper] : 0;
    (f ? f : cr_default)(c);
    cart_map(m, n);
}

static void cart_write(MSX *m, int n, uint16_t a, uint8_t v)
{
    Cartridge *c = &m->cart[n];
    CartWrFn f = (unsigned)c->mapper < MAPPER_COUNT ? cart_wr_fn[c->mapper] : 0;
    if (!f || !f(m, n, c, a, v)) return;
    cart_map(m, n);
    update_all_pages(m);
}
