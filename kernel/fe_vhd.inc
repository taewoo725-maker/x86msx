/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fe_vhd.inc - 3.7.1 가상 하드 (USB VMFolder 아래 폴더 → MSX 하드 디스크 C~H). fe.c가 포함한다.
 *
 * 카트리지 "X86MSX 가상 디스크 (Nextor)" = Nextor 커널(USB Machines\Shared Roms의 Nextor-2.1.3.base.dat 또는 고른 Nextor 롬)
 *   + X86MSX 드라이버 뱅크(tools/vhddrv.asm → vhd_drv.h). 매퍼 MAPPER_VHD (LD (6000h),A로 16 KiB 뱅크).
 * 드라이버는 OUT (3Ah~3Fh)만 하고, 여기(vhd_io)가 Z80 레지스터·메모리를 직접 읽고 써서 결과를 돌려준다.
 *   장치 1 = 플로피 (LUN 1 = A, LUN 2 = B: 드라이브 A·B에 넣은 디스크 이미지), 장치 2~7 = 가상 하드 C~H (kernel/vfat.c).
 *   DRV_CONFIG(Nextor 2.0.5 이상): 부팅 때 드라이브 = A, B, C~(연결한 마지막 하드). 기종의 디스크 롬은 숨김(vhd_hide_fdc).
 * 쓴 내용은 2초 동안 쓰기가 없으면 USB 폴더에 반영 (vf_sync), 리셋·빼기·끝내기 때도. */

#include "vfat.h"
#include "vhd_drv.h"

#define VHD_N 6
#define VHD_ROOT FE_VMF
static Vf vhd_vol[VHD_N];
static char vhd_dir[VHD_N][64];                         /* VMFolder 아래 폴더 이름 (빈 = 연결 안 함) */
static int vhd_mb[VHD_N] = { 32, 32, 32, 32, 32, 32 }, vhd_kind[VHD_N];
static int vhd_up[VHD_N];                               /* 마운트됨 */
static uint8_t vhd_chg[8];                              /* 장치마다 "바뀜"을 한 번 알림 (DEV_STATUS 2) */
static uint32_t vhd_fsig[2];                            /* 플로피 이미지 서명 (바뀌면 vhd_chg[1]) */
static uint32_t vhd_wr_ms, vhd_rw_ms;
static int vhd_pool_ok;
static char vhd_err[64];

static int vhd_cart(void) { for (int n = 0; n < MSX_CART_ALL; n++) if (M->cart[n].present && M->cart[n].mapper == MAPPER_VHD) return n; return -1; }

/* ---- Nextor 롬 → 가상 디스크 롬 (cart_buf 안에서). 커널 뱅크 K개 + 드라이버 뱅크 ---- */
static int vhd_build_rom(uint8_t *b, uint32_t *size)
{
    uint32_t n = *size;
    int K = n >= 16384 ? b[0xFE] : 0;
    if (K < 2 || K > 30 || (uint32_t)K * 16384 > n) return -1;
    for (int k = 0; k < K; k++) if (b[(uint32_t)k * 16384 + 0xFF] != k || b[(uint32_t)k * 16384 + 0xFE] != K) return -1;   /* Nextor 뱅크 머리 */
    static const uint8_t chg[4] = { 0x32, 0x00, 0x60, 0xC9 };   /* CHGBNK: LD (6000h),A / RET */
    static const uint8_t sunrise[32] = { 0xC5,0xCB,0x3F,0xCB,0x10,0xCB,0x3F,0xCB,0x10,0xCB,0x3F,0xCB,0x10,0xCB,0x3F,0xCB,0x10,0xCB,0x3F,0xCB,0x10,0x78,0xC1,0x07,0x07,0x07,0xE6,0xF8,0x32,0x04,0x41,0xC9 };
    for (int k = 0; k < K; k++) {
        uint8_t *bk = b + (uint32_t)k * 16384;
        for (uint32_t i = 0; i + 32 <= 0x3FD0; i++)     /* Sunrise IDE 롬이면 안쪽 사본도 */
            if (bk[i] == 0xC5 && !memcmp(bk + i, sunrise, 32)) { memcpy(bk + i, chg, 4); memset(bk + i + 4, 0, 28); }
        memcpy(bk + 0x3FD0, chg, 4); memset(bk + 0x3FD4, 0xFF, 0x2C);
    }
    uint8_t *d = b + (uint32_t)K * 16384;
    memcpy(d, b, 0x100); d[0xFF] = (uint8_t)K;          /* 페이지 0 코드는 뱅크 0과 같게 */
    memset(d + 0x100, 0xFF, 16384 - 0x100);
    memcpy(d + 0x100, vhd_drv, sizeof vhd_drv);
    memcpy(d + 0x3FD0, chg, 4);
    *size = (uint32_t)(K + 1) * 16384;
    return 0;
}
static int is_nextor_rom(const uint8_t *b, uint32_t n)   /* 파일이 Nextor 롬(또는 base.dat)인가 */
{
    int K = n >= 32768 ? b[0xFE] : 0;
    if (K < 2 || K > 30 || (uint32_t)K * 16384 > n) return 0;
    for (int k = 0; k < K; k++) if (b[(uint32_t)k * 16384 + 0xFF] != k) return 0;
    return b[0] == 'A' && b[1] == 'B';
}

/* ---- USB 쪽 (vfat 콜백) ---- */
static FeDirEntry *vhd_lbuf(void) { return P.scratch && P.scratch_size >= (32u << 20) ? (FeDirEntry *)(void *)(P.scratch + (28u << 20)) : 0; }
static int vh_list(void *c, const char *path, VfHostEnt *out, int max)
{
    (void)c;
    FeDirEntry *e = vhd_lbuf();
    int (*ls)(const char *, FeDirEntry *, int) = P.list_dir_all ? P.list_dir_all : P.list_dir;
    if (!e || !ls) return -1;
    int lim = (int)((4u << 20) / sizeof(FeDirEntry)); if (lim > max) lim = max;
    int n = ls(path, e, lim), k = 0;
    if (n < 0) return -1;
    for (int i = 0; i < n; i++) {
        const char *nm = e[i].key[0] ? e[i].key : e[i].name;
        if (!strcmp(nm, ".") || !strcmp(nm, "..") || (nm[0] && nm[1] == ':')) continue;
        if (!name_cmp(nm, "X86MSX.VHD")) continue;   /* 3.7.2: 가상 하드 설정 파일은 MSX에 안 보임 */
        copy_str(out[k].name, sizeof out[k].name, nm);
        out[k].is_dir = e[i].is_dir; out[k].size = e[i].size;
        out[k].date = e[i].year >= 1980 ? (uint16_t)((e[i].year - 1980) << 9 | e[i].mon << 5 | e[i].day) : 0;
        out[k].time = e[i].year >= 1980 ? (uint16_t)(e[i].hour << 11 | e[i].min << 5) : 0;
        k++;
    }
    return k;
}
static int vh_read_at(void *c, const char *path, uint32_t off, uint8_t *buf, uint32_t len) { (void)c; return P.read_at ? P.read_at(path, off, buf, len) : -1; }
static int vh_write(void *c, const char *path, const uint8_t *d, uint32_t len)
{
    (void)c;
    if (!P.write_file) return -1;
    if (!P.write_file(path, d, len)) return 0;
    if (P.create && !P.create(path, 0) && !P.write_file(path, d, len)) return 0;   /* 새 파일 */
    return -1;
}
static int vh_mkdir(void *c, const char *path) { (void)c; return P.create ? P.create(path, 1) : -1; }
static int vh_del(void *c, const char *path) { (void)c; return P.delete_file ? P.delete_file(path) : -1; }
static const VfHost vhd_host = { 0, vh_list, vh_read_at, vh_write, vh_mkdir, vh_del };

static void vhd_path(int i, char *out, int max) { copy_str(out, max, VHD_ROOT "\\"); str_append(out, max, vhd_dir[i]); }

/* 바뀐 것을 USB에 (모든 하드). 실패하면 알림 (이유 포함) */
static void vhd_sync_all(void)
{
    for (int i = 0; i < VHD_N; i++) {
        if (!vhd_up[i] || !vhd_vol[i].dirty || !P.scratch) continue;
        char err[32], d[48];
        err[0] = 0;
        if (vf_sync(&vhd_vol[i], P.scratch, 28u << 20, err, sizeof err) < 0) {
            copy_str(vhd_err, sizeof vhd_err, err);
            copy_str(d, sizeof d, vhd_dir[i]); str_append(d, sizeof d, " "); str_append(d, sizeof d, err);
            toast(T(S_VHD_SYNC_FAIL), d);
        }
    }
}
static void vhd_unmount_all(void)
{
    vhd_sync_all();
    for (int i = 0; i < VHD_N; i++) if (vhd_up[i]) { vf_unmount(&vhd_vol[i]); vhd_up[i] = 0; }
}
/* 연결한 폴더를 모두 다시 마운트 (실패한 것은 알림) */
static void vhd_mount_all(int quiet)
{
    vhd_unmount_all();
    if (!P.vhd_mem || P.vhd_mem_size < (VHD_N + 2) * VF_ARENA) return;
    if (!vhd_pool_ok) { vf_pool_init(P.vhd_mem + VHD_N * VF_ARENA, P.vhd_mem_size - VHD_N * VF_ARENA); vhd_pool_ok = 1; }
    for (int i = 0; i < VHD_N; i++) {
        if (!vhd_dir[i][0]) continue;
        char p[160], err[32];
        vhd_path(i, p, sizeof p);
        if (vf_mount(&vhd_vol[i], &vhd_host, p, (uint32_t)vhd_mb[i], vhd_kind[i], i + 1, P.vhd_mem + (uint32_t)i * VF_ARENA, err, sizeof err)) {
            if (!quiet) toast(!strcmp(err, "full") ? T(S_VHD_FULL) : !strcmp(err, "root") ? T(S_VHD_ROOTFULL) : T(S_VHD_MOUNT_FAIL), vhd_dir[i]);
            continue;
        }
        vhd_up[i] = 1;
        vhd_chg[2 + i] = 1;
        if (vhd_vol[i].trunc && !quiet) toast(T(S_VHD_TRUNC), vhd_dir[i]);   /* 3.9.12 */
    }
}

static void vhd_img_chg(void) { vhd_chg[2] = 1; }
static int vhd_last(void) { int last = 0; for (int i = 0; i < VHD_N; i++) if (vhd_up[i]) last = i + 1; return last; }
/* ---- 하이퍼콜 ---- */
enum { E_OK = 0, E_NCOMP = 0xFF, E_WRERR = 0xFE, E_DISK = 0xFD, E_NRDY = 0xFC, E_RNF = 0xF9, E_WPROT = 0xF8, E_IDEVL = 0xB5 };
static int vhd_dos1boot;                                /* DOS1 모드 부팅을 맡은 하드 장치 (0 = 없음) */
static int vhd_dev_ok(int dev, int lun)
{
    if (dev == 1) return lun == 1 || lun == 2;
    if (vhd_img && dev == 2) return lun == 1;            /* 하드 디스크 이미지 = 장치 2 (C:). 폴더 가상 하드와는 함께 못 씀 (Nextor 장치 번호는 7까지) */
    if (dev >= 2 && dev <= 7) return lun == 1 && vhd_up[dev - 2];
    return 0;
}
static uint32_t vhd_secs(int dev, int lun)
{
    if (dev == 1) return disk_size[lun - 1] / 512;
    if (vhd_img && dev == 2) return vhd_img_n ? vhd_img_n : hd_secs;
    return vhd_vol[dev - 2].total;
}
static int vhd_sector(int dev, int lun, uint32_t lba, uint8_t *b, int wr)
{
    if (dev == 1) {
        int d = lun - 1;
        if (!disk_size[d]) return E_NRDY;
        if ((uint64_t)lba * 512 + 512 > disk_size[d]) return E_RNF;
        if (!wr) { memcpy(b, disk_buf[d] + lba * 512, 512); return E_OK; }
        if (disk_wp[d]) return E_WPROT;
        memcpy(disk_buf[d] + lba * 512, b, 512);
        fdc_mark(&M->fdc.drv[d], lba * 512, 512);
        return E_OK;
    }
    if (vhd_img && dev == 2) {
        if (lba >= (vhd_img_n ? vhd_img_n : hd_secs)) return E_RNF;
        if (!wr) return hd_rd_cb((void *)0, lba + vhd_img_off, b) ? E_DISK : E_OK;
        vhd_wr_ms = P.ms();
        return hd_wr_cb((void *)0, lba + vhd_img_off, b) ? E_WRERR : E_OK;
    }
    Vf *v = &vhd_vol[dev - 2];
    if (lba >= v->total) return E_RNF;
    if (!wr) return vf_read(v, lba, b) ? E_DISK : E_OK;
    vhd_wr_ms = P.ms();
    return vf_write(v, lba, b) ? E_WRERR : E_OK;
}
static void vhd_io(void *ctx, MSX *m, int port)
{
    (void)ctx;
    Z80 *z = &m->cpu;
    int a = z->af.b.h, b = z->bc.b.h, c = z->bc.b.l;
    uint16_t hl = z->hl.w, de = z->de.w;
#ifdef VHD_TRACE
    fprintf(stderr, "vhd %02X a=%d b=%d c=%d f=%02X pc=%04X\n", port, a, b, c, z->af.b.l, z->pc);
#endif
    switch (port) {
    case 0x3A: if (a == 0) vhd_dos1boot = 0; break;     /* DRV_INIT */
    case 0x3B: {                                        /* DRV_CONFIG */
        if (a == 1) {                                   /* 부팅 때 드라이브 수 = A, B + 마지막으로 연결한 하드까지 */
            uint32_t mseg = (m->mram_size > m->xram_size ? m->mram_size : m->xram_size) >> 14;   /* Nextor는 매퍼 세그먼트가 6개 미만이면 DOS1 모드 */
            int few_segs = mseg < 6;
#ifdef VHD_TRACE
            fprintf(stderr, "DRVCFG mseg %u up0 %d kind %d has %d dsz %d\n", (unsigned)mseg, vhd_up[0], vhd_vol[0].kind, vhd_vol[0].dos1geo, (int)disk_size[0]);
#endif
            vhd_dos1boot = 0;                           /* DOS1 모드는 A:로만 부팅: 플로피 A가 비었으면 첫 DOS1 하드를 A:로, 플로피 A를 B:로 */
            if (b == 1 && !disk_size[0]) for (int i = 0; i < VHD_N && !vhd_dos1boot; i++) if (vhd_up[i] && (vhd_vol[i].kind == VF_DOS1 || (vhd_vol[i].kind == VF_FAT12 && vhd_vol[i].dos1geo && few_segs))) vhd_dos1boot = 2 + i;
            int last = 0;
            for (int i = 0; i < VHD_N; i++) if (vhd_up[i]) last = i + 1;
            z->af.b.h = 0; z->bc.b.h = (uint8_t)(vhd_img ? 3 : 2 + last);
        } else if (a == 2) {                            /* C번째 드라이브 → 장치·LUN */
            if (vhd_dos1boot && c == 0) { z->af.b.h = 0; z->bc.b.h = (uint8_t)vhd_dos1boot; z->bc.b.l = 1; }
            else if (vhd_dos1boot && c == 1) { z->af.b.h = 0; z->bc.b.h = 1; z->bc.b.l = 1; }
            else if (vhd_dos1boot && c == vhd_dos1boot) z->af.b.h = 1;   /* A:로 옮긴 하드의 원래 자리는 비움 */
            else if (c < 2) { z->af.b.h = 0; z->bc.b.h = 1; z->bc.b.l = (uint8_t)(c + 1); }
            else if (c < 2 + VHD_N) { z->af.b.h = 0; z->bc.b.h = (uint8_t)c; z->bc.b.l = 1; }
            else z->af.b.h = 1;
        } else z->af.b.h = 1;
        break;
    }
    case 0x3C: {                                        /* DEV_RW */
        int wr = z->af.b.l & 1, done = 0, err = E_OK;
        if (!vhd_dev_ok(a, c)) { z->af.b.h = E_IDEVL; z->bc.b.h = 0; break; }
        uint32_t lba = (uint32_t)msx_mem_read(m, de) | (uint32_t)msx_mem_read(m, (uint16_t)(de + 1)) << 8 |
                       (uint32_t)msx_mem_read(m, (uint16_t)(de + 2)) << 16 | (uint32_t)msx_mem_read(m, (uint16_t)(de + 3)) << 24;
        uint8_t sec[512];
#ifdef VHD_TRACE
        fprintf(stderr, "  rw dev %d lun %d lba %u n %d %s\n", a, c, lba, b, wr ? "W" : "R");
#endif
        for (; done < b; done++, lba++) {
            if (wr) for (int i = 0; i < 512; i++) sec[i] = msx_mem_read(m, (uint16_t)(hl + i));
            err = vhd_sector(a, c, lba, sec, wr);
            if (err) break;
            if (!wr) for (int i = 0; i < 512; i++) msx_mem_write(m, (uint16_t)(hl + i), sec[i]);
            hl = (uint16_t)(hl + 512);
        }
        vhd_rw_ms = P.ms();
        z->af.b.h = (uint8_t)err; z->bc.b.h = (uint8_t)done;
        break;
    }
    case 0x3D:                                          /* DEV_INFO: 0 = 기본 정보 (LUN 수, 0) */
        if (a < 1 || a > 7 || (a >= 2 && !vhd_up[a - 2] && !(vhd_img && a == 2)) || b != 0) { z->af.b.h = 1; break; }
        msx_mem_write(m, hl, a == 1 ? 2 : 1); msx_mem_write(m, (uint16_t)(hl + 1), 0);
        z->af.b.h = 0;
        break;
    case 0x3E:                                          /* DEV_STATUS: 0 없음, 1 그대로, 2 바뀜 */
        if (!vhd_dev_ok(a, b ? b : 1)) { z->af.b.h = 0; break; }
        if (a == 1) {                                   /* 플로피: 넣은 이미지가 바뀌었는가 */
            int d = (b ? b : 1) - 1;
            uint32_t sg = 0;
            if (disk_size[d]) { sg = 2166136261u ^ disk_size[d]; for (const char *q = disk_path[d][0] ? disk_path[d] : disk_name[d]; *q; q++) sg = (sg ^ (uint8_t)*q) * 16777619u; sg |= 1; }
            z->af.b.h = sg != vhd_fsig[d] ? 2 : 1;
            vhd_fsig[d] = sg;
            break;
        }
        z->af.b.h = vhd_chg[a] ? 2 : 1;
        vhd_chg[a] = 0;
        break;
    case 0x3F: {                                        /* LUN_INFO: 12바이트 */
        if (!vhd_dev_ok(a, b)) { z->af.b.h = 1; break; }
        uint8_t t[12] = { 0 };
        uint32_t n = vhd_secs(a, b);
        t[0] = 0; t[1] = 0x00; t[2] = 0x02;             /* 블록 장치, 섹터 512 */
        t[3] = (uint8_t)n; t[4] = (uint8_t)(n >> 8); t[5] = (uint8_t)(n >> 16); t[6] = (uint8_t)(n >> 24);
        t[7] = a == 1 ? 0x05 : 0x00;                    /* 플로피: 빼고 넣는 매체 + 플로피 */
        for (int i = 0; i < 12; i++) msx_mem_write(m, (uint16_t)(hl + i), t[i]);
        z->af.b.h = 0;
        break;
    }
    }
}

/* 매 프레임: 하이퍼콜 연결, 디스크 롬 숨김, 쉬면 USB에 반영 */
static void vhd_frame(uint32_t now)
{
    M->vhd_io = vhd_io; M->vhd_ctx = 0;
    M->vhd_hide_fdc = vhd_cart() >= 0;
    int dirty = 0;
    for (int i = 0; i < VHD_N; i++) if (vhd_up[i] && vhd_vol[i].dirty) dirty = 1;
    if (dirty && now - vhd_wr_ms >= 2000) vhd_sync_all();
}

/* 기본 폴더 (없으면 만듦, 시작할 때마다): 4.0.13 Storage Device\\{DISK(DirFolder·TEMP)·VMFolder(MSXDOSA·MSXDOS2A·NEXTORA)·CAS·HARDDISK·BASIC·ROMS}와
 * States·SRAM·Screenshots·Audio·Video·Printer·SoundFont·Databases·Icons·Keyboard Config·Machines\\Shared Roms (위 폴더가 먼저 있어야 아래를 만듦) */
static void vhd_make_dirs(void)
{
    if (!P.create) return;
    static const char *const d[] = {
        FE_STORE, FE_DISK, FE_DISK "\\DirFolder", FE_DISK "\\TEMP", VHD_ROOT, VHD_ROOT "\\MSXDOSA", VHD_ROOT "\\MSXDOS2A", VHD_ROOT "\\NEXTORA",
        FE_CAS, FE_HDD, FE_BAS, FE_ROMS,
        "0:\\States", "0:\\SRAM", "0:\\Screenshots", "0:\\Audio", "0:\\Video", "0:\\Printer", "0:\\MidiSound", "0:\\MidiSound\\SoundFont", "0:\\MidiSound\\MT32", "0:\\MidiSound\\SC55", "0:\\Databases", "0:\\Icons", "0:\\Keyboard Config",
        "0:\\Machines", "0:\\Machines\\Shared Roms" };
    for (unsigned i = 0; i < sizeof d / sizeof d[0]; i++) P.create(d[i], 1);
}

/* ---- 설정 (ini) ---- */
static void vhd_save(char *b)
{
    for (int i = 0; i < VHD_N; i++) {
        char k[16] = "VhdDir"; num_append(k, sizeof k, (uint32_t)(i + 1), 0, ' ');
        ini_put(b, k, vhd_dir[i]);
        char v[16] = ""; num_append(v, sizeof v, (uint32_t)vhd_mb[i], 0, ' '); str_append(v, sizeof v, ","); num_append(v, sizeof v, (uint32_t)vhd_kind[i], 0, ' ');
        copy_str(k, sizeof k, "VhdSize"); num_append(k, sizeof k, (uint32_t)(i + 1), 0, ' ');
        ini_put(b, k, v);
    }
}
static int vhd_load_key(const char *k, const char *v)   /* 처리했으면 1 */
{
    if (!strncmp(k, "VhdDir", 6) && k[6] >= '1' && k[6] <= '6' && !k[7]) { copy_str(vhd_dir[k[6] - '1'], sizeof vhd_dir[0], v); return 1; }
    if (!strncmp(k, "VhdSize", 7) && k[7] >= '1' && k[7] <= '6' && !k[8]) {
        int i = k[7] - '1';
        long mb = ini_long(v);
        const char *cm = v; while (*cm && *cm != ',') cm++;
        int kd = *cm ? (int)ini_long(cm + 1) : 0;
        vhd_kind[i] = kd >= VF_AUTO && kd <= VF_FAT16 ? kd : VF_AUTO;
        vhd_mb[i] = mb >= 1 && mb <= 4095 ? (int)mb : 32;
        return 1;
    }
    return 0;
}

void fe_test_vhd(const char *dirs, int slot)               /* 3.7.1: 시험 틀 "NEXTORA,MSXDOS2A" → C, D */
{
    for (int i = 0; i < VHD_N; i++) vhd_dir[i][0] = 0;
    for (int i = 0; i < VHD_N && *dirs; i++) {
        int k = 0; while (*dirs && *dirs != ',') { if (k < 63) vhd_dir[i][k++] = *dirs; dirs++; }
        vhd_dir[i][k] = 0; if (*dirs == ',') dirs++;
        if (!strcmp(vhd_dir[i], "-")) vhd_dir[i][0] = 0;
    }
#ifdef PC_TRACE
    if (getenv("VHDKIND")) for (int i = 0; i < VHD_N; i++) { vhd_kind[i] = atoi(getenv("VHDKIND")); vhd_mb[i] = getenv("VHDMB") ? atoi(getenv("VHDMB")) : 8; }
#endif
    special_apply(slot, 49, 0);
}

/* ---- 3.7.2 가상 하드 고르기: 파일 → 미디어 → 하드 디스크 ▶ C:~H: → VMFolder 폴더 창 (파일 창 browse_slot 20~25) ----
 * Enter 폴더 열기, Backspace 위 폴더 (VMFolder 위로는 안 감), Del 이 드라이브 연결 끊기.
 * Ins: 하드 폴더(설정 파일이 있음) → 그 값으로 고치기 / 보통 폴더 → "하드 폴더로 만들까요?" (Enter 이 폴더, N 새 가상 하드)
 *      / 폴더가 없거나 ".." → 새 가상 하드 (이름·용량·형식).
 * 저장 = 폴더에 설정 파일(X86MSX.VHD, MSX 쪽에는 보이지 않음)을 쓰고 그 드라이브에 연결, 창을 닫음 (삽입 후 리셋 설정대로). */
#define VHD_CFG "X86MSX.VHD"
static const uint16_t vw_sizes[] = { 1, 2, 4, 8, 15, 16, 24, 32, 48, 64, 100, 128, 200, 256, 500, 512, 1000, 1024, 2000, 2047, 3000, 4095 };
#define VW_NS ((int)(sizeof vw_sizes / sizeof vw_sizes[0]))
static int16_t vb_mb[DIR_MAX];                          /* 목록 폴더마다 설정 파일의 용량 (0 = 하드 폴더 아님) */
static int8_t vb_kind[DIR_MAX];
static int vb_new, vb_field;           /* 묻기 창, 설정 창, 새로 만들기, 고른 칸 (0 이름 1 용량 2 형식) */
static char vb_name[41], vb_path[PATH_MAX_FE];
static int vb_mbv = 32, vb_kindv = VF_AUTO; static uint32_t vb_fy;
static void vb_dd_open(void)                            /* 형식 풀다운 (자동·DOS1·FAT12·FAT16) */
{
    static const StrId nm[4] = { S_VHD_AUTO, S_VHD_DOS1, S_VHD_FAT12, S_VHD_FAT16 };
    dd_n = 4; dd_sel = vb_kindv & 3; dd_top = 0; dd_id = 3001; dd_on = 1;
    for (int i = 0; i < 4; i++) { dd_hd[i] = 0; dd_val[i] = i; copy_str(dd_txt[i], sizeof dd_txt[i], T(nm[i])); }
}
static void vb_dd_apply(int i) { vb_kindv = dd_val[i]; }
static void hc_vbfmt(const UiHit *h, int ev) { (void)h; vb_field = 2; menu_dirty = 1; if (ev == 1) vb_dd_open(); }

static int vhd_cfg_read(const char *dir, int *mb, int *kind)   /* 하드 폴더면 1 */
{
    char p[PATH_MAX_FE], b[128]; uint32_t n = 0;
    copy_str(p, sizeof p, dir); str_append(p, sizeof p, "\\" VHD_CFG);
    if (!P.read_file || P.read_file(p, (uint8_t *)b, sizeof b - 1, &n) || n > sizeof b - 1) return 0;
    b[n] = 0;
    int m = 32, k = VF_AUTO;
    for (char *q = b; *q; q++) {
        if (!strncmp(q, "Size=", 5)) m = (int)ini_long(q + 5);
        if (!strncmp(q, "Format=", 7)) k = (int)ini_long(q + 7);
    }
    *mb = m >= 1 && m <= 4095 ? m : 32;
    *kind = k >= VF_AUTO && k <= VF_FAT16 ? k : VF_AUTO;
    return 1;
}
static int vhd_cfg_write(const char *dir, int mb, int kind)
{
    char p[PATH_MAX_FE], b[64] = "Size=";
    copy_str(p, sizeof p, dir); str_append(p, sizeof p, "\\" VHD_CFG);
    num_append(b, sizeof b, (uint32_t)mb, 0, ' '); str_append(b, sizeof b, "\r\nFormat="); num_append(b, sizeof b, (uint32_t)kind, 0, ' '); str_append(b, sizeof b, "\r\n");
    return vh_write(0, p, (const uint8_t *)b, (uint32_t)strlen(b));
}
static int vb_drv(void) { return browse_slot - 20; }
static const char *vb_rel(const char *full)             /* "0:\VMFolder\A\B" → "A\B" (VMFolder 밖이면 0) */
{
    int n = (int)strlen(VHD_ROOT);
    if (strncmp(full, VHD_ROOT, (size_t)n) || full[n] != '\\') return 0;
    return full + n + 1;
}
static int vb_kind_eff(void)
{
    if (vb_kindv != VF_AUTO) return vb_kindv;
    return vb_new ? VF_FAT16 : vf_auto_kind(&vhd_host, vb_path, (uint32_t)vb_mbv);
}
static void vb_clamp(void)
{
    int k = vb_kind_eff();
    if ((uint32_t)vb_mbv > vf_max_mb(k)) vb_mbv = (int)vf_max_mb(k);
    if ((uint32_t)vb_mbv < vf_min_mb(k)) vb_mbv = (int)vf_min_mb(k);
}
/* 폴더를 다시 읽을 때 (load_dir): 폴더마다 설정 파일 */
static void vb_scan(void)
{
    for (int i = 0; i < nent && i < DIR_MAX; i++) {
        int m = 0, k = 0;
        vb_mb[i] = 0; vb_kind[i] = 0;
        if (!entries[i].is_dir) continue;
        char p[PATH_MAX_FE]; entry_path(i, p, sizeof p);
        if (vhd_cfg_read(p, &m, &k)) { vb_mb[i] = (int16_t)m; vb_kind[i] = (int8_t)k; }
    }
}
static int vb_letter_of(const char *full)              /* 이 폴더가 연결된 드라이브 (0~5, 없으면 -1) */
{
    const char *r = vb_rel(full);
    if (!r) return -1;
    for (int i = 0; i < VHD_N; i++) if (vhd_dir[i][0] && !name_cmp(vhd_dir[i], r)) return i;
    return -1;
}
static void open_vhd_browser(int drv)
{
    if ((hd_secs || hd2_secs) && !vhd_dir[drv][0]) { toast(T(S_VHD_EXCL_HD), 0); return; }   /* 3.8.2 */
    if (P.create) P.create(VHD_ROOT, 1);
    open_browser(20 + drv);
    vb_ask = vb_form = 0;
    if (vhd_dir[drv][0]) {                              /* 연결된 폴더가 VMFolder 바로 아래면 그것을 고른 채로 */
        for (int i = 0; i < nlist; i++) if (list[i] >= 0 && !name_cmp(entries[list[i]].key[0] ? entries[list[i]].key : entries[list[i]].name, vhd_dir[drv])) menu_sel = i;
    }
}
static void vb_form_open(int isnew, const char *path, int mb, int kind)
{
    vb_new = isnew; vb_form = 1; vb_ask = 0;
    vb_field = isnew ? 0 : 1;
    copy_str(vb_path, sizeof vb_path, path);
    if (isnew) copy_str(vb_name, sizeof vb_name, "NEWHARD");
    else copy_str(vb_name, sizeof vb_name, path_base(path));
    vb_mbv = mb; vb_kindv = kind;
    vb_clamp();
}
/* 드라이브에 연결 (설정 저장 → 카트리지가 없으면 빈 슬롯에 → 삽입 후 리셋 설정대로) */
static void vhd_connect(int i)
{
    settings_changed();
    int n = vhd_cart();
    if (n < 0) {
        n = free_cart_slot();                           /* 3.9.1: 마지막 빈 슬롯 */
        if (n < 0) { toast(T(S_NO_FREE_SLOT), 0); return; }
        if (special_apply(n, 49, 0)) return;
        if (vhd_reset_insert) { power_on(); if (run_state != RUN_ON) vhd_mount_all(0); } else { vhd_mount_all(0); full_redraw(); }   /* 3.9.8: 삽입 후 다시 시작 설정 */
    } else if (vhd_reset_insert) power_on();
    else vhd_mount_all(0);
    toast(T(S_VHD_CONN), vhd_dir[i]);
}
static void vb_save(void)
{
    int i = vb_drv();
    char full[PATH_MAX_FE];
    if (vb_new) {
        if (!vb_name[0]) return;
        copy_str(full, sizeof full, cur_path); str_append(full, sizeof full, "\\"); str_append(full, sizeof full, vb_name);
        for (int e = 0; e < nent; e++) if (!name_cmp(entries[e].key[0] ? entries[e].key : entries[e].name, vb_name)) { toast(T(S_VHD_EXISTS), vb_name); return; }
        if (!P.create || P.create(full, 1)) { toast(T(S_DIR_MKFAIL), vb_name); return; }
    } else copy_str(full, sizeof full, vb_path);
    const char *rel = vb_rel(full);
    if (!rel || strlen(rel) >= sizeof vhd_dir[0]) { toast(T(S_VHD_LONG), full); return; }
    { int l = vb_letter_of(full); if (l >= 0 && l != i) { toast(T(S_VHD_TAKEN), vhd_dir[l]); return; } }   /* 4.0.30 */
    vb_clamp();
    if (vhd_cfg_write(full, vb_mbv, vb_kindv)) { toast(T(S_DIR_MKFAIL), path_base(full)); return; }
    copy_str(vhd_dir[i], sizeof vhd_dir[0], rel);
    vhd_mb[i] = vb_mbv; vhd_kind[i] = vb_kindv;
    vb_form = 0;
    dialog_done();
    vhd_connect(i);
}
/* 파일 창 키 (browse_vhd일 때). 처리했으면 1 */
static int vb_key_i(int key)
{
    menu_dirty = 1;
    if (vb_ask) {                                       /* 하드 폴더로 만들까요? */
        if (key == 0x31) { vb_form_open(1, cur_path, 32, VF_AUTO); return 1; }   /* N: 새 가상 하드 */
        if (key == 0x1C || key == 0x9C) { int m = 32; vb_form_open(0, vb_path, m, VF_AUTO);
            int fm = vf_folder_mb(&vhd_host, vb_path);       /* 파일보다 크게 */
            if (fm >= 0 && vb_mbv <= fm) { int k = 0; while (k < VW_NS - 1 && vw_sizes[k] <= fm) k++; vb_mbv = vw_sizes[k]; vb_clamp(); } }
        else if (key == 0x01 || key == 0x0E) vb_ask = 0;
        return 1;
    }
    if (vb_form) {
        if (dd_on) { dd_key_event(key); if (!dd_on) vb_clamp(); menu_dirty = 1; return 1; }   /* 5.0.38 */
        switch (key) {
        case 0x01: vb_form = 0; return 1;                       /* 취소 */
        case 0x1C: if (vb_field == 2) { vb_dd_open(); return 1; } vb_save(); return 1;   /* 형식 줄에서 Enter = 풀다운 */
        case 0x9C: vb_save(); return 1;                         /* 저장 (단추) */
        case 0xC8: if (vb_field > (vb_new ? 0 : 1)) vb_field--; return 1;
        case 0xD0: case 0x0F: vb_field = vb_field >= 2 ? (vb_new ? 0 : 1) : vb_field + 1; return 1;
        case 0xCB: case 0xCD: {
            int d = key == 0xCD ? 1 : -1;
            if (vb_field == 2) { }
            else if (vb_field == 1) {
                if (mods & 0x03) vb_mbv += d;                       /* Shift: 1 MB씩 */
                else {
                    int k = 0;
                    if (d > 0) { while (k < VW_NS - 1 && vw_sizes[k] <= vb_mbv) k++; }
                    else { k = VW_NS - 1; while (k > 0 && vw_sizes[k] >= vb_mbv) k--; }
                    vb_mbv = vw_sizes[k];
                }
                vb_clamp();
            }
            return 1;
        }
        case 0x0E: if (vb_field == 0) { int l = (int)strlen(vb_name); if (l) vb_name[l - 1] = 0; } return 1;
        default:
            if (vb_field == 0) {
                char c = key_char(key, (mods & 0x03) != 0);
                int l = (int)strlen(vb_name);
                if (c && c != ' ' && c != '\\' && c != '/' && c != ':' && c != '*' && c != '?' && c != '"' && c != '<' && c != '>' && c != '|' && c != '.' && l < 40) { vb_name[l] = c; vb_name[l + 1] = 0; }
            }
            return 1;
        }
    }
    int it = nlist ? list[menu_sel] : ITEM_UP;
    switch (key) {
    case 0x1C: case 0x9C:                               /* Enter: 폴더 열기 ([새 가상 하드]는 설정 창) */
        if (!nlist) return 1;
        if (it == ITEM_UP) go_up();
        else if (entries[it].is_dir) enter_dir(it);
        return 1;
    case 0xD2: {                                        /* Ins */
        if (!nlist || it < 0 || !entries[it].is_dir) { vb_form_open(1, cur_path, 32, VF_AUTO); return 1; }   /* 폴더가 없거나 ".." = 새로 */
        char p[PATH_MAX_FE]; entry_path(it, p, sizeof p);
        { int l = vb_letter_of(p); if (l >= 0 && l != vb_drv()) { toast(T(S_VHD_TAKEN), vhd_dir[l]); return 1; } }   /* 4.0.30: 다른 드라이브에 이미 잡힌 폴더는 못 씀 */
        if (vb_mb[it]) vb_form_open(0, p, vb_mb[it], vb_kind[it]);
        else { copy_str(vb_path, sizeof vb_path, p); vb_ask = 1; }
        return 1;
    }
    case 0xD3: {                                        /* Del: 이 드라이브 연결 끊기 */
        int i = vb_drv();
        if (!vhd_dir[i][0]) return 1;
        char nm[64]; copy_str(nm, sizeof nm, vhd_dir[i]);
        vhd_dir[i][0] = 0;
        settings_changed();
        if (vhd_cart() >= 0) {
            if (!vhd_any()) {                           /* 3.9.4: 가상 하드를 모두 해제하면 X86MSX VHD 카트리지도 뺌 */
                int c = vhd_cart();
                cart_remove(c);
                cart_special[c] = 0; cart_path[c][0] = 0; cart_name[c][0] = 0; cart_mapper[c] = -1; cart_size[c] = 0;
            } else vhd_mount_all(1);
        }
        if (vhd_reset_insert) power_on();               /* 3.9.10: 연결 끊기 뒤에도 "연결, 연결 끊기 후 리셋" 설정대로 */
        toast(T(S_VHD_DISC), nm);
        browse_full = 1;
        return 1;
    }
    case 0xCD: if (nlist && it >= 0 && entries[it].is_dir) enter_dir(it); return 1;
    case 0xCB: go_up(); return 1;
    default: return 0;
    }
}
static int vb_key(int key)                              /* 4.0.29: 팝업(묻기·설정)이 닫히면 창을 다시 그려 잔상 지움 */
{
    int was = vb_ask || vb_form;
    int r = vb_key_i(key);
    if (was && !(vb_ask || vb_form)) { browse_full = 1; full_redraw(); }
    return r;
}
/* 줄 끝 정보: 연결된 드라이브, 하드 폴더 용량·형식 */
static void vb_row_info(int it, uint32_t xt, uint32_t xs, uint32_t xe, uint32_t y, uint32_t rh, int sel)
{
    if (it < 0 || !entries[it].is_dir) return;
    char p[PATH_MAX_FE]; entry_path(it, p, sizeof p);
    int l = vb_letter_of(p);
    if (l >= 0) {
        char dl[4] = { (char)('C' + l), ':', 0 };
        uint32_t cw = text_w(dl) + 14 * S, ch = LH;
        ui_rrect(xt, y + (rh - ch) / 2, cw, ch, 5 * S, l == vb_drv() ? C_OK : C_CHIP);
        text_f(F, S, xt + 7 * S, ui_ty(y, rh), dl, l == vb_drv() ? 1 : C_TEXT, 0xFF, 0, 1);
    }
    if (vb_mb[it]) {
        char t[48] = "";
        int k = vb_kind[it];
        str_append(t, sizeof t, T(S_VHD_TAG)); str_append(t, sizeof t, "  ");
        num_append(t, sizeof t, (uint32_t)vb_mb[it], 0, ' '); str_append(t, sizeof t, " MB  ");
        str_append(t, sizeof t, k == VF_DOS1 ? "DOS1" : k == VF_FAT12 ? "FAT12" : k == VF_FAT16 ? "FAT16" : T(S_VHD_AUTO));
        uint32_t tw = ui_sw(t);
        text_f(SF, 1, xe > tw ? xe - tw : xs, ui_sty(y, rh), t, sel ? 15 : C_ACC, 0xFF, 0, 1);
    }
}
/* 묻기·설정 팝업 */
static void vb_draw_pop(uint32_t X, uint32_t W, uint32_t py0)
{
    if (!vb_ask && !vb_form) return;
    uint32_t pw = 640 * S; if (pw > W) pw = W;
    uint32_t px = X + (W - pw) / 2, rh = LH + 14 * S;
    if (vb_ask) {
        uint32_t ph = 3 * LH + ui_cmdbar_h() + 66 * S;
        ui_rrect(px + 5 * S, py0 + 6 * S, pw, ph, 10 * S, C_SHADOW);
        ui_rframe(px, py0, pw, ph, 10 * S, C_ACC, C_CARD);
        uint32_t ty = py0 + 12 * S;
        text_f(F, S, px + 16 * S, ty, T(S_VHD_MAKE_ASK), C_TEXT, 0xFF, 0, 1);
        ty += LH + 10 * S;
        draw_icon(px + 16 * S, ty + LH / 8, 1, C_CARD);
        text_ell(px + 22 * S + LH, ty, path_base(vb_path), C_WARN, 0xFF, pw - 44 * S - LH);
        ty += LH + 10 * S;                              /* 4.0.29: [이 폴더로] [신규] [취소] 단추 (누르거나 Enter / N / Esc) */
        {
            const char *bl[3] = { T(S_VHD_BTN_THIS), T(S_VHD_BTN_NEW), T(S_VHD_CANCEL) };
            static const int bk[3] = { 0x1C, 0x31, 0x01 };
            uint32_t bh = LH + 12 * S, bx = px + 16 * S;
            for (int i = 0; i < 3; i++) {
                uint32_t bw = text_w(bl[i]) + 32 * S;
                ui_rframe(bx, ty, bw, bh, 7 * S, i == 0 ? C_ACC : C_LINE, i == 0 ? C_ACC : C_FIELD);
                text_f(F, S, bx + 16 * S, ui_ty(ty, bh), bl[i], i == 0 ? 15 : C_TEXT, 0xFF, 0, 1);
                hit_add(hc_key, bk[i], 0, 0, (uint16_t)bk[i], bx, ty, bw, bh);
                bx += bw + 10 * S;
            }
        }
        ui_cmdbar(px + S, py0 + ph - ui_cmdbar_h() - 6 * S, pw - 2 * S, T(S_VHD_ASK_KEYS));
        return;
    }
    int rows = vb_new ? 3 : 2;
    uint32_t ph = LH + SLH + 30 * S + (uint32_t)rows * (rh + 8 * S) + LH + 24 * S + SLH + 10 * S + ui_cmdbar_h() + 10 * S;
    ui_rrect(px + 5 * S, py0 + 6 * S, pw, ph, 10 * S, C_SHADOW);
    ui_rframe(px, py0, pw, ph, 10 * S, C_ACC, C_CARD);
    uint32_t ty = py0 + 12 * S;
    char tt[64]; copy_str(tt, sizeof tt, T(S_VHD_TITLE)); str_append(tt, sizeof tt, " "); { char dl[4] = { (char)('C' + vb_drv()), ':', 0 }; str_append(tt, sizeof tt, dl); }
    text_f(F, S, px + 16 * S, ty, tt, C_TEXT, 0xFF, 0, 1);
    ty += LH + 4 * S;
    small_ell(px + 16 * S, ty, vb_new ? cur_disp : path_base(vb_path), C_MUTED, C_CARD, pw - 32 * S);
    ty += SLH + 14 * S;
    uint32_t lx = px + 16 * S, vx = px + 16 * S + 120 * S, vw = pw - 152 * S;
    for (int f = vb_new ? 0 : 1; f <= 2; f++, ty += rh + 8 * S) {
        int on = f == vb_field;
        text_f(F, S, lx, ui_ty(ty, rh), T(f == 0 ? S_VHD_NAME : f == 1 ? S_VHD_SIZE : S_VHD_FMT), on ? C_ACC : C_MUTED, 0xFF, 0, 1);
        ui_rframe(vx, ty, vw, rh, 6 * S, on ? C_ACC : C_LINE, C_FIELD);
        if (f == 2) hit_add(hc_vbfmt, 0, 2, 0, 0, vx, ty, vw, rh); else hit_add(hc_sel, (intptr_t)&vb_field, f, 0, 0xCD, vx, ty, vw, rh);
        if (f == 2) vb_fy = ty;
        char v[72] = "";
        if (f == 0) { copy_str(v, sizeof v, vb_name); if (on) str_append(v, sizeof v, "_"); }
        else if (f == 1) { num_append(v, sizeof v, (uint32_t)vb_mbv, 0, ' '); str_append(v, sizeof v, " MB"); }
        else {
            copy_str(v, sizeof v, T(vb_kindv == VF_DOS1 ? S_VHD_DOS1 : vb_kindv == VF_FAT12 ? S_VHD_FAT12 : vb_kindv == VF_FAT16 ? S_VHD_FAT16 : S_VHD_AUTO));
            if (vb_kindv == VF_AUTO) { int e = vb_kind_eff(); str_append(v, sizeof v, " → "); str_append(v, sizeof v, e == VF_DOS1 ? "FAT12 DOS1" : e == VF_FAT12 ? "FAT12" : "FAT16"); }
        }
        if (f == 2) {
            uint32_t ch = SLH + 6 * S, cw = ui_sw("Enter ▼") + 26 * S;
            text_ell(vx + 12 * S, ui_ty(ty, rh), v, C_ACC, 0xFF, vw > cw + 30 * S ? vw - cw - 30 * S : vw - 24 * S);
            if (on) ui_keychip(vx + vw - cw - 6 * S, ty + (rh - ch) / 2, ch, "Enter ▼", 0);
        } else {
            if (f && on) { text_f(F, S, vx + 10 * S, ui_ty(ty, rh), "‹", C_ACC, 0xFF, 0, 1); text_f(F, S, vx + vw - 22 * S, ui_ty(ty, rh), "›", C_ACC, 0xFF, 0, 1); }
            text_ell(vx + (f ? 30 * S : 12 * S), ui_ty(ty, rh), v, C_TEXT, 0xFF, vw - 60 * S);
        }
    }
    small_ell(lx, ty, T(S_VHD_NOTE), C_MUTED, C_CARD, pw - 32 * S);
    ty += SLH + 12 * S;
    /* [저장] [취소] */
    const char *bs = T(S_VHD_SAVE), *bc = T(S_VHD_CANCEL);
    uint32_t bw1 = text_w(bs) + 40 * S, bw2 = text_w(bc) + 40 * S, bh = LH + 12 * S, bx = px + pw - 16 * S - bw1 - bw2 - 10 * S;
    ui_rframe(bx, ty, bw1, bh, 7 * S, C_ACC, C_ACC);
    text_f(F, S, bx + 20 * S, ui_ty(ty, bh), bs, 15, 0xFF, 0, 1);
    hit_add(hc_key, 0x9C, 0, 0, 0x9C, bx, ty, bw1, bh);
    ui_rframe(bx + bw1 + 10 * S, ty, bw2, bh, 7 * S, C_LINE, C_FIELD);
    text_f(F, S, bx + bw1 + 30 * S, ui_ty(ty, bh), bc, C_TEXT, 0xFF, 0, 1);
    hit_add(hc_key, 0x01, 0, 0, 0x01, bx + bw1 + 10 * S, ty, bw2, bh);
    ui_cmdbar(px + S, py0 + ph - ui_cmdbar_h() - 6 * S, pw - 2 * S, T(S_VHD_FORM_KEYS));
    if (dd_on && dd_id == 3001) dd_draw_at(vx, vb_fy + rh, vw, py0 + ph);
}
