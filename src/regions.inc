/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * regions.inc - 기종 영역(config.ini [Slots] 줄 → ProfileRegion) 종류마다 만들기 함수 + 표 region_ops.
 * msx.c 가 한 번역 단위로 #include (msx_setup 바로 앞). 5.2.88 까지 msx_setup 안의 if 사슬(약 180줄)을 그대로 옮김 — 동작 같음.
 *
 *   int rg_X(MSX *m, int i, const ProfileRegion *r, const RomImage *img)
 *     i   = 영역 번호 (m->rom[i] 에 롬을 기억), r = 영역, img = 그 영역의 롬 파일 (없으면 data 0)
 *     돌려줌: 0 = 됨(또는 건너뜀), MSX_ERR_* = 기종 만들기 실패
 *   표 region_ops[RegionType] = { 이름, 만들기 } — 0 번(모르는 종류)과 ROM_NORMAL·ROM_BASIC 은 일반 롬(rg_rom).
 * 새 영역 종류 = profile.h RegionType 에 번호 + devcat.c 표 줄 + 여기 함수 하나와 표 한 줄. msx_setup 은 안 고침.
 * 검증: tests/fuzz/setupcmp.c (make fuzzcmp) — 실제 기종 파일 436개 + 합성 줄에 가짜 롬을 넣어 이전 판과 결과 비교.
 */
#define REGION_TYPES (REGION_CART + 1)

/* 일반 롬 (ROM_NORMAL·ROM_BASIC, 디스크 롬의 롬 부분, 모르는 종류) */
static int rg_rom(MSX *m, int i, const ProfileRegion *r, const RomImage *img)
{
    uint32_t region_size = (uint32_t)r->block_count * 8192;
    int nblk = r->block_count;
    if (!img->data || !img->size) return MSX_ERR_ROM_MISSING;
    /* blueMSX는 ROM 크기를 파일에서 읽음 — config의 크기 칸이 파일보다 작아도(예: Sanyo MPC-25FD 본체 롬 32KB인데 칸은 1) 파일 크기만큼 깔고, 슬롯 끝(64KB)을 넘을 때만 오류 */
    if (img->size > region_size && !(img->size % 8192) && (uint32_t)r->start_block * 8192 + img->size <= 65536) { region_size = img->size; nblk = (int)(img->size / 8192); }
    if (img->size > region_size || img->size % 8192) return MSX_ERR_ROM_SIZE;
    m->rom[i] = *img;
    /* 영역보다 작은 ROM은 반복 배치 (설계서 §1.4: 한글 ROM) */
    for (int k = 0; k < nblk; k++) {
        uint32_t off = ((uint32_t)k * 8192) % img->size;
        m->slot_rd[r->slot][r->subslot][r->start_block + k] = img->data + off;
    }
    /* 8KB ROM이 16KB 페이지 앞쪽 칸에 단독이면 뒤쪽 8KB에도 거울 (Pioneer PX-7 P-BASIC 등 — 실기는 4000-7FFF 전체에 디코딩) */
    if (nblk == 1 && img->size == 8192 && !(r->start_block & 1) && r->start_block < 7) {
        const uint8_t **nx = (const uint8_t **)&m->slot_rd[r->slot][r->subslot][r->start_block + 1];
        if (*nx == NULL || *nx == (const uint8_t *)ff_block) *nx = img->data;
    }
    return 0;
}

/* 램 (슬롯 칸을 본체 ram 에) */
static int rg_ram(MSX *m, int i, const ProfileRegion *r, const RomImage *img)
{
    (void)i; (void)img;
    for (int k = 0; k < r->block_count; k++) {
        int b = r->start_block + k;
        m->slot_rd[r->slot][r->subslot][b] = m->ram + b * 8192;
        m->slot_wr[r->slot][r->subslot][b] = m->ram + b * 8192;
    }
    return 0;
}

/* 메모리 매퍼 RAM (포트 FC~FF) — 크기는 2의 거듭제곱 조각 수로 */
static int rg_mapram(MSX *m, int i, const ProfileRegion *r, const RomImage *img)
{
    (void)i; (void)img;
    uint32_t sz = (uint32_t)r->size_blocks * 8192, p2 = 16384;
    while (p2 * 2 <= sz && p2 * 2 <= MSX_MRAM_MAX) p2 *= 2;
    m->mram_size = p2;
    m->mapper_ps = r->slot;
    m->mapper_ss = r->subslot;
    return 0;
}

/* MSX-DOS2 롬 (종류 2): 16 KiB 뱅크 */
static int rg_dos2(MSX *m, int i, const ProfileRegion *r, const RomImage *img)
{
    if (img->data && img->size) {
        m->dos2 = img->data; m->dos2_size = img->size;
        m->dos2_ps = r->slot; m->dos2_ss = r->subslot; m->rom[i] = *img;
    }
    return 0;
}

/* 디스크 롬 + FDC (64 대우·내셔널, 65 필립스·소니, 57·143 TC8566AF …) — 롬 부분은 일반 롬. turbo R(143) 의 DOS 2.3 롬은 16 KiB 뱅크 */
static int rg_disk(MSX *m, int i, const ProfileRegion *r, const RomImage *img)
{
    m->fdc_kind = r->blue_type;
    m->fdc_ps = r->slot;
    m->fdc_ss = r->subslot;
    if (r->blue_type == 143 && img->data && img->size > 16384) {
        m->dos2 = img->data; m->dos2_size = img->size;
        m->dos2_ps = r->slot; m->dos2_ss = r->subslot; m->dos2_tr = 1; m->rom[i] = *img;
        return 0;
    }
    /* 5.3.59: 디스크 롬(16 KiB)이 칸(32 KiB)보다 작으면 반복(거울) 배치하지 않음 — 8000h 에 롬 머리글(AB)이 또 생겨 BIOS 가 디스크 롬을 두 번 초기화(드라이브 표 중복·HIMEM 약 3 KB 낮아짐)했고, FRAY K 가 A1WSX·CPC-400S 에서 검은 화면 */
    if (img->data && img->size && !(img->size % 8192) && img->size < (uint32_t)r->block_count * 8192) {
        ProfileRegion rr = *r; rr.block_count = (int)(img->size / 8192);
        return rg_rom(m, i, &rr, img);
    }
    return rg_rom(m, i, r, img);
}

/* 3.1.0: 기종 내장 PAC SRAM (종류 43) = 내장 카트리지 자리 */
static int rg_pac(MSX *m, int i, const ProfileRegion *r, const RomImage *img)
{
    (void)i; (void)img;
    int q = int_cart_alloc(m); if (q < 0) return 0;
    Cartridge *c = &m->cart[q];
    memset(c, 0, sizeof *c);
    c->present = 1; c->mapper = MAPPER_PAC; c->data = ff_block; c->size = 8192; c->banks8 = 1;
    m->cart_slot[q] = r->slot; m->cart_ss[q] = r->subslot;
    return 0;
}

/* 3.4.0 펌웨어 매퍼 (27·68·142 파나소닉, 31 내셔널, 40 Halnote) */
static int rg_fw(MSX *m, int i, const ProfileRegion *r, const RomImage *img)
{
    FwDev *f = &m->fw;
    if (!img->data || !img->size) return 0;
    if (!msx_fw_turbor && (r->blue_type == 27 || r->blue_type == 68)) return 0;   /* turbo R 내장 소프트웨어는 켰을 때만 (끄면 디스크·롬팩으로 바로) */
    f->kind = r->blue_type == 31 ? FW_NATIONAL : r->blue_type == 40 ? FW_HALNOTE : FW_PANASONIC;
    f->rom = img->data; f->rom_size = img->size;
    f->ps = r->slot; f->ss = r->subslot; f->start = r->start_block; f->count = r->block_count;
    f->sram_size = r->blue_type == 68 ? 32768u : f->kind == FW_NATIONAL ? 4096u : 16384u;
    f->sram_mirror = r->blue_type == 142; f->ram_banks = r->blue_type == 27 || r->blue_type == 68;
    m->rom[i] = *img;
    return 0;
}

/* 3.4.0 문절 변환 롬 0x4000~0xBFFF (28) + 사전 */
static int rg_bunsetsu(MSX *m, int i, const ProfileRegion *r, const RomImage *img)
{
    if (img->data && img->size) {
        for (int k = 0; k < r->block_count; k++) {
            uint32_t o = (uint32_t)k * 8192;
            m->slot_rd[r->slot][r->subslot][r->start_block + k] = o < img->size ? (uint8_t *)(uintptr_t)img->data + o : ff_block;
        }
        m->fw.bun_ps = r->slot; m->fw.bun_ss = r->subslot; m->rom[i] = *img;
    }
    return 0;
}

/* 3.4.0 사전 롬 (29, 메모리 블록 없음) */
static int rg_jisyo(MSX *m, int i, const ProfileRegion *r, const RomImage *img)
{
    (void)r;
    if (img->data && img->size) { m->fw.jisyo = img->data; m->fw.jisyo_size = img->size; m->rom[i] = *img; }
    return 0;
}

/* 3.4.0 DEV-28 Forte II (138): 롬 칩의 주소선·자료선 배선을 풀어 64 KB로 */
static int rg_forte(MSX *m, int i, const ProfileRegion *r, const RomImage *img)
{
    static uint8_t forte[65536];
    memset(forte, 0xFF, sizeof forte);
    for (uint32_t a = 0; img->data && a < img->size && a < 65536; a++) {
        uint32_t v = a, d = img->data[a];
        uint32_t ca = ((v >> 10) & 1) | ((v & 0xFF) << 1) | (((v >> 12) & 1) << 9) | (((v >> 15) & 1) << 10) | (((v >> 14) & 1) << 11) |
                      (((v >> 13) & 1) << 12) | (((v >> 8) & 1) << 13) | (((v >> 9) & 1) << 14) | (((v >> 11) & 1) << 15);
        uint32_t cd = ((d >> 1) & 1) | (((d >> 2) & 1) << 1) | (((d >> 4) & 1) << 2) | ((d & 1) << 3) | (((d >> 7) & 1) << 4) |
                      (((d >> 6) & 1) << 5) | (((d >> 5) & 1) << 6) | (((d >> 3) & 1) << 7);
        forte[ca] = (uint8_t)cd;
    }
    for (int k = 0; k < r->block_count && r->start_block + k < 8; k++) m->slot_rd[r->slot][r->subslot][r->start_block + k] = forte + k * 8192;
    m->rom[i] = *img;
    return 0;
}

/* 3.4.1 DEV-32: 기종 내장 SFG-01·05 (YIS503M·CX5M, 106·105) = 내장 카트리지 자리 */
static int rg_sfg(MSX *m, int i, const ProfileRegion *r, const RomImage *img)
{
    if (!img->data || !img->size) return 0;
    int q = int_cart_alloc(m); if (q < 0) return 0;
    Cartridge *c = &m->cart[q];
    memset(c, 0, sizeof *c);
    c->present = 1; c->mapper = MAPPER_SFG; c->data = img->data; c->size = img->size;
    c->banks8 = img->size / 8192 ? img->size / 8192 : 1;
    c->plain_base = r->start_block;
    m->cart_slot[q] = r->slot; m->cart_ss[q] = r->subslot;
    m->rom[i] = *img;
    return 0;
}

/* 3.4.0 DEV-15: 기종 내장 Sunrise IDE (Ciel Expert 3 IDE, 96) = 내장 카트리지 자리 */
static int rg_sunrise(MSX *m, int i, const ProfileRegion *r, const RomImage *img)
{
    if (!img->data || !img->size) return 0;
    int q = int_cart_alloc(m); if (q < 0) return 0;
    Cartridge *c = &m->cart[q];
    memset(c, 0, sizeof *c);
    c->present = 1; c->mapper = MAPPER_SUNRISE; c->data = img->data; c->size = img->size;
    c->banks8 = img->size / 8192 ? img->size / 8192 : 1;
    m->cart_slot[q] = r->slot; m->cart_ss[q] = r->subslot;
    m->rom[i] = *img;
    return 0;
}

/* 3.6.8: 기종 내장 카트리지 (config.ini 형식 → 매퍼, 파일 없으면 빈 버퍼) */
static int rg_cart(MSX *m, int i, const ProfileRegion *r, const RomImage *img)
{
    int q = int_cart_alloc(m); if (q < 0) return 0;
    const uint8_t *d = img->data; uint32_t sz = img->size;
    int writable = r->cart_mapper == MAPPER_MFRSCC || r->cart_mapper == MAPPER_MFRSCCP || r->cart_mapper == MAPPER_ESESCC || r->cart_mapper == MAPPER_MEGASCSI || r->cart_mapper == MAPPER_WAVESCSI || r->cart_mapper == MAPPER_MATRAINK || r->cart_mapper == MAPPER_ESERAM ||
                   r->cart_mapper == MAPPER_MEGARAM || r->cart_mapper == MAPPER_RAM || r->cart_mapper == MAPPER_SCCPLUS || r->cart_mapper == MAPPER_GIDE || !d;
    if (writable) {                             /* 쓸 수 있는 버퍼 (파일이 있으면 그 내용) */
        uint32_t want = r->cart_size ? r->cart_size : sz;
        if (!want || m->scr.int_pool_used + want > sizeof m->scr.int_pool) return 0;
        uint8_t *b = m->scr.int_pool + m->scr.int_pool_used; m->scr.int_pool_used += (want + 8191) & ~8191u;
        memset(b, r->cart_fill, want);
        if (d && sz) memcpy(b, d, sz < want ? sz : want);
        d = b; sz = want;
    }
    if (!d || !sz) return 0;
    m->cart_slot[q] = r->slot; m->cart_ss[q] = r->subslot;
    if (msx_insert_cart(m, q, d, sz, (MapperType)r->cart_mapper)) { m->cart_slot[q] = -1; return 0; }
    if (img->data) m->rom[i] = *img;
    return 0;
}

/* 한자/한글 글꼴 롬 (포트 D8h~DBh, 메모리 블록 없음) */
static int rg_kanji(MSX *m, int i, const ProfileRegion *r, const RomImage *img)
{
    (void)r;
    if (img->data && img->size) { m->kanji = img->data; m->kanji_size = img->size; m->rom[i] = *img; }
    {   /* 한글 기종(KeyLayout=korean)의 256KB 폰트 롬은 D9 행 주소를 7비트로 받음 (openMSX MSXKanji "hangul" 형식).
           CPC-400·400S 한글 롬은 한자·기호를 128KB 너머(행 64~127)에서 D8/D9만으로 읽음 */
        const char *kl = m->prof.key_layout;
        m->kanji_hi_mask = (m->kanji_size == 0x40000 && (kl[0] | 0x20) == 'k' && (kl[1] | 0x20) == 'o' && (kl[2] | 0x20) == 'r') ? 0x7F : 0x3F;
    }
    return 0;
}

typedef struct {
    const char *name;                                   /* 진단·로그 이름 */
    int (*setup)(MSX *m, int i, const ProfileRegion *r, const RomImage *img);
} RegionOps;

static const RegionOps region_ops[REGION_TYPES] = {
    [0]                  = { "rom",      rg_rom },      /* 모르는 종류 (예전에도 맨 끝의 일반 롬 처리로 감) */
    [REGION_RAM]         = { "ram",      rg_ram },
    [REGION_ROM_NORMAL]  = { "rom",      rg_rom },
    [REGION_ROM_BASIC]   = { "basic",    rg_rom },
    [REGION_RAM_MAPPER]  = { "mapram",   rg_mapram },
    [REGION_KANJI]       = { "kanji",    rg_kanji },
    [REGION_DISK_ROM]    = { "disk",     rg_disk },
    [REGION_ROM_DOS2]    = { "dos2",     rg_dos2 },
    [REGION_PAC]         = { "pac",      rg_pac },
    [REGION_FW]          = { "fw",       rg_fw },
    [REGION_BUNSETSU]    = { "bunsetsu", rg_bunsetsu },
    [REGION_JISYO]       = { "jisyo",    rg_jisyo },
    [REGION_FORTE]       = { "forte",    rg_forte },
    [REGION_SUNRISE]     = { "sunrise",  rg_sunrise },
    [REGION_SFG]         = { "sfg",      rg_sfg },
    [REGION_CART]        = { "cart",     rg_cart },
};
