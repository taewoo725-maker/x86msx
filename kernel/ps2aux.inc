/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* ps2aux.inc - 3.0 PS/2 마우스 (8042 보조 포트), 레거시 kmain.c와 UEFI main.c가 함께 쓴다.
 * 포함하는 쪽이 inb/outb/fe_push_key를 준비한다. IRQ12는 쓰지 않고(명령 바이트에서 끔) 키보드 쪽에서 8042를 읽을 때
 * 상태 비트 5(보조 데이터)면 aux_byte()로 넘겨 묶음을 모은다. 이동량은 ps2_mouse_take가 가져갈 때까지 쌓인다. */
static Ps2Mouse ps2m = { .size = 3 };
static int ps2_ok, ps2_step;                            /* ps2_step: 진단용 (어디까지 됐나) */
static volatile int ms_dx, ms_dy, ms_dz, ms_btn;
static volatile uint32_t ms_bytes, ms_packets;              /* 진단용 */

static void aux_byte(uint8_t v)
{
    int dx, dy, dz, b;
    ms_bytes++;
    if (ps2_ok && ps2mouse_byte(&ps2m, v, &dx, &dy, &dz, &b)) { ms_packets++; ms_dx += dx; ms_dy += dy; ms_dz += dz; ms_btn = b; }
}
static void aux_wait_write(void) { for (int i = 0; i < 200000 && (inb(0x64) & 2); i++) (void)inb(0x80); }
static int aux_read(void)
{
    for (int i = 0; i < 400000; i++) {
        uint8_t st = inb(0x64);
        if (st == 0xFF) return -1;
        if (!(st & 1)) { (void)inb(0x80); continue; }
        uint8_t v = inb(0x60);
        if (st & 0x20) return v;
        fe_push_key(v);                                     /* 그사이 들어온 키보드 바이트 */
    }
    return -1;
}
static int aux_cmd(uint8_t v)
{
    aux_wait_write(); outb(0x64, 0xD4);
    aux_wait_write(); outb(0x60, v);
    return aux_read() == 0xFA ? 0 : -1;
}
/* 인터럽트를 막은 채로 부른다. 마우스가 있으면 1 */
static int ps2_mouse_init(void)
{
    ps2_step = 1;
    if (inb(0x64) == 0xFF) return 0;
    aux_wait_write(); outb(0x64, 0xA8);                     /* 보조 장치 켜기 */
    aux_wait_write(); outb(0x64, 0x20);                     /* 명령 바이트 읽기 */
    int cb = -1;
    for (int i = 0; i < 400000 && cb < 0; i++) { uint8_t st = inb(0x64); if (st & 1) { uint8_t v = inb(0x60); if (!(st & 0x20)) cb = v; } }
    if (cb >= 0) {
        ps2_step = 2;
        cb = (cb & ~0x22) | 0x01;                           /* 보조 클럭 켜기, 보조 IRQ12 끔, 키보드 IRQ1 켬 */
        aux_wait_write(); outb(0x64, 0x60);
        aux_wait_write(); outb(0x60, (uint8_t)cb);
    }
    int ok = cb >= 0 && !aux_cmd(0xF6);                     /* 기본값 */
    if (ok) {
        ps2_step = 3;
        aux_cmd(0xF3); aux_cmd(200); aux_cmd(0xF3); aux_cmd(100); aux_cmd(0xF3); aux_cmd(80);   /* 휠 켜기 (IntelliMouse) */
        int id = aux_cmd(0xF2) ? -1 : aux_read();
        ps2m.size = (id == 3 || id == 4) ? 4 : 3;
        ps2m.n = 0;
        ok = !aux_cmd(0xF4);                                /* 보내기 시작 */
        if (ok) ps2_step = 4;
    }
    if (!ok) { aux_wait_write(); outb(0x64, 0xA7); }        /* 마우스 없음: 보조 장치 끔 */
    ps2_ok = ok;
    return ok;
}
static int ps2_mouse_present(void);
/* 쌓인 이동량을 가져감 (인터럽트를 막은 채로) */
static int ps2_off;                                         /* 3.8.2: 1 = 사용 안 함 */
static int ps2_mouse_take(int *dx, int *dy, int *dz, int *buttons, char *name, int max)
{
    *dx = ms_dx; *dy = ms_dy; *dz = ms_dz; *buttons = ms_btn;
    ms_dx = ms_dy = ms_dz = 0;
    if (ps2_off) { *dx = *dy = *dz = *buttons = 0; if (name && max > 0) name[0] = 0; return 0; }
    if (name && max > 0) {
        const char *t = !ps2_ok ? "" : ps2m.size == 4 ? "PS/2 mouse (wheel)" : "PS/2 mouse";
        int n = 0;
        while (t[n] && n < max - 1) { name[n] = t[n]; n++; }
        name[n] = 0;
    }
    return ps2_ok;
}
static int ps2_mouse_present(void) { return ps2_ok; }
