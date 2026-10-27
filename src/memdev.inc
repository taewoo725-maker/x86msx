/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * memdev.inc - 메모리 칸(8 KiB) 특수 장치 사슬 (5.2.5 구조 개편 3단계)
 *
 * 롬·램은 rd[]/wr[] 포인터로 바로 읽고 쓴다 (빠른 길). 레지스터가 메모리에 보이는 장치(SCC·IDE·FDC·펌웨어 …)는
 * 칸마다 "장치 사슬"에 들어가고, 그 칸 접근만 사슬을 거친다 (I/O 버스와 같은 방식).
 *
 *   mem_dev[장치].rd_claim/.wr_claim(m, b, ps, ss) : 슬롯이 바뀔 때 — 이 칸에 내가 보이면 0 아닌 값(보통 카트 번호+1, 처리 함수가 받음)
 *   mem_dev[장치].rd(m, arg, a)       : 읽기 — 내 것이면 0~255, 아니면 -1 (다음 장치로)
 *   mem_dev[장치].wr(m, arg, a, v)    : 쓰기 — 1 = 처리 끝, 0 = 다음 장치로
 *   사슬 순서 = md_rd_order / md_wr_order (예전 mem_read·mem_write 의 if 순서 그대로)
 *
 * 새 메모리 장치 = MemDev 번호 + claim·rd·wr 함수 + 표 mem_dev[] 에 MEMDEV(...) 한 줄 + 순서 줄 (5.2.119). mem_read/mem_write 는 안 고침.
 */

enum MemDev { MD_NONE, MD_IDE, MD_YAM, MD_SFG, MD_SPC, MD_FW, MD_BUN, MD_SCC, MD_FDC, MD_LODE, MD_HOOK, MD_MFRP, MD_COUNT };   /* 5.2.46 MD_MFRP: MFR SCC+ 서브슬롯 레지스터 FFFFh 읽기 */

/* 이 슬롯 자리에 꽂힌 카트리지 n 인가 */
static int md_cart_here(const MSX *m, int n, int ps, int ss)
{
    return ps == m->cart_slot[n] && (!m->prof.subslotted[ps] || ss == m->cart_ss[n]) && m->cart[n].present;
}

/* ---- 자리 잡기 (같은 종류가 여럿이면 마지막 카트가 이김 — 예전과 같음) ---- */
static int mc_scc(const MSX *m, int b, int ps, int ss)
{
    int r = 0;
    for (int n = 0; n < MSX_CART_ALL; n++) {
        if (!md_cart_here(m, n, ps, ss)) continue;
        const Cartridge *c = &m->cart[n];
        int scc_mode = c->mapper == MAPPER_KONAMI_SCC || c->mapper == MAPPER_MFRSCC || c->mapper == MAPPER_ESESCC || c->mapper == MAPPER_WAVESCSI || (c->mapper == MAPPER_SCCPLUS && !(c->scc_mode & 0x20));
        if (b == 4 && scc_mode && (c->bank[2] & 0x3F) == 0x3F) r = n + 1;
        if (b == 5 && c->mapper == MAPPER_SCCPLUS && (c->scc_mode & 0x20) && (c->bank[3] & 0x80)) r = n + 1;
        if (c->mapper == MAPPER_FRSCC && b == 4 && (c->xbank[2] & 0x3F) == 0x3F) r = n + 1;   /* 원값 뱅크 2 = 3Fh */
        if (c->mapper == MAPPER_MFRSCCP && (c->mp_cfg & 0xE0) <= 0x20) {   /* 코나미(SCC) 매퍼 모드에서만 SCC (원값 뱅크로 판단) */
            if (!(c->scc_mode & 0x20)) { if (b == 4 && (c->mp_sccb[2] & 0x3F) == 0x3F) r = n + 1; }
            else if (b == 5 && (c->mp_sccb[3] & 0x80)) r = n + 1;
        }
        if (c->mapper == MAPPER_YAMANOOTO && !(c->y_cfg & 8)) {   /* 4.0.129: SCC 는 오프셋 더하기 전 뱅크 값으로, Konami4 모드면 없음 */
            if (!(c->scc_mode & 0x20)) { if (b == 4 && (c->y_raw[2] & 0x3F) == 0x3F) r = n + 1; }
            else if (b == 5 && (c->y_raw[3] & 0x80)) r = n + 1;
        }
    }
    return r;
}
static int mc_mfrp(const MSX *m, int b, int ps, int ss)       /* 설정 비트 4 = 서브슬롯 흉내 → FFFFh 읽기 */
{
    int r = 0;
    for (int n = 0; n < MSX_CART_ALL; n++)
        if (md_cart_here(m, n, ps, ss) && m->cart[n].mapper == MAPPER_MFRSCCP && b == 7 && (m->cart[n].mp_cfg & 0x10)) r = n + 1;
    return r;
}
static int mc_yam(const MSX *m, int b, int ps, int ss)
{
    int r = 0;
    for (int n = 0; n < MSX_CART_ALL; n++)
        if (md_cart_here(m, n, ps, ss) && m->cart[n].mapper == MAPPER_YAMANOOTO && b == 3 && (m->cart[n].y_en & 1)) r = n + 1;
    return r;
}
static int mc_ide(const MSX *m, int b, int ps, int ss)
{
    int r = 0;
    for (int n = 0; n < MSX_CART_ALL; n++)
        if (md_cart_here(m, n, ps, ss) && b == 3 && m->cart[n].mapper == MAPPER_SUNRISE && (m->cart[n].ide_ctl & 1)) r = n + 1;
    return r;
}
static int mc_sfg(const MSX *m, int b, int ps, int ss)
{
    int r = 0;
    for (int n = 0; n < MSX_CART_ALL; n++)
        if (md_cart_here(m, n, ps, ss) && m->cart[n].mapper == MAPPER_SFG && b == m->cart[n].plain_base + 1) r = n + 1;   /* 3.4.1 */
    return r;
}
static int mc_spc(const MSX *m, int b, int ps, int ss)
{
    int r = 0;
    for (int n = 0; n < MSX_CART_ALL; n++) {
        if (!md_cart_here(m, n, ps, ss)) continue;
        const Cartridge *c = &m->cart[n];
        if (c->mapper == MAPPER_MEGASCSI && b >= 2 && b <= 5 && (c->bank[b - 2] & 0xC0) == 0x40) r = n + 1;   /* 3.9.14: 뱅크 0x40~0x7F = SPC */
        if (c->mapper == MAPPER_WAVESCSI && b == 2 && (c->scc_mode & 0x40) && !c->ese_we) r = n + 1;          /* 3.9.14: 0x4000 창만 */
    }
    return r;
}
static int mc_fw(const MSX *m, int b, int ps, int ss)
{
    return m->fw.kind && ps == m->fw.ps && (!m->prof.subslotted[ps] || ss == m->fw.ss) && b >= m->fw.start && b < m->fw.start + m->fw.count;   /* 3.4.0 */
}
static int mc_bun(const MSX *m, int b, int ps, int ss)
{
    return b == 5 && m->fw.bun_ps >= 0 && ps == m->fw.bun_ps && (!m->prof.subslotted[ps] || ss == m->fw.bun_ss);
}
/* FDC: 비트 0 = FDC 레지스터, 비트 1 = MSX-DOS2 뱅크 레지스터 */
static int mc_fdc(const MSX *m, int b, int ps, int ss)
{
    int r = m->fdc_kind && ps == m->fdc_ps && (!m->prof.subslotted[ps] || ss == m->fdc_ss) && (b == 3 || b == 5);
    if (m->dos2 && b == 3 && ps == m->dos2_ps && (!m->prof.subslotted[ps] || ss == m->dos2_ss)) r |= 2;
    return r;
}
static int mc_fdc_rd(const MSX *m, int b, int ps, int ss) { return mc_fdc(m, b, ps, ss) & 1; }   /* 읽기는 FDC 레지스터만 */
/* 3.8.0: Super Lode Runner 는 슬롯과 상관없이 0000~3FFF 쓰기를 받음 */
static int mc_lode(const MSX *m, int b, int ps, int ss)
{
    (void)ps; (void)ss;
    if (b >= 2) return 0;
    for (int n = 0; n < MSX_CART_ALL; n++)
        if (m->cart[n].present && m->cart[n].mapper == MAPPER_LODERUNNER) return n + 1;   /* 처음 것 */
    return 0;
}
/* 매퍼 레지스터 쓰기를 받을 카트리지 */
static int mc_hook(const MSX *m, int b, int ps, int ss)
{
    int r = 0;
    for (int n = 0; n < MSX_CART_ALL; n++) {
        const Cartridge *c = &m->cart[n];
        if (md_cart_here(m, n, ps, ss) && c->mapper != MAPPER_PLAIN &&
            (c->mapper == MAPPER_SFG ? (b >= c->plain_base && b < c->plain_base + (int)c->banks8)    /* SFG 는 자기 롬 칸만 (CX5M-2) */
             : ((b >= 2 && b <= 5) || c->mapper == MAPPER_FSCA1 || c->mapper == MAPPER_MATRAINK || c->mapper == MAPPER_ASCII16X ||
                c->mapper == MAPPER_YAMANOOTO || c->mapper == MAPPER_NEO8 || c->mapper == MAPPER_NEO16 || c->mapper == MAPPER_MFRSCCP || c->mapper == MAPPER_FRSCC)))   /* 64K 모드·FFFFh */   /* 4.0.129: 거울 때문에 0000~FFFF 모두 */
            r = n + 1;
    }
    return r;
}

/* ---- 읽기 ---- */
static int mr_ide(MSX *m, int arg, uint16_t a)
{
    if (a < 0x7C00 || a >= 0x7F00) return -1;             /* Sunrise IDE 레지스터 */
    Cartridge *c = &m->cart[arg - 1];
    if (a < 0x7E00) {                                   /* 데이터: 짝수에서 16비트를 읽고 아래, 홀수는 위 */
        if (a & 1) return c->ide_hi;
        uint16_t w = ide_read_data(&m->ide);
        c->ide_hi = (uint8_t)(w >> 8);
        return (uint8_t)w;
    }
    int r = a & 0x0F;
    if (r == 0x0E) return ide_read_reg(&m->ide, 7);      /* 보조 상태 */
    return (r >= 1 && r <= 7) ? ide_read_reg(&m->ide, r) : 0xFF;
}
static int mr_yam(MSX *m, int arg, uint16_t a)
{
    if (a < 0x7FFC) return -1;                          /* 4.0.129: Yamanooto 레지스터 읽기 (거울 아님) */
    const Cartridge *c = &m->cart[arg - 1];
    static const uint8_t fid[5] = { 0xFF, 0x1F, 0x23, 0x00, 0x00 };
    if (a == 0x7FFF) return c->y_en;
    if (a == 0x7FFE) return c->y_off;
    if (a == 0x7FFD) return (uint8_t)(c->y_cfg | 0x80);
    return (c->y_cfg & 0x40) ? fid[c->y_fsm < 5 ? c->y_fsm : 0] : 0xFF;
}
static int mr_sfg(MSX *m, int arg, uint16_t a) { (void)arg; return (a & 0x1FF8) == 0x1FF0 ? sfg_read(m, a & 7) : -1; }   /* 3.4.1 */
static int mr_spc(MSX *m, int arg, uint16_t a) { (void)arg; return (a & 0x1FFF) < 0x1000 ? spc_read_dreg(&m->spc, &m->ide) : spc_read_reg(&m->spc, &m->ide, a & 0x0F); }   /* 3.9.14 */
static int mr_fw(MSX *m, int arg, uint16_t a) { (void)arg; return fw_read(m, a); }     /* 3.4.0 */
static int mr_bun(MSX *m, int arg, uint16_t a) { (void)arg; return bun_read(m, a); }
static int mr_scc(MSX *m, int arg, uint16_t a)
{
    if ((a & 0x1FFF) < 0x1800) return -1;
    Cartridge *c = &m->cart[arg - 1];
    if (a >= 0xBFFE) return c->scc_mode;
    return ((c->mapper == MAPPER_SCCPLUS || c->mapper == MAPPER_YAMANOOTO || c->mapper == MAPPER_MFRSCCP) && (c->scc_mode & 0x20)) ? scc_read_plus(&c->scc, (uint8_t)a) : scc_read(&c->scc, (uint8_t)a);
}
static int mr_mfrp(MSX *m, int arg, uint16_t a) { return a == 0xFFFF ? (uint8_t)~m->cart[arg - 1].mp_sub : -1; }   /* 5.2.46 */
static int mr_fdc(MSX *m, int arg, uint16_t a)
{
    (void)arg;
    int r = fdc_reg(m, a);
    return r >= 0 ? fdc_mem_read(m, r, m->rd[a >> 13][a & 0x1FFF]) : -1;
}

/* ---- 쓰기 ---- */
static int mw_fdc(MSX *m, int arg, uint16_t a, uint8_t v)
{
    if ((arg & 2) && !m->dos2_tr && ((a & 0xF800) == 0x6000 || a == 0x7FFE)) {   /* MSX-DOS2 카트리지 뱅크 레지스터 */
        m->dos2_bank = v & 3; dos2_map(m); update_all_pages(m); return 1;
    }
    int r = (arg & 1) ? fdc_reg(m, a) : -1;
    if (r >= 0) { fdc_mem_write(m, r, v); return 1; }
    return 0;
}
static int mw_fw(MSX *m, int arg, uint16_t a, uint8_t v) { (void)arg; fw_write(m, a, v); return 1; }    /* 3.4.0 */
static int mw_bun(MSX *m, int arg, uint16_t a, uint8_t v) { (void)arg; bun_write(m, a, v); return 1; }
static int mw_lode(MSX *m, int arg, uint16_t a, uint8_t v) { cart_write(m, arg - 1, a, v); return 0; }   /* 받고 아래로 계속 */
static int mw_hook(MSX *m, int arg, uint16_t a, uint8_t v) { cart_write(m, arg - 1, a, v); return 1; }

/* ---- 표 (5.2.119: 장치 한 줄 MemOps, src/device.h) ---- */
#define MEMDEV(id, nm, rc, wc, r, w) [id] = { nm, rc, wc, r, w }
static const MemOps mem_dev[MD_COUNT] = {
    MEMDEV(MD_IDE,  "Sunrise IDE",  mc_ide,    0,       mr_ide,  0),
    MEMDEV(MD_YAM,  "Yamanooto",    mc_yam,    0,       mr_yam,  0),
    MEMDEV(MD_SFG,  "SFG",          mc_sfg,    0,       mr_sfg,  0),
    MEMDEV(MD_SPC,  "SPC",          mc_spc,    0,       mr_spc,  0),
    MEMDEV(MD_FW,   "Firmware",     mc_fw,     mc_fw,   mr_fw,   mw_fw),
    MEMDEV(MD_BUN,  "Bunsetsu",     mc_bun,    mc_bun,  mr_bun,  mw_bun),
    MEMDEV(MD_SCC,  "SCC",          mc_scc,    0,       mr_scc,  0),
    MEMDEV(MD_FDC,  "FDC/DOS2",     mc_fdc_rd, mc_fdc,  mr_fdc,  mw_fdc),
    MEMDEV(MD_LODE, "Lode Runner",  0,         mc_lode, 0,       mw_lode),
    MEMDEV(MD_HOOK, "Cart hook",    0,         mc_hook, 0,       mw_hook),
    MEMDEV(MD_MFRP, "MFR SCC+",     mc_mfrp,   0,       mr_mfrp, 0),
};
static const uint8_t md_rd_order[] = { MD_IDE, MD_YAM, MD_SFG, MD_SPC, MD_FW, MD_BUN, MD_SCC, MD_FDC, MD_MFRP };
static const uint8_t md_wr_order[] = { MD_FDC, MD_FW, MD_BUN, MD_LODE, MD_HOOK };

/* 슬롯이 바뀐 칸 b 의 사슬을 다시 만듦 (update_page 에서) */
static void md_build(MSX *m, int b, int ps, int ss)
{
    int k = 0;
    for (unsigned i = 0; i < sizeof md_rd_order; i++) {
        int d = md_rd_order[i], arg = mem_dev[d].rd_claim(m, b, ps, ss);
        if (arg) { m->mrd_dev[b][k] = (uint8_t)d; m->mrd_arg[b][k] = (uint8_t)arg; k++; }
    }
    m->mrd_n[b] = (uint8_t)k;
    k = 0;
    for (unsigned i = 0; i < sizeof md_wr_order; i++) {
        int d = md_wr_order[i], arg = mem_dev[d].wr_claim(m, b, ps, ss);
        if (arg) { m->mwr_dev[b][k] = (uint8_t)d; m->mwr_arg[b][k] = (uint8_t)arg; k++; }
    }
    m->mwr_n[b] = (uint8_t)k;
    m->rd_sp[b] = m->mrd_n[b];
}

/* 쓰기 사슬에 매퍼·장치 레지스터가 있는가 (Lode Runner 의 0000~3FFF 받기는 빼고 — 스택 그림자 판단용) */
static int md_wr_owned(const MSX *m, int b)
{
    for (int k = 0; k < m->mwr_n[b]; k++) if (m->mwr_dev[b][k] != MD_LODE) return 1;
    return 0;
}

int msx_mem_owners(const MSX *m, int b, int write, uint8_t *dev, int max)   /* 진단·시험용: 칸 b 사슬의 장치 번호들 */
{
    int n = write ? m->mwr_n[b & 7] : m->mrd_n[b & 7];
    for (int k = 0; k < n && k < max; k++) dev[k] = write ? m->mwr_dev[b & 7][k] : m->mrd_dev[b & 7][k];
    return n;
}
