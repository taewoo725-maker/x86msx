/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - 5.2.37 상태 아이콘 선언 시험 (src/status.c) — 값이 예전 fe.c read_leds 와 같아야 함 */
#include "../src/status.h"
#include "msx_host.h"
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static MSX m;
static uint8_t tape[64];

int main(void)
{
    char err[256]; uint8_t st[MS_N], alt[MS_N];
    for (int i = 0; i < MS_N; i++) CHECK(msx_status_decl[i].key && msx_status_decl[i].icon && msx_status_decl[i].label, "decl %d", i);
    CHECK(!strcmp(msx_status_decl[MS_KANA].icon_alt, "HANGUL") && !strcmp(msx_status_decl[MS_CPU].icon_alt, "R800"), "alt icons");
    if (msx_host_init(&m, "tests/machines/cbios2", err, sizeof err)) { printf("init: %s\n", err); return 1; }
    msx_status(&m, st, alt);
    CHECK(st[MS_FDD1] == SS_NONE && st[MS_FDD2] == SS_NONE, "no fdc -> none");
    CHECK(st[MS_CAS] == SS_OFF, "tape port, no tape");
    CHECK(st[MS_HD] == SS_NONE, "no ide -> none");
    CHECK(st[MS_CPU] == SS_ON && alt[MS_CPU] == 0, "z80");
    CHECK(st[MS_V9990] == SS_NONE, "no v9990");
    CHECK(st[MS_NET] == SS_NONE, "no net card");
    m.net.enabled = 1; msx_status(&m, st, alt); CHECK(st[MS_NET] == SS_ON, "net card on"); m.net.enabled = 0;   /* 5.2.85 */
    CHECK(st[MS_SLOT1] == SS_OFF, "slot1 empty");
    m.ppi_c &= (uint8_t)~0x40; msx_status(&m, st, alt); CHECK(st[MS_CAPS] == SS_ON, "caps on");
    m.ppi_c |= 0x40; msx_status(&m, st, alt); CHECK(st[MS_CAPS] == SS_OFF, "caps off");
    m.tape = tape; m.tape_motor = 0; msx_status(&m, st, alt); CHECK(st[MS_CAS] == SS_ON, "tape in");
    m.tape_motor = 1; msx_status(&m, st, alt); CHECK(st[MS_CAS] == SS_BUSY, "tape motor");
    m.tape = 0; m.tape_motor = 0;
    m.cart[0].present = 1; m.cart[0].mapper = MAPPER_SUNRISE; msx_status(&m, st, alt);
    CHECK(st[MS_SLOT1] == SS_ON && st[MS_HD] == SS_OFF, "ide cart -> hd off, slot on");
    m.cart[0].present = 0;
    m.cpu.r800 = 1; msx_status(&m, st, alt); CHECK(alt[MS_CPU] == 1, "r800 alt"); m.cpu.r800 = 0;
    {   static MSX t;
        if (!msx_host_init(&t, "tests/machines/cbios2_dos2fdc", err, sizeof err)) {
            msx_status(&t, st, alt); CHECK(st[MS_FDD1] == SS_OFF, "fdc drive exists %d", st[MS_FDD1]);
            t.fdc.motor = 1; t.fdc.last_access[0] = t.cpu.cycles; msx_status(&t, st, alt); CHECK(st[MS_FDD1] == SS_BUSY, "fdd busy");
        }
        if (!msx_host_init(&t, "tests/regress/local/machines/a1gt", err, sizeof err)) {   /* 개인 롬이 있을 때만 */
            msx_status(&t, st, alt); CHECK(st[MS_CAS] == SS_NONE, "turbo R no tape");
        }
    }
    {   /* 내장 장치 표(msx_dev)가 기종 깃발과 같은 답을 하는지 — 기종 7개 (개인 롬 기종은 있을 때만) */
        static MSX t;
        static const char *const ms[] = { "tests/machines/cbios2", "tests/machines/cbios2p", "tests/machines/cbios2_dos2fdc", "machines/C-BIOS",
                                          "tests/regress/local/machines/a1gt", "tests/regress/local/machines/a1wsx", "tests/regress/local/machines/cpc400s" };
        int gt = -1;
        for (int i = 0; i < 7; i++) {
            if (msx_host_init(&t, ms[i], err, sizeof err)) continue;
            const MachineProfile *p = &t.prof;
            int pal = 0; for (const char *q = p->vdp; *q; q++) if (q[0] == '9' && q[1] == '9' && q[2] == '2' && q[3] == '9') pal = 1;
            CHECK(msx_dev(&t, MDEV_S1990) == (p->s1990 != 0) && msx_dev(&t, MDEV_TRTIMER) == (p->trtimer != 0) && msx_dev(&t, MDEV_F4) == (p->f4 != 0)
                  && msx_dev(&t, MDEV_F4_INV) == (p->f4 == 2) && msx_dev(&t, MDEV_TRPAUSE) == (p->trpause != 0) && msx_dev(&t, MDEV_PCM) == (p->pcm != 0)
                  && msx_dev(&t, MDEV_MIDI) == (p->midi != 0) && msx_dev(&t, MDEV_MIDI_HW) == (p->midi_hw != 0) && msx_dev(&t, MDEV_MATSU) == (p->matsushita != 0)
                  && msx_dev(&t, MDEV_OPLL) == (p->opll != 0) && msx_dev(&t, MDEV_PAL_VDP) == pal && msx_dev(&t, MDEV_Y8950) == (p->y8950 != 0)
                  && msx_dev(&t, MDEV_MOON) == (p->moonsound != 0) && msx_dev(&t, MDEV_CMOS_BAT) == (p->cmos_battery != 0), "msx_dev = profile flags (%s)", ms[i]);
            if (i == 4) gt = msx_dev(&t, MDEV_S1990) && msx_dev(&t, MDEV_PCM) && msx_dev(&t, MDEV_MIDI_HW) && msx_dev(&t, MDEV_TRPAUSE);
            if (i == 4) { msx_set_midi(&t, 0); CHECK(!msx_dev(&t, MDEV_MIDI) && msx_dev(&t, MDEV_MIDI_HW), "set_midi off keeps hw"); msx_set_midi(&t, 1); CHECK(msx_dev(&t, MDEV_MIDI), "set_midi on"); }
            if (i == 0) CHECK(!msx_dev(&t, MDEV_S1990) && !msx_dev(&t, MDEV_PCM) && !msx_dev(&t, MDEV_MIDI), "cbios2: no turbo R devices");
        }
        if (gt >= 0) CHECK(gt == 1, "a1gt: S1990·PCM·MIDI·pause present");
        CHECK(msx_dev(&t, MDEV_N) == 0 && msx_dev(&t, -1) == 0, "unknown dev = 0");
        if (!msx_host_init(&t, "machines/C-BIOS", err, sizeof err)) {   /* PAL 기종(TMS9929A)은 시험 기종에 없어 이름만 바꿔 봄 */
            t.vsync_force = 0;
            snprintf(t.prof.vdp, sizeof t.prof.vdp, "TMS9929A");
            CHECK(msx_dev(&t, MDEV_PAL_VDP) == 1 && (t.vdp.v9938 || msx_is_pal(&t) == 1), "TMS9929A -> PAL");
            snprintf(t.prof.vdp, sizeof t.prof.vdp, "TMS9918A");
            CHECK(msx_dev(&t, MDEV_PAL_VDP) == 0 && (t.vdp.v9938 || msx_is_pal(&t) == 0), "TMS9918A -> NTSC");
        }
    }
    printf("statustest: %d/%d passed\n", checks - fails, checks);
    return fails != 0;
}
