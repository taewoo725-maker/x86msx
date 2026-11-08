/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - 8.0.179 슬롯 확장기 시험: 없음 / 슬롯 1 / 슬롯 2 / 둘 다 (카트 8칸 배치) */
#include "../host/msx_host.h"
#include <stdio.h>
#include <string.h>
static int pass, fail;
static void ck(const char *n, long got, long want) { if (got == want) pass++; else { fail++; printf("FAIL %-44s %ld (want %ld)\n", n, got, want); } }
static void expect(const MSX *m, const char *tag, const int *slot, const int *ss)
{
    char nm[96];
    for (int n = 0; n < MSX_CART_SLOTS; n++) {
        snprintf(nm, sizeof nm, "%s cart%d slot", tag, n); ck(nm, m->cart_slot[n], slot[n]);
        if (slot[n] >= 0) { snprintf(nm, sizeof nm, "%s cart%d sub", tag, n); ck(nm, m->cart_ss[n], ss[n]); }
    }
}
int main(int argc, char **argv)
{
    static MSX m; char err[256];
    if (msx_host_init(&m, argc > 1 ? argv[1] : "machines/C-BIOS", err, sizeof err)) { printf("init: %s\n", err); return 1; }
    int a = m.prof.ext_slot[0], b = m.prof.ext_slot[1];
    int sub_a = m.prof.subslotted[a], sub_b = m.prof.subslotted[b];
    ck("MSX_CART_SLOTS = 8", MSX_CART_SLOTS, 8);
    ck("slot A,B differ", a != b, 1);
    ck("A,B not expanded to begin with", sub_a == 0 && sub_b == 0, 1);

    int none_slot[8], none_ss[8];
    ck("expand 0 ok", msx_set_expand(&m, 0), 0);
    for (int n = 0; n < 8; n++) { none_slot[n] = m.cart_slot[n]; none_ss[n] = m.cart_ss[n]; }
    ck("none: cart0 = A, cart1 = B", m.cart_slot[0] == a && m.cart_slot[1] == b, 1);
    ck("none: carts 5..7 absent", m.cart_slot[5] == -1 && m.cart_slot[6] == -1 && m.cart_slot[7] == -1, 1);

    { int s[8] = { a, a, a, a, b, -1, -1, -1 }, u[8] = { 0, 1, 2, 3, 0, 0, 0, 0 };
      ck("expand 1 ok", msx_set_expand(&m, 1), 0); expect(&m, "exp1", s, u);
      ck("exp1: A subslotted, B not", m.prof.subslotted[a] == 1 && m.prof.subslotted[b] == 0, 1); }
    { int s[8] = { a, b, b, b, b, -1, -1, -1 }, u[8] = { 0, 0, 1, 2, 3, 0, 0, 0 };
      ck("expand 2 ok", msx_set_expand(&m, 2), 0); expect(&m, "exp2", s, u);
      ck("exp2: B subslotted, A restored", m.prof.subslotted[b] == 1 && m.prof.subslotted[a] == 0, 1); }
    { int s[8] = { a, a, a, a, b, b, b, b }, u[8] = { 0, 1, 2, 3, 0, 1, 2, 3 };
      ck("expand 3 ok", msx_set_expand(&m, 3), 0); expect(&m, "both", s, u);
      ck("both: A and B subslotted", m.prof.subslotted[a] == 1 && m.prof.subslotted[b] == 1, 1); }
    ck("expand 3 again ok", msx_set_expand(&m, 3), 0);
    ck("expand 0 back", msx_set_expand(&m, 0), 0);
    expect(&m, "back to none", none_slot, none_ss);
    ck("restored: A,B not subslotted", m.prof.subslotted[a] == sub_a && m.prof.subslotted[b] == sub_b, 1);
    ck("expand 4 refused", msx_set_expand(&m, 4), -1);
    ck("expand -1 refused", msx_set_expand(&m, -1), -1);
    msx_set_expand(&m, 3);
    msx_reset(&m); msx_run_frame(&m); msx_run_frame(&m);
    ck("both: machine still runs after reset", m.cpu.cycles > 0, 1);
    printf("expandtest: %d/%d passed\n", pass, pass + fail);
    return fail != 0;
}
