/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - 5.2.126 화면 소스 고르기 시험 (src/vsrc.c): V9990 이 화면을 내보내면 자동으로 V9990, 아니면 VDP. 표 칸 순서를 잘못 고쳐 V9990 화면이 안 나오던 일(5.2.125)을 막음 */
#include "../src/vsrc.h"
#include "msx_host.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void)
{
    MSX *m = calloc(1, sizeof *m);
    V9990 *v = calloc(1, sizeof *v);
    CHECK(msx_vsrc_pick(m, -1) == VSRC_VDP, "V9990 없음: 자동 = VDP");
    m->v9 = v;
    CHECK(msx_vsrc_pick(m, -1) == VSRC_VDP, "V9990 꽂았지만 아직 화면 안 냄: 자동 = VDP");
    v->shown = 1;
    CHECK(msx_vsrc_pick(m, -1) == VSRC_V9990, "V9990 이 화면을 내보냄: 자동 = V9990");
    CHECK(msx_vsrc_pick(m, VSRC_VDP) == VSRC_VDP, "VDP 고정");
    CHECK(msx_vsrc_pick(m, VSRC_V9990) == VSRC_V9990, "V9990 고정");
    v->sysreset = 1;
    CHECK(msx_vsrc_pick(m, -1) == VSRC_VDP, "V9990 시스템 리셋 중: 자동 = VDP");
    /* PAL 시간표(R#7 비트 3) V9990 + MSX 262줄 프레임 — V9990 은 자기 프레임(313줄)으로 세어 아래 줄까지 그려짐 (옛 화면 하얀 띠가 남던 일) */
    v->sysreset = 0;
    v9990_reset(v);
    v->regs[6] = 0x96; v->regs[7] = 0x0E; v->regs[13] = 0x40; v->regs[15] = 0;
    v->status = 0; v->mode = 5; v->pal_t = 1; v->disp_en = 1;
    memset(v->fb, 0xFF, sizeof v->fb);
    CHECK(v9990_frame_lines(v) == 313, "PAL 시간표 = 313줄");
    for (int l = 0; l < 313; l++) { v9990_line_start(v, l, 313); v9990_line_end(v, l); }
    CHECK(v->fb[239][5] == 0 && v->fb[235][600] == 0, "PAL 시간표 V9990: 맨 아래 줄까지 그려짐 (옛 화면 안 남음) %06X", v->fb[239][5]);
    printf(fails ? "vsrc: FAIL\n" : "vsrc: OK\n");
    return fails != 0;
}
