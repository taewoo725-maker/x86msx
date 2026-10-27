/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * machine.c - 기종 적재 (설계서 §5)
 * config.ini의 ROM 경로("Machines/X/a.rom" 또는 "Machines\X\a.rom", "Machines/Shared Roms/b.rom")는
 * USB 최상위 기준으로 먼저 찾고, 없으면 그 기종 폴더에서 파일 이름으로 찾는다.
 */
#include "machine.h"
#include "cbios_embed.h"
#include <string.h>

#define ROM_POOL (1024u * 1024u)
static uint8_t rom_pools[2][ROM_POOL];      /* 번갈아 씀: 새 기종 적재가 실패해도 지금 기종은 그대로 */
static int pool_cur;
static char cfg[65536];

static void put(char *d, int max, const char *s)
{
    int k = (int)strlen(d);
    while (*s && k < max - 1) d[k++] = *s++;
    d[k] = 0;
}

uint8_t *machine_big;                   /* 3.4.0: 큰 펌웨어 롬 (FS-A1GT 4 MB 등) 자리, 플랫폼이 정함 */
uint32_t machine_big_size;
uint8_t *cart_big;                      /* 4.0.129: 2 MiB 넘는 카트리지 롬 (ASCII16-X·Yamanooto 8 MB) 자리 (NULL이면 못 씀) */
uint32_t cart_big_size;
int (*machine_rom_find)(const char *base, uint8_t *buf, uint32_t max, uint32_t *size);
void (*machine_rom_seen)(const char *base, const uint8_t *data, uint32_t size);


/* 4.0.28: FALLBACK_MACHINE(C-BIOS) 파일을 USB에서 못 읽으면 내장본을 준다 — 컴퓨터마다 USB 읽기가 달라도 최소한 부팅은 되게 */
static MachineRead emb_real;
static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) { char x = *a, y = *b; if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; if (x != y) return 0; }
    return *a == *b;
}
static int emb_read(const char *path, uint8_t *buf, uint32_t max, uint32_t *size)
{
    if (emb_real && !emb_real(path, buf, max, size)) return 0;
    const uint8_t *d = 0; uint32_t dn = 0;
    if (ieq(path, "0:\\Machines\\" FALLBACK_MACHINE "\\config.ini")) { d = cbios_cfg_data; dn = sizeof cbios_cfg_data; }
    else if (ieq(path, "0:\\Machines\\" FALLBACK_MACHINE "\\cbios_main_msx1.rom") || ieq(path, "0:\\Machines\\cbios_main_msx1.rom")) { d = cbios_main_data; dn = sizeof cbios_main_data; }
    else if (ieq(path, "0:\\Machines\\" FALLBACK_MACHINE "\\cbios_logo_msx1.rom") || ieq(path, "0:\\Machines\\cbios_logo_msx1.rom")) { d = cbios_logo_data; dn = sizeof cbios_logo_data; }
    if (!d) return -1;
    uint32_t n = dn < max ? dn : max;
    memcpy(buf, d, n); *size = dn;
    return 0;
}
static MachineRead emb_wrap(MachineRead rd, const char *name)
{
    if (!ieq(name, FALLBACK_MACHINE)) return rd;
    emb_real = rd;
    return emb_read;
}

int machine_load(MSX *m, MachineRead rd, const char *name, MachineProfile *out, char *err, int errlen)
{
    rd = emb_wrap(rd, name);
    char path[512] = "0:\\Machines\\";
    put(path, sizeof path, name);
    put(path, sizeof path, "\\config.ini");
    uint32_t n = 0;
    if (rd(path, (uint8_t *)cfg, sizeof cfg - 1, &n) || !n) {
        err[0] = 0; put(err, errlen, "missing "); put(err, errlen, path + 2);
        return -1;
    }
    if (n > sizeof cfg - 1) n = sizeof cfg - 1;
    static MachineProfile prof;
    if (profile_parse(cfg, n, &prof, err, (size_t)errlen)) return -1;
    int tms = prof.vdp[0] == 'T' && prof.vdp[1] == 'M' && prof.vdp[2] == 'S';
    int v9938 = !strcmp(prof.vdp, "V9938") || !strcmp(prof.vdp, "V9958");
    if (prof.vdp[0] && !tms && !v9938) {
        err[0] = 0; put(err, errlen, "needs "); put(err, errlen, prof.vdp); put(err, errlen, ": this video chip is not supported");
        return -1;
    }
    int np = !pool_cur;
    uint8_t *rom_pool = rom_pools[np];

    static RomImage roms[MAX_REGIONS];
    memset(roms, 0, sizeof roms);
    uint32_t used = 0;
    for (int i = 0; i < prof.region_count; i++) {
        ProfileRegion *r = &prof.region[i];
        if (r->type == REGION_RAM || r->type == REGION_RAM_MAPPER || r->type == REGION_PAC) continue;
        if (r->type == REGION_CART && !r->file[0]) continue;   /* 3.6.8: 파일 없는 내장 카트리지 (SCC·MegaRAM …) */
        char p1[512] = "0:\\", p2[512] = "0:\\Machines\\";
        int k = 3;
        for (const char *s = r->file; *s && k < (int)sizeof p1 - 1; s++) p1[k++] = (*s == '/') ? '\\' : *s;
        p1[k] = 0;
        const char *base = r->file;
        for (const char *s = r->file; *s; s++) if (*s == '/' || *s == '\\') base = s + 1;
        put(p2, sizeof p2, name); put(p2, sizeof p2, "\\"); put(p2, sizeof p2, base);
        uint32_t size = 0, room = ROM_POOL - used;
        int big = r->type == REGION_FW && machine_big && machine_big_size;   /* 3.4.0 */
        uint8_t *dst = big ? machine_big : rom_pool + used;
        if (big) room = machine_big_size;
        if (rd(p1, dst, room, &size) && rd(p2, dst, room, &size) &&
            (!machine_rom_find || machine_rom_find(base, dst, room, &size))) {   /* 3.3.2: SHA-1로 기억한 파일 */
            /* 메인 BIOS(슬롯 0-0, 0x0000부터)는 꼭 필요. 그 밖의 ROM은 빼고 켠다 */
            if (r->slot == 0 && r->subslot == 0 && r->start_block == 0 && r->type != REGION_KANJI) {
                err[0] = 0; put(err, errlen, "missing ROM "); put(err, errlen, r->file);
                return -1;
            }
            if (prof.missing_count < 4) {
                char *d = prof.missing[prof.missing_count++];
                int dn = 0;
                for (const char *t = base; *t && dn < 63; t++) d[dn++] = *t;
                d[dn] = 0;
            }
            for (int j = i; j < prof.region_count - 1; j++) prof.region[j] = prof.region[j + 1];
            prof.region_count--;
            i--;
            continue;
        }
        if (!size || size > room || (!big && size > MSX_ROM_MAX)) { err[0] = 0; put(err, errlen, "bad ROM size "); put(err, errlen, r->file); return -1; }
        roms[i].data = dst;
        roms[i].size = size;
        if (machine_rom_seen) machine_rom_seen(base, dst, size);
        if (!big) used += (size + 15) & ~15u;
    }
    if (prof.moonsound && opl4_memory()) {              /* 3.1.0: MoonSound 웨이브 롬 (2 MiB) — 기종 폴더 또는 Shared Roms */
        static const char *const alt[3] = { 0, "Machines/Shared Roms/MOONSOUND.rom", "Machines/Shared Roms/yrw801.rom" };
        uint32_t size = 0;
        int ok = 0;
        for (int t = 0; t < 3 && !ok; t++) {
            const char *f = t ? alt[t] : prof.ms_rom;
            if (!f[0]) continue;
            char p1[300] = "0:\\";
            int k = 3;
            for (const char *s = f; *s && k < (int)sizeof p1 - 1; s++) p1[k++] = (*s == '/') ? '\\' : *s;
            p1[k] = 0;
            if (!rd(p1, opl4_memory(), 2u << 20, &size) && size >= 0x200000 / 2) ok = 1;
        }
        opl4_set_memory(opl4_memory(), ok ? (size > (2u << 20) ? 2u << 20 : size) : 0);
        if (!ok && prof.missing_count < 4) { char *d = prof.missing[prof.missing_count++]; int dn = 0; for (const char *t = "MOONSOUND.rom"; *t && dn < 63; t++) d[dn++] = *t; d[dn] = 0; }
    }
    int e = msx_setup(m, &prof, roms);
    if (e) { err[0] = 0; put(err, errlen, msx_error(e)); return -1; }
    pool_cur = np;
    if (out) *out = prof;
    return 0;
}

/* 기종을 켤 수 있는지 미리 검사 (요구 4). 기종은 바꾸지 않는다.
 * 반환: 0 켤 수 있음, 1 BIOS 파일 없음(miss에 파일 경로), 2 아직 지원하지 않는 VDP, 3 config.ini 없음·오류 */
int machine_check(MachineRead rd, const char *name, char *miss, int misslen)
{
    static char ccfg[16384];
    rd = emb_wrap(rd, name);
    static uint8_t probe[16];                          /* 있는지만 보면 되므로 앞 16바이트만 (2.11: 전에는 64 KiB씩 읽어 느렸음) */
    char path[512] = "0:\\Machines\\";
    put(path, sizeof path, name);
    put(path, sizeof path, "\\config.ini");
    uint32_t n = 0;
    miss[0] = 0;
    if (rd(path, (uint8_t *)ccfg, sizeof ccfg - 1, &n) || !n) { put(miss, misslen, "config.ini"); return 3; }
    if (n > sizeof ccfg - 1) n = sizeof ccfg - 1;
    static MachineProfile prof;
    char err[64];
    if (profile_parse(ccfg, n, &prof, err, sizeof err)) { put(miss, misslen, "config.ini"); return 3; }
    int tms = prof.vdp[0] == 'T' && prof.vdp[1] == 'M' && prof.vdp[2] == 'S';
    if (prof.vdp[0] && !tms && strcmp(prof.vdp, "V9938") && strcmp(prof.vdp, "V9958")) { put(miss, misslen, prof.vdp); return 2; }
    for (int i = 0; i < prof.region_count; i++) {
        ProfileRegion *r = &prof.region[i];
        if (!(r->slot == 0 && r->subslot == 0 && r->start_block == 0) || r->type == REGION_RAM ||
            r->type == REGION_RAM_MAPPER || r->type == REGION_KANJI) continue;
        char p1[512] = "0:\\", p2[512] = "0:\\Machines\\";
        int k = 3;
        for (const char *s = r->file; *s && k < (int)sizeof p1 - 1; s++) p1[k++] = (*s == '/') ? '\\' : *s;
        p1[k] = 0;
        const char *base = r->file;
        for (const char *s = r->file; *s; s++) if (*s == '/' || *s == '\\') base = s + 1;
        put(p2, sizeof p2, name); put(p2, sizeof p2, "\\"); put(p2, sizeof p2, base);
        uint32_t sz = 0;
        if (rd(p1, probe, sizeof probe, &sz) && rd(p2, probe, sizeof probe, &sz) && (!machine_rom_find || machine_rom_find(base, probe, sizeof probe, &sz))) {
            for (int j = 3; p1[j] && (int)strlen(miss) < misslen - 1; j++) { char t[2] = { p1[j], 0 }; put(miss, misslen, t); }
            return 1;
        }
    }
    return 0;
}

/* 4.0.28: 처음 켤 기종 고르기 — 기존 PC(DPC-200 롬이 있는 USB)는 그대로 DPC-200, 롬이 없으면 C-BIOS */
const char *machine_default_name(MachineRead rd)
{
    (void)rd;
    return FALLBACK_MACHINE;                           /* 4.0.34: 아무 상태도 없을 때 기본은 늘 MSX - C-BIOS */                           /* C-BIOS는 내장본이 있으므로 항상 켤 수 있음 */
}
