/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* status.c - 5.2.37: 상태 아이콘 선언 (status.h 설명). 값은 5.2.36 까지 fe.c read_leds 와 같음 */
#include "status.h"
#include "media.h"
#include "cart.h"
#include "psg.h"
#include "v9990.h"
#include <string.h>

const MsxStatusDecl msx_status_decl[MS_N] = {
    [MS_CAPS]  = { "CAPS",  "CAPS",  0,        "CAPS"  },
    [MS_KANA]  = { "KANA",  "KANA",  "HANGUL", "KANA"  },
    [MS_FDD1]  = { "FDD1",  "FDD",   0,        "FDD1"  },
    [MS_FDD2]  = { "FDD2",  "FDD",   0,        "FDD2"  },
    [MS_CAS]   = { "CAS",   "CAS",   0,        "CAS"   },
    [MS_HD]    = { "HD",    "HD",    0,        "HD"    },
    [MS_SLOT1] = { "SLOT1", "CART",  0,        "SLOT1" },
    [MS_SLOT2] = { "SLOT2", "CART",  0,        "SLOT2" },
    [MS_SLOT3] = { "SLOT3", "CART",  0,        "SLOT3" },
    [MS_SLOT4] = { "SLOT4", "CART",  0,        "SLOT4" },
    [MS_SLOT5] = { "SLOT5", "CART",  0,        "SLOT5" },
    [MS_CPU]   = { "CPU",   "Z80",   "R800",   "CPU"   },
    [MS_V9990] = { "V9990", "V9990", 0,        "V9990" },
    [MS_NET]   = { "NET",   "NET",   0,        "NET"   },     /* 네트워크 카드. 연결됐나(주소를 받았나)는 호스트가 아는 것이라 fe 가 더함 */
};

void msx_status(MSX *m, uint8_t st[MS_N], uint8_t alt[MS_N])
{
    memset(st, 0, MS_N); memset(alt, 0, MS_N);
    st[MS_CAPS] = !(m->ppi_c & 0x40) ? SS_ON : SS_OFF;                  /* PPI C 비트 6 = 0 이면 켜짐 */
    st[MS_KANA] = !(psg_read_reg(&m->psg, 15) & 0x80) ? SS_ON : SS_OFF; /* PSG 포트 B 비트 7 */
    MsxMediaSlots ms; msx_media_slots(m, &ms);
    for (int d = 0; d < 2; d++)                                         /* 드라이브: 없음 / 읽고 쓰는 중 (모터 + 1/6초) */
        st[MS_FDD1 + d] = d >= ms.disk ? SS_NONE : (m->fdc.motor && m->cpu.cycles - m->fdc.last_access[d] < 3579545u / 6) ? SS_BUSY : SS_OFF;
    st[MS_CAS] = !ms.tape ? SS_NONE : !m->tape ? SS_OFF : m->tape_motor ? SS_BUSY : SS_ON;
    {   int ide = 0;                                                    /* 하드 디스크: IDE·SCSI 카트리지가 있어야 */
        for (int n = 0; n < MSX_CART_ALL; n++) if (m->cart[n].present && CART_IS_IDE(m->cart[n].mapper)) ide = 1;
        st[MS_HD] = ide ? SS_OFF : SS_NONE; }
    for (int k = 0; k < 5; k++) st[MS_SLOT1 + k] = m->cart[k].present ? SS_ON : m->cart_slot[k] < 0 ? SS_NONE : SS_OFF;
    st[MS_CPU] = SS_ON; alt[MS_CPU] = m->cpu.r800 != 0;
    st[MS_V9990] = !m->v9 ? SS_NONE : v9990_visible(m->v9) ? SS_ON : SS_OFF;
    st[MS_NET] = m->net.enabled ? SS_ON : SS_NONE;                     /* 네트워크 카트(UNAPI)가 꽂혀 있음 */
}
