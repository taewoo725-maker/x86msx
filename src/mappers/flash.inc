/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * mappers/flash.inc - 카트리지 매퍼 부품: 플래시 롬 (MFR SCC·ASCII16-X·Yamanooto·Matra Ink·NEO8/16)  (5.2.4 구조 개편 3단계)
 * msx.c 가 한 번역 단위로 끼워 넣음 (msx.c 의 공용 도움 함수·블록을 그대로 씀, 인라인 가능).
 * 매퍼 하나 = cw_(쓰기, 1 = 뱅크 바뀜) + cm_(창 맞추기, 없으면 cm_default) + cr_(리셋, 없으면 cr_default). 표는 mappers/table.inc
 */

static int cw_mfrscc(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    {                               /* 3.1.1: 코나미 SCC 매퍼 + AMD 29F040 */
    if (a < 0x4000 || a >= 0xC000) return 0;
    int w = (a >> 13) - 2;
    uint32_t fa = ((uint32_t)(c->bank[w] & 0x3F) * 0x2000 + (a & 0x1FFF)) % c->size;
    int remap = 0;
    if (a >= 0x9800 && a < 0xA000 && (c->bank[2] & 0x3F) == 0x3F) { scc_catch_up(m, c); scc_write(&c->scc, (uint8_t)a, v); }
    if ((a & 0x1800) == 0x1000) { c->bank[w] = v; remap = 1; }
    if (cart_flash(c, fa, v)) remap = 1;              /* 플래시 명령 (5.2.7: 칩 부품) */
    if (!remap) return 0;
    return 1;
}
}

static void cm_mfrscc(MSX *m, Cartridge *c, int s, int q)
{
    (void)m; (void)c; (void)s; (void)q;
    if (!c->flash.mode) { cm_default(m, c, s, q); return; }
    /* 3.1.1: 플래시 자동 선택 모드 */
    for (int i = 0; i < 4; i++) m->slot_rd[s][q][2 + i] = (uint8_t *)(uintptr_t)cart_flash_id(c);
    return;
}

/* 5.2.46 Mega Flash ROM SCC+ (Manuel Pazos, 2010) — openMSX MegaFlashRomSCCPlus.cc 와 같게.
 * 설정 레지스터(7FFEh/7FFFh): 비트 7·6 매퍼 (00 Konami/Konami-SCC, 01 64K, 10 ASCII8, 11 ASCII16), 5 Konami 보통 매퍼(0 = SCC 매퍼 주소), 4 서브슬롯 흉내(FFFFh),
 * 3 Konami 모드에서 4000~5FFF 뱅크 바꾸기 금지, 2 설정 레지스터 잠금, 1 뱅크 레지스터 잠금, 0 뱅크 수 제한(SCC 512K / Konami 256K).
 * 플래시 주소 = (서브슬롯 x 256 KiB + 뱅크 x 8 KiB(64K 모드 16 KiB) + 안 주소) & FFFFFh. 자료는 CPU 주소 순서로 둠 (칩 A0=CPU A12 바꿔 꽂은 건 명령 주소 비교에만, flash.c a12_swap). */
static uint32_t mfrp_sub(const Cartridge *c, uint16_t a) { return (c->mp_cfg & 0x10) ? (uint32_t)((c->mp_sub >> (2 * (a >> 14))) & 3) : 0; }
static uint32_t mfrp_flash_addr(const Cartridge *c, uint16_t a)   /* 0xFFFFFFFF = 이 주소엔 플래시 없음 */
{
    uint32_t sub = mfrp_sub(c, a), t;
    if ((c->mp_cfg & 0xC0) == 0x40) t = ((uint32_t)c->mp_bank[sub][a >> 14] + c->mp_off) * 0x4000u + (a & 0x3FFF);
    else { uint32_t pg = (uint32_t)(a >> 13) - 2; if (pg >= 4) return 0xFFFFFFFFu; t = ((uint32_t)c->mp_bank[sub][pg] + c->mp_off) * 0x2000u + (a & 0x1FFF); }
    return (0x40000u * sub + t) & 0xFFFFFu;
}
static int mfrp_scc_on(const Cartridge *c) { return (c->mp_cfg & 0xE0) == 0x00 || (c->mp_cfg & 0xE0) == 0x20; }   /* 두 코나미 모드 모두 SCC 있음 */

static int cw_mfrscp(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)n;
    uint32_t fa = mfrp_flash_addr(c, a);                /* 다른 레지스터가 바뀌기 전 주소 */
    int remap = 0;
    if ((c->mp_cfg & 0x10) && a == 0xFFFF) { c->mp_sub = v; remap = 1; }
    if (!(c->mp_cfg & 0x04) && (a & 0xFFFE) == 0x7FFE) { c->mp_cfg = v; remap = 1; }
    if (mfrp_scc_on(c)) {
        if ((a & 0xFFFE) == 0xBFFE) { c->scc_mode = v; remap = 1; }
        int plus = (c->scc_mode & 0x20) != 0;
        int scc = !plus && (c->mp_sccb[2] & 0x3F) == 0x3F, sccp = plus && (c->mp_sccb[3] & 0x80);
        int ram2 = (c->scc_mode & 0x24) == 0x24 || (c->scc_mode & 0x10), ram3 = (c->scc_mode & 0x10) != 0;
        if ((scc && !ram2 && a >= 0x9800 && a < 0xA000) || (sccp && !ram3 && a >= 0xB800 && a < 0xC000)) {
            scc_catch_up(m, c);
            if (plus) scc_write_plus(&c->scc, (uint8_t)a, v); else scc_write(&c->scc, (uint8_t)a, v);
            return remap;                               /* SCC 레지스터가 보이면 플래시는 명령을 안 받음 */
        }
    }
    uint32_t sub = mfrp_sub(c, a), pg = (uint32_t)(a >> 13) - 2;
    if (!(c->mp_cfg & 0x02) && pg < 4) {
        switch (c->mp_cfg & 0xE0) {
        case 0x00:                                      /* Konami SCC: 5000/7000/9000/B000, 뱅크 0 에 비트 7 = 오프셋 */
            if ((a & 0x1800) == 0x1000) {
                c->mp_sccb[pg] = v;
                if ((v & 0x80) && pg == 0) c->mp_off = v & 0x7F;
                else c->mp_bank[sub][pg] = v & ((c->mp_cfg & 1) ? 0x3F : 0x7F);
                remap = 1;
            }
            break;
        case 0x20:                                      /* Konami: 6000/8000/A000 (+ 5000~57FF, 비트 3 이면 4000~5FFF 금지) */
            if ((c->mp_cfg & 0x08) && a < 0x6000) break;
            if (a < 0x5000 || (a >= 0x5800 && a < 0x6000)) break;
            c->mp_bank[sub][pg] = v & ((c->mp_cfg & 1) ? 0x1F : 0x7F); c->mp_sccb[pg] = v; remap = 1;
            break;
        case 0x40: case 0x60:                           /* 64K: 레지스터 주소는 코나미, 뱅크 16 KiB */
            c->mp_bank[sub][pg] = v; remap = 1;
            break;
        case 0x80: case 0xA0:                           /* ASCII8: 6000/6800/7000/7800 */
            if (a >= 0x6000 && a < 0x8000) { c->mp_bank[sub][(a >> 11) & 3] = v; remap = 1; }
            break;
        default:                                        /* ASCII16: 6000 → 뱅크 0·1, 7000 → 뱅크 2·3 (레지스터 4개 모두 씀) */
            if (a >= 0x6000 && a < 0x6800) { c->mp_bank[sub][0] = (uint8_t)(2 * v); c->mp_bank[sub][1] = (uint8_t)(2 * v + 1); remap = 1; }
            if (a >= 0x7000 && a < 0x7800) { c->mp_bank[sub][2] = (uint8_t)(2 * v); c->mp_bank[sub][3] = (uint8_t)(2 * v + 1); remap = 1; }
            break;
        }
    }
    if (fa != 0xFFFFFFFFu && cart_flash(c, fa, v)) remap = 1;   /* 플래시 명령·쓰기 (64K 모드는 0000~FFFF, 그 밖 4000~BFFF) */
    return remap;
}

static void cm_mfrscp(MSX *m, Cartridge *c, int s, int q)
{
    const uint8_t *idp = c->flash.mode ? cart_flash_id(c) : 0;
    int lin = (c->mp_cfg & 0xC0) == 0x40;
    for (int b = 0; b < 8; b++) {
        if (!lin && (b < 2 || b > 5)) { m->slot_rd[s][q][b] = ff_block; continue; }
        uint32_t fa = mfrp_flash_addr(c, (uint16_t)(b * 0x2000));
        m->slot_rd[s][q][b] = idp ? idp : cart_block(c, fa >> 13);
    }
}

static void cr_mfrscp(Cartridge *c)
{
    cr_scc_family(c);
    c->mp_cfg = 0; c->mp_off = 0; c->mp_sub = 0; c->mp_psg = 0;
    for (int s = 0; s < 4; s++) for (int i = 0; i < 4; i++) c->mp_bank[s][i] = (uint8_t)i;
    for (int i = 0; i < 4; i++) c->mp_sccb[i] = (uint8_t)i;
}

/* 5.2.47 popolon Flash-ROM SCC (Developer Edition) — github.com/popolonfr/Flash-ROM-SCC-Cartridge, blueMSX+ romMapperFlashRomScc.c 와 같게.
 * 16 MiB M29W128 (128 KiB 섹터), Konami SCC 매퍼(5000/7000/9000/B000, x7FFh 거울), 14비트 오프셋 3800h(L)/3801h(H) 가 모든 뱅크에 더해짐,
 * 동작 모드 3804h 비트 1 = 16비트 뱅크 레지스터(짝수 = 아래, 홀수 = 위 6비트). SCC(뱅크 2 원값 = 3Fh → 9800h) 의 시험 레지스터 자리에
 * 98FBh Knm_DAC_Ctrl(비트 4 = 코나미 DAC 창 4000h~4FFFh), 98FCh DAC_Data. 3800h~3FFFh 는 쓰기 전용(읽기 FF). 터치 버튼은 안 함.
 * 상태: xbank[0..3] 뱅크(14비트), xbank[4] 오프셋, y_en 동작 모드, y_cfg DAC 제어 */
static int cw_frscc(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)n;
    if (a < 0x4000) {
        if (a < 0x3800) return 0;
        if (a & 4) { c->y_en = v & 3; return 0; }
        uint16_t o = (a & 1) ? (uint16_t)((c->xbank[4] & 0x00FF) | ((v & 0x3F) << 8)) : (uint16_t)((c->xbank[4] & 0x3F00) | v);
        if (o == c->xbank[4]) return 0;
        c->xbank[4] = o; return 1;
    }
    if (a >= 0xC000) return 0;
    int pg = (a >> 13) - 2, remap = 0;
    uint32_t fa = (((uint32_t)c->xbank[pg] + c->xbank[4]) & 0x7FF) * 0x2000u + (a & 0x1FFF);
    if ((c->xbank[2] & 0x3F) == 0x3F && a >= 0x9800 && a < 0xA000) {       /* SCC + DAC 레지스터 */
        uint8_t r = (uint8_t)a;
        if (r == 0xFB) c->y_cfg = v;
        else if (r == 0xFC) dac_push(m, v);
        else { scc_catch_up(m, c); scc_write(&c->scc, r, v); }
    }
    if ((c->y_cfg & 0x10) && a < 0x5000) dac_push(m, v);                   /* 코나미 DAC 창 (그동안 플래시 명령 못 받음) */
    else if (cart_flash(c, fa, v)) remap = 1;
    if ((a & 0x1800) == 0x1000) {
        uint16_t nb = (c->y_en & 2) ? ((a & 1) ? (uint16_t)((c->xbank[pg] & 0x00FF) | ((v & 0x3F) << 8)) : (uint16_t)((c->xbank[pg] & 0x3F00) | v)) : v;
        if (nb != c->xbank[pg]) { c->xbank[pg] = nb; remap = 1; }
    }
    return remap;
}

static void cm_frscc(MSX *m, Cartridge *c, int s, int q)
{
    const uint8_t *idp = c->flash.mode ? cart_flash_id(c) : 0;
    for (int b = 0; b < 8; b++)
        m->slot_rd[s][q][b] = (b < 2 || b > 5) ? ff_block : idp ? idp : cart_block(c, ((uint32_t)c->xbank[b - 2] + c->xbank[4]) & 0x7FF);
}

static void cr_frscc(Cartridge *c)
{
    cr_scc_family(c);
    for (int i = 0; i < 4; i++) c->xbank[i] = (uint16_t)i;
    c->xbank[4] = 0; c->y_en = 0; c->y_cfg = 0;
}

static int cw_ascii16x(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    {                             /* 4.0.129: 뱅크 번호 = 주소 A8~A11 + 데이터 (12비트), 레지스터는 각 16 KiB 쪽의 +0x2000 이상 */
    uint32_t pg = (a >> 14) & 1;                    /* 0 = 0x4000·0xC000 칸, 1 = 0x8000·0x0000 칸 */
    uint32_t fa = ((uint32_t)c->xbank[pg ^ 1] << 14) | (a & 0x3FFF);
    int chg = cart_flash(c, fa, v);
    if ((a & 0x3FFF) >= 0x2000) { c->xbank[(a >> 12) & 1] = (uint16_t)((a & 0x0F00) | v); return 1; }
    if (!chg) return 0;
    return 1;
}
}

static void cm_ascii16x(MSX *m, Cartridge *c, int s, int q)
{
    (void)m; (void)c; (void)s; (void)q;
    /* 4.0.129: 0x4000·0xC000 = 뱅크 0, 0x8000·0x0000 = 뱅크 1 (16 KiB, 12비트) */
    for (int b = 0; b < 8; b++) m->slot_rd[s][q][b] = c->flash.mode ? (uint8_t *)(uintptr_t)cart_flash_id(c) : cart_block(c, (uint32_t)c->xbank[(b >= 2 && b < 4) || b >= 6 ? 0 : 1] * 2 + (uint32_t)(b & 1));
    return;
}

static void cr_ascii16x(Cartridge *c) { c->xbank[0] = 0; c->xbank[1] = 1; flash_reset(&c->flash); }   /* openMSX와 같이 (0, 1) */

static int cw_yamanooto(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    {                            /* 4.0.129: openMSX Yamanooto.cc 와 같게 */
    if (a >= 0x7FFC && a <= 0x7FFF) {               /* 거울 아님 */
        if (a == 0x7FFF) c->y_en = v;
        else if (c->y_en & 1) {
            if (a == 0x7FFD) c->y_cfg = v;
            else if (a == 0x7FFE) c->y_off = v;
            else if ((c->y_cfg & 0x40)) { if (c->y_fsm == 0) { if (v == 0x9F) c->y_fsm = 1; } else c->y_fsm = (uint8_t)(c->y_fsm == 4 ? 0 : c->y_fsm + 1); }
        }
    }
    if (a < 0x4000 || a >= 0xC000) a ^= 0x8000;
    int pg = (a >> 13) - 2;
    if (c->y_en & 0x10) {                           /* 플래시 쓰기 */
        if (!(c->y_cfg & 4)) cart_flash(c, ((uint32_t)(c->xbank[pg] & 0x3FF) << 13) | (a & 0x1FFF), v);
    } else {
        unsigned off = ((unsigned)c->y_off << 2) | ((c->y_cfg & 0x30) >> 4);
        if (c->y_cfg & 8) {                         /* Konami4: 뱅크 0 은 고정 */
            if (!(c->y_cfg & 1) && a >= 0x6000) { c->xbank[pg] = (uint16_t)((v + off) & 0x3FF); c->y_raw[pg] = v; }
        } else {
            if (((c->scc_mode & 0x20) ? ((c->y_raw[3] & 0x80) && a >= 0xB800 && a < 0xBFFE) : ((c->y_raw[2] & 0x3F) == 0x3F && a >= 0x9800 && a < 0xA000))) {
                scc_catch_up(m, c);
                if (c->scc_mode & 0x20) scc_write_plus(&c->scc, (uint8_t)a, v); else scc_write(&c->scc, (uint8_t)a, v);
            }
            if ((a & 0x1800) == 0x1000 && !(c->y_cfg & 1)) { c->xbank[pg] = (uint16_t)((v + off) & 0x3FF); c->y_raw[pg] = v; }
            if ((a & 0xFFFE) == 0xBFFE) { c->scc_mode = v; }
        }
    }
    return 1;
}
}

static void cm_yamanooto(MSX *m, Cartridge *c, int s, int q)
{
    (void)m; (void)c; (void)s; (void)q;
    /* 4.0.129: 8 KiB 뱅크 4개 (0x4000~0xBFFF), 0x0000~0x3FFF = 0x8000~0xBFFF, 0xC000~ = 0x4000~ 거울 */
    for (int b = 0; b < 8; b++) {
        int pg = b < 2 ? b + 2 : b >= 6 ? b - 6 : b - 2;
        m->slot_rd[s][q][b] = (c->y_cfg & 4) ? ff_block : c->flash.mode ? (uint8_t *)(uintptr_t)cart_flash_id(c) : cart_block(c, c->xbank[pg]);
    }
    return;
}

static void cr_yamanooto(Cartridge *c)
{
    for (int i = 0; i < 4; i++) { c->xbank[i] = (uint16_t)i; c->y_raw[i] = (uint8_t)i; }
    c->y_en = c->y_cfg = c->y_off = c->y_fsm = 0; c->scc_mode = 0; flash_reset(&c->flash);
    scc_set_stereo(&c->scc);                            /* 4.0.131: scc_reset 뒤 좌우 배치 다시 */
}

static int cw_matraink(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    /* 3.9.14: 쓰기 명령 주소 = 주소 + 0x10000 (openMSX RomMatraInk와 같음) */
    if (cart_flash(c, ((uint32_t)a + 0x10000u) % c->size, v)) return 1;
    return 0;
}

static void cm_matraink(MSX *m, Cartridge *c, int s, int q)
{
    (void)m; (void)c; (void)s; (void)q;
    /* 3.9.14: 0x0000~0xFFFF = 플래시 첫 64 KiB (자동 선택 중이면 ID) */
    for (int b = 0; b < 8; b++) m->slot_rd[s][q][b] = c->flash.mode ? (uint8_t *)(uintptr_t)cart_flash_id(c) : cart_block(c, (uint32_t)b);
    return;
}

static int cw_neo8(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    {              /* 주소 A13~A11 = 뱅크 (010~111), A0 = 0 하위 / 1 상위 바이트 (14비트) */
    int bb = (a >> 11) & 7, w;
    if (c->mapper == MAPPER_NEO8) { if (bb < 2) return 0; w = bb - 2; }
    else { if (bb < 2 || (bb & 1)) return 0; w = (bb >> 1) - 1; }
    uint16_t o = c->xbank[w];
    c->xbank[w] = (a & 1) ? (uint16_t)((o & 0x00FF) | ((v & 0x3F) << 8)) : (uint16_t)((o & 0x3F00) | v);
    return 1;
}
}

static void cm_neo8(MSX *m, Cartridge *c, int s, int q)
{
    (void)m; (void)c; (void)s; (void)q;
    /* 0000~BFFF = 뱅크, C000~ = FFh (범위 밖 구간도 FFh) */
    for (int b = 0; b < 8; b++) {
        uint32_t sg = 0xFFFFFFFFu;
        if (b < 6) sg = c->mapper == MAPPER_NEO8 ? (uint32_t)c->xbank[b] : (uint32_t)c->xbank[b >> 1] * 2 + (uint32_t)(b & 1);
        m->slot_rd[s][q][b] = (sg == 0xFFFFFFFFu || (uint64_t)(sg + 1) * 8192 > c->size) ? ff_block : c->data + (size_t)sg * 8192;
    }
    return;
}

static void cr_neo(Cartridge *c) { for (int i = 0; i < 6; i++) c->xbank[i] = 0; }
