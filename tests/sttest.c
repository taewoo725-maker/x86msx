/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - 5.2.18 장치별 상태 저장 시험 (src/msxstate.c)
 * A 기계를 돌리다 저장 → 다른 B 기계(같은 기종·카트, 다른 시점)에 불러와 같은 프레임만큼 돌려 화면·CPU·RAM 이 같은지.
 * 덩이 하나의 판을 바꾸면 그 장치만 건너뛰는지, CORE 를 바꾸면 거부하는지, 모르는 덩이는 건너뛰는지
 * CORE 필드 표(COR2) — 필드가 빠진 옛 파일·크기가 바뀐 필드·없어진 필드·5.2.86 꼴 CORE(통째) 파일 */
#include "msx_host.h"
#include "../src/msxstate.h"
#include "../src/vsrc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint64_t hash_m(const MSX *m)
{
    uint64_t h = 1469598103934665603ull;
#define HB(p, n) do { const uint8_t *b_ = (const uint8_t *)(p); for (size_t i_ = 0; i_ < (size_t)(n); i_++) { h ^= b_[i_]; h *= 1099511628211ull; } } while (0)
    HB(m->vdp.vram, sizeof m->vdp.vram); HB(m->vdp.reg, sizeof m->vdp.reg);
    HB(&m->cpu.pc, sizeof m->cpu.pc); HB(&m->cpu.sp, sizeof m->cpu.sp); HB(&m->cpu.af, sizeof m->cpu.af); HB(&m->cpu.hl, sizeof m->cpu.hl);
    HB(m->mram, m->mram_size); HB(&m->primary, 1); HB(m->subslot, 4);
    HB(m->cart[0].bank, sizeof m->cart[0].bank);
    return h;
}

static uint8_t rom[1u << 20], buf[12u << 20], buf2[12u << 20];
static MSX a, b;

static uint32_t rs_; static uint8_t rnd8(void) { rs_ ^= rs_ << 13; rs_ ^= rs_ >> 17; rs_ ^= rs_ << 5; return (uint8_t)rs_; }
/* 불러올 기계를 일부러 어지럽힘 — 덮지 못한 곳이 있으면 결과가 달라지게 (MSX 구조체 전체에서 포인터가 아닌 칸을 흔듦) */
static void scramble(MSX *m)
{
    rs_ = 99991;
    for (uint32_t i = 0; i < m->mram_size; i++) m->mram[i] = rnd8();
    for (size_t i = 0; i < sizeof m->vdp.vram; i++) m->vdp.vram[i] = rnd8();
    for (size_t i = 0; i < sizeof m->vdp.reg; i++) m->vdp.reg[i] = rnd8();
    m->cpu.pc = 0x1234; m->cpu.sp.w = 0x4321; m->cpu.af.w = 0x5555; m->cpu.hl.w = 0xAAAA;
    m->primary = 0x5A; for (int k = 0; k < 4; k++) m->subslot[k] = rnd8();
    m->irq_lines = 0xFFFFFFFFu;
    for (size_t i = 0; i < sizeof m->cart[0].bank; i++) m->cart[0].bank[i] = 0;
    for (size_t i = 0; i < sizeof m->psg.reg; i++) m->psg.reg[i] = rnd8();
}

/* 저장 파일의 COR2 덩이를 고쳐 out 에 다시 씀. mode 0 = 이름 skip 인 기록 빼기, 1 = 그 기록 크기를 4 늘림, 2 = 모르는 기록 하나 더하기,
 * 3 = COR2 대신 v1(5.2.86 꼴 CORE 덩이, 미리 만든 것)을 넣기 */
static uint8_t c2tmp[12u << 20];
static uint32_t rewrite_core(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t max, int mode, const char *skip, const uint8_t *v1, uint32_t v1len)
{
    uint32_t pos = 0, k = 0;
    while (pos + 12 <= n) {
        uint32_t vv, ll; memcpy(&vv, in + pos + 4, 4); memcpy(&ll, in + pos + 8, 4);
        const uint8_t *d = in + pos + 12;
        if (!memcmp(in + pos, "COR2", 4)) {
            if (mode == 3) { memcpy(out + k, v1, v1len); k += v1len; }
            else {
                uint32_t at = 0, c = 0;
                while (at < ll) {
                    uint32_t nl = d[at], sz; memcpy(&sz, d + at + 1 + nl, 4);
                    int hit = nl == strlen(skip) && !memcmp(d + at + 1, skip, nl);
                    if (!(hit && mode == 0)) {
                        uint32_t nsz = hit && mode == 1 ? sz + 4 : sz;
                        memcpy(c2tmp + c, d + at, 1 + nl); c += 1 + nl; memcpy(c2tmp + c, &nsz, 4); c += 4;
                        memcpy(c2tmp + c, d + at + 1 + nl + 4, sz); if (nsz > sz) memset(c2tmp + c + sz, 0x77, nsz - sz); c += nsz;
                    }
                    at += 1 + nl + 4 + sz;
                }
                if (mode == 2) { uint32_t z = 3; c2tmp[c++] = 7; memcpy(c2tmp + c, "zz_gone", 7); c += 7; memcpy(c2tmp + c, &z, 4); c += 4; memcpy(c2tmp + c, "abc", 3); c += 3; }
                k += st_put(out + k, max - k, "COR2", vv, c2tmp, c);
            }
        } else k += st_put(out + k, max - k, (const char *)(in + pos), vv, d, ll);
        pos += 12 + ll + ((4u - (ll & 3u)) & 3u);
    }
    return k;
}

static int setup(MSX *m, const char *prof, uint32_t rs)
{
    char err[256];
    if (msx_host_init(m, prof, err, sizeof err)) { printf("init %s: %s\n", prof, err); return -1; }
    if (rs && msx_insert_cart(m, 0, rom, rs, cart_guess_mapper(rom, rs))) { printf("cart insert\n"); return -1; }
    return 0;
}

static void run_case(const char *name, const char *prof, const char *cart)
{
    uint32_t rs = 0;
    if (cart) { FILE *f = fopen(cart, "rb"); if (!f) { printf("SKIP %s (no %s)\n", name, cart); return; } rs = (uint32_t)fread(rom, 1, sizeof rom, f); fclose(f); }
    if (setup(&a, prof, rs)) { fails++; return; }
    for (int f = 0; f < 300; f++) msx_run_frame(&a);
    uint32_t n = msx_st_save(&a, buf, sizeof buf);
    CHECK(n > 0, "%s: save", name);
    static uint8_t v1[6u << 20]; uint32_t v1len = msx_st_core_v1(&a, v1, sizeof v1);   /* 같은 순간의 5.2.86 꼴 CORE */
    for (int f = 0; f < 200; f++) msx_run_frame(&a);
    uint64_t ha = hash_m(&a);

    /* 같은 기종을 다른 시점에서 불러오기 */
    if (setup(&b, prof, rs)) { fails++; return; }
    for (int f = 0; f < 77; f++) msx_run_frame(&b);
    scramble(&b);
    char sk[96];
    CHECK(msx_st_load(&b, buf, n, sk, sizeof sk) == 0 && !sk[0], "%s: load (skipped '%s')", name, sk);
    for (int f = 0; f < 200; f++) msx_run_frame(&b);
    CHECK(hash_m(&b) == ha, "%s: same after load", name);

    /* 모르는 덩이를 앞에 붙여도 */
    uint32_t k = st_put(buf2, sizeof buf2, "ZZZZ", 7, "hello", 5);
    memcpy(buf2 + k, buf, n);
    if (setup(&b, prof, rs)) { fails++; return; }
    scramble(&b);
    CHECK(msx_st_load(&b, buf2, k + n, sk, sizeof sk) == 0 && !sk[0], "%s: unknown chunk", name);
    for (int f = 0; f < 200; f++) msx_run_frame(&b);
    CHECK(hash_m(&b) == ha, "%s: same with unknown chunk", name);

    /* 장치 하나(PSG0) 판이 다르면 그것만 건너뜀 */
    memcpy(buf2, buf, n);
    uint32_t v, s; uint8_t *p = (uint8_t *)st_find(buf2, n, "PSG0", &v, &s);
    CHECK(p != 0, "%s: PSG0 chunk", name);
    if (p) { v++; memcpy(p - 8, &v, 4); }
    if (setup(&b, prof, rs)) { fails++; return; }
    CHECK(msx_st_load(&b, buf2, n, sk, sizeof sk) == 0 && !strcmp(sk, "PSG0"), "%s: PSG0 skipped ('%s')", name, sk);

    /* VDP 판 1 (팔레트가 밖에 있던 5.2.39 까지) 덩이 — 앞부분만 넣고 판을 1 로: 건너뛰지 않고 읽혀야 하고 팔레트는 기본값 */
    {
        uint32_t k2 = 0; uint32_t pos = 0;
        while (pos + 12 <= n) {                         /* 덩이를 하나씩 옮기며 VDP 만 판 1·짧은 길이로 */
            uint32_t vv, ll; memcpy(&vv, buf + pos + 4, 4); memcpy(&ll, buf + pos + 8, 4);
            if (!memcmp(buf + pos, "VDP ", 4)) k2 += st_put(buf2 + k2, sizeof buf2 - k2, "VDP ", 1, buf + pos + 12, (uint32_t)VDP_ST_V1_SIZE);
            else k2 += st_put(buf2 + k2, sizeof buf2 - k2, (const char *)(buf + pos), vv, buf + pos + 12, ll);
            pos += 12 + ll + ((4u - (ll & 3u)) & 3u);
        }
        if (setup(&b, prof, rs)) { fails++; return; }
        scramble(&b);
        CHECK(msx_st_load(&b, buf2, k2, sk, sizeof sk) == 0 && !sk[0], "%s: VDP v1 chunk loads (skipped '%s')", name, sk);
        CHECK(b.vdp.pal_nsnap == 1 && (b.vdp.palette[15][0] | b.vdp.palette[15][1] | b.vdp.palette[15][2]) != 0, "%s: VDP v1 default palette", name);
    }

    /* COR2 에서 필드 하나가 빠진 파일(그 필드가 나중에 생긴 것처럼) — 읽히고, 그 필드는 켠 상태 그대로 */
    {
        uint32_t k2 = rewrite_core(buf, n, buf2, sizeof buf2, 0, "sched_test_hits", 0, 0);
        if (setup(&b, prof, rs)) { fails++; return; }
        scramble(&b); b.sched_test_hits = 4242;
        CHECK(msx_st_load(&b, buf2, k2, sk, sizeof sk) == 0 && !sk[0] && b.sched_test_hits == 4242, "%s: missing CORE field keeps power-on value (skipped '%s')", name, sk);
        for (int f = 0; f < 200; f++) msx_run_frame(&b);
        CHECK(hash_m(&b) == ha, "%s: same with missing CORE field", name);
    }
    /* 크기가 바뀐 필드는 그것만 건너뜀 ("CORE.stereo"), 나머지는 읽힘 */
    {
        uint32_t k2 = rewrite_core(buf, n, buf2, sizeof buf2, 1, "stereo", 0, 0);
        if (setup(&b, prof, rs)) { fails++; return; }
        scramble(&b); b.stereo = 5;
        CHECK(msx_st_load(&b, buf2, k2, sk, sizeof sk) == 0 && !strcmp(sk, "CORE.stereo") && b.stereo == 5, "%s: resized CORE field skipped ('%s')", name, sk);
        for (int f = 0; f < 200; f++) msx_run_frame(&b);
        CHECK(hash_m(&b) == ha, "%s: same with resized CORE field", name);
    }
    /* 표에 없는 이름(없어진 필드)은 버림 */
    {
        uint32_t k2 = rewrite_core(buf, n, buf2, sizeof buf2, 2, "", 0, 0);
        if (setup(&b, prof, rs)) { fails++; return; }
        scramble(&b);
        CHECK(msx_st_load(&b, buf2, k2, sk, sizeof sk) == 0 && !sk[0], "%s: unknown CORE field ignored ('%s')", name, sk);
        for (int f = 0; f < 200; f++) msx_run_frame(&b);
        CHECK(hash_m(&b) == ha, "%s: same with unknown CORE field", name);
    }
    /* 5.2.86 까지 꼴(CORE 통째) 파일도 읽힘 */
    if (v1len) {
        uint32_t k2 = rewrite_core(buf, n, buf2, sizeof buf2, 3, "", v1, v1len);
        if (setup(&b, prof, rs)) { fails++; return; }
        scramble(&b);
        CHECK(msx_st_check(buf2, k2) == 0 && msx_st_load(&b, buf2, k2, sk, sizeof sk) == 0 && !sk[0], "%s: old CORE v1 file loads ('%s')", name, sk);
        for (int f = 0; f < 200; f++) msx_run_frame(&b);
        CHECK(hash_m(&b) == ha, "%s: same with old CORE v1 file", name);
    } else CHECK(0, "%s: v1 core build", name);

    /* CORE(COR2) 판이 다르면 거부 (기계는 그대로) */
    memcpy(buf2, buf, n);
    p = (uint8_t *)st_find(buf2, n, "COR2", &v, &s);
    CHECK(p != 0, "%s: COR2 chunk", name);
    if (p) { v++; memcpy(p - 8, &v, 4); }
    if (setup(&b, prof, rs)) { fails++; return; }
    uint64_t hb0 = hash_m(&b);
    CHECK(msx_st_check(buf2, n) != 0 && msx_st_load(&b, buf2, n, sk, sizeof sk) == -1 && hash_m(&b) == hb0, "%s: CORE mismatch refused", name);

    /* 잘린 파일 */
    CHECK(msx_st_check(buf, 100) != 0, "%s: truncated refused", name);
    printf("%s: %u bytes, ok\n", name, n);
}

int main(void)
{
    {   /* CORE 필드 표가 MSX 구조체를 빠짐없이 덮는지 — 장치 덩이·매퍼 RAM·작업 메모리(scr)·필드가 겹치지 않고, 덮지 않은 곳은 맞춤 빈칸(8바이트 미만)뿐 */
        size_t N = sizeof(MSX); uint8_t *own = calloc(N, 1); int bad_ov = 0, gaps = 0; size_t big = 0, big_at = 0;
        #define MARK(o, z, tag) do { for (size_t q_ = (o); q_ < (size_t)(o) + (z); q_++) { if (own[q_]) bad_ov++; own[q_] = (tag); } } while (0)
        for (int i = 0; i < msx_st_ndevs; i++) MARK(msx_st_devs[i].off, msx_st_devs[i].size, 1);
        MARK(offsetof(MSX, mram), sizeof(((MSX *)0)->mram), 2);
        MARK(offsetof(MSX, scr), sizeof(((MSX *)0)->scr), 3);
        for (int i = 0; i < msx_st_ncore; i++) {
            MARK(msx_st_core[i].off, msx_st_core[i].size, 4);
            CHECK(strlen(msx_st_core[i].name) > 0 && strlen(msx_st_core[i].name) < 256, "core name %d", i);
            for (int j = i + 1; j < msx_st_ncore; j++) CHECK(strcmp(msx_st_core[i].name, msx_st_core[j].name), "dup core field %s", msx_st_core[i].name);
        }
        for (size_t q = 0; q < N; ) { if (own[q]) { q++; continue; } size_t e = q; while (e < N && !own[e]) e++; gaps++; if (e - q > big) { big = e - q; big_at = q; } q = e; }
        CHECK(!bad_ov, "core table overlaps (%d bytes)", bad_ov);
        CHECK(big < 8, "MSX bytes not in CORE table: %zu at offset %zu (add the field to msx_st_core)", big, big_at);
        printf("core table: %d fields, %d padding gaps\n", msx_st_ncore, gaps);
        free(own);
    }
    /* 덩이 표: 겹침 없음 */
    for (int i = 0; i < msx_st_ndevs; i++)
        for (int j = i + 1; j < msx_st_ndevs; j++) {
            const MsxStDev *x = &msx_st_devs[i], *y = &msx_st_devs[j];
            CHECK(x->off + x->size <= y->off || y->off + y->size <= x->off, "overlap %.4s %.4s", x->tag, y->tag);
            CHECK(memcmp(x->tag, y->tag, 4), "dup tag %.4s", x->tag);
        }
    run_case("cb2p_boot", "tests/machines/cbios2p", 0);
    run_case("cb2p_konscc", "tests/machines/cbios2p", "tests/carts/konamiscc.rom");
    run_case("cb1_konami", "machines/C-BIOS", "tests/carts/konami.rom");
    run_case("cb2_dos2fdc", "tests/machines/cbios2_dos2fdc", 0);
    {   /* 5.2.19 화면 소스 고르기 (src/vsrc.c) */
        static V9990 v9;
        if (!setup(&a, "tests/machines/cbios2p", 0)) {
            CHECK(msx_vsrc_pick(&a, -1) == VSRC_VDP && msx_vsrc_pick(&a, VSRC_V9990) == VSRC_VDP, "vsrc: no V9990 -> VDP");
            msx_v9990_enable(&a, &v9);
            for (int f = 0; f < 5; f++) msx_run_frame(&a);
            CHECK(msx_vsrc_pick(&a, VSRC_V9990) == VSRC_V9990 && msx_vsrc_pick(&a, VSRC_VDP) == VSRC_VDP, "vsrc: forced");
            CHECK(msx_vsrc_pick(&a, -1) == (v9990_visible(&v9) ? VSRC_V9990 : VSRC_VDP), "vsrc: auto follows V9990 display");
            CHECK(msx_vsrc[VSRC_V9990].aspect43 && !msx_vsrc[VSRC_VDP].aspect43, "vsrc: aspect");
        }
    }
    printf("sttest: %d/%d passed\n", checks - fails, checks);
    return fails != 0;
}
