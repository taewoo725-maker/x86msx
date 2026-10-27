/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * kmain.c - 레거시 BIOS(CSM) 플랫폼: 32비트 보호 모드 본체
 *
 * 로더 정보: 0x0500 부팅 정보(VESA 모드, 메모리 끝), 0x0600 적재 목록(본체). 고정 메모리 배치는 memmap.h
 * 이 파일은 인터럽트(IDT/PIC), PIT 타이머, 8042 키보드, VESA 팔레트, PAT 쓰기 결합, HD Audio 연결만 맡고
 * 화면 그리기·메뉴·키 처리는 공통 프런트엔드(fe.c)가 한다.
 */
#include "version.h"
#include "pcdb.h"
#include "net/netstack.h"
#include "../src/msx.h"
#include "bootinfo.h"
#include "fe.h"
#include "bios.h"
#include "fat.h"
#include "machine.h"
#include "usbgrow.h"
#include "hda.h"
#include "smbios.h"
#include "amddet.h"
#include "acpi.h"
#include "memmap.h"
#include "hidmouse.h"
#include <string.h>

/* entry.asm에서 부르는 함수 */
void irq0_handler(void);
void irq1_handler(void);
void exc_handler(uint32_t n);
void kmain(void);


static inline void outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(p)); }
static inline uint8_t inb(uint16_t p) { uint8_t v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void io_wait(void) { outb(0x80, 0); }

static uint8_t pal_sh[256][3]; static int pal_i, pal_c;      /* 4.0.107: 팔레트 그림자 (내장 패널을 파이프 B로 같이 켤 때 파이프 B의 LUT에 옮김) */
static void dac_start(int n) { outb(0x3C8, (uint8_t)n); pal_i = n; pal_c = 0; }
static void dac_w(uint8_t v) { outb(0x3C9, v); if (pal_i < 256) pal_sh[pal_i][pal_c] = v; if (++pal_c == 3) { pal_c = 0; pal_i++; } }
static void ivb_lut_sync(void);
static int cube_done;
static void cube_reset(void) { cube_done = 0; }
static void set_palette_dac(void)
{
    const uint8_t *pal = fe_palette_rgb();
    dac_start(0);
    for (int i = 0; i < 16; i++) {
        dac_w(pal[i * 3] >> 2);
        dac_w(pal[i * 3 + 1] >> 2);
        dac_w(pal[i * 3 + 2] >> 2);
    }
    if (cube_done) return;
    cube_done = 1;                                  /* SCREEN 8 근사색 (fe.c의 g7_8과 같은 배치) */
    dac_start(16);
    for (int r = 0; r < 6; r++)
        for (int g = 0; g < 7; g++)
            for (int b = 0; b < 5; b++) {
                dac_w((uint8_t)(r * 63 / 5));
                dac_w((uint8_t)(g * 63 / 6));
                dac_w((uint8_t)(b * 63 / 4));
            }
    /* 240~255: 화면 장식용 고정 색 (fe.c의 ui_rgb와 같음) */
    static const uint8_t ui[16][3] = {
        {0,0,0},{0,0,0},{36,219,36},{109,255,109},{36,36,255},{73,109,255},{182,36,36},{73,219,255},
        {255,36,36},{255,109,109},{219,219,36},{219,219,146},{36,146,36},{219,73,182},{182,182,182},{255,255,255},
    };
    dac_start(240);
    for (int i = 0; i < 16; i++) { dac_w(ui[i][0] >> 2); dac_w(ui[i][1] >> 2); dac_w(ui[i][2] >> 2); }
}
static void set_palette(void) { set_palette_dac(); ivb_lut_sync(); }
/* 색 시험 화면이 DAC 16~239 를 쓰고, 나갈 때 근사색을 되돌림 */
static void plat_dac_set(int first, int n, const uint8_t *rgb8)
{
    static int dirty;
    if (first < 0) { if (dirty) { dirty = 0; cube_reset(); set_palette(); } return; }
    dirty = 1;
    dac_start(first);
    for (int i = 0; i < n; i++) { dac_w(rgb8[i * 3] >> 2); dac_w(rgb8[i * 3 + 1] >> 2); dac_w(rgb8[i * 3 + 2] >> 2); }
    ivb_lut_sync();
}
static void plat_pal_get(uint8_t *rgb) { for (int i = 0; i < 256; i++) for (int c = 0; c < 3; c++) { uint8_t v = pal_sh[i][c] & 63; rgb[i * 3 + c] = (uint8_t)((v << 2) | (v >> 4)); } }   /* 5.0.74 */

/* ------------------------------------------------------------------ */
/* 쓰기 결합(WC): PAT 페이징 또는 MTRR                                   */
/* ------------------------------------------------------------------ */
static inline void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}
static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
static inline void wrmsr(uint32_t msr, uint64_t v)
{
    __asm__ volatile("wrmsr" : : "c"(msr), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)));
}

/* PAT + 페이징: 4 GiB를 4 MiB 페이지로 1:1 매핑하고 그래픽 메모리 페이지만 WC.
 * MTRR이 UC로 막아도 PAT WC가 우선한다 (Intel SDM 11.5.2.2). */
static uint32_t page_dir[1024] __attribute__((aligned(4096)));

static int paging_on;

/* 4 MiB 페이지를 캐시 안 됨(PAT 3 = UC)으로 (페이징이 꺼져 있으면 아무것도 안 함) */
void paging_set_uc(uintptr_t addr)
{
    if (!paging_on || (uint64_t)addr >> 32) return;
    page_dir[addr >> 22] |= 0x18;                                /* PCD | PWT */
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");  /* TLB 비우기 */
    __asm__ volatile("wbinvd" ::: "memory");
}

static int enable_wc_pat(uint32_t base, uint32_t bytes)
{
    uint32_t a, b, c, d;
    cpuid(1, &a, &b, &c, &d);
    if (!(d & (1u << 16)) || !(d & (1u << 3))) return 0;      /* PAT, PSE 필요 */

    /* PAT 항목 1을 WC(0x01)로: PWT=1, PCD=0 페이지가 WC가 됨 */
    uint64_t patv = rdmsr(0x277);
    patv = (patv & ~(0xFFull << 8)) | (0x01ull << 8);

    uint32_t first = base >> 22, last = (base + bytes - 1) >> 22;
    for (uint32_t i = 0; i < 1024; i++) {
        uint32_t e = (i << 22) | 0x83;                           /* 존재, 쓰기, 4 MiB */
        if (i >= first && i <= last) e |= 0x08;                  /* PWT → PAT 항목 1 (WC) */
        page_dir[i] = e;
    }

    __asm__ volatile("cli");
    __asm__ volatile("wbinvd" ::: "memory");
    wrmsr(0x277, patv);
    uint32_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4 | 0x10));       /* PSE */
    __asm__ volatile("mov %0, %%cr3" : : "r"((uint32_t)page_dir) : "memory");
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0 | 0x80000000u) : "memory");   /* 페이징 켜기 */
    __asm__ volatile("wbinvd" ::: "memory");
    paging_on = 1;
    return 1;
}

static const char *enable_wc(uint32_t base, uint32_t bytes)
{
    uint32_t a, b, c, d;
    cpuid(0, &a, &b, &c, &d);
    if (a < 1) return "WC: no cpuid";
    cpuid(1, &a, &b, &c, &d);
    if (!(d & (1u << 12))) return "WC: no MTRR";
    uint64_t cap = rdmsr(0xFE);
    uint32_t vcnt = (uint32_t)cap & 0xFF;
    if (!(cap & (1u << 10))) return "WC: unsupported";
    uint64_t deftype = rdmsr(0x2FF);
    if (!(deftype & 0x800)) return "WC: MTRR off";

    uint32_t phys_bits = 36;
    cpuid(0x80000000, &a, &b, &c, &d);
    if (a >= 0x80000008) { cpuid(0x80000008, &a, &b, &c, &d); phys_bits = a & 0xFF; }
    if (phys_bits < 32 || phys_bits > 52) phys_bits = 36;

    uint32_t size = 0x1000;
    while (size < bytes) size <<= 1;
    if (base & (size - 1)) return "WC: misaligned";

    int slot = -1;
    for (uint32_t i = 0; i < vcnt; i++) {
        uint64_t mask = rdmsr(0x201 + 2 * i);
        uint64_t mbase = rdmsr(0x200 + 2 * i);
        if (!(mask & 0x800)) { if (slot < 0) slot = (int)i; continue; }
        /* 이미 있는 범위와 겹치는지 (UC가 겹치면 WC가 적용되지 않음) */
        uint32_t m = (uint32_t)mask & 0xFFFFF000u, mb = (uint32_t)mbase & 0xFFFFF000u;
        if ((base & m) == (mb & m) || (mb & ~(size - 1)) == base) {
            if ((mbase & 0xFF) == 1) return "WC: on (BIOS)";
            if ((mbase & 0xFF) == 0) return "WC: blocked UC";
        }
    }
    if (slot < 0) return "WC: no slot";

    uint64_t phys_mask = (phys_bits >= 64) ? ~0ull : ((1ull << phys_bits) - 1);
    uint64_t base_v = (uint64_t)(base & 0xFFFFF000u) | 1;          /* 형식 1 = WC */
    uint64_t mask_v = ((~(uint64_t)(size - 1)) & phys_mask & ~0xFFFull) | 0x800;

    __asm__ volatile("cli");
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %0, %%cr0" : : "r"((cr0 | 0x40000000u) & ~0x20000000u));  /* CD=1, NW=0 */
    __asm__ volatile("wbinvd" ::: "memory");
    wrmsr(0x2FF, deftype & ~0x800ull);
    wrmsr(0x200 + 2 * (uint32_t)slot, base_v);
    wrmsr(0x201 + 2 * (uint32_t)slot, mask_v);
    __asm__ volatile("wbinvd" ::: "memory");
    wrmsr(0x2FF, deftype);
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
    return "WC: on";
}

/* ------------------------------------------------------------------ */
/* 인터럽트                                                             */
/* ------------------------------------------------------------------ */
typedef struct __attribute__((packed)) {
    uint16_t off_lo, sel;
    uint8_t zero, flags;
    uint16_t off_hi;
} IdtEntry;

static IdtEntry idt[256];
extern void isr_irq0(void), isr_irq1(void), isr_spurious(void);
extern const uint32_t isr_exc_table[32];

static void idt_set(int n, uint32_t handler)
{
    idt[n].off_lo = (uint16_t)handler;
    idt[n].sel = 0x08;
    idt[n].zero = 0;
    idt[n].flags = 0x8E;                /* 존재, 링 0, 32비트 인터럽트 게이트 */
    idt[n].off_hi = (uint16_t)(handler >> 16);
}

static void idt_init(void)
{
    for (int i = 0; i < 32; i++) idt_set(i, isr_exc_table[i]);
    for (int i = 0x20; i < 0x30; i++) idt_set(i, (uint32_t)isr_spurious);
    idt_set(0x20, (uint32_t)isr_irq0);
    idt_set(0x21, (uint32_t)isr_irq1);
    struct __attribute__((packed)) { uint16_t limit; uint32_t base; } d = { sizeof idt - 1, (uint32_t)idt };
    __asm__ volatile("lidt %0" : : "m"(d));
}

static void pic_init(void)
{
    outb(0x20, 0x11); io_wait(); outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20); io_wait(); outb(0xA1, 0x28); io_wait();   /* IRQ0-7 → 0x20, IRQ8-15 → 0x28 */
    outb(0x21, 0x04); io_wait(); outb(0xA1, 0x02); io_wait();
    outb(0x21, 0x01); io_wait(); outb(0xA1, 0x01); io_wait();
    outb(0x21, 0xFC);                   /* IRQ0(타이머), IRQ1(키보드)만 */
    outb(0xA1, 0xFF);
}

#define PIT_DIV 1193u                   /* 약 1000.15 Hz */
#define FRAME_PIT 19912u                /* 59,736 T x (1,193,181.67 / 3,579,545) */

static volatile uint32_t ticks;

void delay_ms(uint32_t ms)
{
    uint32_t t0 = ticks;
    while (ticks - t0 < ms + 1) __asm__ volatile("hlt");
}

static uint32_t plat_ms(void) { return ticks; }

static void pit_init(void)
{
    outb(0x43, 0x34);
    outb(0x40, PIT_DIV & 0xFF);
    outb(0x40, PIT_DIV >> 8);
}

void irq0_handler(void)
{
    ticks++;
    outb(0x20, 0x20);
}

#include "ps2aux.inc"

void irq1_handler(void)
{
    uint8_t st = inb(0x64);
    if (st & 1) {
        uint8_t v = inb(0x60);
        if (!(st & 0x20)) fe_push_key(v);
        else aux_byte(v);                   /* 보조 장치(마우스) 데이터 */
    }
    outb(0x20, 0x20);
}

/* 8042에 남은 바이트를 직접 읽는다. BIOS 호출(썽크) 뒤 PIC를 다시 초기화하면 대기 중이던 IRQ1이
 * 지워지고, 8042는 바이트를 읽어 갈 때까지 새 인터럽트를 보내지 않아 키보드가 멈출 수 있기 때문. */
static void kbc_drain(void)
{
    for (int i = 0; i < 64; i++) {
        __asm__ volatile("cli");
        uint8_t st = inb(0x64);
        if (st == 0xFF || !(st & 1)) { __asm__ volatile("sti"); break; }
        uint8_t v = inb(0x60);
        if (st & 0x20) aux_byte(v);         /* 3.0: 마우스 묶음 */
        else fe_push_key(v);   /* 2.10.1: 큐에 넣는 동안에도 인터럽트를 막음 — IRQ1이 끼어들면 같은 자리에
                                             * 두 바이트를 써서 순서가 바뀌거나 잃는다 (E0을 잃으면 키가 눌린 채로 남음) */
        __asm__ volatile("sti");
    }
}

void exc_handler(uint32_t n)
{
    static char msg[] = "CPU exception 00";
    msg[14] = (char)('0' + n / 10);
    msg[15] = (char)('0' + n % 10);
    fe_fatal(msg, 0);
}

static void kbc_wait_write(void)
{
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++) io_wait();
}

static void kbd_init(void)
{
    kbc_wait_write();
    outb(0x64, 0xA7);                   /* 보조 장치(마우스) 끄기 */
    for (int i = 0; i < 64 && (inb(0x64) & 1); i++) { (void)inb(0x60); io_wait(); }
}

static int plat_mouse_read(int *dx, int *dy, int *dz, int *buttons, char *name, int max)
{
    __asm__ volatile("cli");
    int r = ps2_mouse_take(dx, dy, dz, buttons, name, max);
    __asm__ volatile("sti");
    return r;
}

static int plat_mouse_dev(int n, char *name, int max, int *on)
{
    if (n || !ps2_mouse_present()) return 0;
    const char *t = "PS/2 mouse / touchpad"; int k = 0;
    while (t[k] && k < max - 1) { name[k] = t[k]; k++; }
    name[k] = 0; *on = !ps2_off; return 1;
}
static void plat_mouse_set(int n, int on) { (void)n; ps2_off = !on; }

static void netlog_write(void);
static void reboot(void)
{
    net_halt(); netlog_write();                                            /* 5.0.110 랜카드 DMA 멈춤 (5.0.126: 카드를 뺀 뒤에도) */
    hda_silence();                                  /* 3.8.1 */
    __asm__ volatile("cli");
    /* 4.0.92: 재시작이 안 되는 PC(Acer Aspire V5-171 등)를 위해 여러 방법을 차례로: 키보드 컨트롤러 → 리셋 컨트롤 레지스터(CF9) 소프트/하드/전체 → 마지막으로 3중 폴트 */
    kbc_wait_write();
    outb(0x64, 0xFE);
    for (int i = 0; i < 20000; i++) io_wait();
    outb(0xCF9, 0x02); for (int i = 0; i < 1000; i++) io_wait();
    outb(0xCF9, 0x06); for (int i = 0; i < 20000; i++) io_wait();
    outb(0xCF9, 0x0A); for (int i = 0; i < 1000; i++) io_wait();
    outb(0xCF9, 0x0E); for (int i = 0; i < 20000; i++) io_wait();
    {   /* 빈 IDT로 예외를 일으켜 3중 폴트 (CPU가 리셋됨) */
        struct { uint16_t lim; uint32_t base; } __attribute__((packed)) nidt = { 0, 0 };
        __asm__ volatile("lidt %0\n\tint3" : : "m"(nidt));
    }
    for (;;) __asm__ volatile("hlt");
}

/* ------------------------------------------------------------------ */
/* MSX                                                                  */
/* ------------------------------------------------------------------ */
static MSX m;

/* ------------------------------------------------------------------ */
/* 드라이브와 파일: BIOS INT 13h(실모드 썽크) + FAT16/32 (설계서 §3.3)   */
/* ------------------------------------------------------------------ */
#define MAX_VOLS 16
typedef struct { int drive; } DiskCtx;
static DiskCtx disks[8];
static FatVol vols[MAX_VOLS];
static int nvols, boot_drive;

static int disk_read(void *ctx, uint64_t lba, uint32_t count, uint8_t *buf)
{
    const DiskCtx *d = ctx;
    if (bios_read(d->drive, lba, (int)count)) return -1;
    memcpy(buf, (const void *)BIOS_BOUNCE, count * 512);
    return 0;
}

static int disk_write(void *ctx, uint64_t lba, uint32_t count, const uint8_t *buf)
{
    const DiskCtx *d = ctx;
    memcpy((void *)BIOS_BOUNCE, buf, count * 512);
    return bios_write(d->drive, lba, (int)count);
}

/* PC의 CMOS 시계 (파일 날짜, 2.10) */
static uint8_t cmos_rd(uint8_t r) { outb(0x70, r); io_wait(); return inb(0x71); }
static int cmos_time(int t[6])                         /* 2.14: 년 월 일 시 분 초 */
{
    for (int i = 0; i < 1000 && (cmos_rd(0x0A) & 0x80); i++) io_wait();     /* 갱신 중이면 기다림 */
    uint8_t sec = cmos_rd(0), min = cmos_rd(2), hr = cmos_rd(4), day = cmos_rd(7), mon = cmos_rd(8), yr = cmos_rd(9), b = cmos_rd(0x0B);
    if (!(b & 4)) {                                     /* BCD */
#define BCD(x) (uint8_t)(((x) & 15) + ((x) >> 4) * 10)
        sec = BCD(sec); min = BCD(min); hr = (uint8_t)(BCD(hr & 0x7F) | (hr & 0x80)); day = BCD(day); mon = BCD(mon); yr = BCD(yr);
#undef BCD
    }
    if (!(b & 2) && (hr & 0x80)) hr = (uint8_t)(((hr & 0x7F) + 12) % 24);   /* 12시간제 */
    hr &= 0x7F;
    if (!mon || mon > 12 || !day || day > 31 || hr > 23 || min > 59 || sec > 59) return -1;
    t[0] = 2000 + yr; t[1] = mon; t[2] = day; t[3] = hr; t[4] = min; t[5] = sec;
    return 0;
}
static void pc_clock(uint16_t *date, uint16_t *time)
{
    int t[6];
    if (cmos_time(t)) return;
    *date = (uint16_t)(((uint32_t)(t[0] - 1980) << 9) | ((uint32_t)t[1] << 5) | (uint32_t)t[2]);
    *time = (uint16_t)(((uint32_t)t[3] << 11) | ((uint32_t)t[4] << 5) | (uint32_t)(t[5] / 2));
}

static void volumes_init(void)
{
    fat_set_clock(pc_clock);
    nvols = 0;
    int order[8], no = 0;
    order[no++] = boot_drive;                           /* 부팅 USB 먼저 */
    for (int d = 0x80; d < 0x88; d++) if (d != boot_drive) order[no++] = d;
    for (int i = 0; i < no && nvols < MAX_VOLS; i++) {
        int d = order[i];
        if (!bios_has_ext(d)) continue;
        disks[i].drive = d;
        int k = fat_scan_disk(disk_read, &disks[i], &vols[nvols], MAX_VOLS - nvols);
        for (int j = 0; j < k; j++) vols[nvols + j].write = disk_write;
        nvols += k;
    }
}

static int lower_c(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int hidden_dir(const char *n)
{
    if (n[0] == '.' || n[0] == '$') return 1;
    const char *sys = "system volume information";
    int i = 0;
    while (sys[i] && lower_c(n[i]) == sys[i]) i++;
    return !sys[i] && !n[i];
}

/* "N:\a\b" → 볼륨 번호, 하위 경로 */
static int parse_path(const char *path, const char **sub)
{
    int v = 0, i = 0;
    while (path[i] >= '0' && path[i] <= '9') v = v * 10 + (path[i++] - '0');
    if (!i || path[i] != ':' || v >= nvols) return -1;
    *sub = path + i + 1;
    return v;
}

static void str_cat(char *d, int max, const char *s)
{
    int k = (int)strlen(d);
    while (*s && k < max - 1) d[k++] = *s++;
    d[k] = 0;
}

static void num_cat(char *d, int max, uint32_t v)
{
    char t[12]; int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    char r[12]; int k = 0;
    while (n) r[k++] = t[--n];
    r[k] = 0;
    str_cat(d, max, r);
}

#define FAT_PAGE 48                                     /* 4.0.29: 한 쪽에 읽는 항목 수 (BSS를 아끼려 작게, 폴더가 크면 쪽마다 다시 읽음) */
static FatEntry fat_tmp[FAT_PAGE];

static int list_all;                                            /* 3.0.2: 모든 파일 (디스크 파일 복사) */
static int plat_list(const char *path, FeDirEntry *out, int max)
{
    int n = 0, base = fe_list_skip;                      /* 4.0.34: 창 읽기 (한 번 쓰면 지움) */
    fe_list_skip = 0; fe_list_next = -1;
    if (!path[0]) {                                     /* 드라이브 목록 */
        for (int v = 0; v < nvols && n < max; v++) {
            FeDirEntry *e = &out[n++];
            memset(e, 0, sizeof *e);
            str_cat(e->name, sizeof e->name, "Drive ");
            num_cat(e->name, sizeof e->name, (uint32_t)v + 1);
            str_cat(e->name, sizeof e->name, " ");
            if (vols[v].label[0]) { str_cat(e->name, sizeof e->name, vols[v].label); str_cat(e->name, sizeof e->name, " "); }
            uint64_t gb = vols[v].bytes >> 30;
            num_cat(e->name, sizeof e->name, (uint32_t)(gb ? gb : vols[v].bytes >> 20));
            str_cat(e->name, sizeof e->name, gb ? " GB" : " MB");
            if (v == 0) str_cat(e->name, sizeof e->name, " (boot)");
            num_cat(e->key, sizeof e->key, (uint32_t)v);
            str_cat(e->key, sizeof e->key, ":");
            e->is_dir = 1;
        }
        return n;
    }
    const char *sub;
    int v = parse_path(path, &sub);
    if (v < 0) return -1;
    for (int skip = base; n < max; ) {
        int cnt = fat_list_skip(&vols[v], sub, fat_tmp, FAT_PAGE, skip);
        if (cnt < 0) return skip ? n : -1;
        if (cnt == 0) break;
        int i;
        for (i = 0; i < cnt && n < max; i++) {
            FatEntry *f = &fat_tmp[i];
            if (f->is_dir && hidden_dir(f->disp)) continue;  /* 3.6.2: 파일은 모두 (종류 거르기는 프런트엔드가) */
            FeDirEntry *e = &out[n++];
            memset(e, 0, sizeof *e);
            {                                               /* 3.4.7: 표시 이름도 UTF-8 (한글 등), 글자 단위로 자름 */
                int k = 0;
                for (; f->key[k] && k < (int)sizeof e->name - 1; k++) e->name[k] = f->key[k];
                if (f->key[k] && ((uint8_t)f->key[k] & 0xC0) == 0x80) { while (k > 0 && ((uint8_t)e->name[k - 1] & 0xC0) == 0x80) k--; if (k > 0) k--; }
                e->name[k] = 0;
            }
            memcpy(e->key, f->key, sizeof e->key - 1);
            e->is_dir = (uint8_t)f->is_dir;
            e->size = f->size;
            e->year = f->year; e->mon = f->mon; e->day = f->day; e->hour = f->hour; e->min = f->min;
        }
        if (i < cnt) { fe_list_next = skip + i; break; }       /* 4.0.34: 칸이 차서 못 읽은 나머지 */
        skip += cnt;
        if (cnt < FAT_PAGE) break;
        if (n >= max) { fe_list_next = skip; break; }
    }
    return n;
}
static int plat_list_all(const char *path, FeDirEntry *out, int max) { list_all = 1; int n = plat_list(path, out, max); list_all = 0; return n; }

static void ra_flush(void);
static int plat_write(const char *path, const uint8_t *data, uint32_t len)
{
    const char *sub;
    int v = parse_path(path, &sub);
    if (v < 0) return -1;
    ra_flush();                                         /* 3.7.1: 파일 자리가 바뀔 수 있음 */
    return fat_write_file(&vols[v], sub, data, len);      /* 크기가 달라도 됨 (테이프 저장 등) */
}

static int plat_create(const char *path, int is_dir)
{
    const char *sub;
    int v = parse_path(path, &sub);
    if (v < 0) return -1;
    return fat_create(&vols[v], sub, is_dir);
}

static int plat_delete(const char *path)               /* 3.0.2 */
{
    const char *sub;
    int v = parse_path(path, &sub);
    if (v < 0) return -1;
    ra_flush();
    return fat_delete(&vols[v], sub);
}

static int plat_make_file(const char *path, uint32_t size, const uint8_t *prefix, uint32_t plen, void (*progress)(int pct))
{   /* 3.0.2: 자리만 잡고 앞부분을 씀 (FAT를 직접 고침) */
    const char *sub;
    int v = parse_path(path, &sub);
    if (v < 0) return -1;
    if (fat_create(&vols[v], sub, 0)) return -1;
    return fat_alloc_file(&vols[v], sub, size, prefix, plen, progress);
}

/* ---- 하드 디스크 이미지 (2.8): 파일이 차지한 구간표를 만들어 두고 섹터 단위로 ---- */
#define HD_EXT_MAX 4096
static FatExtent hd_ext[4][HD_EXT_MAX];                 /* 3.4.3 DEV-15: 0 마스터, 1 슬레이브, 3.7.0: 2 = 이미지 파일 관리 */
static int hd_next[4], hd_vol[4] = { -1, -1, -1, -1 };
static uint32_t hd_secs[4];

static void plat_hd_close(int u) { if (u < 0 || u > 3) return; hd_vol[u] = -1; hd_next[u] = 0; hd_secs[u] = 0; }

static int plat_hd_open(int u, const char *path, uint32_t *sectors)
{
    if (u < 0 || u > 3) return -1;
    plat_hd_close(u);
    const char *sub;
    int v = parse_path(path, &sub);
    uint32_t size = 0;
    if (v < 0) return -1;
    int n = fat_file_extents(&vols[v], sub, hd_ext[u], HD_EXT_MAX, &size);
    if (n <= 0 || size < 512) return -1;
    hd_vol[u] = v; hd_next[u] = n; hd_secs[u] = size / 512;
    *sectors = hd_secs[u];
    return 0;
}

static int hd_lba(int u, uint32_t s, uint64_t *lba)       /* 파일 안 섹터 → 장치 LBA */
{
    if (hd_vol[u] < 0 || s >= hd_secs[u]) return -1;
    for (int i = 0; i < hd_next[u]; i++) {
        if (s < hd_ext[u][i].secs) { *lba = hd_ext[u][i].lba + s; return 0; }
        s -= hd_ext[u][i].secs;
    }
    return -1;
}

static int plat_hd_read(int u, uint32_t s, uint8_t *buf)
{
    uint64_t lba;
    if (u < 0 || u > 3) return -1;
    if (hd_lba(u, s, &lba)) return -1;
    return vols[hd_vol[u]].read(vols[hd_vol[u]].ctx, lba, 1, buf);
}

static int plat_hd_write(int u, uint32_t s, const uint8_t *buf)
{
    uint64_t lba;
    if (u < 0 || u > 3) return -1;
    if (hd_lba(u, s, &lba) || !vols[hd_vol[u]].write) return -1;
    if (vols[hd_vol[u]].cache_lba == lba) vols[hd_vol[u]].cache_lba = 0xFFFFFFFFu;   /* FAT 캐시와 겹치면 버림 */
    return vols[hd_vol[u]].write(vols[hd_vol[u]].ctx, lba, 1, buf);
}

/* ---- 3.7.1 가상 하드: 파일 일부 읽기 (최근 파일 4개의 구간표를 기억) ---- */
#define RA_N 4
#define RA_EXT 512
static struct { char path[FE_PATH_MAX]; int vol, n; uint32_t size, use; FatExtent ext[RA_EXT]; } ra[RA_N];
static uint32_t ra_clock;
static void ra_flush(void) { for (int i = 0; i < RA_N; i++) ra[i].path[0] = 0; }
static int plat_read_at(const char *path, uint32_t off, uint8_t *buf, uint32_t len)
{
    int k = -1;
    for (int i = 0; i < RA_N; i++) if (ra[i].path[0] && !strcmp(ra[i].path, path)) { k = i; break; }
    if (k < 0) {
        k = 0;
        for (int i = 1; i < RA_N; i++) if (ra[i].use < ra[k].use) k = i;
        const char *sub;
        int v = parse_path(path, &sub);
        if (v < 0) return -1;
        uint32_t size = 0;
        int n = fat_file_extents(&vols[v], sub, ra[k].ext, RA_EXT, &size);
        if (n < 0) return -1;
        int i = 0; for (; path[i] && i < FE_PATH_MAX - 1; i++) ra[k].path[i] = path[i]; ra[k].path[i] = 0;
        ra[k].vol = v; ra[k].n = n; ra[k].size = size;
    }
    ra[k].use = ++ra_clock;
    static uint8_t sec[512];
    while (len) {
        uint32_t s = off / 512, o = off % 512, n = 512 - o;
        if (n > len) n = len;
        if (off >= ra[k].size) { memset(buf, 0, len); return 0; }
        uint64_t lba = 0; int found = 0; uint32_t left = 0;
        for (int i = 0; i < ra[k].n; i++) { if (s < ra[k].ext[i].secs) { lba = ra[k].ext[i].lba + s; left = ra[k].ext[i].secs - s; found = 1; break; } s -= ra[k].ext[i].secs; }
        if (!found) return -1;
        if (o == 0 && len >= 1024) {                     /* 4.0.133: 같은 덩어리 안 이어진 섹터는 한 번에 (큰 SoundFont 읽기 속도) */
            uint32_t run = len / 512 > 64 ? 64 : len / 512;
            if (run > left) run = left;
            if (run >= 2 && !vols[ra[k].vol].read(vols[ra[k].vol].ctx, lba, run, buf)) { buf += run * 512; off += run * 512; len -= run * 512; continue; }
        }
        if (vols[ra[k].vol].read(vols[ra[k].vol].ctx, lba, 1, sec)) return -1;
        memcpy(buf, sec + o, n);
        buf += n; off += n; len -= n;
    }
    return 0;
}

/* ---- USB 전체 용량 사용 (부팅 드라이브) ---- */
static uint32_t mem_end;                        /* 로더가 잰 1 MiB 위 연속 메모리 끝 (0 = 모름) */

static int boot_rd(void *ctx, uint64_t lba, uint32_t count, uint8_t *buf)
{
    (void)ctx;
    if (bios_read(boot_drive, lba, (int)count)) return -1;
    memcpy(buf, (const void *)BIOS_BOUNCE, count * 512);
    return 0;
}

static int boot_wr(void *ctx, uint64_t lba, uint32_t count, const uint8_t *buf)
{
    (void)ctx;
    memcpy((void *)BIOS_BOUNCE, buf, count * 512);
    return bios_write(boot_drive, lba, (int)count);
}

static int plat_grow_query(uint64_t *part, uint64_t *disk)
{
    uint64_t total = bios_disk_sectors(boot_drive);
    if (!total) return -1;
    if (mem_end && mem_end < MEM_GROW_END) return -1;   /* 3.0: 임시 보관 자리(168 MiB까지)가 없으면 못 함 */
    GrowIO io = { boot_rd, boot_wr, 0, total, 0, 0, 0 };
    uint64_t ps = usbgrow_part_sectors(&io);
    if (!ps) return -1;
    *part = ps * 512; *disk = total * 512;
    return 0;
}

static int plat_grow_run(void (*progress)(int, const char *), char *err, int errlen)
{
    uint64_t total = bios_disk_sectors(boot_drive);
    if (!total) { err[0] = 0; str_cat(err, errlen, "disk size unknown"); return -1; }
    GrowIO io = { boot_rd, boot_wr, 0, total, (uint8_t *)(uintptr_t)MEM_GROW, MEM_GROW_SIZE, progress };
    return usbgrow_run(&io, err, errlen);
}

static int plat_read(const char *path, uint8_t *buf, uint32_t max, uint32_t *size)
{
    const char *sub;
    int v = parse_path(path, &sub);
    if (v < 0) return -1;
    return fat_read_file(&vols[v], sub, buf, max, size);
}

static void plat_audio(const int16_t *s, int n, int volume, int mute) { hda_write(s, n, volume, mute); }
static void plat_audio2(const int16_t *l, const int16_t *r, int n, int volume, int mute) { hda_write2(l, r, n, volume, mute); }
static void ivb_audio_clock(void);
static int plat_audio_select(int pref, char *status, int len) { if (pref == 2) ivb_audio_clock(); return hda_select(pref, status, len); }


/* 4.0.102: 레거시 Intel — HDMI를 뽑으면 비디오 BIOS(INT 10h 5F64h)로 내부 모니터를 다시 켬. HDMI가 꽂혀 있던 것을 본 뒤에만 */
static inline void outl(uint16_t p, uint32_t v) { __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(p)); }
static inline uint32_t inl(uint16_t p) { uint32_t v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static int vid_intel(void)
{
    static int c = -1;
    if (c >= 0) return c;
    outl(0xCF8, 0x80001000u); uint32_t id = inl(0xCFC);                 /* 00:02.0 */
    outl(0xCF8, 0x80001008u); uint32_t cl = inl(0xCFC);
    return c = ((id & 0xFFFF) == 0x8086 && (cl >> 24) == 0x03);
}
static int vid_act;
static int plat_video_outputs(char (*lines)[64], int max, int *active)
{
    /* 순서 = 1 내부 모니터(내부만), 2 내부 + 외부 복제, 3 외부 모니터(외부만). 외부(HDMI)가 안 꽂혀 있으면 목록을 비워 고를 수 없게 한다 */
    static const char *const t[3] = { "1: Intel", "2: HDMI Intel copy: internal+HDMI", "3: HDMI Intel only: HDMI (internal off)" };
    if (!vid_intel() || hda_hdmi_present() != 1) return 0;
    for (int i = 0; i < 3 && i < max; i++) { int k = 0; while (t[i][k] && k < 63) { lines[i][k] = t[i][k]; k++; } lines[i][k] = 0; }
    if (active) *active = vid_act;
    return 3 < max ? 3 : max;
}
static void vlog_hex(char *d, int max, uint32_t v) { char t[9]; for (int i = 0; i < 8; i++) t[i] = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 15]; t[8] = 0; str_cat(d, max, t); }
/* 4.0.106: 5번(Ivy Bridge HD 4000) — HDMI를 꽂고 부팅하면 BIOS가 HDMI를 파이프 A에만 켜고 내장 패널(LVDS 1366x768)은 파이프 B에 값만 넣고 꺼 둠.
 * HDMI(파이프 A)는 그대로 두고 파이프 B + PCH 변환기 B로 내장 패널을 추가로 켠다 (DisplaySelect v38·v39 시험 성공, 같은 그림이 두 화면에). 레지스터 순서는 그 인계 문서 그대로. */
static volatile uint32_t *ivb_mm;
static uint32_t ivb_dref0, ivb_sel0, ivb_lvds0;
static int ivb_dual_state; static int ivb_fdi_ok = -1;   /* FDI B 훈련 결과 (-1 안 함, 0 실패, 1 성공) */                              /* 1 = 내가 켬 */
static uint32_t ivb_hdmi_reg, ivb_hdmi_val;            /* HDMI 포트 레지스터와 켜져 있던 값 (내부 모니터만일 때 포트만 끔) */
static uint32_t rd32m(uint32_t o) { return ivb_mm[o >> 2]; }
static void wr32m(uint32_t o, uint32_t v) { ivb_mm[o >> 2] = v; }
static int mm_wait(uint32_t o, uint32_t mask, uint32_t val, int ms)
{
    for (int i = 0; i <= ms; i++) { if ((rd32m(o) & mask) == val) return 1; delay_ms(1); }
    return (rd32m(o) & mask) == val;
}
static char vl_buf[3500]; static int vl_n; static int vl_wr = 99;   /* 마지막 로그 쓰기 결과 (99 = 아직 안 씀) */
static void vl_s(const char *s) { while (*s && vl_n < (int)sizeof vl_buf - 2) vl_buf[vl_n++] = *s++; vl_buf[vl_n] = 0; }
static void vl_h(const char *k, uint32_t v) { vl_s(" "); vl_s(k); vl_s("="); char t[9]; for (int i = 0; i < 8; i++) t[i] = "0123456789ABCDEF"[(v >> (28 - 4 * i)) & 15]; t[8] = 0; vl_s(t); }
/* 공용 로그 규약(X86MSX_로그규약_기종테스트방_공용) — 머리 6줄 + 파일 이름 LOG\VID_<PC>_X<버전>.TXT. 기존 줄은 그대로 뒤에 붙음 */
/* 네트워크 기록을 LOG\NET_X<ver>.TXT 에 (바뀌었을 때만) */
static void netguard(int on)
{
    static const char *nm = "0:\\LOG\\NETGUARD.TXT";
    plat_create("0:\\LOG", 1); plat_create(nm, 0);
    plat_write(nm, (const uint8_t *)(on ? "RESET\r\n" : "OK\r\n"), on ? 7u : 4u);
}
static void netlog_write(void)
{
    static char nb[14000], fl[14300]; static int nlen;
    int n = net_log_take(nb, (int)sizeof nb);
    if (n == nlen || n <= 0) return;
    nlen = n;
    const char *h0 = "#LOG 1\r\n#prog=X86MSX ver=" MSXEMU_VERSION "\r\n"; int k = 0;
    while (h0[k]) { fl[k] = h0[k]; k++; }
    memcpy(fl + k, nb, (size_t)n);
    plat_create("0:\\LOG", 1); plat_create("0:\\LOG\\NET_X" MSXEMU_VERSION ".TXT", 0);
    plat_write("0:\\LOG\\NET_X" MSXEMU_VERSION ".TXT", (const uint8_t *)fl, (uint32_t)(k + n));
}

static uint32_t pci_id_at(uint32_t bdf) { outl(0xCF8, 0x80000000u | bdf << 8); return inl(0xCFC); }
static uint32_t pci_rd4(int b, int d, int f, int o) { outl(0xCF8, 0x80000000u | (uint32_t)b << 16 | (uint32_t)d << 11 | (uint32_t)f << 8 | (uint32_t)(o & 0xFC)); return inl(0xCFC); }
static void hexs(char *d, uint32_t v, int n) { for (int i = 0; i < n; i++) d[i] = "0123456789ABCDEF"[(v >> ((n - 1 - i) * 4)) & 15]; d[n] = 0; }
static char vl_out[3500 + 400];
static void vl_flush(void)
{
    char gp[5], gd[5], pc[4]; uint32_t g = pci_id_at(0x10); { uint32_t sg; if (pcdb_scan(pci_rd4, &sg)) g = sg; } hexs(gp, g & 0xFFFF, 4); hexs(gd, g >> 16, 4);
    const PcEntry *pe = pcdb_find(g);                                   /* 판별 표 kernel/pcdb.h */
    pc[0] = '?'; pc[1] = 0; if (pe) { pc[0] = (char)('0' + pe->pc); pc[1] = 0; }
    int n = 0; const char *h[4];
    char l1[] = "#LOG 1\r\n#prog=X86MSX ver=" MSXEMU_VERSION " build=" MSXEMU_BUILD "\r\n";
    char l2[128] = "#pc="; str_cat(l2, sizeof l2, pc); str_cat(l2, sizeof l2, " boot=legacy"); if (pe) { str_cat(l2, sizeof l2, " name=\""); str_cat(l2, sizeof l2, pe->name); str_cat(l2, sizeof l2, "\""); } str_cat(l2, sizeof l2, "\r\n");
    char l3[64] = "#pci gpu="; str_cat(l3, sizeof l3, gp); str_cat(l3, sizeof l3, ":"); str_cat(l3, sizeof l3, gd); str_cat(l3, sizeof l3, "\r\n");
    h[0] = l1; h[1] = l2; h[2] = l3; h[3] = 0;
    for (int i = 0; h[i]; i++) for (const char *q = h[i]; *q && n < 399; ) vl_out[n++] = *q++;
    for (int i = 0; i < vl_n && n < (int)sizeof vl_out - 1; i++) vl_out[n++] = vl_buf[i];
    vl_out[n] = 0;
    char nm[64] = "0:\\LOG\\VID_"; str_cat(nm, sizeof nm, pc); str_cat(nm, sizeof nm, "_X" MSXEMU_VERSION ".TXT");
    plat_create("0:\\LOG", 1);
    plat_create(nm, 0);                                                  /* fat_write_file은 있는 파일만 고쳐 씀 → 새 이름이면 먼저 만들어야 함 (5.0.86~92의 logwr=3 원인) */
    vl_wr = plat_write(nm, (const uint8_t *)vl_out, (uint32_t)n);
    if (vl_wr < 0) { plat_create("0:\\VIDLOG.TXT", 0); vl_wr = plat_write("0:\\VIDLOG.TXT", (const uint8_t *)vl_out, (uint32_t)n) < 0 ? -2 : 1; }
}
/* 기종별 설정 파일 DRV\IVB.INI (없거나 틀리면 아래 기본값 = 5번 Acer Aspire V5-171 시험에서 확정된 값으로 계속) */
static struct { uint32_t lvds_bios, lvds_on, w, h, t[6]; } ivbc = { 0x20300302u, 0xA0300302u, 1366, 768, { 0x05DB0555, 0x05DB0555, 0x0599057E, 0x031302FF, 0x031302FF, 0x03060302 } };
static int ivbc_loaded;
static int str_eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a - *b; }
static uint32_t ini_num(const char *v) { uint32_t r = 0; int hex = 0; if (v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) { hex = 1; v += 2; }
    for (; *v; v++) { int d; if (*v >= '0' && *v <= '9') d = *v - '0'; else if (hex && (*v | 32) >= 'a' && (*v | 32) <= 'f') d = (*v | 32) - 'a' + 10; else break; r = hex ? r * 16 + d : r * 10 + d; } return r; }
static void ivb_cfg_load(void)
{
    if (ivbc_loaded) return; ivbc_loaded = 1;
    static uint8_t b[1024]; uint32_t sz = 0;
    vl_s("1 DRV.scan dir=DRV\\ n=1\r\n");
    if (plat_read("0:\\DRV\\IVB.INI", b, sizeof b - 1, &sz) < 0 || !sz) { vl_s("2 DRV.load name=IVB.INI res=skip why=not_found\r\n3 DRV.fallback to=default\r\n"); return; }
    b[sz] = 0; int n = 0;
    for (uint32_t i = 0; i < sz; ) {
        char k[24], v[24]; int ki = 0, vi = 0;
        while (i < sz && b[i] != '=' && b[i] != '\n') { if (ki < 23 && b[i] > ' ') k[ki++] = (char)b[i]; i++; }
        if (i < sz && b[i] == '=') { i++; while (i < sz && b[i] != '\n' && b[i] != '\r') { if (vi < 23 && b[i] > ' ') v[vi++] = (char)b[i]; i++; } }
        while (i < sz && b[i] != '\n') i++;
        i++; k[ki] = 0; v[vi] = 0; if (!ki || k[0] == ';' || k[0] == '#' || !vi) continue;
        uint32_t x = ini_num(v);
        if (!str_eq(k, "LVDS_BIOS")) ivbc.lvds_bios = x; else if (!str_eq(k, "LVDS_ON")) ivbc.lvds_on = x;
        else if (!str_eq(k, "PANEL_W")) ivbc.w = x; else if (!str_eq(k, "PANEL_H")) ivbc.h = x;
        else if (k[0] == 'T' && k[1] >= '0' && k[1] <= '5' && !k[2]) ivbc.t[k[1] - '0'] = x; else continue;
        n++;
    }
    vl_s("2 DRV.load name=IVB.INI res=ok"); vl_h("keys", (uint32_t)n); vl_s("\r\n");
}
static int ivb_map(void)
{
    if (ivb_mm) return 1;
    if (!vid_intel()) return 0;
    ivb_cfg_load();
    outl(0xCF8, 0x80001000u | 0x10); uint32_t bar = inl(0xCFC) & 0xFFFFFFF0u;
    if (!bar || bar == 0xFFFFFFF0u) return 0;
    paging_set_uc((uintptr_t)bar);
    ivb_mm = (volatile uint32_t *)(uintptr_t)bar;
    return 1;
}
/* 5.2.16 화면 맞추기 (레거시 BIOS 부팅): 전에는 PIT(1 ms) 시계로만 프레임 간격을 맞추고 화면 갱신(60 Hz)과 상관없이 복사해,
 * 움직임이 고르지 않고(프레임 몇 장이 몰렸다 끊김) 찢김이 생겼다 (5번 Acer 실기 영상, blueMSX 와 비교).
 * 인텔 내장 그래픽의 스캔 줄 레지스터(PIPEDSL 0x70000/71000/72000) 또는 VGA 상태(0x3DA 비트 3)로 새 화면 시작을 기다려 그때 복사하고,
 * 화면 주기가 MSX 프레임(NTSC 59.92 Hz)과 1.5% 안이면 화면 주기에 맞춰 프레임을 돌린다 (소리는 hda.c 가 조금씩 맞춤) */
static volatile uint32_t *lvs_dsl;
static uint16_t lvs_port;
static uint32_t lvs_hz100;                                  /* 화면 주기 (Hz x 100), 0 = 못 씀 */
static uint32_t lvs_last, lvs_wait_ms, lvs_fail, lvs_calls;
static int lvs_pipe = -1;
static int lvs_raw(uint32_t lim_ms)                         /* 새 화면 시작(줄 번호가 되돌아감 / 귀선 시작)까지. 1 = 옴 */
{
    uint32_t t0 = ticks;
    if (lvs_dsl) {
        uint32_t prev = *lvs_dsl & 0x1FFF;
        for (;;) {
            uint32_t cur = *lvs_dsl & 0x1FFF;
            if (cur < prev && cur < 64) return 1;
            prev = cur;
            if (ticks - t0 > lim_ms) return 0;
            __asm__ volatile("pause");
        }
    }
    if (lvs_port) {
        while (inb(lvs_port) & 8) if (ticks - t0 > lim_ms) return 0;
        while (!(inb(lvs_port) & 8)) if (ticks - t0 > lim_ms) return 0;
        return 1;
    }
    return 0;
}
static uint32_t lvs_measure(void)                           /* 30번 재서 Hz x 100 (40~100 Hz 가 아니면 0) */
{
    if (!lvs_raw(60)) return 0;
    uint32_t t0 = ticks;
    for (int k = 0; k < 30; k++) if (!lvs_raw(60)) return 0;
    uint32_t el = ticks - t0;
    if (!el) return 0;
    uint32_t hz = 3000000u / el;
    return hz >= 4000 && hz <= 10000 ? hz : 0;
}
static void lvs_probe(void)
{
    if (ivb_map()) {
        static const uint32_t pipes[3] = { 0x70000, 0x71000, 0x72000 };
        for (int pi = 0; pi < 3 && !lvs_hz100; pi++) {
            lvs_dsl = ivb_mm + pipes[pi] / 4;
            if ((lvs_hz100 = lvs_measure()) != 0) lvs_pipe = pi;
            else lvs_dsl = 0;
        }
    }
    if (!lvs_hz100) {
        static const uint16_t ports[2] = { 0x3DA, 0x3BA };
        for (int pi = 0; pi < 2 && !lvs_hz100; pi++) { lvs_port = ports[pi]; if (!(lvs_hz100 = lvs_measure())) lvs_port = 0; }
    }
    lvs_last = ticks;
}
static int plat_vsync(void)                                 /* fe 가 뒤 버퍼를 화면에 복사하기 바로 앞에 부름 */
{
    if (!lvs_hz100) return 0;
    uint32_t t0 = ticks, per = 100000u / lvs_hz100;          /* ms */
    while (ticks - lvs_last + 3 < per && ticks - lvs_last < 100) __asm__ volatile("hlt");   /* 다음 화면 3 ms 앞까지는 쉼 (CPU 를 돌리지 않음) */
    int ok = lvs_raw(40);
    lvs_last = ticks; lvs_wait_ms += ticks - t0; lvs_calls++;
    if (ok) lvs_fail = 0; else if (++lvs_fail >= 5) { lvs_hz100 = 0; lvs_dsl = 0; lvs_port = 0; }   /* 5번 연속 안 오면 끔 (예전 방식으로) */
    return ok;
}
static void vl_regs(const char *tag)
{
    vl_s("\r\n["); vl_s(tag); vl_s("]\r\n");
    if (!ivb_map()) { vl_s(" no gfx mmio\r\n"); return; }
    static const struct { const char *n; uint32_t o; } R[] = {
        { "LVDS", 0xE1180 }, { "HDMIB", 0xE1140 }, { "PIPEA", 0x70008 }, { "PIPEB", 0x71008 }, { "SRCA", 0x6001C }, { "SRCB", 0x6101C }, { "HTA", 0x60000 }, { "VTA", 0x6000C },
        { "DSPA", 0x70180 }, { "STRA", 0x70188 }, { "SURFA", 0x7019C }, { "DSPB", 0x71180 }, { "STRB", 0x71188 }, { "SURFB", 0x7119C }, { "PFB", 0x68880 },
        { "TRB", 0xF1008 }, { "FTXB", 0x61100 }, { "FRXB", 0xF100C }, { "FIIRB", 0xF1014 }, { "DPLLB", 0xC6018 }, { "DREF", 0xC6200 }, { "DSEL", 0xC7000 }, { "AUDA", 0xE5000 }, { "PPST", 0xC7200 },
    };
    for (unsigned i = 0; i < sizeof R / sizeof R[0]; i++) { vl_h(R[i].n, rd32m(R[i].o)); if ((i & 3) == 3) vl_s("\r\n"); }
    vl_s("\r\n");
}
static void ivb_hdmi_set(int on)
{
    if (!ivb_map()) return;
    static const uint32_t PR[3] = { 0xE1140, 0xE1150, 0xE1160 };
    if (!ivb_hdmi_reg) for (int i = 0; i < 3; i++) { uint32_t v = rd32m(PR[i]); if (v & 0x80000000u) { ivb_hdmi_reg = PR[i]; ivb_hdmi_val = v; break; } }
    if (!ivb_hdmi_reg) return;
    uint32_t cur = rd32m(ivb_hdmi_reg);
    if (on) { if (!(cur & 0x80000000u)) wr32m(ivb_hdmi_reg, ivb_hdmi_val | 0x80000000u); }
    else if (cur & 0x80000000u) { ivb_hdmi_val = cur; wr32m(ivb_hdmi_reg, cur & ~0x80000000u); }
    vl_s("hdmi port "); vl_s(on ? "on" : "off"); vl_h("reg", rd32m(ivb_hdmi_reg)); vl_s("\r\n");
}
static int ivb_dual_possible(void)
{
    if (!ivb_map()) { vl_s("dual: no intel gfx / no BAR\r\n"); return 0; }
    uint32_t lv = rd32m(0xE1180);
    if (lv != ivbc.lvds_bios && !ivb_dual_state) { vl_s("dual: skipped, LVDS not in BIOS-HDMI state"); vl_h("LVDS", lv); vl_s("\r\n"); return 0; }
    return 1;                                            /* BIOS가 HDMI로 부팅하며 내장 패널을 꺼 둔 상태일 때만 (이 PC의 1366x768 패널 값) */
}
static int ivb_fdi_train(uint32_t TX, uint32_t RX, uint32_t IIR, uint32_t IMR, uint32_t ftx, uint32_t frx)
{
    static const uint32_t SW[4] = { 0x00, 0x3a, 0x39, 0x38 };
    uint32_t tx = ftx & ~0x80000000u & ~0x300u & ~0x400u & ~(0x3Fu << 22), rx = frx & ~0x80000000u & ~0x300u & ~0x400u;
    int bl = 0, sl = 0;
    for (int tr = 0; tr < 8 && !(bl && sl); tr++) {
        uint32_t t0 = tx | (SW[tr >> 1] << 22) | 0x4000;
        wr32m(TX, t0 & ~0x80000000u); wr32m(RX, rx | 0x2000 | 0x10); delay_ms(1);
        wr32m(IMR, rd32m(IMR) & ~0x300u); wr32m(IIR, 0x300);
        wr32m(TX, t0 | 0x80000000u); wr32m(RX, rx | 0x2000 | 0x10 | 0x80000000u); delay_ms(1);
        bl = sl = 0;
        for (int i = 0; i < 20 && !bl; i++) { if (rd32m(IIR) & 0x100) bl = 1; else delay_ms(1); }
        wr32m(IIR, 0x100);
        if (bl) {
            wr32m(TX, t0 | 0x80000000u | 0x100); wr32m(RX, rx | 0x2000 | 0x10 | 0x80000000u | 0x100); delay_ms(1);
            for (int i = 0; i < 20 && !sl; i++) { if (rd32m(IIR) & 0x200) sl = 1; else delay_ms(1); }
            wr32m(IIR, 0x200);
        }
        if (!(bl && sl)) { wr32m(TX, t0 & ~0x80000000u); wr32m(RX, rx | 0x2000 | 0x10); delay_ms(1); }
    }
    { uint32_t cur = rd32m(TX) & (0x3Fu << 22);
      wr32m(TX, (ftx & ~0x400u & ~(0x3Fu << 22)) | cur | 0x4000 | 0x80000000u); wr32m(RX, (frx & ~0x400u) | 0x80000000u); delay_ms(1); }
    return bl && sl;
}
static void ivb_lut_sync(void)
{
    if (!ivb_dual_state || !ivb_mm) return;
    if (((rd32m(0x70180) >> 26) & 0xF) != 2) return;              /* 8비트 인덱스 면일 때만 */
    for (int i = 0; i < 256; i++) wr32m(0x4A800 + 4 * i, ((uint32_t)(pal_sh[i][0] << 2) << 16) | ((uint32_t)(pal_sh[i][1] << 2) << 8) | (uint32_t)(pal_sh[i][2] << 2));
}
static int ivb_dual_on(void)
{
    if (!ivb_dual_possible()) return -1;
    if (ivb_dual_state) return 0;
    vl_s("v" MSXEMU_VERSION "\r\n"); vl_regs("before dual on");
    ivb_dref0 = rd32m(0xC6200); ivb_sel0 = rd32m(0xC7000); ivb_lvds0 = rd32m(0xE1180);
    wr32m(0xC6200, 0x1402); delay_ms(1);
    wr32m(0xC6048, 0x00021007); wr32m(0xC604C, 0x00021007);
    for (int i = 0; i < 3; i++) { wr32m(0xC6018, 0x88046004u); delay_ms(1); }
    wr32m(0xC7000, (rd32m(0xC7000) & ~0xF0u) | 0x90);
    wr32m(0xE1180, ivbc.lvds_on); delay_ms(1);
    const uint32_t *T = ivbc.t;
    for (int i = 0; i < 6; i++) { wr32m(0x61000 + 4 * i, T[i]); wr32m(0xE1000 + 4 * i, T[i]); }
    wr32m(0x6101C, rd32m(0x6001C));                                  /* 4.0.107: 원본 크기는 지금 화면(파이프 A)과 같게 — 비디오 모드가 1024x768이 아닐 수 있음 */
    wr32m(0x61030, 0x7E4BBBBB); wr32m(0x61034, 0x00800000); wr32m(0x61040, 0x00021A8C); wr32m(0x61044, 0x00080000);
    wr32m(0xF1010, 0x00200080); wr32m(0xF1030, 0x7E000000);
    wr32m(0x68880, 0xA0800000u); wr32m(0x68870, 0); wr32m(0x68874, (ivbc.w << 16) | ivbc.h);   /* 패널 피터는 제어→위치→크기 순 (리눅스 순서). 제어를 마지막에 쓰면 내장 패널이 깨졌음 (5.0.70~71 실기 시험) */
    wr32m(0x71008, 0x80000050u); mm_wait(0x71008, 0x40000000u, 0x40000000u, 400);
    int fdi = ivb_fdi_train(0x61100, 0xF100C, 0xF1014, 0xF1018, 0x80044B00u, 0x80022B50u);
    wr32m(0xF1008, 0x80000000u); mm_wait(0xF1008, 0x40000000u, 0x40000000u, 400);
    wr32m(0x71188, rd32m(0x70188)); wr32m(0x71184, rd32m(0x70184)); wr32m(0x7118C, rd32m(0x7018C)); wr32m(0x71190, rd32m(0x70190)); wr32m(0x711A4, rd32m(0x701A4));
    ivb_dual_state = 1; ivb_lut_sync(); ivb_dual_state = 0;
    wr32m(0x71180, rd32m(0x70180) | 0x80000000u); wr32m(0x7119C, rd32m(0x7019C)); delay_ms(20);
    wr32m(0xC7204, 0xABCD0003u); mm_wait(0xC7200, 0x80000000u, 0x80000000u, 800);
    wr32m(0x48254, 0x000003D0); wr32m(0xC8254, 0x03D003D0); wr32m(0x48250, 0x80000000u); wr32m(0xC8250, 0x80000000u);
    ivb_dual_state = 1;
    ivb_fdi_ok = fdi ? 1 : 0;
    vl_s(fdi ? "FDI B train OK\r\n" : "FDI B train FAIL\r\n"); vl_regs("after dual on"); vl_flush();
    return 0;
}
static void ivb_dual_off(void)
{
    if (!ivb_dual_state) return;
    wr32m(0xC8250, rd32m(0xC8250) & ~0x80000000u); wr32m(0x48250, rd32m(0x48250) & ~0x80000000u);
    wr32m(0xC7204, 0xABCD0000u); mm_wait(0xC7200, 0x80000000u, 0, 600);
    wr32m(0x71180, rd32m(0x71180) & ~0x80000000u); wr32m(0x7119C, rd32m(0x7119C)); delay_ms(20);
    wr32m(0xE1180, ivb_lvds0 & ~0x80000000u);
    wr32m(0x71008, rd32m(0x71008) & ~0x80000000u); mm_wait(0x71008, 0x40000000u, 0, 300);
    wr32m(0x68880, 0);
    wr32m(0x61100, rd32m(0x61100) & ~0x80000000u); wr32m(0xF100C, rd32m(0xF100C) & ~0x80000000u); delay_ms(1);
    wr32m(0xF1008, rd32m(0xF1008) & ~0x80000000u); mm_wait(0xF1008, 0x40000000u, 0, 300);
    wr32m(0xC6018, rd32m(0xC6018) & ~0x80000000u); delay_ms(1);
    wr32m(0xC7000, ivb_sel0); wr32m(0xC6200, ivb_dref0); delay_ms(1);
    ivb_dual_state = 0;
}
/* 4.0.107: 5번 HDMI 소리 — AUD_CONFIG(0xE5000+0x100*변환기) 비트 19:16에 실제 화면 클럭 항목을 써야 소리가 남 (PIPESRC는 믿지 않음: BIOS가 1024x768을 패널 피터로 1080p에 늘림).
 * 항목: 25.2MHz→1, 27→2, 54→4, 74.25→7, 148.5→9 (표에 없으면 1). 클럭 = ((HTOTAL>>16)+1)*((VTOTAL>>16)+1)*60/1000 kHz */
static void ivb_audio_clock(void)
{   /* DisplaySelect v30 ivb_on_run과 같은 순서: HDMI 포트 모드·오디오 비트, AUD_CONFIG 클럭 항목(다른 비트는 그대로), GCP 켜기 */
    vl_s("\r\naudio select HDMI: v" MSXEMU_VERSION "\r\n");
    if (!ivb_map()) { vl_s(" no intel gfx\r\n"); vl_flush(); return; }
    uint32_t port = 0, orig = 0;
    static const uint32_t PR[3] = { 0xE1140, 0xE1150, 0xE1160 };
    for (int i = 0; i < 3 && !port; i++) { uint32_t v = rd32m(PR[i]); vl_h("HDMI", v); if (v & 0x80000000u) { port = PR[i]; orig = v; } }
    if (!port) { vl_s(" no HDMI port on, skipped\r\n"); vl_flush(); return; }
    uint32_t tsel = (orig >> 29) & 3, nv = (orig & ~0xC00u) | 0x800u | 0x200u | 0x40u;
    uint32_t ac = 0xE5000 + 0x100 * tsel, ac_orig = rd32m(ac), dip = 0xE0200 + 0x1000 * tsel;
    if (nv != orig) wr32m(port, nv);
    uint32_t ht = rd32m(0x60000 + 0x1000 * tsel), vt = rd32m(0x6000C + 0x1000 * tsel);
    uint32_t khz = ((ht >> 16) + 1) * ((vt >> 16) + 1) / 1000 * 60;
    static const struct { uint32_t k; uint32_t i; } T[5] = { { 25200, 1 }, { 27000, 2 }, { 54000, 4 }, { 74250, 7 }, { 148500, 9 } };
    uint32_t idx = 1, best = 0xFFFFFFFFu;
    for (int j = 0; j < 5; j++) { uint32_t dd = khz > T[j].k ? khz - T[j].k : T[j].k - khz; if (dd < best) { best = dd; idx = T[j].i; } }
    if (best * 50 > khz) idx = 1;
    uint32_t nc = (ac_orig & ~(0xFu << 16)) | (idx << 16);
    if (nc != ac_orig) wr32m(ac, nc);
    wr32m(dip + 0x10, 0); wr32m(dip, rd32m(dip) | (1u << 25));          /* GCP 켜기 (AVMUTE 해제) */
    delay_ms(100);
    vl_h("trans", tsel); vl_h("HDMI_before", orig); vl_h("HDMI_after", rd32m(port)); vl_h("HT", ht); vl_h("VT", vt); vl_h("kHz", khz); vl_h("item", idx);
    vl_h("AUD_before", ac_orig); vl_h("AUD_after", rd32m(ac)); vl_h("DIP", rd32m(dip)); vl_h("ST2", rd32m(0xE50C0)); vl_s("\r\n");
    vl_flush();
}
static int plat_video_switch(int idx)
{
    if (!vid_intel() || idx < 0 || idx > 2) return -1;
    vl_s("video switch idx"); vl_h("i", (uint32_t)idx); vl_s("\r\n");
    if (idx == 2) { ivb_hdmi_set(1); if (ivb_dual_state) ivb_dual_off(); vid_act = 2; vl_flush(); return 0; }   /* 외부 모니터만 = 내장 패널 끔 (HDMI 켬) */
    if (ivb_dual_on() < 0 && !ivb_dual_state) { vl_flush(); return -2; }                                      /* 내부·복제 = 내장 패널 켬 */
    ivb_hdmi_set(idx == 1);                                                                                 /* 복제: HDMI 포트 켬, 내부만: HDMI 포트 끔 */
    vid_act = idx;
    vl_flush();
    return 0;
}
static int plat_hdmi_active(void) { return vid_intel() && hda_hdmi_present() == 1; }   /* 4.0.103: HDMI 모니터가 꽂혀 있으면 HDMI 소리 항목을 보임 */
static int plat_video_lost(void)
{
    static uint32_t next; static int seen;
    uint32_t t = plat_ms();
    if ((int32_t)(t - next) < 0) return 0;
    next = t + 500;
    { static int bl; if (!bl && t > 2500) { bl = 1;   /* 켤 때마다 한 번 무조건 남기는 부팅 로그 (Intel이 아니어도) */
        char b[900] = "#LOG 1\r\n#prog=X86MSX ver=" MSXEMU_VERSION " build=" MSXEMU_BUILD "\r\n#boot=legacy gpu=", h[9]; uint32_t g0 = pci_id_at(0x10); { uint32_t sg; if (pcdb_scan(pci_rd4, &sg)) g0 = sg; } hexs(h, g0, 8);
        str_cat(b, sizeof b, h); { const PcEntry *pe = pcdb_find(g0); str_cat(b, sizeof b, pe ? "\r\n#pc=" : "\r\n#pc=?"); if (pe) { char d[2] = { (char)('0' + pe->pc), 0 }; str_cat(b, sizeof b, d); str_cat(b, sizeof b, " name=\""); str_cat(b, sizeof b, pe->name); str_cat(b, sizeof b, "\""); } }
        str_cat(b, sizeof b, "\r\nBOOT.start res=ok\r\n");
        { int bn = (int)strlen(b); bn = pcdb_dump_class(pci_rd4, 0x02, "NET", b, bn, (int)sizeof b); b[bn] = 0; }
        plat_create("0:\\LOG", 1); plat_create("0:\\LOG\\BOOT_X" MSXEMU_VERSION ".TXT", 0); plat_write("0:\\LOG\\BOOT_X" MSXEMU_VERSION ".TXT", (const uint8_t *)b, (uint32_t)strlen(b)); } }
    if (!vid_intel()) return 0;
    int p = hda_hdmi_present();
    { static int logged; if (!logged && plat_ms() > 3000) { logged = 1; vl_s("v" MSXEMU_VERSION " boot state: hda_hdmi_present="); vl_h("p", (uint32_t)p); vl_s("\r\n"); vl_regs("boot"); vl_flush(); } }   /* 부팅 직후 상태를 항상 LOG\VIDLOG.TXT에 (HDMI 감지가 안 돼도 레지스터를 볼 수 있게) */
    if (p < 0) return 0;
    if (p) { if (!seen) { seen = 1; vl_s("HDMI detected\r\n"); int r = ivb_dual_on(); if (r == 0) vid_act = 1; else { vl_s("dual on not done\r\n"); vl_flush(); } } return 0; }   /* 4.0.106: HDMI가 꽂히면(BIOS가 HDMI로만 켜도) 내장 패널도 같이 켬 */
    if (!seen) return 0;
    seen = 0;
    return 1;                                                          /* 뽑음: 내장 패널은 계속 켜 있음 — fe가 소리 출력을 자동으로 되돌림 */
}

/* 끝내기: ACPI S5로 끄고, 안 되면 안내 후 멈춤 */
static void plat_poweroff(void)
{
    net_halt(); netlog_write();  /* 5.0.110 */
    acpi_poweroff();                                    /* 3.3.2 PLAT-01: ACPI S5 (꺼지면 돌아오지 않음) */
    __asm__ volatile("cli");
    fe_fill(0, 0, 4096, 4096, 1);
    fe_text(32, 32, "X86MSX: 이제 전원을 꺼도 됩니다. (It is now safe to turn off the PC.)", 15, 1);
    for (;;) __asm__ volatile("hlt");
}

static void put_line(char *d, const char *s) { int i = 0; for (; s[i] && i < 63; i++) d[i] = s[i]; d[i] = 0; }

static int plat_info(char (*lines)[64], int max)
{
    int n = 0;
    if (n < max) put_line(lines[n++], "boot mode: legacy BIOS (CSM), 32-bit");
    {   /* SMBIOS (PC 이름·BIOS·보드) */
        static char sm[4][64]; static int smn = -1;
        if (smn < 0) smn = smbios_lines(smbios_find_legacy(), sm, 4);
        for (int i = 0; i < smn && n < max; i++) put_line(lines[n++], sm[i]);
    }
    {   /* 3.3.2 */
        char al[64] = "ACPI power off: ";
        if (!strcmp(acpi_status(), "not tried")) acpi_init();
        str_cat(al, sizeof al, acpi_status());
        if (n < max) put_line(lines[n++], al);
    }
    {   /* 3.0 */
        char ml[64] = "PS/2 mouse: ";
        str_cat(ml, sizeof ml, ps2_ok ? (ps2m.size == 4 ? "on (wheel)" : "on") : "none");
        str_cat(ml, sizeof ml, ", init step ");
        num_cat(ml, sizeof ml, (uint32_t)ps2_step);
        str_cat(ml, sizeof ml, ", memory ");
        num_cat(ml, sizeof ml, mem_end >> 20);
        str_cat(ml, sizeof ml, " MB, bytes ");
        num_cat(ml, sizeof ml, ms_bytes);
        str_cat(ml, sizeof ml, "/");
        num_cat(ml, sizeof ml, ms_packets);
        if (n < max) put_line(lines[n++], ml);
    }
    if (n < max) {   /* 5.2.16 화면 맞추기 */
        char vl[64] = "vsync: ";
        if (!lvs_hz100) str_cat(vl, sizeof vl, "off (PIT timing)");
        else {
            str_cat(vl, sizeof vl, lvs_dsl ? "GPU pipe " : "VGA port"); if (lvs_dsl) num_cat(vl, sizeof vl, (uint32_t)lvs_pipe);
            str_cat(vl, sizeof vl, ", "); num_cat(vl, sizeof vl, lvs_hz100 / 100); str_cat(vl, sizeof vl, ".");
            if (lvs_hz100 % 100 < 10) str_cat(vl, sizeof vl, "0");
            num_cat(vl, sizeof vl, lvs_hz100 % 100); str_cat(vl, sizeof vl, " Hz");
        }
        put_line(lines[n++], vl);
    }
    n += hda_info(lines + n, max - n);
    if (n < max) n += amddet_info(lines + n, max - n);        /* 4.0.34: 그래픽·무선랜 PCI 열거 (읽기 전용, 다른 AI 작업물 반영) */
    if (n < max) n += amddet_wifi_info(lines + n, max - n);
    if (vid_intel() && ivb_map()) {   /* 로그 파일이 안 생겨도 진단 화면(사진)으로 그래픽 상태를 볼 수 있게 */
        static const struct { const char *n; uint32_t o; } R[] = {   /* 4개씩 3줄 (진단 창 줄 수 제한 안에 들게) */
            { "SRCA", 0x6001C }, { "SRCB", 0x6101C }, { "DSPA", 0x70180 }, { "DSPB", 0x71180 },
            { "STRA", 0x70188 }, { "STRB", 0x71188 }, { "PFB", 0x68880 }, { "TRB", 0xF1008 },
            { "HTA", 0x60000 }, { "VTA", 0x6000C }, { "AUDA", 0xE5000 }, { "HDMIB", 0xE1140 },   /* HDMI 소리 진단 — 화면 클럭(HTA·VTA)과 소리 클럭 항목(AUDA 비트19:16) */
        };
        char g[64] = "gfx: hda_hdmi=";
        num_cat(g, sizeof g, (uint32_t)(hda_hdmi_present() + 1));
        str_cat(g, sizeof g, " dual=");
        num_cat(g, sizeof g, (uint32_t)ivb_dual_state);
        str_cat(g, sizeof g, " fdi=");
        num_cat(g, sizeof g, (uint32_t)(ivb_fdi_ok + 1));
        str_cat(g, sizeof g, " logwr=");
        num_cat(g, sizeof g, (uint32_t)(vl_wr + 5));
        if (n < max) put_line(lines[n++], g);
        for (unsigned i = 0; i < 12 && n < max; i += 4) {
            char r[64] = "";
            for (unsigned j = i; j < i + 4; j++) { str_cat(r, sizeof r, R[j].n); str_cat(r, sizeof r, " "); vlog_hex(r, sizeof r, rd32m(R[j].o)); str_cat(r, sizeof r, " "); }
            put_line(lines[n++], r);
        }
    }
    char vl[64] = "drives: ";
    num_cat(vl, sizeof vl, (uint32_t)nvols);
    str_cat(vl, sizeof vl, " FAT volume(s), boot BIOS drive 0x");
    vl[strlen(vl) + 2] = 0;
    vl[strlen(vl) + 1] = "0123456789ABCDEF"[boot_drive & 15];
    vl[strlen(vl)] = "0123456789ABCDEF"[(boot_drive >> 4) & 15];
    if (n < max) put_line(lines[n++], vl);
    uint8_t st = inb(0x64);
    char t[64] = "8042 keyboard controller: status xx";
    t[33] = "0123456789ABCDEF"[st >> 4]; t[34] = "0123456789ABCDEF"[st & 15];
    if (n < max) put_line(lines[n++], st == 0xFF ? "8042 keyboard controller: not present" : t);
    if (n < max) put_line(lines[n++], "");
    if (n < max) put_line(lines[n++], "USB devices: listed only in UEFI boot (BIOS handles USB here)");
    if (n < max) put_line(lines[n++], "In legacy mode, USB/wireless keyboards work only through");
    if (n < max) put_line(lines[n++], "the firmware option 'USB Legacy Support' (USB keyboard");
    if (n < max) put_line(lines[n++], "emulation). If they do not work, boot this USB in UEFI");
    if (n < max) put_line(lines[n++], "mode instead (choose 'UEFI: <USB>' in the boot menu,");
    if (n < max) put_line(lines[n++], "Secure Boot off). UEFI mode has its own USB driver.");
    return n;
}

/* 레거시 판도 진단 전체를 LOG\DIAG.TXT 로 (사진 대신) — fe 가 준 시스템·소리 줄 + 정보 줄 */
static void plat_diag_save(const char *head)
{
    static char buf[26000];
    uint32_t q = 0;
    if (head) while (*head && q < sizeof buf - 2) buf[q++] = *head++;
    plat_create("0:\\LOG", 1);
    plat_create("0:\\LOG\\DIAG.TXT", 0);
    plat_write("0:\\LOG\\DIAG.TXT", (const uint8_t *)buf, q);
}

void kmain(void)
{
    {                                               /* x87 FPU 켜기 (emu2413 표 초기화용): CR0.EM=0, MP=1, NE=1 */
        uint32_t cr0;
        __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
        cr0 = (cr0 & ~4u) | 2u | 0x20u;
        __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));
        __asm__ volatile("fninit");
    }
    const BootInfo *bi = (const BootInfo *)BOOTINFO_ADDR;
    if (bi->magic != BOOT_MAGIC) for (;;) __asm__ volatile("hlt");

    FeDisplay d;
    memset(&d, 0, sizeof d);
    d.fb = (uint8_t *)bi->lfb;
    d.pitch = bi->pitch;
    d.w = bi->width;
    d.h = bi->height;
    d.bpp = bi->bpp == 32 ? 4 : 1;
    d.bgr = bi->red_pos == 16;
    {
        uint32_t sx = d.w / 256, sy = d.h / 192, sc = sx < sy ? sx : sy;
        if (!sc) sc = 1;
        d.scale_x = d.scale_y = sc;
        /* 1366x768 패널이 1024x768 모드를 가로로 늘려 보여 주는 노트북(Acer V5-171) 보정 */
        if (d.bpp == 1 && d.w == 1024 && d.h == 768) d.scale_x = 3;
    }
    if (d.bpp == 1) set_palette();

    uint8_t bios_pic_m = inb(0x21), bios_pic_s = inb(0xA1);    /* BIOS 호출 때 되돌릴 마스크 */
    bios_init(bios_pic_m, bios_pic_s);
    boot_drive = (int)(bi->drive & 0xFF);
    idt_init();
    pic_init();
    pit_init();
    kbd_init();

    FePlatform p = { .ms = plat_ms, .hp_state = hda_headphone, .snd_gpio = hda_gpio_next, .video_lost = plat_video_lost, .hdmi_active = plat_hdmi_active, .video_outputs = plat_video_outputs, .video_switch = plat_video_switch, .silence = hda_silence, .reboot = reboot, .poweroff = plat_poweroff, .audio = plat_audio, .audio2 = plat_audio2, .audio_select = plat_audio_select, .info = plat_info, .diag_save = plat_diag_save, .sound_diag = hda_sound_diag, .now = cmos_time,
                     .list_dir = plat_list, .list_dir_all = plat_list_all, .read_file = plat_read, .write_file = plat_write, .create = plat_create, .delete_file = plat_delete,
                     .scratch = (uint8_t *)(uintptr_t)MEM_SCRATCH, .scratch_size = MEM_SCRATCH_SIZE, .back = (uint8_t *)(uintptr_t)MEM_BACK, .back_size = MEM_BACK_SIZE,   /* 3.0 배치 (memmap.h) */
                     .xram = (uint8_t *)(uintptr_t)MEM_XRAM, .v9990 = (void *)(uintptr_t)MEM_V9990,
                     .hd_open = plat_hd_open, .hd_close = plat_hd_close, .hd_read = plat_hd_read, .hd_write = plat_hd_write, .set_palette = d.bpp == 1 ? set_palette : 0, .dac_set = d.bpp == 1 ? plat_dac_set : 0, .pal_get = d.bpp == 1 ? plat_pal_get : 0,
                     .grow_query = plat_grow_query, .grow_run = plat_grow_run, .mouse_read = plat_mouse_read, .mouse_dev = plat_mouse_dev, .mouse_set = plat_mouse_set, .make_file = plat_make_file,
                     .read_at = plat_read_at, .vsync_wait = plat_vsync, .vhd_mem = (uint8_t *)(uintptr_t)MEM_VHD, .vhd_mem_size = MEM_VHD_SIZE };
    {   /* 3.2.13: 영상 녹화 버퍼 = USB 확장 임시 자리(118 MiB~)부터 연속 메모리 끝까지 (512 MiB까지). 모르면 80 MiB */
        uint32_t me = bi->mem_end;
        if (!me || me >= MEM_GROW_END) {
            uint32_t sz = me ? me - MEM_GROW : MEM_GROW_SIZE;
            if (sz > (512u << 20)) sz = 512u << 20;
            sz -= 4u << 20;                             /* 3.4.0: 끝 4 MiB는 큰 펌웨어 롬 (FS-A1GT 등) */
            if (sz > (64u << 20)) { sz -= 16u << 20; cart_big = (uint8_t *)(uintptr_t)(MEM_GROW + sz + (4u << 20)); cart_big_size = 16u << 20; }   /* 4.0.129: 그 앞에 16 MiB는 큰 카트리지 롬 */
            p.vbuf = (uint8_t *)(uintptr_t)MEM_GROW; p.vbuf_size = sz;
            machine_big = (uint8_t *)(uintptr_t)(MEM_GROW + sz); machine_big_size = 4u << 20;
        }
    }
    msx_net_host = nethost_get();                          /* 5.0.108 MSX 네트워크 카트리지 ↔ lwIP */
    fe_init(&d, &p, &m, FE_ROMS, FE_DISP_STORE " > ROMS");
    const char *wc = enable_wc_pat(bi->lfb, d.pitch * d.h) ? "WC: on (PAT)" : enable_wc(bi->lfb, d.pitch * d.h);
    __asm__ volatile("sti");
    mem_end = bi->mem_end;
    __asm__ volatile("cli");
    ps2_mouse_init();
    __asm__ volatile("sti");                         /* 3.0: PS/2 마우스 (USB 마우스도 BIOS가 PS/2로 흉내 내면 여기로) */
    if (mem_end && mem_end < MEM_RUN_END) fe_fatal("Not enough memory: X86MSX needs at least 142 MB (legacy boot).", 0);
    opl4_set_memory((uint8_t *)(uintptr_t)MEM_WAVE, 0);    /* 3.1.0: MoonSound 웨이브 메모리 */
    memset((uint8_t *)(uintptr_t)MEM_WAVE, 0, MEM_WAVE_SIZE);

    volumes_init();
    if (!nvols) fe_fatal("Boot drive not readable (BIOS INT 13h).", 0);
    char err[160];
    const char *first_machine = machine_default_name(plat_read);
    if (machine_load(&m, plat_read, first_machine, 0, err, sizeof err)) fe_fatal("Machine setup failed:", err);
    fe_set_default_machine(first_machine);

    char snd[24];
    { extern void fe_trace_ext(const char *); hda_trace = fe_trace_ext; }   /* 5.2.23 */
    hda_init(snd, sizeof snd, MEM_HDA_DMA);       /* 타이머 인터럽트가 필요해서 sti 뒤 */
    fe_set_status_text(wc, snd);
    fe_restore();                                   /* 저장된 출력 장치 선택이 HD Audio 초기화 뒤여야 함 */

    lvs_probe();                                    /* 화면 맞추기 (인텔 스캔 줄 / VGA) */
    uint64_t target = (uint64_t)ticks * PIT_DIV;
    uint32_t sec_start = ticks, busy = 0, frames = 0, lock_n = 0;
    for (;;) {
        uint32_t t0 = ticks;
        kbc_drain();
        lvs_calls = 0;
        fe_frame();
        { /* 5.0.94 네트워크(레거시): 5.0.114부터는 네트워크 카트리지가 슬롯에 있을 때만 — 없으면 랜카드·lwIP 코드는 전혀 실행하지 않는다.
           * 카트리지를 빼면 랜카드를 멈추고 스택을 내리고, 다시 꽂으면 처음부터 시작한다. 기록은 2초마다 */
            static int nst, nlast_ms; static uint32_t nlast;
            extern int fe_net_cart_present(void);
            int want = fe_net_cart_present();
            if (want && !nst && ticks > 3000) { nst = 1; { uint8_t gb[16]; uint32_t gs = 0; int sk = 0;
                   if (plat_read("0:\\LOG\\NETGUARD.TXT", gb, sizeof gb, &gs) >= 0 && gs >= 5 && gb[0] == 'R') sk = 1;
                   net_set_guard(netguard, sk); }
                net_set_flush(netlog_write); net_start(ticks); netlog_write(); }
            else if (!want && nst) { nst = 0; net_stop(); netlog_write(); }
            if (nst) {
                net_poll(ticks);
                if (ticks - nlast > 2000) { nlast = ticks; netlog_write(); }
            }
            (void)nlast_ms;
        }
        busy += ticks - t0 - lvs_wait_ms; lvs_wait_ms = 0;   /* 화면 기다린 시간은 일한 시간이 아님 */
        frames++;
        int sp = fe_speed_pct();                    /* 속도 %, 0 = 최대 속도 */
        uint32_t fhz = 357954500u / fe_frame_tstates();     /* MSX 프레임 Hz x 100 (NTSC 5992, PAL 5000) */
        int lock = sp == 100 && lvs_calls && lvs_hz100 && (lvs_hz100 > fhz ? lvs_hz100 - fhz : fhz - lvs_hz100) * 1000u < fhz * 15u;
        if (lock) lock_n++;
        if (sp == 0 || lock) target = (uint64_t)ticks * PIT_DIV;   /* 화면 주기가 MSX 프레임과 같으면(1.5% 안) 화면이 박자 */
        else {
            uint32_t fp = FRAME_PIT * (fe_frame_tstates() / 4u) / (59736u / 4u);   /* 2.14: PAL 50 Hz면 길게 */
            target += (uint32_t)(fp * 100u / (uint32_t)sp);   /* 32비트 나눗셈 */
            for (;;) {
                uint64_t now = (uint64_t)ticks * PIT_DIV;
                if (now >= target) {
                    if (now > target + FRAME_PIT * 4) target = now;   /* 크게 밀리면 다시 맞춤 */
                    break;
                }
                __asm__ volatile("hlt");
            }
        }
        uint32_t el = ticks - sec_start;
        if (el >= 1000) {
            fe_set_speed((int)(frames * 100000u / (el * (357954500u / fe_frame_tstates()) / 100u)), (int)(busy * 100u / el));
            { static char src[16]; char *d = src; const char *q = lvs_dsl ? "GPU pipe " : "VGA port"; while (*q) *d++ = *q++;
              if (lvs_dsl) *d++ = (char)('0' + lvs_pipe); *d = 0;
              fe_set_vsync(src, lvs_hz100, (int)lock_n, (int)frames); lock_n = 0; }   /* 5.2.17 진단 System 칸 */
            sec_start = ticks; busy = 0; frames = 0;
        }
    }
}
