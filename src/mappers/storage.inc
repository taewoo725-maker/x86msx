/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * mappers/storage.inc - 카트리지 매퍼 부품: 저장 장치 카트 (DOS2·Sunrise IDE·가상 디스크·Beer/GIDE/Gouda·네트워크)  (5.2.4 구조 개편 3단계)
 * msx.c 가 한 번역 단위로 끼워 넣음 (msx.c 의 공용 도움 함수·블록을 그대로 씀, 인라인 가능).
 * 매퍼 하나 = cw_(쓰기, 1 = 뱅크 바뀜) + cm_(창 맞추기, 없으면 cm_default) + cr_(리셋, 없으면 cr_default). 표는 mappers/table.inc
 */

static int cw_dos2(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    if ((a & 0xF800) == 0x6000 || a == 0x7FFE) { c->bank[0] = (uint8_t)((v & 3) * 2); c->bank[1] = (uint8_t)((v & 3) * 2 + 1); return 1; }
    return 0;
}

static int cw_sunrise(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    if ((a & 0xBF04) == 0x0104 && a >= 0x4000 && a < 0x8000) {               /* 제어: 비트 0 IDE 켜기, 비트 7~3 = 뱅크(뒤집음) */
        uint8_t r = 0, x = v & 0xF8;
        for (int i = 0; i < 8; i++) if (x & (1 << i)) r |= (uint8_t)(1 << (7 - i));
        uint32_t nb = c->size / 16384 ? c->size / 16384 : 1;
        c->ide_ctl = v;
        c->bank[0] = (uint8_t)((r % nb) * 2); c->bank[1] = (uint8_t)((r % nb) * 2 + 1);
        return 1;
    }
    if ((c->ide_ctl & 1) && a >= 0x7C00 && a < 0x7E00) {                    /* 데이터 포트 (짝수 = 아래, 홀수 = 위 → 쓰기) */
        if (a & 1) ide_write_data(&m->ide, (uint16_t)(c->ide_hi | (v << 8)));
        else c->ide_hi = v;
        return 0;
    }
    if ((c->ide_ctl & 1) && a >= 0x7E00 && a < 0x7F00) {
        int r = a & 0x0F;
        if (r == 0x0E) ide_write_control(&m->ide, v);
        else if (r >= 1 && r <= 7) ide_write_reg(&m->ide, r, v);
        return 0;
    }
    return 0;
}

static int cw_vhd(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    {                                  /* 3.7.1: Nextor CHGBNK = LD (6000h),A */
    if ((a & 0xF800) != 0x6000) return 0;
    uint32_t nb = c->size / 16384 ? c->size / 16384 : 1;
    c->bank[0] = (uint8_t)((v % nb) * 2); c->bank[1] = (uint8_t)((v % nb) * 2 + 1);
    return 1;
}
}

static int cw_beeride(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    return 0;
}

static void cm_beeride(MSX *m, Cartridge *c, int s, int q)
{
    (void)m; (void)c; (void)s; (void)q;
    /* 3.4.1: 롬 16 KiB = 0x4000~0x7FFF */
    m->slot_rd[s][q][2] = cart_block(c, 0);
    m->slot_rd[s][q][3] = (c->mapper == MAPPER_NET && c->banks8 >= 2) ? c->sram : cart_block(c, 1);   /* NET 은 RAM 사본 */
    return;
}

static void cm_gide(MSX *m, Cartridge *c, int s, int q)
{
    (void)m; (void)c; (void)s; (void)q;
    /* 3.4.1: 메모리 없음 */

}

static int cw_net(MSX *m, int n, Cartridge *c, uint16_t a, uint8_t v)
{
    (void)m; (void)n; (void)c; (void)a; (void)v;
    /* 7F00~7FFF 만 RAM (읽기 표가 sram 사본을 가리킴) */
    if (a >= 0x7F00 && a < 0x8000 && c->banks8 >= 2) c->sram[a - 0x6000] = v;
    return 0;
}
