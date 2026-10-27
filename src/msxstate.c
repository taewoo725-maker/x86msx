/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * msxstate.c - 장치별 상태 저장 (msxstate.h 설명 참고).
 * 장치 구조를 바꿀 때: 그 장치 줄의 판 번호를 올림 (길이만 바뀌어도 저절로 건너뜀). 옛 판을 읽게 하려면 그 장치의 옮김 함수를 여기에.
 */
#include "msxstate.h"
#include <stddef.h>
#include <string.h>

#define DEV(t, f, v) { t, (uint32_t)offsetof(MSX, f), (uint32_t)sizeof(((MSX *)0)->f), v }
const MsxStDev msx_st_devs[] = {
    DEV("CPU ", cpu, 1),        DEV("VDP ", vdp, 2),       DEV("PROF", prof, 1),     DEV("FW  ", fw, 1),
    DEV("FDC ", fdc, 1),        DEV("IDE0", ide, 1),       DEV("IDE1", ide_slave, 1), DEV("SPC ", spc, 1),
    DEV("SCSI", scsi, 1),       DEV("F765", fdc765, 1),    DEV("OPM ", opm, 1),      DEV("OPL4", opl4, 1),
    DEV("CART", cart, 3),       DEV("PSG0", psg, 1),       DEV("PSG1", psg2, 1),     DEV("NET ", net, 1),
    DEV("SCHD", sched, 1),
};
const int msx_st_ndevs = (int)(sizeof msx_st_devs / sizeof msx_st_devs[0]);
#define CORE_VER 1                                          /* 5.2.86 까지의 CORE (구조체 통째) — 크기가 맞으면 지금도 읽음 */
/* CORE 필드 표 — MSX 구조체에서 장치 덩이·매퍼 RAM·작업 메모리(scr)를 뺀 모든 필드. 덩이 "COR2" 에 필드마다 [이름 길이 u8][이름][크기 u32][내용].
 * 불러올 때 이름으로 맞춤: 크기가 같으면 덮고, 크기가 다르면 그 필드만 건너뛰고(켠 상태, 건너뜀 목록에 "CORE.이름"), 파일에 없는 새 필드는 켠 상태 그대로.
 * → MSX 구조체에 필드를 더하거나 빼도 옛 상태 파일이 읽힘. 새 필드를 MSX 에 더하면 여기에도 한 줄 (sttest 가 빠진 필드를 잡음). 이름을 바꾸면 옛 파일의 그 필드는 못 읽음 */
#define CF(f) { #f, (uint32_t)offsetof(MSX, f), (uint32_t)sizeof(((MSX *)0)->f) }
const MsxStField msx_st_core[] = {
    CF(ram), CF(mram_size), CF(mapper_ps), CF(mapper_ss), CF(mapper_reg), CF(xram),
    CF(xram_size), CF(xram_ps), CF(xram_ss), CF(rtc_on), CF(rtc_sel), CF(rtc_mode),
    CF(rtc), CF(rtc_now), CF(rtc_ref), CF(rtc_delta), CF(rtc_wlast), CF(vsync_force),
    CF(audio_on), CF(y_pos), CF(y_buf), CF(kanji), CF(kanji_size), CF(kanji_a1),
    CF(kanji_a2), CF(kanji_hi_mask), CF(s1985), CF(sw_id), CF(s1985_addr), CF(s1985_ram),
    CF(fdc_kind), CF(fdc_ps), CF(fdc_ss), CF(io_chain), CF(io_built), CF(io_sig),
    CF(rd_sp), CF(mrd_n), CF(mrd_dev), CF(mrd_arg), CF(mwr_n), CF(mwr_dev),
    CF(mwr_arg), CF(dos2), CF(dos2_size), CF(dos2_ps), CF(dos2_ss), CF(dos2_bank),
    CF(ch_vol), CF(opll), CF(y8950), CF(opll_on), CF(opll_acc), CF(opll_quiet),
    CF(opll_last), CF(opll_prev), CF(dos2_tr), CF(s1990_reg), CF(s1990_cpu), CF(s1990_switch), CF(r800_pct),
    CF(trtimer_base), CF(f4_status), CF(trpause_reg), CF(pause_sw), CF(pcm_ctrl), CF(pcm_da),
    CF(pcm_n), CF(pcm_t), CF(pcm_v), CF(frame_start), CF(ms_on), CF(ds_head),
    CF(ds_pending), CF(ds_click), CF(ds_motor), CF(ds_rng), CF(ds_phase), CF(sfg_on),
    CF(opm_t), CF(opm_acc), CF(opm_l), CF(opm_r), CF(opm_pl), CF(opm_pr), CF(sfg_kbd), CF(sfg_vec),
    CF(ms_pos), CF(ms_buf), CF(pcm_last), CF(tape), CF(tape_size), CF(tape_cap),
    CF(tape_pos), CF(tape_dirty), CF(tape_motor), CF(joy), CF(mouse_port), CF(mouse_btn),
    CF(mouse_phase), CF(mouse_pin), CF(mouse_cx), CF(mouse_cy), CF(mouse_lx), CF(mouse_ly),
    CF(mouse_t), CF(cur_line), CF(line_start), CF(rom), CF(slot_rd), CF(slot_wr),
    CF(rd), CF(wr), CF(sh_orig), CF(sh_lo), CF(sh_hi), CF(sh_blk),
    CF(sh_buf), CF(cart_slot), CF(cart_ss), CF(expand), CF(primary), CF(subslot),
    CF(ppi_c), CF(kbd), CF(prn_on), CF(prn_strobe), CF(prn_data), CF(prn_poked),
    CF(prn_n), CF(prn_q), CF(stereo), CF(pan), CF(out_l), CF(out_r),
    CF(lvl), CF(midi_cmd), CF(midi_mode_next), CF(midi_n), CF(midi_q), CF(midi_t),
    CF(midi_rx_q), CF(midi_rx_r), CF(midi_rx_w), CF(midi_rx_data), CF(midi_rxrdy), CF(midi_tflag),
    CF(midi_rx_next), CF(pit), CF(joyrex), CF(v9), CF(chip_act), CF(vdp_busy_until),
    CF(r800_acc), CF(r800_ref), CF(vdp_io_last), CF(io_stall), CF(vhd_hide_fdc), CF(vhd_on),
    CF(vhd_io), CF(vhd_ctx), CF(ide_cart_io), CF(ide_compat), CF(chput_n), CF(chput_ret),
    CF(chput_sp), CF(chput_ix), CF(chput_iy), CF(frame), CF(line_budget), CF(irq_lines),
    CF(sched_test_hits),
};
const int msx_st_ncore = (int)(sizeof msx_st_core / sizeof msx_st_core[0]);
#define COR2_VER 1

uint32_t st_put(uint8_t *out, uint32_t max, const char tag[4], uint32_t ver, const void *data, uint32_t len)
{
    uint32_t pad = (4u - (len & 3u)) & 3u;
    if ((uint64_t)ST_CHUNK_HDR + len + pad > max) return 0;
    memcpy(out, tag, 4); memcpy(out + 4, &ver, 4); memcpy(out + 8, &len, 4);
    if (len) memcpy(out + ST_CHUNK_HDR, data, len);
    memset(out + ST_CHUNK_HDR + len, 0, pad);
    return ST_CHUNK_HDR + len + pad;
}

const uint8_t *st_find(const uint8_t *buf, uint32_t len, const char tag[4], uint32_t *ver, uint32_t *size)
{
    uint32_t p = 0;
    while (p + ST_CHUNK_HDR <= len) {
        uint32_t v, l;
        memcpy(&v, buf + p + 4, 4); memcpy(&l, buf + p + 8, 4);
        if (l > len - p - ST_CHUNK_HDR) return 0;           /* 잘린 파일 */
        if (!memcmp(buf + p, tag, 4)) { if (ver) *ver = v; if (size) *size = l; return buf + p + ST_CHUNK_HDR; }
        p += ST_CHUNK_HDR + l + ((4u - (l & 3u)) & 3u);
    }
    return 0;
}

/* CORE 에서 빼는 칸 (장치 + 매퍼 RAM), 앞에서부터 */
static int core_holes(uint32_t (*h)[2])
{
    int n = 0;
    for (int i = 0; i < msx_st_ndevs; i++) { h[n][0] = msx_st_devs[i].off; h[n][1] = msx_st_devs[i].off + msx_st_devs[i].size; n++; }
    h[n][0] = (uint32_t)offsetof(MSX, mram); h[n][1] = h[n][0] + MSX_MRAM_MAX; n++;
    h[n][0] = (uint32_t)offsetof(MSX, scr); h[n][1] = h[n][0] + (uint32_t)sizeof(((MSX *)0)->scr); n++;   /* 5.2.40 작업 메모리 */
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && h[j][0] < h[j - 1][0]; j--) { uint32_t a = h[j][0], b = h[j][1]; h[j][0] = h[j - 1][0]; h[j][1] = h[j - 1][1]; h[j - 1][0] = a; h[j - 1][1] = b; }
    return n;
}
#define HOLES_MAX 32
static uint32_t core_size(void)
{
    uint32_t h[HOLES_MAX][2]; int n = core_holes(h), s = (int)sizeof(MSX);
    for (int i = 0; i < n; i++) s -= (int)(h[i][1] - h[i][0]);
    return (uint32_t)s;
}
/* dir 1 = 기계 → buf, 0 = buf → 기계 */
static void core_copy(MSX *m, uint8_t *buf, int dir)
{
    uint32_t h[HOLES_MAX][2]; int n = core_holes(h); uint32_t at = 0, q = 0;
    for (int i = 0; i <= n; i++) {
        uint32_t e = i < n ? h[i][0] : (uint32_t)sizeof(MSX);
        if (e > at) { if (dir) memcpy(buf + q, (uint8_t *)m + at, e - at); else memcpy((uint8_t *)m + at, buf + q, e - at); q += e - at; }
        if (i < n) at = h[i][1];
    }
}

/* COR2 덩이 내용 크기 */
static uint32_t cor2_size(void)
{
    uint32_t s = 0;
    for (int i = 0; i < msx_st_ncore; i++) s += 1u + (uint32_t)strlen(msx_st_core[i].name) + 4u + msx_st_core[i].size;
    return s;
}
static void cor2_write(MSX *m, uint8_t *o)
{
    for (int i = 0; i < msx_st_ncore; i++) {
        const MsxStField *f = &msx_st_core[i];
        uint8_t nl = (uint8_t)strlen(f->name);
        *o++ = nl; memcpy(o, f->name, nl); o += nl; memcpy(o, &f->size, 4); o += 4;
        memcpy(o, (uint8_t *)m + f->off, f->size); o += f->size;
    }
}
/* 기록을 하나씩: 돌려줌 1 = 하나 읽음, 0 = 끝, -1 = 깨짐 */
static int cor2_next(const uint8_t *p, uint32_t len, uint32_t *at, const char **name, uint32_t *nlen, const uint8_t **data, uint32_t *size)
{
    if (*at == len) return 0;
    if (*at + 1 > len) return -1;
    uint32_t nl = p[*at];
    if (!nl || *at + 1 + nl + 4 > len) return -1;
    uint32_t sz; memcpy(&sz, p + *at + 1 + nl, 4);
    if (sz > len - (*at + 1 + nl + 4)) return -1;
    *name = (const char *)p + *at + 1; *nlen = nl; *data = p + *at + 1 + nl + 4; *size = sz;
    *at += 1 + nl + 4 + sz;
    return 1;
}
static int cor2_valid(const uint8_t *p, uint32_t len)
{
    uint32_t at = 0, nl, sz; const char *nm; const uint8_t *d; int r, n = 0;
    while ((r = cor2_next(p, len, &at, &nm, &nl, &d, &sz)) == 1) n++;
    return r == 0 && n > 0;
}
static void skip_add(char *s, int max, const char *tag);
static void skip_name(char *s, int max, const char *name, uint32_t nl)
{
    if (!s || max < 8) return;
    int n = (int)strlen(s);
    if (n + 6 + (int)nl + 2 >= max) return;
    if (n) s[n++] = ',';
    memcpy(s + n, "CORE.", 5); n += 5; memcpy(s + n, name, nl); n += (int)nl;
    s[n] = 0;
}
/* 이름으로 맞춰 덮기 */
static void cor2_read(MSX *m, const uint8_t *p, uint32_t len, char *skipped, int skiplen)
{
    uint32_t at = 0, nl, sz; const char *nm; const uint8_t *d;
    while (cor2_next(p, len, &at, &nm, &nl, &d, &sz) == 1) {
        for (int i = 0; i < msx_st_ncore; i++) {
            const MsxStField *f = &msx_st_core[i];
            if (strlen(f->name) != nl || memcmp(f->name, nm, nl)) continue;
            if (sz == f->size) memcpy((uint8_t *)m + f->off, d, sz);
            else skip_name(skipped, skiplen, nm, nl);        /* 크기가 바뀐 필드: 켠 상태 그대로 */
            break;
        }                                                   /* 표에 없는 이름(없어진 필드)은 버림 */
    }
}
/* 시험용: 5.2.86 까지의 CORE(통째) 덩이를 만듦 */
uint32_t msx_st_core_v1(MSX *m, uint8_t *out, uint32_t max)
{
    uint32_t cs = core_size();
    if (ST_CHUNK_HDR + cs + 4 > max) return 0;
    memcpy(out, "CORE", 4); { uint32_t v = CORE_VER; memcpy(out + 4, &v, 4); } memcpy(out + 8, &cs, 4);
    core_copy(m, out + ST_CHUNK_HDR, 1);
    uint32_t l = ST_CHUNK_HDR + cs; while (l & 3u) out[l++] = 0;
    return l;
}

uint32_t msx_st_save(MSX *m, uint8_t *out, uint32_t max)
{
    uint32_t len = 0, k, cs = cor2_size();                 /* CORE 는 필드 표(COR2)로 */
    if (ST_CHUNK_HDR + cs + 4 > max) return 0;
    memcpy(out, "COR2", 4); { uint32_t v = COR2_VER; memcpy(out + 4, &v, 4); } memcpy(out + 8, &cs, 4);
    cor2_write(m, out + ST_CHUNK_HDR);
    len = ST_CHUNK_HDR + cs; while (len & 3u) out[len++] = 0;
    for (int i = 0; i < msx_st_ndevs; i++) {
        const MsxStDev *d = &msx_st_devs[i];
        if (!(k = st_put(out + len, max - len, d->tag, d->ver, (uint8_t *)m + d->off, d->size))) return 0;
        len += k;
    }
    if (!(k = st_put(out + len, max - len, "MRAM", 1, m->mram, m->mram_size))) return 0;
    len += k;
    if (m->xram && m->xram_size) { if (!(k = st_put(out + len, max - len, "XRAM", 1, m->xram, m->xram_size))) return 0; len += k; }
    { uint8_t r[64]; msx_opll_regs(m, r); if (!(k = st_put(out + len, max - len, "OPLL", 1, r, 64))) return 0; len += k; }
    { uint8_t r[256]; msx_y8950_regs(m, r); if (!(k = st_put(out + len, max - len, "Y895", 1, r, 256))) return 0; len += k; }
    { uint32_t ys = 0; uint8_t *yr = msx_y8950_ram(m, &ys); if (yr && ys) { if (!(k = st_put(out + len, max - len, "YRAM", 1, yr, ys))) return 0; len += k; } }
    if (m->ms_on && opl4_memory()) {                        /* MoonSound 웨이브 RAM (쓴 앞부분만, 4 KiB 단위) */
        const uint8_t *w = opl4_memory() + OPL4_RAM_OFF;
        uint32_t wl = OPL4_RAM_SIZE;
        while (wl && !w[wl - 1]) wl--;
        wl = (wl + 4095u) & ~4095u;
        if (wl > OPL4_RAM_SIZE) wl = OPL4_RAM_SIZE;
        if (!(k = st_put(out + len, max - len, "WRAM", 1, w, wl))) return 0;
        len += k;
    }
    if (m->v9) { if (!(k = st_put(out + len, max - len, "V9K ", 1, m->v9, (uint32_t)offsetof(V9990, fb)))) return 0; len += k; }   /* 화면 버퍼 fb 앞까지 */
    return len;
}

static const MsxStDev *dev_of(const char *tag) { for (int i = 0; i < msx_st_ndevs; i++) if (!memcmp(msx_st_devs[i].tag, tag, 4)) return &msx_st_devs[i]; return 0; }

int msx_st_cart(const uint8_t *buf, uint32_t len, int n, int *present, int *mapper)
{
    uint32_t v, s; const MsxStDev *d = dev_of("CART");
    const uint8_t *p = st_find(buf, len, "CART", &v, &s);
    if (!p || n < 0 || n >= MSX_CART_ALL) return -1;
    Cartridge c;
    if (v == d->ver && s == d->size) memcpy(&c, p + (size_t)n * sizeof(Cartridge), sizeof c);
    else if (v == 1 && s == (uint32_t)(MSX_CART_ALL * CART_ST_V1_SIZE)) { memset(&c, 0, sizeof c); memcpy(&c, p + (size_t)n * CART_ST_V1_SIZE, CART_ST_V1_SIZE); }   /* 판 1 */
    else if (v == 2 && s == (uint32_t)(MSX_CART_ALL * CART_ST_V2_SIZE)) { memset(&c, 0, sizeof c); memcpy(&c, p + (size_t)n * CART_ST_V2_SIZE, CART_ST_V2_SIZE); }   /* 판 2 */
    else return -1;
    *present = c.present; *mapper = (int)c.mapper;
    return 0;
}

static void skip_add(char *s, int max, const char *tag)
{
    if (!s || max < 8) return;
    int n = (int)strlen(s);
    if (n + 6 >= max) return;
    if (n) s[n++] = ',';
    for (int i = 0; i < 4 && tag[i] != ' '; i++) s[n++] = tag[i];
    s[n] = 0;
}

int msx_st_check(const uint8_t *buf, uint32_t len)
{
    uint32_t v, s;
    const uint8_t *c2 = st_find(buf, len, "COR2", &v, &s);   /* 필드 표 — 판만 맞으면 됨 (필드는 이름으로 맞춤) */
    if (c2) { if (v != COR2_VER || !cor2_valid(c2, s)) return -1; }
    else if (!st_find(buf, len, "CORE", &v, &s) || v != CORE_VER || s != core_size()) return -1;   /* 5.2.86 까지: 구조체 통째, 크기가 같아야 */
    if (!st_find(buf, len, "MRAM", &v, &s) || v != 1 || s > MSX_MRAM_MAX) return -1;
    return 0;
}

int msx_st_load(MSX *m, const uint8_t *buf, uint32_t len, char *skipped, int skiplen)
{
    if (skipped && skiplen) skipped[0] = 0;
    uint32_t v, s;
    if (msx_st_check(buf, len)) return -1;
    uint32_t core_len = 0;
    const uint8_t *cor2 = st_find(buf, len, "COR2", &v, &core_len);
    const uint8_t *core = cor2 ? 0 : st_find(buf, len, "CORE", &v, &s);
    const uint8_t *mr = st_find(buf, len, "MRAM", &v, &s);
    uint32_t mram_len = s;
    /* 지금 기계의 포인터·설정을 잡아 둠 (구조체 전체는 5 MiB 라 포인터가 든 필드만) */
    static struct {
        Z80 cpu;
        const uint8_t *kanji, *dos2;
        RomImage rom[MAX_REGIONS];
        const uint8_t *slot_rd[4][4][8];
        uint8_t *slot_wr[4][4][8];
        Cartridge cart[MSX_CART_ALL];
        FdcDrive drv[3];
        uint8_t *tape; uint32_t tape_size, tape_cap;
        IDE ide, ide2;
        FwDev fw;
        int r800_pct, vsync_force;
        int ch_vol[MSX_CH_N];
        V9990 *v9;
        uint8_t *xram;
        struct __OPLL *opll; struct __OPL *y8950;   /* 5.2.40 */
    } L;
    L.cpu = m->cpu; L.v9 = m->v9; L.xram = m->xram; L.opll = m->opll; L.y8950 = m->y8950;
    L.kanji = m->kanji; L.dos2 = m->dos2;
    memcpy(L.rom, m->rom, sizeof m->rom);
    memcpy(L.slot_rd, m->slot_rd, sizeof m->slot_rd);
    memcpy(L.slot_wr, m->slot_wr, sizeof m->slot_wr);
    memcpy(L.cart, m->cart, sizeof m->cart);
    memcpy(L.drv, m->fdc.drv, sizeof m->fdc.drv);
    L.tape = m->tape; L.tape_size = m->tape_size; L.tape_cap = m->tape_cap;
    L.fw = m->fw; L.ide = m->ide; L.ide2 = m->ide_slave;
    L.r800_pct = m->r800_pct; L.vsync_force = m->vsync_force;
    memcpy(L.ch_vol, m->ch_vol, sizeof m->ch_vol);
    /* 덮기: CORE, 맞는 장치 덩이, 매퍼 RAM */
    if (cor2) cor2_read(m, cor2, core_len, skipped, skiplen);   /* 이름으로 맞춰 덮기 */
    else core_copy(m, (uint8_t *)core, 0);
    for (int i = 0; i < msx_st_ndevs; i++) {
        const MsxStDev *d = &msx_st_devs[i];
        const uint8_t *p = st_find(buf, len, d->tag, &v, &s);
        if (p && v == d->ver && s == d->size) memcpy((uint8_t *)m + d->off, p, d->size);
        else if (p && !memcmp(d->tag, "VDP ", 4) && v == 1 && s == VDP_ST_V1_SIZE) {   /* 판 1 = 팔레트가 밖에 있던 VDP (앞부분만 같음) */
            memcpy((uint8_t *)m + d->off, p, s); m->vdp.pal_58 = m->vdp.v9958; m->vdp.pal_nsnap = 1; vdp_default_palette(&m->vdp);
        }
        else if (p && !memcmp(d->tag, "CART", 4) && ((v == 1 && s == (uint32_t)(MSX_CART_ALL * CART_ST_V1_SIZE)) || (v == 2 && s == (uint32_t)(MSX_CART_ALL * CART_ST_V2_SIZE)))) {   /* 5.2.46·5.2.48: 옛 판 = 뒤 필드가 없던 카트 (앞부분만 같음) */
            size_t one = v == 1 ? CART_ST_V1_SIZE : CART_ST_V2_SIZE;
            for (int n = 0; n < MSX_CART_ALL; n++) { memset(&m->cart[n], 0, sizeof(Cartridge)); memcpy(&m->cart[n], p + (size_t)n * one, one); }
        }
        else skip_add(skipped, skiplen, d->tag);
    }
    memcpy(m->mram, mr, mram_len);
    m->xram = L.xram; m->opll = L.opll; m->y8950 = L.y8950;   /* 칩은 이 기계 것 그대로 (레지스터는 OPLL·Y895 덩이로 되살림) */
    { const uint8_t *x = st_find(buf, len, "XRAM", &v, &s);
      if (x && m->xram && v == 1 && s <= MSX_MRAM_MAX) memcpy(m->xram, x, s); else m->xram_size = 0; }
    /* 포인터·설정 되살리기 */
    m->v9 = L.v9;
    msx_io_rebuild(m);
    m->cpu.ctx = L.cpu.ctx;
    m->cpu.mem_read = L.cpu.mem_read; m->cpu.mem_write = L.cpu.mem_write;
    m->cpu.io_read = L.cpu.io_read; m->cpu.io_write = L.cpu.io_write;
    m->kanji = L.kanji; m->dos2 = L.dos2;
    memcpy(m->rom, L.rom, sizeof m->rom);
    memcpy(m->slot_rd, L.slot_rd, sizeof m->slot_rd);
    memcpy(m->slot_wr, L.slot_wr, sizeof m->slot_wr);
    for (int n = 0; n < MSX_CART_ALL; n++) {
        Cartridge *c = &m->cart[n], *l = &L.cart[n];
        if (!l->present || c->present != l->present || c->mapper != l->mapper) { *c = *l; continue; }   /* 다시 꽂지 못한 카트리지 */
        c->data = l->data; c->size = l->size; c->banks8 = l->banks8;
    }
    for (int d = 0; d < 3; d++) {
        FdcDrive *x = &m->fdc.drv[d], *l = &L.drv[d];
        x->data = l->data; x->size = l->size;
        if (!l->data) x->size = 0;
        x->dirty = 0;
    }
    if (!L.tape) { m->tape = 0; m->tape_size = 0; m->tape_pos = 0; }
    else { m->tape = L.tape; m->tape_size = L.tape_size; m->tape_cap = L.tape_cap; if (m->tape_pos > m->tape_size) m->tape_pos = 0; }
    m->tape_dirty = 0;
    m->ide.rd = L.ide.rd; m->ide.wr = L.ide.wr; m->ide.ctx = L.ide.ctx;
    m->fw.rom = L.fw.rom; m->fw.rom_size = L.fw.rom_size; m->fw.jisyo = L.fw.jisyo; m->fw.jisyo_size = L.fw.jisyo_size;
    if (!L.ide.present) m->ide = L.ide;
    m->ide_slave.rd = L.ide2.rd; m->ide_slave.wr = L.ide2.wr; m->ide_slave.ctx = L.ide2.ctx;
    if (!L.ide2.present) m->ide_slave = L.ide2;
    m->ide.slave = &m->ide_slave;
    m->r800_pct = L.r800_pct;
    memcpy(m->ch_vol, L.ch_vol, sizeof m->ch_vol);
    { uint8_t r[64]; const uint8_t *p = st_find(buf, len, "OPLL", &v, &s); if (p && v == 1 && s == 64) memcpy(r, p, 64); else memset(r, 0, 64); msx_state_fixup(m, r); }
    m->vsync_force = L.vsync_force;                         /* 주사 주파수는 설정값 그대로 */
    { uint8_t r[256]; const uint8_t *p = st_find(buf, len, "Y895", &v, &s); if (p && v == 1 && s == 256) memcpy(r, p, 256); else memset(r, 0, 256); msx_state_fixup_audio(m, r); }
    /* 따로 둔 메모리 (장치가 켜져 있을 때만) */
    { uint32_t ys = 0; uint8_t *yr = msx_y8950_ram(m, &ys); const uint8_t *p = st_find(buf, len, "YRAM", &v, &s);
      if (yr && p && v == 1 && s <= ys) memcpy(yr, p, s); }
    if (m->ms_on && opl4_memory()) {
        uint8_t *w = opl4_memory() + OPL4_RAM_OFF; const uint8_t *p = st_find(buf, len, "WRAM", &v, &s);
        if (!p || v != 1 || s > OPL4_RAM_SIZE) s = 0;
        if (s) memcpy(w, p, s);
        memset(w + s, 0, OPL4_RAM_SIZE - s);
    }
    if (m->v9) {
        const uint8_t *p = st_find(buf, len, "V9K ", &v, &s);
        if (p && v == 1 && s == (uint32_t)offsetof(V9990, fb)) { memcpy(m->v9, p, s); memset(m->v9->fb, 0, sizeof m->v9->fb); }
        else if (p) skip_add(skipped, skiplen, "V9K ");
    }
    return 0;
}
