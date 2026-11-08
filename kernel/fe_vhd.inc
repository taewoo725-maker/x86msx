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
#include "hdfat.h"
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

/* ---- 8.0.168 Nextor 커널 손질: FAT16 하드에서 절대 섹터 읽기(_RDABS 2Fh) 허용 ----
 * Nextor 는 FAT12 가 아닌 드라이브에 _RDABS/_WRABS 가 오면 일부러 "Not a DOS disk"(.NDOS) 를 내고, 디스크 오류 처리기를 거쳐
 * 같은 일을 다시 한다 (Nextor source/kernel/bank2/rw.mac abs_common). 시작할 때 모든 드라이브의 0번 섹터를 _RDABS 로 읽는
 * 게임(Moon Light Saga IDSS 등)은 오류 처리기가 늘 "다시"라서 FAT16 가상 하드가 있으면 끝없이 멈춘다. MSX-DOS 2 는 그냥 읽어 줬다.
 * 그래서 읽기(_RDABS)만 MSX-DOS 2 처럼 허용하고, 쓰기(_WRABS)는 Nextor 가 막는 그대로 둔다 (CHKDSK 같은 옛 도구의 FAT16 손상 방지).
 *   원래 (18 바이트):  bit RF_DOS2,(iy+RW_FLAGS) / jr nz,dorw / push hl / ld de,UD_FLAGS / add hl,de / bit UF_F12,(hl) / pop hl / jr nz,dorw / ld e,.NDOS
 *   손질:              call 읽기검사 / jr nz,dorw / nop / (뒤 12 바이트 그대로)
 *   읽기검사 (뱅크 끝 빈자리 6 바이트): ld a,(iy+RW_FLAGS) / and RFM_D2|RFM_RD / ret
 * 커널 뱅크는 부팅 때 뱅크 0100h~3FF0h 가 RAM 0000h~3EF0h 로 복사되어 돌므로(OFFSET 4100h), call 주소 = 뱅크 자리 - 100h. */
#define NEXTOR_RAMMOD_OFFSET 0x100                     /* 뱅크 자리 → 돌 때 RAM 주소 */
#define NEXTOR_STUB_AT 0x3FC0                          /* 읽기검사 스텁 자리 (뱅크 끝의 0 채움, CHGBNK 3FD0h 바로 앞, 복사 범위 3FF0h 안) */
/* vhd_rdabs_patched 는 fe.c 앞쪽에 선언 (진단 fe_sys.inc 가 먼저 포함됨) — 점검용: 손질한 커널 뱅크 수 (Nextor 2.1.3 = 1, 0 이면 모양이 달라 손대지 않음) */
static int nextor_bank_has_rdabs_block(const uint8_t *q)
{
    return q[0] == 0xFD && q[1] == 0xCB && q[3] == 0x6E && q[4] == 0x20 && q[6] == 0xE5 && q[7] == 0x11 && q[10] == 0x19 &&
           q[11] == 0xCB && q[12] == 0x46 && q[13] == 0xE1 && q[14] == 0x20 && q[16] == 0x1E && q[17] == 0xF6;
}
static int nextor_bank_stub_room_free(const uint8_t *bank)
{
    for (int i = 0; i < 16; i++) if (bank[NEXTOR_STUB_AT + i]) return 0;
    return 1;
}
static void vhd_patch_rdabs_fat16(uint8_t *bank)
{
    if (!nextor_bank_stub_room_free(bank)) return;
    for (uint32_t i = 0; i + 18 <= NEXTOR_STUB_AT; i++) {
        uint8_t *q = bank + i;
        if (!nextor_bank_has_rdabs_block(q)) continue;
        uint8_t flags_offset = q[2];
        uint16_t stub_ram = NEXTOR_STUB_AT - NEXTOR_RAMMOD_OFFSET;
        uint16_t jump_target = (uint16_t)(6 + q[5]);                      /* 원래 jr nz 의 목적지 (q 기준, 앞쪽이면 q[5] 는 음수) */
        uint8_t *stub = bank + NEXTOR_STUB_AT;
        stub[0] = 0xFD; stub[1] = 0x7E; stub[2] = flags_offset;             /* ld a,(iy+RW_FLAGS) */
        stub[3] = 0xE6; stub[4] = 0x21;                                     /* and RFM_D2(20h) | RFM_RD(01h) → 읽기나 xxDRV 면 NZ */
        stub[5] = 0xC9;                                                     /* ret */
        q[0] = 0xCD; q[1] = (uint8_t)stub_ram; q[2] = (uint8_t)(stub_ram >> 8);   /* call 읽기검사 */
        q[3] = 0x20; q[4] = (uint8_t)(jump_target - 5);                     /* jr nz,dorw */
        q[5] = 0x00;                                                        /* nop — 뒤 12 바이트(FAT12 검사·.NDOS)는 그대로 */
        vhd_rdabs_patched++;
        return;
    }
}

/* ---- Nextor 롬 → 가상 디스크 롬 (cart_buf 안에서). 커널 뱅크 K개 + 드라이버 뱅크 ---- */
static int vhd_build_rom(uint8_t *b, uint32_t *size)
{
    uint32_t n = *size;
    int K = n >= 16384 ? b[0xFE] : 0;
    if (K < 2 || K > 30 || (uint32_t)K * 16384 > n) return -1;
    for (int k = 0; k < K; k++) if (b[(uint32_t)k * 16384 + 0xFF] != k || b[(uint32_t)k * 16384 + 0xFE] != K) return -1;   /* Nextor 뱅크 머리 */
    vhd_rdabs_patched = 0;
    static const uint8_t chg[4] = { 0x32, 0x00, 0x60, 0xC9 };   /* CHGBNK: LD (6000h),A / RET */
    static const uint8_t sunrise[32] = { 0xC5,0xCB,0x3F,0xCB,0x10,0xCB,0x3F,0xCB,0x10,0xCB,0x3F,0xCB,0x10,0xCB,0x3F,0xCB,0x10,0xCB,0x3F,0xCB,0x10,0x78,0xC1,0x07,0x07,0x07,0xE6,0xF8,0x32,0x04,0x41,0xC9 };
    for (int k = 0; k < K; k++) {
        uint8_t *bk = b + (uint32_t)k * 16384;
        for (uint32_t i = 0; i + 32 <= 0x3FD0; i++)     /* Sunrise IDE 롬이면 안쪽 사본도 */
            if (bk[i] == 0xC5 && !memcmp(bk + i, sunrise, 32)) { memcpy(bk + i, chg, 4); memset(bk + i + 4, 0, 28); }
        vhd_patch_rdabs_fat16(bk);                      /* 8.0.168: FAT16 에서 _RDABS 허용 (맞는 뱅크 하나에서만 바뀜) */
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
/* ---- 8.0.66 이름에 X86 이 들어간 롬팩의 실제 롬 파일 (Machines\Shared Roms) ----
 * X86-Multimedia.rom : X86 Multimedia 카트리지(소리 + 영상). 16 KB, 코드 없음, 맨 앞 글자 "X86MM-ID" 로 알아봄. 없거나 틀리면 만들어 둠. (옛 X86MV-Sound.rom 은 카트리지째 없앴고 부팅 때 지움)
 * X86MSX-VHD.rom  : X86MSX 가상 하드 카트리지(Nextor 커널 + X86MSX 드라이버). Nextor-2.1.3.base.dat 에서 만든 것과 똑같이 저장 (없거나 다르면 다시 씀) */
#define X86ROM_DIR "0:\\Machines\\Shared Roms\\"
static void x86_mm_rom_ensure(uint8_t *t)           /* 8.0.75 X86-Multimedia.rom: 코드 없는 표시 파일 */
{
    static const char id[] = "X86MM-ID";
    static const char txt[] = "X86 Multimedia cartridge (X86MSX) - sound I/O 2Ch-2Fh + video I/O 24h-27h (MV2 from the Video folder). No ROM code: this file only marks the cartridge.";
    if (!P.write_file || !P.read_file) return;
    uint32_t sz = 0; uint8_t h[16];
    if (!P.read_file(X86ROM_DIR "X86-Multimedia.rom", h, sizeof h, &sz) && sz == 16384 && !memcmp(h, id, 8)) return;
    memset(t, 0xFF, 16384); memset(t, 0, 256); memcpy(t, id, 8); memcpy(t + 16, txt, sizeof txt);
    P.write_file(X86ROM_DIR "X86-Multimedia.rom", t, 16384);
}
static void x86_roms_boot(void)                     /* 8.0.77: 부팅 때 Shared Roms 에 X86 Multimedia 롬 파일 확인 */
{
    static uint8_t b[16384];
    if (!P.create) return;
    P.create("0:\\Machines", 1); P.create("0:\\Machines\\Shared Roms", 1);
    x86_mm_rom_ensure(b);
    if (P.delete_file) P.delete_file(X86ROM_DIR "X86MV-Sound.rom");   /* 8.0.78: 없앤 X86MV Sound 카트리지의 옛 롬 파일 — 이제 없는 이름이라 있으면 지움 (X86-Multimedia.rom 이 대신함) */
}
static void x86_vhd_rom_save(const uint8_t *b, uint32_t n)
{
    if (!P.write_file) return;
    int same = 0;
    if (P.read_file && P.scratch && P.scratch_size >= (32u << 20)) {
        uint32_t sz = 0; uint8_t *tmp = P.scratch + (24u << 20);
        same = !P.read_file(X86ROM_DIR "X86MSX-VHD.rom", tmp, 2u << 20, &sz) && sz == n && !memcmp(tmp, b, n);
    }
    if (!same) P.write_file(X86ROM_DIR "X86MSX-VHD.rom", b, n);
}
static int is_nextor_rom(const uint8_t *b, uint32_t n)   /* 파일이 Nextor 롬(또는 base.dat)인가 */
{
    int K = n >= 32768 ? b[0xFE] : 0;
    if (K < 2 || K > 30 || (uint32_t)K * 16384 > n) return 0;
    for (int k = 0; k < K; k++) if (b[(uint32_t)k * 16384 + 0xFF] != k) return 0;
    return b[0] == 'A' && b[1] == 'B';
}

static uint32_t vh_now(void);

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
static uint32_t vh_now(void) { return P.ms ? P.ms() : 0; }
static const VfHost vhd_host = { 0, vh_list, vh_read_at, vh_write, vh_mkdir, vh_del, vh_now };

static void vhd_path(int i, char *out, int max) { copy_str(out, max, VHD_ROOT "\\"); str_append(out, max, vhd_dir[i]); }

/* 바뀐 것을 USB에 (모든 하드). 실패하면 알림 (이유 포함) */
static void vhd_sync_all(uint32_t budget)   /* budget 0 = 끝까지 (리셋·빼기·끝낼 때), > 0 = 이 ms 만큼만 하고 나머지는 다음 프레임 (8.0.42) */
{
    for (int i = 0; i < VHD_N; i++) {
        if (!vhd_up[i] || !vhd_vol[i].dirty || !P.scratch) continue;
        char err[32], d[48];
        err[0] = 0;
        if (vf_sync2(&vhd_vol[i], P.scratch, 28u << 20, err, sizeof err, budget) < 0) {
            copy_str(vhd_err, sizeof vhd_err, err);
            copy_str(d, sizeof d, vhd_dir[i]); str_append(d, sizeof d, " "); str_append(d, sizeof d, err);
            toast(T(S_VHD_SYNC_FAIL), d);
        }
    }
}
static void vhd_unmount_all(void)
{
    for (int g = 0; g < 100000; g++) { int pend = 0; vhd_sync_all(0); for (int i = 0; i < VHD_N; i++) if (vhd_up[i] && vhd_vol[i].dirty) pend = 1; if (!pend) break; }   /* 8.0.42: 시간 제한 없이 끝까지 */
    for (int i = 0; i < VHD_N; i++) if (vhd_up[i]) { vf_unmount(&vhd_vol[i]); vhd_up[i] = 0; }
}
/* 연결한 폴더를 모두 다시 마운트 (실패한 것은 알림) */
static int vhd_lfn(const char *path, char *out, int max);
static const MvFileOps vhd_fops;
static const MmDisk vhd_mmdisk;
static const MmHost mm_host;
static const UsbHost mu_host;
static int vhd_suggest_mb(uint32_t need_mb);             /* 8.0.169: 아래 가상 하드 폴더 창 쪽 용량 목록으로 정함 */
/* ---- 8.0.169 가상 하드 용량 부족 안내 창 (창 22) ----
 * 폴더가 설정 용량보다 커서 연결이 안 되면: 필요·설정·모자란 용량을 막대와 숫자로 보이고, 권장 용량으로 올려 연결 / 설정 열기 / 닫기를 고르게 한다.
 * 부팅·리셋 도중(power_on 안)에는 창을 열지 않고 표시만 해 두었다가 프레임 루프(vhd_notice_tick)에서 연다. 같은 실행에서 같은 드라이브는 창을 한 번만 (이후 리셋은 한 줄 알림). */
#define VN_DIALOG 22
static int vb_lock;                                     /* 아래 가상 하드 폴더 창 쪽 것들 (앞선 선언) */
static int vhd_cfg_write(const char *dir, int mb, int kind);
static void vb_form_open(int isnew, const char *path, int mb, int kind);
static void vhd_connect(int i);
static void open_vhd_browser(int drv);
static uint32_t vn_due;                                /* 이 시각(ms) 뒤에 창을 엶 — 부팅 직후 연결이 몇 번 더 되풀이되며 창을 닫아 버려서, 마지막 실패 1초 뒤로 미룸 */
static int vn_mask, vn_shown, vn_drive, vn_sel;        /* 알릴 드라이브 비트, 이미 창으로 알린 비트, 지금 창의 드라이브, 고른 단추 (0 권장 1 설정 2 닫기) */
static uint32_t vn_need[VHD_N], vn_have[VHD_N];
static int vn_sug[VHD_N];
static int vn_can_apply(int i)                          /* 권장 용량으로 올릴 수 있는가 (형식을 안 바꾸고) */
{
    if (vn_sug[i] <= 0) return 0;
    if (vhd_kind[i] == VF_AUTO && vhd_vol[i].kind != VF_DOS1) return 1;   /* 자동: 커지면 FAT16 이 됨 */
    return (uint32_t)vn_sug[i] <= vf_max_mb(vhd_vol[i].kind);
}
static void vhd_toast_full(int i)
{
    vn_need[i] = vf_need_mb(&vhd_vol[i]); vn_have[i] = (uint32_t)vhd_mb[i]; vn_sug[i] = vhd_suggest_mb(vn_need[i]);
    if (!(vn_shown & (1 << i))) { vn_mask |= 1 << i; vn_due = P.ms() + 1000; return; }   /* 첫 실패 = 창 (프레임 루프가 엶, 실패가 되풀이되면 그만큼 미룸) */
    char b[192];                                         /* 두 번째부터 = 한 줄 알림 */
    copy_str(b, sizeof b, vhd_dir[i]);
    str_append(b, sizeof b, nt(" : 필요 ", " : needs ", " : 必要 ")); num_append(b, sizeof b, vn_need[i], 0, ' ');
    str_append(b, sizeof b, nt(" MB / 설정 ", " MB / set ", " MB / 設定 ")); num_append(b, sizeof b, vn_have[i], 0, ' '); str_append(b, sizeof b, " MB");
    if (vn_sug[i] > 0) { str_append(b, sizeof b, " → "); num_append(b, sizeof b, (uint32_t)vn_sug[i], 0, ' '); str_append(b, sizeof b, nt(" MB 이상", " MB or more", " MB 以上")); }
    toast_ms = 8000;
    toast(T(S_VHD_FULL), b);
}
static void vhd_notice_tick(void)                       /* 프레임마다: 알릴 것이 있고 다른 창이 없으면 안내 창을 엶 */
{
    if (!vn_mask || dialog || ui_active() || (int32_t)(P.ms() - vn_due) < 0) return;
    for (int i = 0; i < VHD_N; i++) {
        if (!(vn_mask & (1 << i))) continue;
        vn_mask &= ~(1 << i);
        if (vhd_up[i] || !vhd_dir[i][0]) continue;       /* 그새 연결됐거나 연결을 끊음 */
        vn_shown |= 1 << i;                              /* 이 실행에서는 창을 한 번만 */
        vn_drive = i; vn_sel = vn_can_apply(i) ? 0 : 1;
        open_dialog(VN_DIALOG);
        return;
    }
}
static void vn_apply(int i)                             /* 권장 용량으로 올려 저장하고 연결 (삽입 후 리셋 설정대로) */
{
    char p[160]; vhd_path(i, p, sizeof p);
    uint32_t from = (uint32_t)vhd_mb[i];
    vhd_mb[i] = vn_sug[i];
    vhd_cfg_write(p, vhd_mb[i], vhd_kind[i]);
    dialog_done();
    vhd_connect(i);
    char b[64] = ""; num_append(b, sizeof b, from, 0, ' '); str_append(b, sizeof b, " MB → "); num_append(b, sizeof b, (uint32_t)vhd_mb[i], 0, ' '); str_append(b, sizeof b, " MB");
    toast_ms = 5000;
    toast(nt("가상 하드 용량을 올렸습니다:", "Virtual disk size raised:", "仮想ハードの容量を上げました:"), b);
}
static void vn_open_settings(int i)                     /* 가상 하드 폴더 창의 설정 칸을 권장 용량으로 채워 엶 */
{
    char p[160]; vhd_path(i, p, sizeof p);
    dialog_done();
    open_vhd_browser(i);
    vb_form_open(0, p, vn_sug[i] > 0 ? vn_sug[i] : vhd_mb[i], vhd_kind[i]);
    vb_lock = 0;
}
static void vn_key(int key)
{
    int i = vn_drive, can = vn_can_apply(i);
    if (key == 0xCB || key == 0xCD || key == 0x0F) {
        int d = key == 0xCB ? -1 : 1;
        for (int k = 0; k < 3; k++) { vn_sel = (vn_sel + d + 3) % 3; if (vn_sel != 0 || can) break; }
    }
    else if (key == 0x01 || key == 0x31) dialog_done();                         /* Esc, N */
    else if (key == 0x15 && can) vn_apply(i);                                   /* Y */
    else if (key == 0x1F) vn_open_settings(i);                                  /* S */
    else if (key == 0x1C || key == 0x9C) { if (vn_sel == 0 && can) vn_apply(i); else if (vn_sel == 1) vn_open_settings(i); else if (vn_sel == 2) dialog_done(); }
    menu_dirty = 1;
}
static uint32_t vn_big(uint32_t x, uint32_t y, const char *t, uint8_t c, int draw) { return text_f(F, S * 2, x, y, t, c, 0xFF, 0, draw); }
static void draw_vhd_notice(void)
{
    int i = vn_drive, can = vn_can_apply(i);
    uint32_t need = vn_need[i], have = vn_have[i], lack = need > have ? need - have : 0;
    ui_backdrop();
    UiRect o = card_rect();
    uint32_t pw = 700 * S; if (pw > o.w - 24 * S) pw = o.w - 24 * S;
    uint32_t pad = 24 * S, gap = 12 * S, tile_h = LH * 2 + SLH + 26 * S, bh = LH + 18 * S;
    uint32_t ph = 2 * pad + LH + SLH + 20 * S      /* 제목 + 부제 */
                + 14 * S + 16 * S + SLH + 18 * S   /* 막대 + 막대 아래 글자 */
                + tile_h + 18 * S                  /* 숫자 세 칸 */
                + LH + (can || !vn_sug[i] ? 0 : SLH + 4 * S) + 14 * S   /* 권장 줄 (+ 형식 안내) */
                + SLH * 2 + 14 * S                 /* 설명 두 줄 */
                + bh + 18 * S + ui_cmdbar_h();
    uint32_t px = o.x + (o.w - pw) / 2, py = o.y + (o.h > ph ? (o.h - ph) / 2 : 0);
    ui_rrect(px + 6 * S, py + 8 * S, pw, ph, 14 * S, C_SHADOW);
    ui_rframe(px, py, pw, ph, 14 * S, C_WARN, C_CARD);
    uint32_t y = py + pad, ix = px + pad, iw = pw - 2 * pad;

    /* 제목: 노란 느낌표 동그라미 + 제목 + 부제(드라이브·폴더) */
    uint32_t d = LH + SLH + 6 * S;
    ui_rrect(ix, y, d, d, d / 2, C_WARN);
    { const char *ex = "!"; uint32_t ew = text_w(ex); text_f(F, S, ix + (d - ew) / 2, ui_ty(y, d), ex, C_CARD, 0xFF, 0, 1); }
    text_clip(ix + d + 14 * S, y, nt("가상 하드 용량이 모자랍니다", "Virtual disk is too small", "仮想ハードの容量が足りません"), C_TEXT, 0xFF, iw - d - 14 * S);
    { char sub[96]; sub[0] = (char)('C' + i); sub[1] = ':'; sub[2] = ' '; sub[3] = 0; str_append(sub, sizeof sub, "VMFolder\\"); str_append(sub, sizeof sub, vhd_dir[i]);
      small_ell(ix + d + 14 * S, y + LH + 2 * S, sub, C_MUTED, 0xFF, iw - d - 14 * S); }
    y += d + 20 * S;

    /* 막대: 전체 = 필요한 용량, 파란 부분 = 설정한 용량, 노란 부분 = 모자란 만큼 */
    { uint32_t bar_h = 14 * S, fw = need ? (uint32_t)((uint64_t)iw * (have > need ? need : have) / need) : iw;
      ui_rrect(ix, y, iw, bar_h, bar_h / 2, C_WARN);
      if (fw >= bar_h) ui_rrect(ix, y, fw, bar_h, bar_h / 2, C_ACC); else if (fw) ui_rrect(ix, y, bar_h, bar_h, bar_h / 2, C_ACC);
      y += bar_h + 8 * S;
      char l1[48] = "", l2[48] = "";
      copy_str(l1, sizeof l1, nt("설정 ", "Set ", "設定 ")); num_append(l1, sizeof l1, have, 0, ' '); str_append(l1, sizeof l1, " MB");
      copy_str(l2, sizeof l2, nt("필요 ", "Needed ", "必要 ")); num_append(l2, sizeof l2, need, 0, ' '); str_append(l2, sizeof l2, " MB");
      small_ell(ix, y, l1, C_ACC, 0xFF, iw / 2);
      uint32_t w2 = ui_sw(l2); small_ell(ix + iw - w2, y, l2, C_WARN, 0xFF, iw / 2);
      y += SLH + 18 * S; }

    /* 숫자 세 칸: 필요 / 설정 / 모자람 */
    { uint32_t tw = (iw - 2 * gap) / 3;
      for (int t = 0; t < 3; t++) {
          uint32_t tx = ix + (uint32_t)t * (tw + gap), v = t == 0 ? need : t == 1 ? have : lack;
          const char *lab = t == 0 ? nt("폴더가 필요한 용량", "Folder needs", "フォルダに必要") : t == 1 ? nt("지금 설정", "Current setting", "現在の設定") : nt("모자란 용량", "Short by", "不足");
          uint8_t col = t == 2 ? C_WARN : t == 1 ? C_ACC : C_TEXT;
          ui_rframe(tx, y, tw, tile_h, 10 * S, t == 2 ? C_WARN : C_LINE, C_FIELD);
          small_ell(tx + 14 * S, y + 10 * S, lab, C_MUTED, 0xFF, tw - 28 * S);
          char num[24] = ""; num_append(num, sizeof num, v, 0, ' ');
          uint32_t nw = vn_big(0, 0, num, col, 0);
          vn_big(tx + 14 * S, y + 10 * S + SLH + 4 * S, num, col, 1);
          text_f(F, S, tx + 14 * S + nw + 6 * S, y + 10 * S + SLH + 4 * S + LH, "MB", C_MUTED, 0xFF, 0, 1);
      }
      y += tile_h + 18 * S; }

    /* 권장 */
    if (vn_sug[i] > 0) {
        char r[96]; copy_str(r, sizeof r, nt("권장 설정  ", "Suggested  ", "推奨設定  ")); num_append(r, sizeof r, (uint32_t)vn_sug[i], 0, ' '); str_append(r, sizeof r, nt(" MB 이상", " MB or more", " MB 以上"));
        text_clip(ix, y, r, C_OK, 0xFF, iw);
        y += LH;
        if (!can) { small_ell(ix, y + 4 * S, nt("이 형식은 더 못 키웁니다 — 설정에서 형식을 FAT16 으로 바꾸세요.", "This format cannot grow that big - switch the format to FAT16 in settings.", "この形式ではこれ以上大きくできません。設定で形式をFAT16にしてください。"), C_WARN, 0xFF, iw); y += SLH + 4 * S; }
    } else {
        text_clip(ix, y, nt("4095 MB 로도 모자랍니다. 폴더의 파일을 줄여 주세요.", "Even 4095 MB is not enough. Please remove some files.", "4095MBでも足りません。ファイルを減らしてください。"), C_BAD, 0xFF, iw);
        y += LH;
    }
    y += 14 * S;
    small_ell(ix, y, nt("폴더 안 파일이 설정한 용량보다 커서 이 가상 하드를 연결하지 못했습니다.", "The folder's files are larger than the setting, so this virtual disk was not connected.", "フォルダ内のファイルが設定容量より大きいため、この仮想ハードは接続されませんでした。"), C_MUTED, 0xFF, iw);
    y += SLH;
    small_ell(ix, y, nt("지금 MSX 에는 이 드라이브가 없습니다. 용량을 올리면 다시 시작해서 연결합니다.", "This drive is not present in the MSX now. Raising the size reconnects it (with a restart).", "いまMSXにこのドライブはありません。容量を上げると再起動して接続します。"), C_MUTED, 0xFF, iw);
    y += SLH + 14 * S;

    /* 단추 */
    { char b0[64]; copy_str(b0, sizeof b0, nt("권장 ", "Raise to ", "推奨 ")); num_append(b0, sizeof b0, (uint32_t)(vn_sug[i] > 0 ? vn_sug[i] : 0), 0, ' '); str_append(b0, sizeof b0, nt(" MB 로 올리고 연결", " MB and connect", " MBに上げて接続"));
      const char *lab[3] = { b0, nt("설정 열기", "Open settings", "設定を開く"), nt("닫기", "Close", "閉じる") };
      static const int key[3] = { 0x15, 0x1F, 0x01 };
      uint32_t bw[3], total = 0;
      for (int k = 0; k < 3; k++) { bw[k] = text_w(lab[k]) + 36 * S; total += bw[k]; }
      total += 2 * gap;
      uint32_t bx = ix + (iw > total ? (iw - total) / 2 : 0);
      for (int k = 0; k < 3; k++) {
          int enabled = k != 0 || can, sel = vn_sel == k && enabled;
          ui_rrect(bx + 2 * S, y + 3 * S, bw[k], bh, bh / 2, C_SHADOW);
          ui_rframe(bx, y, bw[k], bh, bh / 2, sel ? (k == 0 ? C_OK : C_ACC) : C_LINE, sel ? C_ROWSEL : C_HEAD);
          text_clip(bx + 18 * S, ui_ty(y, bh), lab[k], enabled ? C_TEXT : C_FAINT, 0xFF, bw[k] - 24 * S);
          if (enabled) hit_add(hc_sel, (intptr_t)&vn_sel, k, 1, key[k], bx, y, bw[k], bh);
          bx += bw[k] + gap;
      } }
    ui_cmdbar(px + S, py + ph - ui_cmdbar_h() - 4 * S, pw - 2 * S, nt("Enter 선택   ←/→ 고르기   Y 올리고 연결   S 설정   Esc 닫기", "Enter Select   ←/→ Choose   Y Raise   S Settings   Esc Close", "Enter 選択   ←/→ 選ぶ   Y 上げて接続   S 設定   Esc 閉じる"));
}
static void vhd_mount_all(int quiet)
{
    mmvid_set_host(&mm_host);                           /* 8.0.75 X86 Multimedia 영상 폴더 */
    { extern const UiFont *mmtext_font_big; mmtext_font_big = &font_small; }   /* 8.0.88 30h TEXT: 높이 11~16 은 내장 Galmuri11 */
    mmusb_ms = P.ms; mmusb_crumb = P.crumb;                              /* 8.0.83 진단 */
    mmusb_set_host(&mu_host);                           /* 8.0.80 X86 Multimedia USB 직접 접근 (읽기 전용) */
    mvsnd_set_lfn(vhd_lfn);                             /* 8.0.62 */
    mvsnd_set_file(&vhd_fops);                          /* 8.0.66 */
    mmvid_set_disk(&vhd_mmdisk);                        /* 8.0.109 MSX 디스크의 영상 */
    mvsnd_set_usbfile(mmusb_cur_file);                  /* 8.0.81 USB 의 MP3 파일 직접 재생 */
    vhd_unmount_all();
    if (!P.vhd_mem || P.vhd_mem_size < (VHD_N + 2) * VF_ARENA) return;
    if (!vhd_pool_ok) { vf_pool_init(P.vhd_mem + VHD_N * VF_ARENA, P.vhd_mem_size - VHD_N * VF_ARENA); vhd_pool_ok = 1; }
    for (int i = 0; i < VHD_N; i++) {
        if (!vhd_dir[i][0]) continue;
        char p[160], err[32];
        vhd_path(i, p, sizeof p);
        if (vf_mount(&vhd_vol[i], &vhd_host, p, (uint32_t)vhd_mb[i], vhd_kind[i], i + 1, P.vhd_mem + (uint32_t)i * VF_ARENA, err, sizeof err)) {
            /* 8.0.169: 연결 실패는 부팅·리셋(quiet)에서도 알림 — 전에는 용량이 넘쳐도 아무 말 없이 하드만 빠졌음 */
            if (!strcmp(err, "full")) vhd_toast_full(i); else toast(!strcmp(err, "root") ? T(S_VHD_ROOTFULL) : T(S_VHD_MOUNT_FAIL), vhd_dir[i]);
            continue;
        }
        vhd_up[i] = 1;
        vhd_chg[2 + i] = 1;
        if (vhd_vol[i].trunc && !quiet) toast(T(S_VHD_TRUNC), vhd_dir[i]);   /* 3.9.12 */
    }
}

/* 8.0.62 X86 Multimedia 소리 쪽 긴 이름 조회: 마운트한 폴더 하드에서 차례로 찾음 (드라이브 글자는 안 가림) */
static int vhd_lfn(const char *path, char *out, int max)
{
    for (int i = 0; i < VHD_N; i++) if (vhd_up[i] && vf_lfn(&vhd_vol[i], path, out, max)) return 1;
    return 0;
}
/* 8.0.66 X86 Multimedia 소리 쪽 파일 재생: DOS 짧은 경로 → USB 파일 → 읽기 */
static int vhd_ffind(const char *path, char *host, int max, uint32_t *size)
{
    for (int i = 0; i < VHD_N; i++) if (vhd_up[i] && vf_find_file(&vhd_vol[i], path, host, max, size)) return 1;
    return 0;
}
static int vhd_fread(const char *host, uint32_t off, uint8_t *buf, uint32_t len) { return P.read_at ? P.read_at(host, off, buf, len) : -1; }
static uint32_t vhd_fnow(void) { return vh_now(); }
static const MvFileOps vhd_fops = { vhd_ffind, vhd_fread, vhd_fnow };
/* 8.0.109 X86 Multimedia 영상: 드라이브 글자(0=A:) → 가상 하드 C~H 의 호스트 파일.
 * 8.0.110: 하드 디스크 이미지도 읽음 (8.0.114: FAT12/16/32 모두, 플로피 A:·B: 포함) — 드라이브 C: 부터 마스터·슬레이브 이미지의 FAT 파티션 순서(파티션 표가 없으면 이미지 전체가 C:).
 * 8.0.114: 플로피 A:·B: 이미지(FAT12·FAT16)도 읽음(8.0.112·113 에서 잠깐 뺐다가 되살림). 디스크가 없는 A:·B:, 읽을 수 없는 이미지, 없는 드라이브는 -1 (오류 9) */
static HdFat vhd_hf;                                   /* 열어 둔 하드 이미지 볼륨 (영상은 한 번에 하나) */
static int vhd_hf_ok;
static int vhd_hfrd(void *c, uint32_t lba, uint8_t *b) { return hd_rd_cb(c, lba, b); }
static int vhd_fdrd(void *c, uint32_t lba, uint8_t *b)   /* 8.0.114 플로피 이미지(메모리) 섹터 */
{
    int d = (int)(uintptr_t)c & 1;
    if (!disk_size[d] || (uint64_t)lba * 512 + 512 > disk_size[d]) return -1;
    memcpy(b, disk_buf[d] + (size_t)lba * 512, 512); return 0;
}
static int vhd_img_vol(int idx, int *unit, uint32_t *st, uint32_t *ns)   /* idx 번째 FAT 볼륨 */
{
    int k = 0;
    for (int u = 0; u < 2; u++) {
        if (!(u ? hd2_secs : hd_secs)) continue;
        for (int n = 0; n < 8; n++) {
            uint32_t a, b;
            if (!hf_part(vhd_hfrd, (void *)(uintptr_t)u, n, &a, &b)) break;
            static HdFat t; if (hf_open(&t, vhd_hfrd, (void *)(uintptr_t)u, a, b)) continue;   /* FAT 이 아니면 건너뜀 */
            if (k++ == idx) { *unit = u; *st = a; *ns = b; return 1; }
        }
    }
    return 0;
}
static int vhd_dfind(int drive, const char *path, char *host, int max, uint32_t *size)
{
    int i = drive - 2;
    if (i < 0) {                                        /* 8.0.114 A:·B: 플로피 이미지 (마스터 결정: 막지 않고 다 됨) */
        if (drive < 0 || max < 8 || !disk_size[drive]) return -1;
        vhd_hf_ok = 0;
        if (hf_open(&vhd_hf, vhd_fdrd, (void *)(uintptr_t)drive, 0, disk_size[drive] / 512)) return -1;
        vhd_hf_ok = 1;
        if (path[0] && path[1] == ':') path += 2;
        if (!hf_find(&vhd_hf, path, size)) return 0;
        host[0] = 1; host[1] = 'I'; host[2] = 'M'; host[3] = 'G'; host[4] = 0;
        return 2;
    }
    if (i < VHD_N && vhd_up[i]) { int r = vf_find_file(&vhd_vol[i], path, host, max, size); return r ? 1 : 0; }
    if (vhd_any() || max < 8) return -1;
    int u; uint32_t st, ns;
    if (!vhd_img_vol(i, &u, &st, &ns)) return -1;       /* 이미지 없음·FAT16 이 아님·그 글자 없음 */
    vhd_hf_ok = 0;
    if (hf_open(&vhd_hf, vhd_hfrd, (void *)(uintptr_t)u, st, ns)) return -1;
    vhd_hf_ok = 1;
    if (path[0] && path[1] == ':') path += 2;
    if (!hf_find(&vhd_hf, path, size)) return 0;
    host[0] = 1; host[1] = 'I'; host[2] = 'M'; host[3] = 'G'; host[4] = 0;   /* 표식: 첫 글자 01h */
    return 2;
}
static int vhd_dread(const char *token, uint32_t off, uint8_t *buf, uint32_t len)
{
    (void)token;
    return vhd_hf_ok ? hf_read(&vhd_hf, off, buf, len) : -1;
}
static const MmDisk vhd_mmdisk = { vhd_dfind, vhd_dread };
/* 8.0.75 X86 Multimedia 영상: USB 의 Video 폴더를 목록으로 보고 파일을 직접 읽음 */
static int mm_list(const char *dir, MmEnt *out, int max)
{
    FeDirEntry *e = vhd_lbuf();
    int (*ls)(const char *, FeDirEntry *, int) = P.list_dir_all ? P.list_dir_all : P.list_dir;
    if (!e || !ls) return -1;
    int lim = (int)((4u << 20) / sizeof(FeDirEntry)); if (lim > 4096) lim = 4096;
    int n = ls(dir, e, lim), k = 0;
    if (n < 0) return -1;
    for (int i = 0; i < n && k < max; i++) {
        const char *nm = e[i].key[0] ? e[i].key : e[i].name;
        copy_str(out[k].name, sizeof out[k].name, nm);
        out[k].is_dir = e[i].is_dir; out[k].size = e[i].size; k++;
    }
    return k;
}
static int mm_read(const char *path, uint32_t off, uint8_t *buf, uint32_t len) { return P.read_at ? P.read_at(path, off, buf, len) : -1; }
static const MmHost mm_host = { mm_list, mm_read, FE_STORE "\\Video", 0 };
/* 8.0.80 X86 Multimedia USB 직접 접근: 드라이브 목록 ("" 경로 = Drive 1 (boot), Drive 2 …)과 어느 USB 의 폴더든 읽기만 한다 */
static int mu_vols(UsbVol *out, int max)
{
    FeDirEntry *e = vhd_lbuf();
    int (*ls)(const char *, FeDirEntry *, int) = P.list_dir_all ? P.list_dir_all : P.list_dir;
    if (!e || !ls) return -1;
    int n = ls("", e, 32), k = 0;
    if (n < 0) return -1;
    for (int i = 0; i < n && k < max; i++) {
        if (!e[i].is_dir || !e[i].key[0]) continue;
        copy_str(out[k].name, sizeof out[k].name, e[i].name);
        copy_str(out[k].key, sizeof out[k].key, e[i].key);
        out[k].boot = e[i].key[0] == '0';
        k++;
    }
    return k;
}
static int mu_list(const char *dir, UsbEnt *out, int max)
{
    FeDirEntry *e = vhd_lbuf();
    int (*ls)(const char *, FeDirEntry *, int) = P.list_dir_all ? P.list_dir_all : P.list_dir;
    if (!e || !ls) return -1;
    int lim = (int)((4u << 20) / sizeof(FeDirEntry)); if (lim > max) lim = max;
    int n = ls(dir, e, lim), k = 0;
    if (n < 0) return -1;
    for (int i = 0; i < n && k < max; i++) {
        const char *nm = e[i].key[0] ? e[i].key : e[i].name;
        if (strlen(nm) >= sizeof out[k].name) continue;            /* 이름이 너무 길어 번호로 열 수 없는 것은 뺌 (잘린 이름으로는 못 엶) */
        copy_str(out[k].name, sizeof out[k].name, nm);
        out[k].is_dir = e[i].is_dir; out[k].size = e[i].size; k++;
    }
    return k;
}
static const UsbHost mu_host = { mu_vols, mu_list, mm_read, 0 };
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
/* 8.0.167: 플로피가 1대인 기종(예: FS-A1GT)의 원래 디스크 ROM 은 B: 를 A: 와 같은 한 드라이브로 다룸 (둘째 드라이브가 없는 것처럼 같은 디스크를 읽음).
 * 가상 하드(Nextor)를 켜면 B: 가 "준비 안 됨" 이 되어, 시작할 때 B: 를 계속 읽는 게임(Moon Light Saga IDSS)이 끝없이 되풀이하며 멈췄음.
 * B: 에 디스크를 넣은 경우와 2대짜리 기종은 그대로 B: 를 씀. 반환: 쓸 플로피 번호 (0 = A, 1 = B) */
static int vhd_floppy_index(int lun)
{
    if (lun == 2 && !disk_size[1] && disk_size[0] && M->fdc.ndrives == 1) return 0;
    return lun - 1;
}
static uint32_t vhd_secs(int dev, int lun)
{
    if (dev == 1) return disk_size[vhd_floppy_index(lun)] / 512;
    if (vhd_img && dev == 2) return vhd_img_n ? vhd_img_n : hd_secs;
    return vhd_vol[dev - 2].total;
}
static int vhd_sector(int dev, int lun, uint32_t lba, uint8_t *b, int wr)
{
    if (dev == 1) {
        int d = vhd_floppy_index(lun);
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
#ifdef VHD_TRACE
            if (!wr && a == 2 && lba < 2) { fprintf(stderr, "    SEC%u:", lba); for (int i = 0; i < 64; i++) fprintf(stderr, "%s%02X", i % 16 ? " " : "\n      ", sec[i]); fprintf(stderr, "\n      ...510: %02X %02X\n", sec[510], sec[511]); if (lba == 0) { fprintf(stderr, "      PT:"); for (int i = 446; i < 510; i++) fprintf(stderr, " %02X", sec[i]); fprintf(stderr, "\n"); } }
#endif
            if (!wr) for (int i = 0; i < 512; i++) msx_mem_write(m, (uint16_t)(hl + i), sec[i]);
            hl = (uint16_t)(hl + 512);
        }
        vhd_rw_ms = P.ms();
#ifdef VHD_TRACE
        fprintf(stderr, "    -> err %02X done %d\n", err, done);
#endif
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
            int d = vhd_floppy_index(b ? b : 1);
            uint32_t sg = 0;
            if (disk_size[d]) { sg = 2166136261u ^ disk_size[d]; for (const char *q = disk_path[d][0] ? disk_path[d] : disk_name[d]; *q; q++) sg = (sg ^ (uint8_t)*q) * 16777619u; sg |= 1; }
            int li = (b ? b : 1) - 1;                   /* 바뀜 표시는 LUN(A·B)마다 따로 */
            z->af.b.h = sg != vhd_fsig[li] ? 2 : 1;
            vhd_fsig[li] = sg;
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
    if (dirty && now - vhd_wr_ms >= 2000) vhd_sync_all(40);   /* 8.0.42: 한 프레임에 40 ms 만큼만 (많이 지우거나 복사해도 화면·소리가 안 멈춤) */
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
static int vhd_suggest_mb(uint32_t need_mb)             /* 필요한 MB 에 여유 1/8 을 더한 것보다 큰 가장 작은 용량 (없으면 0). 클러스터가 커지면 낭비도 늘어서 여유를 둠 */
{
    uint32_t want = need_mb + need_mb / 8 + 1;
    for (int k = 0; k < VW_NS; k++) if (vw_sizes[k] >= want) return vw_sizes[k];
    return 0;
}
static int16_t vb_mb[DIR_MAX];                          /* 목록 폴더마다 설정 파일의 용량 (0 = 하드 폴더 아님) */
static int8_t vb_kind[DIR_MAX];
static int vb_lock;                    /* 5.3.43: 1 = 쓰던 가상 하드 폴더 → 용량·형식은 못 고름 (5.3.47, 보이기만) */
static int vb_new, vb_field;           /* 묻기 창, 설정 창, 새로 만들기, 고른 칸 (0 이름 1 용량 2 형식, 5.3.33: 3 저장 단추 4 취소 단추) */
static int vb_btn;                      /* 5.3.33 묻기 창에서 고른 단추 (0 이 폴더로 1 신규 2 취소) */
static char vb_name[41], vb_path[PATH_MAX_FE];
static int vb_mbv = 32, vb_kindv = VF_AUTO; static uint32_t vb_fy;
static int vb_fold_mb = -1;             /* 8.0.169: 설정 창을 열 때 읽은 폴더 안 파일 크기 합 (MB, 올림, -1 = 모름) */
static int vb_raise_from;               /* 8.0.169: 저장하며 용량을 올렸으면 원래 용량 (0 = 안 올림) */
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
    vb_new = isnew; vb_form = 1; vb_ask = 0; vb_lock = 0;
    vb_field = isnew ? 0 : 1;
    copy_str(vb_path, sizeof vb_path, path);
    if (isnew) copy_str(vb_name, sizeof vb_name, "NEWHARD");
    else copy_str(vb_name, sizeof vb_name, path_base(path));
    vb_mbv = mb; vb_kindv = kind;
    vb_fold_mb = isnew ? 0 : vf_folder_mb(&vhd_host, path);
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
    dos2_mapper_hint();                                 /* 8.0.65 */
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
    vb_raise_from = 0;
    if (!vb_new && !vb_lock && vb_fold_mb > 0 && (uint32_t)vb_mbv * 100 < (uint32_t)vb_fold_mb * 108 + 100) {   /* 8.0.169: 폴더가 용량보다 크거나 거의 같으면 (클러스터 낭비 8% 여유) 올려서 저장 */
        int suggested = vhd_suggest_mb((uint32_t)vb_fold_mb);
        if (suggested > vb_mbv) { vb_raise_from = vb_mbv; vb_mbv = suggested; vb_clamp(); if (vb_mbv == vb_raise_from) vb_raise_from = 0; }
    }
    if (vhd_cfg_write(full, vb_mbv, vb_kindv)) { toast(T(S_DIR_MKFAIL), path_base(full)); return; }
    copy_str(vhd_dir[i], sizeof vhd_dir[0], rel);
    vhd_mb[i] = vb_mbv; vhd_kind[i] = vb_kindv;
    vb_form = 0;
    dialog_done();
    vhd_connect(i);
    if (vb_raise_from) {                                /* 8.0.169: 연결 알림 뒤에, 용량을 올렸음을 알림 */
        char b[96] = ""; num_append(b, sizeof b, (uint32_t)vb_raise_from, 0, ' '); str_append(b, sizeof b, " MB → "); num_append(b, sizeof b, (uint32_t)vb_mbv, 0, ' '); str_append(b, sizeof b, " MB");
        toast_ms = 6000;
        toast(nt("폴더가 커서 가상 하드 용량을 올렸습니다:", "Folder is large; virtual disk size raised:", "フォルダが大きいため仮想ハードの容量を上げました:"), b);
    }
}
/* 파일 창 키 (browse_vhd일 때). 처리했으면 1 */
static int vb_key_i(int key)
{
    menu_dirty = 1;
    if (vb_ask) {                                       /* 하드 폴더로 만들까요? */
        if (key == 0xCD) { if (vb_btn < 2) vb_btn++; return 1; }      /* 5.3.33: →/← 로 단추 옮기기 */
        if (key == 0xCB) { if (vb_btn > 0) vb_btn--; return 1; }
        if (key == 0x1C && vb_btn == 1) key = 0x31;                    /* Enter = 고른 단추 (마우스는 0x9C = 이 폴더로) */
        else if (key == 0x1C && vb_btn == 2) key = 0x01;
        if (key == 0x31) { vb_form_open(1, cur_path, 32, VF_AUTO); return 1; }   /* N: 새 가상 하드 */
        if (key == 0x1C || key == 0x9C) { int m = 32; vb_form_open(0, vb_path, m, VF_AUTO);
            int fm = vb_fold_mb;                              /* 파일보다 크게 (8.0.169: 설정 창을 열며 이미 읽음) */
            if (fm >= 0 && vb_mbv <= fm) { int k = 0; while (k < VW_NS - 1 && vw_sizes[k] <= fm) k++; vb_mbv = vw_sizes[k]; vb_clamp(); } }
        else if (key == 0x01 || key == 0x0E) vb_ask = 0;
        return 1;
    }
    if (vb_form) {
        if (dd_on) { dd_key_event(key); if (!dd_on) vb_clamp(); menu_dirty = 1; return 1; }   /* 5.0.38 */
        switch (key) {
        case 0x01: vb_form = 0; return 1;                       /* 취소 */
        case 0x1C: if (vb_field == 2) { vb_dd_open(); return 1; } if (vb_field == 4) { vb_form = 0; return 1; } vb_save(); return 1;   /* 형식 줄에서 Enter = 풀다운, 5.3.33 취소 단추 = 닫기 */
        case 0x9C: vb_save(); return 1;                         /* 저장 (단추) */
        case 0xC8: if (vb_field == 4) vb_field = 3; else if (vb_field > (vb_new ? 0 : vb_lock ? 3 : 1)) vb_field--; return 1;
        case 0xD0: if (vb_field < 3) vb_field++; return 1;                      /* 5.3.33: 아래로 [저장]까지 */
        case 0x0F: vb_field = vb_field >= 4 ? (vb_new ? 0 : vb_lock ? 3 : 1) : vb_field + 1; return 1;   /* Tab: 한 바퀴 (단추 포함) */
        case 0xCB: case 0xCD: {
            int d = key == 0xCD ? 1 : -1;
            if (vb_field >= 3) { vb_field = d > 0 ? 4 : 3; return 1; }       /* 단추 줄: 저장 ↔ 취소 */
            if (vb_field == 2) { if (!vb_lock) { vb_kindv = (vb_kindv + d + 4) & 3; vb_clamp(); } }   /* 8.0.169: 형식 칸에서 ←/→ = 자동·DOS1·FAT12·FAT16 */
            else if (vb_field == 1 && !vb_lock) {
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
        { int l = vb_letter_of(p); if (l >= 0) { toast(l != vb_drv() ? T(S_VHD_TAKEN) : T(S_VHD_CONN), vhd_dir[l]); return 1; } }   /* 4.0.30: 다른 드라이브에 이미 잡힌 폴더는 못 씀. 5.3.47: 이 드라이브에 잡힌 폴더도 Ins 로 설정 창을 열지 않음 (이미 만든 가상 하드의 형식·용량은 못 바꿈) */
        if (vb_mb[it]) { vb_form_open(0, p, vb_mb[it], vb_kind[it]); vb_lock = 1; vb_field = 3; }   /* 5.3.43: 쓰던 폴더는 용량 고르기 없음. 5.3.47: 형식도 못 고름 → [저장]부터 (형식·용량은 새로 만드는 폴더만) */
        else { copy_str(vb_path, sizeof vb_path, p); vb_ask = 1; vb_btn = 0; }
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
static void hc_vbsize(const UiHit *h, int ev) { if (ev != 1) return; vb_mbv = h->b; vb_field = 1; vb_clamp(); menu_dirty = 1; }      /* 8.0.169 빠른 용량 단추 */
static void hc_vbkind(const UiHit *h, int ev) { if (ev != 1) return; vb_kindv = h->b; vb_field = 2; vb_clamp(); menu_dirty = 1; }    /* 8.0.169 형식 칸 */
static void vb_draw_pop(uint32_t X, uint32_t W, uint32_t py0, uint32_t top, uint32_t bottom)   /* top~bottom = 창이 놓일 수 있는 세로 범위 */
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
            static const int bk[3] = { 0x9C, 0x31, 0x01 };       /* 5.3.33: 마우스로 [이 폴더로] = 0x9C (Enter 0x1C 는 고른 단추) */
            uint32_t bh = LH + 12 * S, bx = px + 16 * S;
            for (int i = 0; i < 3; i++) {
                uint32_t bw = text_w(bl[i]) + 32 * S;
                int fo = i == vb_btn;                   /* 5.3.33: 고른 단추가 파랗게 */
                ui_rframe(bx, ty, bw, bh, 7 * S, fo ? C_ACC : C_LINE, fo ? C_ACC : C_FIELD);
                text_f(F, S, bx + 16 * S, ui_ty(ty, bh), bl[i], fo ? 15 : C_TEXT, 0xFF, 0, 1);
                hit_add(hc_key, bk[i], 0, 0, (uint16_t)bk[i], bx, ty, bw, bh);
                bx += bw + 10 * S;
            }
        }
        ui_cmdbar(px + S, py0 + ph - ui_cmdbar_h() - 6 * S, pw - 2 * S, T(S_VHD_ASK_KEYS));
        return;
    }
    /* 8.0.169 설정 창 다시 디자인: 제목 칩 + 큰 용량 칸 + 폴더가 찬 정도 막대 + 빠른 용량 단추 + 형식 4칸 + 안내 두 줄 */
    uint32_t pad = 20 * S, sgap = 8 * S;
    uint32_t gap_head = 12 * S, gap_section = 10 * S, gap_gauge = 8 * S, gap_note = 10 * S, gap_buttons = 16 * S;   /* 8.0.170 세로 간격을 줄여 화면 아래로 안 잘리게 */
    uint32_t ix = px + pad, iw = pw - 2 * pad;
    uint32_t head_d = LH + SLH + 6 * S, size_h = LH * 2 + 18 * S, chip_h = SLH + 14 * S, pill_h = LH + 14 * S, bth = LH + 12 * S;
    int show_gauge = !vb_new && vb_fold_mb >= 0;
    uint32_t ph = pad + head_d + gap_head
                + (vb_new ? SLH + 4 * S + rh + 14 * S : 0)
                + SLH + 4 * S + size_h + 10 * S + (show_gauge ? 12 * S + 6 * S + SLH + gap_gauge : 0) + chip_h + gap_section
                + SLH + 4 * S + pill_h + 6 * S + SLH + gap_section
                + SLH * 2 + 6 * S + gap_note
                + bth + gap_buttons + ui_cmdbar_h();
    if (py0 + ph + 8 * S > bottom) py0 = bottom > ph + 8 * S + top ? bottom - ph - 8 * S : top;   /* 8.0.170 아래가 넘치면 위로 올림 (맨 위가 한계) */
    ui_rrect(px + 6 * S, py0 + 8 * S, pw, ph, 14 * S, C_SHADOW);
    ui_rframe(px, py0, pw, ph, 14 * S, C_ACC, C_CARD);
    uint32_t ty = py0 + pad;

    /* 제목: 드라이브 글자 칩 + 제목 + 폴더 */
    { char dl[4] = { (char)('C' + vb_drv()), ':', 0 };
      uint32_t cw = text_w(dl) + 28 * S;
      ui_rframe(ix, ty, cw, head_d, 10 * S, C_ACC, C_ACC);
      text_f(F, S, ix + 14 * S, ui_ty(ty, head_d), dl, 15, 0xFF, 0, 1);
      text_clip(ix + cw + 14 * S, ty, vb_new ? nt("새 가상 하드", "New virtual disk", "新しい仮想ハード") : nt("가상 하드 설정", "Virtual disk settings", "仮想ハード設定"), C_TEXT, 0xFF, iw - cw - 14 * S);
      small_ell(ix + cw + 14 * S, ty + LH + 2 * S, vb_new ? cur_disp : path_base(vb_path), C_MUTED, 0xFF, iw - cw - 14 * S); }
    ty += head_d + gap_head;

    /* 이름 (새로 만들 때만) */
    if (vb_new) {
        int on = vb_field == 0;
        small_ell(ix, ty, T(S_VHD_NAME), on ? C_ACC : C_MUTED, 0xFF, iw); ty += SLH + 4 * S;
        ui_rframe(ix, ty, iw, rh, 8 * S, on ? C_ACC : C_LINE, C_FIELD);
        hit_add(hc_sel, (intptr_t)&vb_field, 0, 0, 0xCD, ix, ty, iw, rh);
        char v[48]; copy_str(v, sizeof v, vb_name); if (on) str_append(v, sizeof v, "_");
        text_ell(ix + 14 * S, ui_ty(ty, rh), v, C_TEXT, 0xFF, iw - 28 * S);
        ty += rh + 14 * S;
    }

    /* 용량: 큰 숫자 칸 (←/→ 로 바꿈) */
    { int on = vb_field == 1;
      small_ell(ix, ty, T(S_VHD_SIZE), on ? C_ACC : C_MUTED, 0xFF, iw); ty += SLH + 4 * S;
      ui_rframe(ix, ty, iw, size_h, 10 * S, on ? C_ACC : C_LINE, C_FIELD);
      if (!vb_lock) hit_add(hc_sel, (intptr_t)&vb_field, 1, 0, 0xCD, ix, ty, iw, size_h);
      char num[16] = ""; num_append(num, sizeof num, (uint32_t)vb_mbv, 0, ' ');
      uint32_t nw = text_f(F, S * 2, 0, 0, num, 0, 0, 0, 0), mw = text_w("MB"), tx = ix + (iw - nw - 8 * S - mw) / 2, vy = ty + (size_h - LH * 2) / 2;
      text_f(F, S * 2, tx, vy, num, vb_lock ? C_MUTED : C_TEXT, 0xFF, 0, 1);
      text_f(F, S, tx + nw + 8 * S, vy + LH, "MB", C_MUTED, 0xFF, 0, 1);
      if (on && !vb_lock) { text_f(F, S * 2, ix + 16 * S, vy, "‹", C_ACC, 0xFF, 0, 1); text_f(F, S * 2, ix + iw - 16 * S - text_f(F, S * 2, 0, 0, "›", 0, 0, 0, 0), vy, "›", C_ACC, 0xFF, 0, 1); }
      ty += size_h + 10 * S; }

    /* 폴더가 찬 정도: 막대 + 글자 (이미 있는 폴더일 때) */
    if (show_gauge) {
        uint32_t bar_h = 12 * S, used = (uint32_t)vb_fold_mb, size = (uint32_t)vb_mbv;
        int over = used >= size, tight = used * 100 >= size * 92;
        uint8_t col = over ? C_BAD : tight ? C_WARN : C_OK;
        uint32_t fw = size ? (uint32_t)((uint64_t)iw * (used > size ? size : used) / size) : 0;
        ui_rrect(ix, ty, iw, bar_h, bar_h / 2, C_HEAD);
        if (fw >= bar_h) ui_rrect(ix, ty, fw, bar_h, bar_h / 2, col); else if (fw) ui_rrect(ix, ty, bar_h, bar_h, bar_h / 2, col);
        ty += bar_h + 6 * S;
        char l1[64], l2[96];
        copy_str(l1, sizeof l1, nt("폴더 안 파일 약 ", "Files in folder about ", "フォルダ内ファイル 約 ")); num_append(l1, sizeof l1, used, 0, ' '); str_append(l1, sizeof l1, " MB");
        small_ell(ix, ty, l1, C_MUTED, 0xFF, iw / 2 - 8 * S);
        if (over) { copy_str(l2, sizeof l2, vb_lock ? nt("용량이 모자랍니다", "Not enough room", "容量不足") : nt("모자라서 저장하면 용량을 올립니다", "Saving will raise the size", "不足のため保存すると容量を上げます")); }
        else { copy_str(l2, sizeof l2, nt("여유 약 ", "Free about ", "空き 約 ")); num_append(l2, sizeof l2, size - used, 0, ' '); str_append(l2, sizeof l2, " MB"); }
        uint32_t w2 = ui_sw(l2), maxw = iw / 2; if (w2 > maxw) w2 = maxw;
        small_ell(ix + iw - w2, ty, l2, over ? C_BAD : tight ? C_WARN : C_OK, 0xFF, maxw);
        ty += SLH + gap_gauge;
    }

    /* 빠른 용량 단추 */
    { static const uint16_t quick[7] = { 8, 32, 64, 128, 512, 1024, 4095 };
      int k = vb_kind_eff();
      uint32_t cw = (iw - 6 * sgap) / 7;
      for (int q = 0; q < 7; q++) {
          uint32_t cx = ix + (uint32_t)q * (cw + sgap);
          int ok = !vb_lock && quick[q] >= vf_min_mb(k) && quick[q] <= vf_max_mb(k), sel = vb_mbv == quick[q];
          char t[16] = ""; num_append(t, sizeof t, quick[q], 0, ' ');
          ui_rframe(cx, ty, cw, chip_h, chip_h / 2, sel ? C_ACC : C_LINE, sel ? C_ACC : C_FIELD);
          uint32_t tw = ui_sw(t);
          small_ell(cx + (cw > tw ? (cw - tw) / 2 : 0), ty + (chip_h - SLH) / 2, t, sel ? 15 : ok ? C_TEXT : C_FAINT, 0xFF, cw);
          if (ok) hit_add(hc_vbsize, 0, quick[q], 0, 0, cx, ty, cw, chip_h);
      }
      ty += chip_h + gap_section; }

    /* 형식: 자동 / DOS1 / FAT12 / FAT16 (누르거나 ←/→) */
    { int on = vb_field == 2;
      small_ell(ix, ty, T(S_VHD_FMT), on ? C_ACC : C_MUTED, 0xFF, iw); ty += SLH + 4 * S;
      ui_rframe(ix, ty, iw, pill_h, pill_h / 2, on ? C_ACC : C_LINE, C_FIELD);
      vb_fy = ty;
      const char *nm[4] = { nt("자동", "Auto", "自動"), "DOS1", "FAT12", "FAT16" };   /* 칸이 좁아 짧게 (긴 설명은 아래 안내 줄) */
      uint32_t pin = 4 * S, pw4 = (iw - 2 * pin) / 4;
      for (int q = 0; q < 4; q++) {
          uint32_t qx = ix + pin + (uint32_t)q * pw4; int sel = vb_kindv == q;
          if (sel) ui_rframe(qx, ty + pin, pw4, pill_h - 2 * pin, (pill_h - 2 * pin) / 2, C_ACC, C_ACC);
          uint32_t tw = text_w(nm[q]); if (tw > pw4 - 8 * S) tw = pw4 - 8 * S;
          text_clip(qx + (pw4 - tw) / 2, ui_ty(ty, pill_h), nm[q], sel ? 15 : vb_lock ? C_FAINT : C_TEXT, 0xFF, pw4 - 8 * S);
          if (!vb_lock) hit_add(hc_vbkind, 0, q, 0, 0, qx, ty, pw4, pill_h);
      }
      ty += pill_h + 6 * S;
      int e = vb_kind_eff();
      char ef[96]; copy_str(ef, sizeof ef, nt("사용 형식 ", "Format in use ", "使用する形式 ")); str_append(ef, sizeof ef, e == VF_DOS1 ? "FAT12 DOS1" : e == VF_FAT12 ? "FAT12" : "FAT16");
      str_append(ef, sizeof ef, "  ·  "); num_append(ef, sizeof ef, vf_min_mb(e), 0, ' '); str_append(ef, sizeof ef, " ~ "); num_append(ef, sizeof ef, vf_max_mb(e), 0, ' '); str_append(ef, sizeof ef, " MB");
      small_ell(ix, ty, ef, C_MUTED, 0xFF, iw);
      ty += SLH + gap_section; }

    /* 안내 두 줄 */
    small_ell(ix, ty, nt("MSX-DOS1 = FAT12 (15 MB까지)  ·  MSX-DOS2·Nextor = FAT16 (4 GB까지)", "MSX-DOS1 = FAT12 (up to 15 MB)  ·  MSX-DOS2/Nextor = FAT16 (up to 4 GB)", "MSX-DOS1 = FAT12 (15MBまで)  ·  MSX-DOS2・Nextor = FAT16 (4GBまで)"), C_MUTED, 0xFF, iw);
    ty += SLH + 6 * S;
    small_ell(ix, ty, nt("MSX 가 쓴 내용은 2초 뒤 USB 폴더에 반영됩니다.", "What the MSX writes is saved to the USB folder after 2 seconds.", "MSXの書き込みは2秒後にUSBフォルダへ反映されます。"), C_MUTED, 0xFF, iw);
    ty += SLH + gap_note;

    /* [저장] [취소] */
    const char *bs = T(S_VHD_SAVE), *bc = T(S_VHD_CANCEL);
    uint32_t bw1 = text_w(bs) + 44 * S, bw2 = text_w(bc) + 44 * S, bx = px + pw - pad - bw1 - bw2 - 10 * S;
    ui_rframe(bx, ty, bw1, bth, bth / 2, vb_field == 3 ? 15 : C_ACC, C_ACC);
    text_f(F, S, bx + 22 * S, ui_ty(ty, bth), bs, 15, 0xFF, 0, 1);
    hit_add(hc_key, 0x9C, 0, 0, 0x9C, bx, ty, bw1, bth);
    ui_rframe(bx + bw1 + 10 * S, ty, bw2, bth, bth / 2, vb_field == 4 ? C_ACC : C_LINE, C_FIELD);
    text_f(F, S, bx + bw1 + 32 * S, ui_ty(ty, bth), bc, vb_field == 4 ? C_ACC : C_TEXT, 0xFF, 0, 1);
    hit_add(hc_key, 0x01, 0, 0, 0x01, bx + bw1 + 10 * S, ty, bw2, bth);
    ui_cmdbar(px + S, py0 + ph - ui_cmdbar_h() - 6 * S, pw - 2 * S, T(S_VHD_FORM_KEYS));
    if (dd_on && dd_id == 3001) dd_draw_at(ix, vb_fy + pill_h, iw, py0 + ph);
}

/* 8.0.65: 메모리 매퍼 RAM 이 작은 기종(Nextor 가 DOS1 모드로 올라옴)에서 DOS2 볼륨(가상 하드 폴더·하드 이미지)을 쓰면 안내.
 * 기종 바꿈·가상 하드 연결·하드 이미지 삽입·시작할 때. 자동으로 매퍼를 꽂지는 않음 (카트 자리를 쓰므로 마스터님이 고르게) */
static void dos2_mapper_hint(void)
{
    uint32_t seg = (M->mram_size > M->xram_size ? M->mram_size : M->xram_size) >> 14;
    if (seg >= 6) return;                               /* vhd_io DRV_CONFIG 와 같은 기준 */
    int need = 0;
    if (vhd_cart() >= 0) {
        for (int i = 0; i < VHD_N; i++)
            if (vhd_up[i] && vhd_vol[i].kind != VF_DOS1 && !(vhd_vol[i].kind == VF_FAT12 && vhd_vol[i].dos1geo)) need = 1;
        if (hd_secs && hd_has_nextor() > 0) need = 1;
    }
    if (!need) return;
    toast_ms = 7000;
    toast(grow_t("MSX-DOS2를 실행하려면 메모리 매퍼 RAM을 꽂아야 합니다.", "MSX-DOS2 needs a memory mapper cartridge.", "MSX-DOS2を使うにはメモリマッパーRAMが必要です。"),
          grow_t("이 기종은 매퍼 RAM이 작습니다. 카트리지 슬롯 > 특수 카트리지 > 외장 RAM(256 KB 이상)", "This machine has too little mapper RAM. Insert External RAM (256 KB or more) as a special cartridge.", "この機種はマッパーRAMが少なめです。特殊カートリッジの外部RAM(256KB以上)を挿してください。"));
}
