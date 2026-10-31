/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * fehost.c - 프런트엔드(fe.c)를 호스트에서 돌리는 시험 틀 (2.16)
 *
 * UEFI 플랫폼 대신 리눅스 파일 시스템("0:\" = ROOT 폴더)과 메모리 프레임 버퍼를 쓴다.
 * 메뉴·머신 편집기 화면을 키 입력으로 움직여 PPM으로 저장하고, 키 하나 처리에 걸린 시간을 잰다.
 *
 *   ROOT=/tmp/usb W=1280 H=800 KEYS="f10,down,enter" GAP=20 SHOT=out ./build/fehost
 *   KEYS: 쉼표로 나눈 키 이름 (up down left right enter esc tab space bs del home end pgup pgdn
 *         f1~f12 a~z 0~9 shift+X ctrl+X alt+X, wait = 한 칸 쉼). GAP 프레임마다 하나.
 *   SHOT: 각 키 처리 뒤 마지막 화면을 SHOT_NN.ppm으로 (SHOTALL=1이면 키마다)
 */
#include "../kernel/fe.h"
#include "../src/msx.h"
#include "../kernel/machine.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <math.h>
#include <pthread.h>

/* 8.0.23: FEPAR=1 이면 화면 줄 그리기를 진짜 스레드로 나눠 돌림 (UEFI 일꾼 코어 흉내) — 혼자 그린 화면과 해시가 같은지 시험 */
typedef struct { void (*fn)(void *); void *a; } HPar;
static void *h_par_thr(void *x) { HPar *h = (HPar *)x; h->fn(h->a); return 0; }
#define H_JOBS 8
static pthread_t h_job_t[H_JOBS]; static HPar h_job[H_JOBS]; static int h_job_used[H_JOBS];   /* 8.0.35: 영상 압축이 여러 장을 동시에 올림 */
static int h_job_post(void (*fn)(void *), void *arg)
{
    for (int i = 0; i < H_JOBS; i++) if (!h_job_used[i]) { h_job_used[i] = 1; h_job[i].fn = fn; h_job[i].a = arg; pthread_create(&h_job_t[i], 0, h_par_thr, &h_job[i]); return i; }
    return -1;
}
static int h_job_wait(int h) { pthread_join(h_job_t[h], 0); h_job_used[h] = 0; return 1; }
static int h_par_run(void (*fn)(void *), void **args, int n)
{
    pthread_t t[16]; HPar h[16]; if (n > 16) n = 16;
    for (int i = 1; i < n - (getenv("FEPARBUG") ? 1 : 0); i++) { h[i].fn = fn; h[i].a = args[i]; pthread_create(&t[i], 0, h_par_thr, &h[i]); }
    fn(args[0]);
    for (int i = 1; i < n - (getenv("FEPARBUG") ? 1 : 0); i++) pthread_join(t[i], 0);
    return n - 1;
}
void gamma_lut(unsigned char *out, int g100)   /* kernel/libm.c와 같은 결과 (호스트 libm) */
{
    if (g100 < 10) g100 = 10;
    for (int i = 0; i < 256; i++) { int r = i ? (int)(255.0 * pow(i / 255.0, 100.0 / g100) + 0.5) : 0; out[i] = (unsigned char)(r > 255 ? 255 : r); }
}

int errno_dir_exists(const char *p);
static const char *root;
static uint64_t dirty_px_total;
static int dirty_bad;
static uint32_t W = 1280, H = 800;
static uint8_t *fb;

static int cur_frame;
static int h_vd(char (*l)[64], int max) { (void)max; strcpy(l[0], "> 1: 1920x1080 AMD 15BF - DELL U2720Q"); strcpy(l[1], "    modes 12 fmt 1 fb E000xxxx 8MB edid 128"); strcpy(l[2], "  2: 1920x1080 NVIDIA 2803 - no monitor info"); strcpy(l[3], "    modes 9 fmt 1 fb D000xxxx 8MB edid -"); return 4; }
static int h_vo(char (*l)[64], int max, int *act) { (void)max; strcpy(l[0], "1: 1920x1080 AMD 15BF"); strcpy(l[1], "2: 1920x1080 NVIDIA 2803"); *act = 0; return 2; }
static uint32_t h_ms(void) { static int lf = -1; static uint32_t ex; if (lf != (int)cur_frame) { lf = (int)cur_frame; ex = 0; } else ex++; return (uint32_t)(10000 + cur_frame * 1000 / 60) + ex; }   /* 같은 프레임 안에서 기다리는 반복이 끝나도록 한 번 부를 때마다 1 ms씩 */   /* 가상 시계: 프레임마다 1/60초 */

/* 3.0 마우스 시험: MOUSE="프레임,dx,dy,dz,버튼;…" — 그 프레임에 움직임·버튼 상태(다음 줄까지 유지) */
/* 3.4.4: MIDIIN=16진 바이트열 → 시험용 USB MIDI 장치 (MIDIIN_F 프레임째에 한 번 보냄) */
static int h_frame;
static int h_midi_read(uint8_t *buf, int max, char *name, int nmax)
{
    const char *e = getenv("MIDIIN");
    if (!e) return -1;
    snprintf(name, (size_t)nmax, "Test MIDI");
    static int sent;
    int at = getenv("MIDIIN_F") ? atoi(getenv("MIDIIN_F")) : 60;
    if (sent || h_frame < at) return 0;
    sent = 1;
    int n = 0;
    for (const char *q = e; q[0] && q[1] && n < max; q += 2) { unsigned v; sscanf(q, "%2x", &v); buf[n++] = (uint8_t)v; }
    return n;
}
/* 3.5.4: MIDIOUT=파일 → 시험용 USB MIDI 출력 장치 (USB-MIDI 패킷을 파일에) */
#include "../src/midipkt.h"
static int h_midi_write(const uint8_t *buf, int n, char *name, int nmax)
{
    const char *e = getenv("MIDIOUT");
    if (!e) return -1;
    if (name && nmax > 0) snprintf(name, (size_t)nmax, "Test MIDI out");
    static FILE *f; static MidiPk pk;
    if (!f) f = fopen(e, "wb");
    for (int i = 0; i < n && f; i++) { uint8_t o[4]; if (midipk_feed(&pk, buf[i], o)) fwrite(o, 1, 4, f); }
    if (f) fflush(f);
    return 0;
}
/* 3.4.4: 진단 화면 시험용 플랫폼 줄 (INFO=1일 때) */
static int h_info(char (*lines)[64], int max)
{
    if (getenv("INFOFILE")) {                            /* 실제 DIAG.TXT 의 == info == 줄 (진단 요약 시험) */
        FILE *f = fopen(getenv("INFOFILE"), "rb"); char s[256]; int n = 0, on = 0;
        while (f && fgets(s, sizeof s, f) && n < max) { s[strcspn(s, "\r\n")] = 0; if (!strcmp(s, "== info ==")) { on = 1; continue; } if (!strncmp(s, "== steps", 8)) break; if (on) snprintf(lines[n++], 64, "%s", s); }
        if (f) fclose(f);
        if (n < max) snprintf(lines[n++], 64, "SMBIOS system LG Electronics 16Z90S-GA5VK");
        if (n < max) snprintf(lines[n++], 64, "SMBIOS bios Phoenix Technologies Ltd. A1ZG0450 X64 05/2024");
        return n;
    }
    static const char *const t[] = { "boot mode: host test", "PS/2 mouse: on (wheel), bytes 12/3", "USB mouse if0 x8/8 wheel ep81 mps8",
        "  packets 10, mouse reports 9", "  mouse last: 01 02 03", "HID 046D:C52B iface 1 1/2 -> mouse (Logitech)", "HID 1234:5678 iface 0 1/1 -> keyboard", "USB mice: 1", "USB MIDI in: bulk, packets 3" };
    int n = 0;
    for (unsigned i = 0; i < sizeof t / sizeof t[0] && n < max; i++) snprintf(lines[n++], 64, "%s", t[i]);
    return n;
}
/* 3.8.1: 가짜 조이스틱 FEPAD="x,y,hat,버튼마스크" (축 0~255) — 진단 조이스틱 탭 화면 시험 */
static int h_pad_view(int idx, PadView *v, char *name, int max)
{
    const char *e = getenv("FEPAD");
    if (!e || idx) return -1;
    int x = 128, y = 128, hat = -1, b = 0;
    sscanf(e, "%d,%d,%d,%i", &x, &y, &hat, &b);
    memset(v, 0, sizeof *v);
    v->kind = 1; v->x = x; v->y = y; v->xmin = v->ymin = 0; v->xmax = v->ymax = 255; v->hat = hat; v->btn = (uint32_t)b; v->nbtn = 12;
    v->dec = (x < 64 ? 0x04 : 0) | (x > 192 ? 0x08 : 0) | (y < 64 ? 0x01 : 0) | (y > 192 ? 0x02 : 0) | ((b & 1) ? 0x10 : 0) | ((b & 2) ? 0x20 : 0) | ((b & 0x200) ? 0x40 : 0);
    snprintf(name, (size_t)max, "USB gamepad");
    return 1;
}
static int h_off;
static int h_mouse_dev(int n, char *name, int max, int *on)
{
    int nd = getenv("MICE") ? atoi(getenv("MICE")) : 0;
    if (n >= nd) return 0;
    snprintf(name, (size_t)max, n == nd - 1 && nd > 1 ? "PS/2 mouse / touchpad" : "USB mouse if%d", n);
    *on = !((h_off >> n) & 1); return 1;
}
static void h_mouse_set(int n, int on) { h_off = on ? (h_off & ~(1 << n)) : (h_off | (1 << n)); }
static int h_mouse_read(int *dx, int *dy, int *dz, int *b, char *name, int max)
{
    static int btn;
    *dx = *dy = *dz = 0; *b = btn;
    const char *e = getenv("MOUSE");
    if (!e) return 0;
    for (const char *q = e; *q; ) {
        int f, x, y, z, bb;
        if (sscanf(q, "%d,%d,%d,%d,%d", &f, &x, &y, &z, &bb) == 5 && f == cur_frame) { *dx = x; *dy = y; *dz = z; *b = btn = bb; }
        while (*q && *q != ';') q++;
        if (*q) q++;
    }
    if (name && max > 0) snprintf(name, (size_t)max, "host mouse");
    return 1;
}
static void h_flush_shot(void);
static void h_dump_line(const char *s) { printf("%s\n", s); }
static void h_reboot(void) { printf("reboot requested\n"); fflush(stdout); exit(3); }
static void h_audio(const int16_t *s, int n, int v, int m)    /* 3.4.1: WAV=파일 이면 원시 16비트 모노로 덧붙임 */
{
    (void)v; (void)m;
    static FILE *wf; static int tried;
    if (!tried) { tried = 1; if (getenv("WAV")) wf = fopen(getenv("WAV"), "wb"); }
    if (wf) fwrite(s, 2, (size_t)n, wf);
}

static void host_path(const char *p, char *out, size_t max)
{
    /* "0:\A\B" → ROOT/A/B */
    const char *c = strchr(p, ':');
    c = c ? c + 1 : p;
    snprintf(out, max, "%s", root);
    size_t k = strlen(out);
    for (; *c && k + 1 < max; c++) out[k++] = *c == '\\' ? '/' : *c;
    out[k] = 0;
}

static int h_list(const char *path, FeDirEntry *out, int max)
{
    if (!path[0]) {
        fe_list_skip = 0; fe_list_next = -1;
        if (max < 1) return 0;
        memset(out, 0, sizeof *out);
        snprintf(out->name, sizeof out->name, "Drive 1 X86MSX (boot)");
        snprintf(out->key, sizeof out->key, "0:");
        out->is_dir = 1;
        return 1;
    }
    char hp[2048]; host_path(path, hp, sizeof hp);
    DIR *d = opendir(hp);
    if (!d) return -1;
    int n = 0, raw = -1, base = fe_list_skip;
    fe_list_skip = 0; fe_list_next = -1;
    struct dirent *e;
    while ((e = readdir(d))) {
        raw++;
        if (raw < base) continue;
        if (n >= max) { fe_list_next = raw; break; }
        if (e->d_name[0] == '.') continue;
        char fp[2400]; snprintf(fp, sizeof fp, "%s/%s", hp, e->d_name);
        struct stat st; if (stat(fp, &st)) continue;
        FeDirEntry *o = &out[n++];
        memset(o, 0, sizeof *o);
        snprintf(o->key, sizeof o->key, "%s", e->d_name);   /* 3.4.7: 긴 이름 그대로 */
        { int k = 0; for (; e->d_name[k] && k < (int)sizeof o->name - 1; k++) o->name[k] = e->d_name[k];
          if (e->d_name[k] && ((unsigned char)e->d_name[k] & 0xC0) == 0x80) { while (k > 0 && ((unsigned char)o->name[k - 1] & 0xC0) == 0x80) k--; if (k > 0) k--; }
          o->name[k] = 0; }
        o->is_dir = S_ISDIR(st.st_mode);
        o->size = (uint32_t)st.st_size;
    }
    closedir(d);
    return n;
}

static int h_read_at(const char *path, uint32_t off, uint8_t *buf, uint32_t len)   /* 3.7.1 */
{
    char hp[2048]; host_path(path, hp, sizeof hp);
    FILE *f = fopen(hp, "rb");
    if (!f) return -1;
    memset(buf, 0, len);
    fseek(f, off, SEEK_SET); size_t r = fread(buf, 1, len, f); (void)r;
    fclose(f);
    return 0;
}
static int h_read(const char *path, uint8_t *buf, uint32_t max, uint32_t *size)
{
    char hp[2048]; host_path(path, hp, sizeof hp);
    FILE *f = fopen(hp, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    *size = (uint32_t)n;
    size_t r = fread(buf, 1, n < (long)max ? (size_t)n : max, f);
    (void)r;
    fclose(f);
    return 0;
}

static FILE *hd_f[4];                                   /* 3.4.1: 하드 디스크 이미지 (3.4.3: 마스터·슬레이브) */
static int h_hd_open(int u, const char *path, uint32_t *secs)
{
    char hp[2048]; host_path(path, hp, sizeof hp);
    if (u < 0 || u > 3) return -1;
    if (hd_f[u]) fclose(hd_f[u]);
    hd_f[u] = fopen(hp, "r+b");
    if (!hd_f[u]) return -1;
    fseek(hd_f[u], 0, SEEK_END); *secs = (uint32_t)(ftell(hd_f[u]) / 512);
    return 0;
}
static void h_hd_close(int u) { if (u < 0 || u > 3) return; if (hd_f[u]) fclose(hd_f[u]); hd_f[u] = 0; }
static int h_hd_read(int u, uint32_t lba, uint8_t *b) { FILE *f = u >= 0 && u <= 3 ? hd_f[u] : 0; if (!f || fseek(f, (long)lba * 512, SEEK_SET) || fread(b, 1, 512, f) != 512) return -1; return 0; }
static int h_hd_write(int u, uint32_t lba, const uint8_t *b) { FILE *f = u >= 0 && u <= 3 ? hd_f[u] : 0; if (!f || fseek(f, (long)lba * 512, SEEK_SET) || fwrite(b, 1, 512, f) != 512) return -1; return 0; }

static int h_write(const char *path, const uint8_t *data, uint32_t len)
{
    char hp[2048]; host_path(path, hp, sizeof hp);
    FILE *f = fopen(hp, "wb");
    if (!f) return -1;
    fwrite(data, 1, len, f);
    fclose(f);
    printf("write %s (%u bytes)\n", path, len);
    return 0;
}
static int h_append(const char *path, const uint8_t *data, uint32_t len)   /* 5.2.69 */
{
    char hp[2048]; host_path(path, hp, sizeof hp);
    FILE *f = fopen(hp, "ab");
    if (!f) return -1;
    size_t w = fwrite(data, 1, len, f);
    fclose(f);
    return w == len ? 0 : -1;
}
static int h_write_at(const char *path, uint32_t off, const uint8_t *data, uint32_t len)
{
    char hp[2048]; host_path(path, hp, sizeof hp);
    FILE *f = fopen(hp, "r+b");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    if ((uint64_t)ftell(f) < (uint64_t)off + len) { fclose(f); return -1; }
    fseek(f, off, SEEK_SET);
    fwrite(data, 1, len, f);
    fclose(f);
    printf("write_at %s (+%u, %u bytes)\n", path, off, len);
    return 0;
}
static int h_make_file(const char *path, uint32_t size, const uint8_t *prefix, uint32_t plen, void (*progress)(int))
{
    char hp[2048]; host_path(path, hp, sizeof hp);
    FILE *f = fopen(hp, "rb"); if (f) { fclose(f); return -1; }
    f = fopen(hp, "wb"); if (!f) return -1;
    if (plen) fwrite(prefix, 1, plen, f);
    if (size) { fseek(f, (long)size - 1, SEEK_SET); fputc(0, f); }
    fclose(f);
    if (progress) progress(100);
    return 0;
}
static int h_create(const char *path, int is_dir)
{
    char hp[2048]; host_path(path, hp, sizeof hp);
    if (is_dir) return mkdir(hp, 0755) && errno_dir_exists(hp) ? -1 : 0;
    FILE *f = fopen(hp, "ab"); if (!f) return -1; fclose(f); return 0;
}

static int h_delete(const char *path) { char hp[2048]; host_path(path, hp, sizeof hp); return remove(hp) ? -1 : 0; }

static void shot(const char *name)
{
    FILE *f = fopen(name, "wb");
    if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", W, H);
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++) {
            uint8_t *p = fb + ((size_t)y * W + x) * 4;
            uint8_t rgb[3] = { p[2], p[1], p[0] };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

/* 5.3.9: USB 확장 진행 화면 시험용 가짜 (GROWSHOT=파일, GROWPCT=퍼센트, GROWSTAGE=단계 이름) */
static int h_grow_query(uint64_t *a, uint64_t *b) { *a = 63ull << 20; *b = 14ull << 30; return 0; }
static int h_grow_run(void (*pr)(int, const char *), char *err, int n)
{
    (void)err; (void)n;
    const char *pc = getenv("GROWPCT"), *st = getenv("GROWSTAGE");
    if (getenv("GROWANIM")) {                             /* 5.3.10: 접시가 도는 모습을 GROWANIM 장 찍음 (60 ms 마다) */
        int n = atoi(getenv("GROWANIM"));
        static const char *const sq[] = { "scan", "list", "format", "FAT", "dirs", "copy", "verify", "mbr" };
        for (int i = 0; i < n; i++) {
            int pct = i * 100 / (n - 1 ? n - 1 : 1);
            pr(pct, sq[pct < 5 ? 0 : pct < 10 ? 1 : pct < 11 ? 2 : pct < 21 ? 3 : pct < 25 ? 4 : pct < 71 ? 5 : pct < 97 ? 6 : 7]);
            char nm[256]; snprintf(nm, sizeof nm, "%s_%03d.ppm", getenv("GROWANIM_OUT") ? getenv("GROWANIM_OUT") : "/tmp/ga", i); shot(nm);
            usleep(60000);
        }
        return getenv("GROWRET") ? atoi(getenv("GROWRET")) : 1;
    }
    pr(pc ? atoi(pc) : 46, st ? st : "copy");
    if (getenv("GROWSHOT")) shot(getenv("GROWSHOT"));
    return getenv("GROWRET") ? atoi(getenv("GROWRET")) : 1;
}

/* 4.0: 진행 창처럼 P.flush를 부르는 동안의 화면을 FLUSHSHOT_<번호>.ppm으로 (FLUSHN=쉼표로 나눈 번호) */
static void h_flush_shot(void)
{
    static int n; n++;
    const char *l = getenv("FLUSHN");
    for (const char *c = l ? l : ""; *c; ) { if (atoi(c) == n) { char nm[256]; snprintf(nm, sizeof nm, "%s_f%03d.ppm", getenv("FLUSHSHOT"), n); shot(nm); break; } while (*c && *c != ',') c++; if (*c) c++; }
}

typedef struct { const char *n; uint8_t c; int e0; } KeyName;
static const KeyName keys[] = {
    { "caps", 0x3A, 0 }, { "esc", 0x01, 0 }, { "1", 0x02, 0 }, { "2", 0x03, 0 }, { "3", 0x04, 0 }, { "4", 0x05, 0 }, { "5", 0x06, 0 },
    { "6", 0x07, 0 }, { "7", 0x08, 0 }, { "8", 0x09, 0 }, { "9", 0x0A, 0 }, { "0", 0x0B, 0 }, { "minus", 0x0C, 0 },
    { "bs", 0x0E, 0 }, { "tab", 0x0F, 0 }, { "q", 0x10, 0 }, { "w", 0x11, 0 }, { "e", 0x12, 0 }, { "r", 0x13, 0 },
    { "t", 0x14, 0 }, { "y", 0x15, 0 }, { "u", 0x16, 0 }, { "i", 0x17, 0 }, { "o", 0x18, 0 }, { "p", 0x19, 0 },
    { "enter", 0x1C, 0 }, { "a", 0x1E, 0 }, { "s", 0x1F, 0 }, { "d", 0x20, 0 }, { "f", 0x21, 0 }, { "g", 0x22, 0 },
    { "h", 0x23, 0 }, { "j", 0x24, 0 }, { "k", 0x25, 0 }, { "l", 0x26, 0 }, { "z", 0x2C, 0 }, { "x", 0x2D, 0 },
    { "c", 0x2E, 0 }, { "v", 0x2F, 0 }, { "b", 0x30, 0 }, { "n", 0x31, 0 }, { "m", 0x32, 0 }, { "space", 0x39, 0 },
    { "f1", 0x3B, 0 }, { "f2", 0x3C, 0 }, { "f3", 0x3D, 0 }, { "f4", 0x3E, 0 }, { "f5", 0x3F, 0 }, { "f6", 0x40, 0 },
    { "f7", 0x41, 0 }, { "f8", 0x42, 0 }, { "f9", 0x43, 0 }, { "f10", 0x44, 0 }, { "f11", 0x57, 0 }, { "f12", 0x58, 0 },
    { "up", 0x48, 1 }, { "down", 0x50, 1 }, { "left", 0x4B, 1 }, { "right", 0x4D, 1 }, { "home", 0x47, 1 },
    { "end", 0x4F, 1 }, { "pgup", 0x49, 1 }, { "pgdn", 0x51, 1 }, { "del", 0x53, 1 }, { "ins", 0x52, 1 },
    { "dot", 0x34, 0 }, { "slash", 0x35, 0 }, { "quote", 0x28, 0 }, { "lbr", 0x1A, 0 }, { "rbr", 0x1B, 0 }, { "semi", 0x27, 0 }, { "equal", 0x0D, 0 }, { "comma", 0x33, 0 }, { "prtsc", 0x37, 1 }, { "ralt", 0x38, 1 }, { "rctrl", 0x1D, 1 }, { 0, 0, 0 }
};
static const KeyName *find_key(const char *n)
{
    for (int i = 0; keys[i].n; i++) if (!strcmp(keys[i].n, n)) return &keys[i];
    return 0;
}
static void press(const KeyName *k, int down)
{
    if (k->e0) fe_push_key(0xE0);
    fe_push_key((uint8_t)(k->c | (down ? 0 : 0x80)));
}

int errno_dir_exists(const char *p) { struct stat st; return !(stat(p, &st) == 0 && S_ISDIR(st.st_mode)); }

int fe_test_net;
/* FENETON=1 이면 "인터넷 연결됨" (레거시 판 net_online 의 약한 연결을 시험 틀이 채움) — 인터넷 아이콘 시험 */
/* 8.0.34: 시스템 보기 > 성능 시험용 흉내 수치 (호출할 때마다 조금씩 변함). FESYS=1 일 때만 연결 */
static int h_sys_sample(FeSysSample *o)
{
    static uint64_t n, mb, w0[5], w1[5], iom; static uint32_t ior, iow;
    n++;
    memset(o, 0, sizeof *o);
    const uint64_t hz = 2300000000ull;
    mb += (uint64_t)((double)hz * (0.10 + 0.05 * (double)((n * 7) % 5) / 4.0));
    for (int i = 0; i < 5; i++) { w0[i] += (uint64_t)((double)hz * (0.04 + 0.03 * (double)((n * (uint64_t)(i + 3)) % 6) / 5.0)); if (i == 4) w1[i] += (uint64_t)((double)hz * (0.12 + 0.06 * (double)((n * 5) % 4) / 3.0)); }
    iom += n % 7 == 0 ? 90 : 3; ior += 2; iow += n % 3 == 0;
    snprintf(o->cpu_name, sizeof o->cpu_name, "12th Gen Intel(R) Core(TM) i7-12700H");
    o->logical = 20; o->workers = 5; o->tsc_hz = hz; o->tsc = n * hz; o->main_busy = mb;
    for (int i = 0; i < 5; i++) { o->wk_busy[0][i] = w0[i]; o->wk_busy[1][i] = w1[i]; }
    o->mem_total = 31ull << 30; o->mem_free = (29ull << 30) - n * (1ull << 20); o->mem_prog = 6ull << 20; o->mem_data = 420ull << 20; o->mem_fw = o->mem_total - o->mem_free - o->mem_prog - o->mem_data;
    o->io_ms = iom; o->io_r = ior; o->io_w = iow;
    o->io_n = 5; for (int i = 0; i < 5; i++) { o->io[i].at = 8200 + (uint32_t)i * 12000; o->io[i].ms = i == 0 ? 114 : i == 3 ? 56 : 23; o->io[i].w = (uint8_t)(i & 1); snprintf(o->io[i].path, sizeof o->io[i].path, i == 0 ? "Machines\\A1GT\\a1gtfirm.rom" : i == 3 ? "x86msx.ini" : "Storage Device\\DISK"); }
    return 1;
}
int net_online(void) { return getenv("FENETON") != 0; }
/* 시험용 가짜 네트워크 (FENETHOST=1): 네트워크 올라 있음, 에코 요청에 바로 응답 */
#include "../src/netcard.h"
static int th_state(void *c) { (void)c; return NC_NET_UP; }
static int th_ipinfo(void *c, uint8_t o[20]) { (void)c; static const uint8_t v[20] = {192,168,0,50, 255,255,255,0, 192,168,0,1, 8,8,8,8, 0,0,0,0}; memcpy(o, v, 20); return 0; }
static uint8_t th_eq[11]; static int th_has;
static int th_echo_send(void *c, const uint8_t ip[4], uint8_t ttl, uint16_t id, uint16_t seq, uint16_t len)
{ (void)c; printf("ECHO_SEND %u.%u.%u.%u ttl=%u id=%u seq=%u len=%u\n", ip[0], ip[1], ip[2], ip[3], ttl, id, seq, len);
  memcpy(th_eq, ip, 4); th_eq[4] = 57; th_eq[5] = (uint8_t)id; th_eq[6] = (uint8_t)(id >> 8); th_eq[7] = (uint8_t)seq; th_eq[8] = (uint8_t)(seq >> 8); th_eq[9] = (uint8_t)len; th_eq[10] = (uint8_t)(len >> 8); th_has = 1; return 0; }
static int th_echo_recv(void *c, uint8_t o[11]) { (void)c; if (!th_has) return NC_E_NODATA; memcpy(o, th_eq, 11); th_has = 0; printf("ECHO_RECV\n"); return 0; }
static NetHost th_host;

static void h_diag_save(const char *head)   /* DIAG.TXT 머리 (fe 가 만든 시스템·소리 줄) */
{
    char p[512]; snprintf(p, sizeof p, "%s/LOG", root); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/LOG/DIAG.TXT", root);
    FILE *f = fopen(p, "wb"); if (f) { fputs(head, f); fclose(f); }
}
int main(void)
{
    if (getenv("FENETHOST")) { th_host.net_state = th_state; th_host.get_ipinfo = th_ipinfo; th_host.echo_send = th_echo_send; th_host.echo_recv = th_echo_recv; msx_net_host = &th_host; }
    if (getenv("FENET")) fe_test_net = atoi(getenv("FENET"));
    machine_big = malloc(4u << 20); machine_big_size = 4u << 20;   /* 3.4.0 */
    cart_big = malloc(16u << 20); cart_big_size = 16u << 20;   /* 큰 롬 시험 */
    root = getenv("ROOT") ? getenv("ROOT") : "/tmp/usb";
    if (getenv("W")) W = (uint32_t)atoi(getenv("W"));
    if (getenv("H")) H = (uint32_t)atoi(getenv("H"));
    fb = calloc((size_t)W * H, 4);
    static MSX m;
    FeDisplay d = { fb, W * 4, W, H, 4, 1, 2, 2, 0 };
    static uint8_t scratch[32u << 20], xram[4u << 20];
    { static uint8_t wv[OPL4_MEM_SIZE]; opl4_set_memory(wv, 0); }   /* 3.1.0 MoonSound */
    FePlatform p = { .sys_sample = getenv("FESYS") ? h_sys_sample : 0, .par_run = getenv("FEPAR") ? h_par_run : 0, .ms = h_ms, .diag_save = h_diag_save, .video_outputs = getenv("FEVO") ? h_vo : 0, .video_diag = getenv("FEVO") ? h_vd : 0, .mouse_read = h_mouse_read, .mouse_dev = h_mouse_dev, .mouse_set = h_mouse_set, .pad_view = h_pad_view, .midi_read = h_midi_read, .midi_write = h_midi_write, .info = getenv("INFO") ? h_info : 0, .make_file = h_make_file, .reboot = h_reboot, .flush = getenv("FLUSHSHOT") ? h_flush_shot : 0, .grow_query = h_grow_query, .grow_run = h_grow_run, .audio = h_audio, .list_dir = h_list, .read_file = h_read,
                     .hd_open = h_hd_open, .hd_close = h_hd_close, .hd_read = h_hd_read, .hd_write = h_hd_write,
                     .write_file = h_write, .write_at = h_write_at, .append = h_append, .read_at = h_read_at, .vhd_mem = malloc(24u << 20), .vhd_mem_size = 24u << 20, .create = h_create, .delete_file = h_delete, .scratch = scratch, .scratch_size = sizeof scratch, .xram = xram, .v9990 = malloc(2u << 20), .vbuf = malloc(128u << 20), .vbuf_size = 128u << 20 };
    {
        char err[200];
        if (machine_load(&m, h_read, getenv("MACHINE") ? getenv("MACHINE") : DEFAULT_MACHINE, 0, err, sizeof err)) { printf("machine: %s\n", err); return 1; }
    }
    if (getenv("MACHINE") && strcmp(getenv("MACHINE"), "C-BIOS")) {   /* 저장된 x86msx.ini 의 Machine= 이 MACHINE 환경변수를 덮어쓰던 것을 막음 (ini 의 그 줄을 MACHINE 값으로 바꿔 둔다) */
        char ip[512], buf[16384]; snprintf(ip, sizeof ip, "%s/x86msx.ini", root);
        FILE *fi = fopen(ip, "rb"); size_t n = fi ? fread(buf, 1, sizeof buf - 1, fi) : 0; if (fi) fclose(fi); buf[n] = 0;
        char *ln = strstr(buf, "Machine="); if (ln && (ln == buf || ln[-1] == '\n')) {
            char *e = strchr(ln, '\n'); char tail[16384]; snprintf(tail, sizeof tail, "%s", e ? e : "");
            snprintf(ln, sizeof buf - (size_t)(ln - buf), "Machine=%s%s", getenv("MACHINE"), tail);
            if ((fi = fopen(ip, "wb"))) { fputs(buf, fi); fclose(fi); }
        }
    }
    if (getenv("FEPAR")) { extern int (*msx_job_post)(void (*)(void *), void *); extern int (*msx_job_wait)(int); msx_job_post = h_job_post; msx_job_wait = h_job_wait; }   /* 8.0.25 */
    fe_init(&d, &p, &m, FE_ROMS, FE_DISP_STORE " > ROMS");
    fe_restore();
    int gap = getenv("GAP") ? atoi(getenv("GAP")) : 20;
    int start = getenv("START") ? atoi(getenv("START")) : 120;
    const char *ks = getenv("KEYS") ? getenv("KEYS") : "";
    const char *sp = getenv("SHOT");
    int all = getenv("SHOTALL") != 0;
    char list[512][24]; int nk = 0;
    for (const char *c = ks; *c && nk < 512; ) {
        int l = 0;
        while (c[l] && c[l] != ',') l++;
        snprintf(list[nk++], sizeof list[0], "%.*s", l, c);
        c += l; if (*c) c++;
    }
    int total = start + nk * gap + (getenv("TAIL") ? atoi(getenv("TAIL")) : 30);
    if (getenv("MENUDUMP")) { fe_test_menu_dump(h_dump_line); return 0; }
    if (getenv("SCDUMP")) { char b[300]; for (int r = 0; fe_dump_shortcut(r, b, sizeof b); r++) printf("%d\t%s\n", r + 1, b); return 0; }
    double worst = 0, sum = 0; int cnt = 0;
    for (int f = 0; f < total; f++) {
        h_frame = f;
        if (getenv("V9DUMP") && f == total - 2 && m.v9) { printf("V9 mode=%d overscan? pal=%d\n regs:", m.v9->mode, m.v9->pal_t); for (int i = 0; i < 24; i++) printf(" %02X", m.v9->regs[i]); printf("\n rows=%d\n pal0:", m.v9->rows); for (int i = 0; i < 16; i++) printf(" %02X", m.v9->pal[i]); printf("\n r100:"); for (int x = 150; x < 200; x++) printf(" %06X", m.v9->fb[100][x]); printf("\n r230:"); for (int x = 0; x < 6; x++) printf(" %06X", m.v9->fb[230][x]); printf("\n vrow:"); for (int r = 0; r < 460; r += 20) { int a = -1, b = -1; for (int x = 0; x < 512; x++) { unsigned ad = (unsigned)(r*512+x); if (m.v9->vram[(ad>>1)|((ad&1)<<18)]) { if (a < 0) a = x; b = x; } } printf(" y%d:%d..%d", r, a, b); } printf("\n vram0:"); for (int x = 0; x < 64; x++) printf(" %02X", m.v9->vram[((100*512+x)>>1)|(((100*512+x)&1)<<18)]); printf("\n"); for (int r = 0; r < 240; r += 10) { int a = -1, b = -1; for (int x = 0; x < 640; x++) if (m.v9->fb[r][x]) { if (a < 0) a = x; b = x; } printf(" r%d:%d..%d", r, a, b); } printf("\n"); }
        if (getenv("REDRAWCMP") && f == total - 6 && sp) { char nm[256]; snprintf(nm, sizeof nm, "%s_pre.ppm", sp); shot(nm); printf("DLG %d\n", fe_test_dialog()); fe_test_action("redraw"); }   /* 5.2.130 */
        if (getenv("R800PR") && f % 200 == 0) printf("f%d r800=%d\n", f, m.cpu.r800);
        if (getenv("WATCHV")) { static uint8_t pv[4]; if (memcmp(pv, m.vdp.vram + 0x7600, 4)) { printf("WV f%d %02X %02X %02X %02X reg14=%02X\n", f, m.vdp.vram[0x7600], m.vdp.vram[0x7601], m.vdp.vram[0x7602], m.vdp.vram[0x7603], m.vdp.reg[14]); memcpy(pv, m.vdp.vram + 0x7600, 4); } }
        if (getenv("P1WATCH")) { static uint32_t ph = 0; uint32_t h = 2166136261u; for (int a = 32768; a < 65536; a++) h = (h ^ m.vdp.vram[a]) * 16777619u; if (h != ph) { int nz = 0; for (int a = 32768; a < 65536; a++) nz += m.vdp.vram[a] != 0; printf("P1 f%d nz=%d R0=%02X R1=%02X R2=%02X R25=%02X R26=%02X\n", f, nz, m.vdp.reg[0], m.vdp.reg[1], m.vdp.reg[2], m.vdp.reg[25], m.vdp.reg[26]); ph = h; } }
        if (getenv("WATCHFF")) { int c0 = 0, c1 = 0; for (int r = 20; r < 60; r++) for (int x = 0; x < 128; x++) { c0 += m.vdp.vram[r * 128 + x] == 0xFF; c1 += m.vdp.vram[32768 + r * 128 + x] == 0xFF; } static int p0 = -1, p1 = -1; if (c0 != p0 || c1 != p1) { if (f > 2500) printf("FF f%d p0=%d p1=%d r2=%02X\n", f, c0, c1, m.vdp.reg[2]); p0 = c0; p1 = c1; } }
        if (getenv("MEMDUMP_F") && (int)m.frame == atoi(getenv("MEMDUMP_F"))) { FILE *vf = fopen(getenv("MEMDUMP"), "wb"); if (vf) { for (int a = 0; a < 65536; a++) fputc(msx_mem_read(&m, (uint16_t)a), vf); fclose(vf); } }
        if (getenv("VRAMDUMP_F") && (int)m.frame == atoi(getenv("VRAMDUMP_F")) && getenv("VRAMDUMP2")) { FILE *vf = fopen(getenv("VRAMDUMP2"), "wb"); if (vf) { fwrite(m.vdp.vram, 1, 131072, vf); fwrite(m.vdp.reg, 1, 64, vf); fwrite(m.vdp.palette, 1, 48, vf); fclose(vf); } }
        if (getenv("FEVHD") && f == (getenv("FEVHD_F") ? atoi(getenv("FEVHD_F")) : 2)) fe_test_vhd(getenv("FEVHD"), getenv("FEVHD_SLOT") ? atoi(getenv("FEVHD_SLOT")) : 0);   /* 3.7.1 */
        if (getenv("FEHD") && f == (getenv("FEHD_F") ? atoi(getenv("FEHD_F")) : 4)) fe_test_hd(getenv("FEHD"));   /* 3.7.1 */
        if (getenv("FESLOTS") && f == (getenv("FESLOTS_F") ? atoi(getenv("FESLOTS_F")) : 40)) { printf("fehost slots:"); for (int k = 0; k < MSX_CART_SLOTS; k++) printf(" %d:%s", k + 1, m.cart_slot[k] < 0 ? "none" : m.cart[k].present ? "cart" : "empty"); printf("\n  specials:"); for (int k = 0; k < MSX_CART_SLOTS; k++) printf(" %d", fe_test_cart_special(k)); printf("\n"); }   /* 3.9.1 */
        if (getenv("DISKSEQ") && f >= 10 && f % 10 == 0 && f <= 10 + 10 * atoi(getenv("DISKSEQ"))) { printf("disk[%d]: %s\n", (f - 10) / 10, fe_test_disk_path(0)); if (f < 10 + 10 * atoi(getenv("DISKSEQ"))) fe_test_action("disknext"); }
        if (getenv("FEACT2") && f == (getenv("FEACT2_F") ? atoi(getenv("FEACT2_F")) : 200)) fe_test_action(getenv("FEACT2"));   /* 4.0.31: 두 번째 동작 (다시 열기 시험) */
        if (getenv("FECART") && f == (getenv("FECART_F") ? atoi(getenv("FECART_F")) : 3)) {   /* 시험 카트 (FECART=경로, FECARTMAP=매퍼 번호) */
            static uint8_t cb[1u << 20]; FILE *cf = fopen(getenv("FECART"), "rb"); size_t cn = cf ? fread(cb, 1, sizeof cb, cf) : 0; if (cf) fclose(cf);
            if (cn) msx_insert_cart(&m, 0, cb, (uint32_t)cn, (MapperType)(getenv("FECARTMAP") ? atoi(getenv("FECARTMAP")) : 0));
            if (getenv("FECARTRESET")) msx_reset(&m);
        }
        if (getenv("FEEJECT_F") && f == atoi(getenv("FEEJECT_F"))) msx_eject_cart(&m, 0);   /* 5.2.11 */
        if (getenv("FEACT") && f == (getenv("FEACT_F") ? atoi(getenv("FEACT_F")) : 5)) fe_test_action(getenv("FEACT"));   /* 3.7.0 */
        if (getenv("FESP") && f == (getenv("FESP_F") ? atoi(getenv("FESP_F")) : 2)) fe_insert_special(getenv("FESP_SLOT") ? atoi(getenv("FESP_SLOT")) : 0, atoi(getenv("FESP")));   /* 3.6.7 */
        if (getenv("FESP2") && f == (getenv("FESP_F") ? atoi(getenv("FESP_F")) : 2) + 2) fe_insert_special(getenv("FESP2_SLOT") ? atoi(getenv("FESP2_SLOT")) : 1, atoi(getenv("FESP2")));   /* 3.6.8 */
        if (getenv("FEROM") && f == (getenv("FEROM_F") ? atoi(getenv("FEROM_F")) : 2)) fe_insert_rom(getenv("FEROM_SLOT") ? atoi(getenv("FEROM_SLOT")) : 0, getenv("FEROM"));   /* 3.6.1 (3.6.7: FEROM_SLOT) */
        if (getenv("FEDIR") && f == (getenv("FEDIR_F") ? atoi(getenv("FEDIR_F")) : 2)) fe_insert_dir(0, getenv("FEDIR"));   /* 3.6.1 */
        if (getenv("FEDISK") && f == (getenv("FEDISK_F") ? atoi(getenv("FEDISK_F")) : 2)) fe_insert_disk(0, getenv("FEDISK"));   /* 3.5.5: 프런트엔드로 넣기 (저장까지 시험) */
        if (f == 1 && getenv("DISKA")) {                /* 3.4.2: 시험용 A 드라이브 디스크 (호스트 경로, 첫 프레임 뒤) */
            static uint8_t db[2u << 20];
            FILE *df = fopen(getenv("DISKA"), "rb");
            if (df) { size_t n = fread(db, 1, sizeof db, df); fclose(df); fdc_insert(&m.fdc, 0, db, (uint32_t)n, 0); }
        }
        int i = f - start;
        const KeyName *k = 0, *mod = 0;
        int ph = -1;
        if (i >= 0 && i / gap < nk) {
            ph = i % gap;
            char nm[24]; snprintf(nm, sizeof nm, "%s", list[i / gap]);
            int hold = getenv("HOLD") ? atoi(getenv("HOLD")) : 3;
            char *star = strchr(nm, '*');
            if (star) { hold = atoi(star + 1); *star = 0; }
            static const KeyName mk_ralt = { "ralt", 0x38, 1 }, mk_rctrl = { "rctrl", 0x1D, 1 }, mk_rshift = { "rshift", 0x36, 0 }, mk_shift = { "shift", 0x2A, 0 }, mk_ctrl = { "ctrl", 0x1D, 0 }, mk_alt = { "alt", 0x38, 0 };
            const KeyName *mods_l[3]; int nm_ = 0; (void)mod;               /* 3.2.14: 수식키 여러 개 (ctrl+alt+f8) */
            char *cur = nm, *plus;
            while ((plus = strchr(cur, '+')) && nm_ < 3) {
                *plus = 0;
                mods_l[nm_++] = !strcmp(cur, "ralt") ? &mk_ralt : !strcmp(cur, "rctrl") ? &mk_rctrl : !strcmp(cur, "rshift") ? &mk_rshift : !strcmp(cur, "shift") ? &mk_shift : !strcmp(cur, "ctrl") ? &mk_ctrl : !strcmp(cur, "alt") ? &mk_alt : find_key(cur);   /* 4.0.2: 아무 키나 함께 누름 */
                cur = plus + 1;
            }
            k = find_key(cur);
            mod = nm_ ? mods_l[0] : 0;
            if (ph == 0 && k) { for (int q = 0; q < nm_; q++) press(mods_l[q], 1); press(k, 1); }
            if (getenv("REP") && k && ph > 25 && ph < hold && ph % 2 == 0) press(k, 1);   /* 4.0.2: 하드웨어 자동 반복 흉내 */
            if (ph == hold && k) { press(k, 0); for (int q = nm_ - 1; q >= 0; q--) press(mods_l[q], 0); }
        }
        cur_frame = f;
        if (getenv("PRNFILE") && f >= (getenv("PRNFILE_F") ? atoi(getenv("PRNFILE_F")) : 20)) {   /* 4.0.4: 파일 내용을 프레임마다 2000바이트씩 프린터로 */
            static uint8_t *pf; static long pn, pp;
            if (!pf) { FILE *q = fopen(getenv("PRNFILE"), "rb"); if (q) { pf = malloc(1 << 20); pn = (long)fread(pf, 1, 1 << 20, q); fclose(q); } }
            if (pf && pp < pn && !m.prn_n) { long k = pn - pp > 2000 ? 2000 : pn - pp; memcpy(m.prn_q, pf + pp, (size_t)k); m.prn_n = (uint16_t)k; pp += k; }
        }
        if (getenv("PRNFEED") && f >= atoi(getenv("PRNFEED")) && f < atoi(getenv("PRNFEED")) + 20 && !m.prn_n) { memcpy(m.prn_q, "HELLO", 5); m.prn_n = 5; }   /* 4.0.3: 프린터 아이콘 시험 (PrnMode=1 필요) */
        if (getenv("FETAPE") && f == 2) {   /* 4.0.34: 테이프 넣기 시험 */
            static uint8_t tb[1u << 20]; FILE *q = fopen(getenv("FETAPE"), "rb");
            if (q) { size_t n = fread(tb, 1, sizeof tb, q); fclose(q); msx_tape_insert(&m, tb, (uint32_t)n, sizeof tb); }
        }
        if (getenv("FETYPE") && f == (getenv("FETYPE_F") ? atoi(getenv("FETYPE_F")) : 250)) {   /* 4.0.34: 키 버퍼에 글자 넣기 */
            const char *t = getenv("FETYPE"); int n = (int)strlen(t); if (n > 38) n = 38;
            for (int i = 0; i < n; i++) msx_mem_write(&m, (uint16_t)(0xFBF0 + i), (uint8_t)(t[i] == '|' ? 13 : t[i]));
            msx_mem_write(&m, 0xF3FA, 0xF0); msx_mem_write(&m, 0xF3FB, 0xFB);
            msx_mem_write(&m, 0xF3F8, (uint8_t)(0xF0 + n)); msx_mem_write(&m, 0xF3F9, 0xFB);
        }
        struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
        fe_frame();
        if (getenv("TAPEMOT") && f % 100 == 0) printf("f%d motor %d pos %u/%u pc %04X\n", f, m.tape_motor, m.tape_pos, m.tape_size, m.cpu.pc);
        if (getenv("PCLOW") && m.cpu.pc < 0x0600 && f % 10 == 0) printf("f%d pc %04X sp %04X\n", f, m.cpu.pc, m.cpu.sp.w);
        if (getenv("PCLOG") && f % 30 == 0) printf("f%d pc %04X slots %02X sub3 %02X\n", f, m.cpu.pc, m.primary, m.subslot[3]);
        if (getenv("RAMDUMP") && f == total - 2) { FILE *q = fopen(getenv("RAMDUMP"), "wb"); if (q) { for (unsigned a = 0; a < 65536; a++) fputc(msx_peek(&m, (uint16_t)a), q); fclose(q); } }   /* 4.0.9 */
        if (getenv("VRAMDUMP") && f == total - 2) { FILE *q = fopen(getenv("VRAMDUMP"), "wb"); if (q) { fwrite(m.vdp.vram, 1, 16384, q); fclose(q); } }   /* 4.0.10 */
        if (getenv("PEEKA") && f % 100 == 0) { unsigned a = (unsigned)strtol(getenv("PEEKA"), 0, 16); printf("f%d peek %04X = %02X\n", f, a, msx_peek(&m, (uint16_t)a)); }   /* 4.0.9 */
        if (getenv("DUMPAT") && f == atoi(getenv("DUMPAT"))) {   /* 3.4.4: 레지스터와 PC 둘레 */
            printf("PC %04X SP %04X AF %04X BC %04X DE %04X HL %04X IX %04X IY %04X\n", m.cpu.pc, m.cpu.sp.w, m.cpu.af.w, m.cpu.bc.w, m.cpu.de.w, m.cpu.hl.w, m.cpu.ix.w, m.cpu.iy.w);
            for (int r = -16; r < 32; r++) printf("%02X%s", msx_peek(&m, (uint16_t)(m.cpu.pc + r)), r == -1 ? " | " : " ");
            printf("\nstack:"); for (int r = 0; r < 16; r += 2) printf(" %04X", msx_peek(&m, (uint16_t)(m.cpu.sp.w + r)) | msx_peek(&m, (uint16_t)(m.cpu.sp.w + r + 1)) << 8);
            printf("\n");
        }
        clock_gettime(CLOCK_MONOTONIC, &b);
        double ms = (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
        {   /* 3.5.5: 바뀐 사각형. CHECKDIRTY=1이면 사각형만 옮긴 "화면"이 실제 그림과 같은지 확인 (빠뜨린 표시 찾기) */
            FeRect rr[16]; int nr = fe_take_dirty_rects(rr, 16);
            uint64_t px = 0; for (int k = 0; k < nr; k++) px += (uint64_t)(rr[k].x1 - rr[k].x0) * (rr[k].y1 - rr[k].y0);
            if (getenv("DIRTY") && nr) {
                printf("f%d key%d ph%d dirty %d rects %llu px (%.1f%%) %.2f ms:", f, i >= 0 ? i / gap : -1, ph, nr, (unsigned long long)px, px * 100.0 / ((double)W * H), ms);
                for (int k = 0; k < nr; k++) printf(" [%u,%u-%u,%u]", rr[k].x0, rr[k].y0, rr[k].x1, rr[k].y1);
                printf("\n");
            }
            dirty_px_total += px;
            if (getenv("CHECKDIRTY")) {
                static uint32_t *scr;
                if (!scr) scr = calloc((size_t)W * H, 4);
                for (int k = 0; k < nr; k++)
                    for (uint32_t y = rr[k].y0; y < rr[k].y1; y++) memcpy(scr + (size_t)y * W + rr[k].x0, (uint32_t *)fb + (size_t)y * W + rr[k].x0, (rr[k].x1 - rr[k].x0) * 4);
                uint32_t bx0 = W, by0 = H, bx1 = 0, by1 = 0, bad = 0;
                for (uint32_t y = 0; y < H; y++) {
                    if (!memcmp(scr + (size_t)y * W, (uint32_t *)fb + (size_t)y * W, W * 4)) continue;
                    for (uint32_t x = 0; x < W; x++) if (scr[(size_t)y * W + x] != ((uint32_t *)fb)[(size_t)y * W + x]) {
                        bad++; if (x < bx0) bx0 = x; if (x >= bx1) bx1 = x + 1; if (y < by0) by0 = y; if (y >= by1) by1 = y + 1;
                    }
                    memcpy(scr + (size_t)y * W, (uint32_t *)fb + (size_t)y * W, W * 4);   /* 한 번만 알림 */
                }
                if (bad) { dirty_bad++; printf("DIRTYMISS f%d key%d: %u px at [%u,%u-%u,%u]\n", f, i >= 0 ? i / gap : -1, bad, bx0, by0, bx1, by1); }
            }
        }
        if (ph == 0) { sum += ms; cnt++; if (ms > worst) worst = ms; }
        if (getenv("HASHALL") && ph == gap - 1) {       /* 키마다 화면 해시 (FNV-1a) — 구조 개편 전후 화면이 같은지 비교 */
            uint64_t hh = 1469598103934665603ull;
            for (size_t q = 0; q < (size_t)W * H * 4; q++) { hh ^= fb[q]; hh *= 1099511628211ull; }
            printf("key %d %s %016llx\n", i / gap, list[i / gap], (unsigned long long)hh);
        }
        if (sp && ((all && ph == gap - 1) || f == total - 1)) {
            char nm[256]; snprintf(nm, sizeof nm, "%s_%02d.ppm", sp, f == total - 1 && !(all && ph == gap - 1) ? 99 : i / gap);
            shot(nm);
        }
    }
    if (getenv("CARTDUMP")) {   /* 카트 자리·장치 상태 (특수 카트리지 꽂기 시험 — 이전 판과 비교) */
        for (int n = 0; n < MSX_CART_ALL; n++) {
            const Cartridge *c = &m.cart[n];
            uint64_t hh = 1469598103934665603ull;
            if (c->present && c->data) for (uint32_t q = 0; q < c->size && q < 65536; q++) { hh ^= c->data[q]; hh *= 1099511628211ull; }
            printf("cart %d present=%d mapper=%d size=%u banks8=%u slot=%d ss=%d data=%016llx\n", n, c->present, (int)c->mapper, c->size, c->banks8, m.cart_slot[n], m.cart_ss[n], (unsigned long long)hh);
        }
        printf("dev audio=%d ms=%d joyrex=%d opll=%d v9=%d net=%d fdc=%d vhd=%d sfg=%d idecart=%d dos2=%d midi=%d xram=%u ide=%d\n", m.audio_on, m.ms_on, m.joyrex, m.opll_on, m.v9 != 0, m.net.enabled,
               m.fdc_kind, m.vhd_on, m.sfg_on, m.ide_cart_io, m.dos2 != 0, m.prof.midi, m.xram_size, m.ide.present);
    }
        if (getenv("SPRPOS")) {   /* 4.0.4: 스프라이트 위치 (SAT 앞쪽) 인쇄 — 마우스 시험 */
        const VDP *v = &m.vdp;
        uint32_t sat = !v->v9938 ? (uint32_t)(v->reg[5] & 0x7F) << 7 : ((uint32_t)(v->reg[11] & 3) << 15) | ((uint32_t)(v->reg[5] & 0xFC) << 7);
        printf("spr:"); for (int q = 0; q < 32; q++) printf(" #%d(%d,%d)", q, v->vram[sat + q * 4 + 1], v->vram[sat + q * 4]); printf(" R1=%02X R5=%02X R11=%02X", v->reg[1], v->reg[5], v->reg[11]);
        printf("\n");
    }
printf("fehost: %d keys, key frame avg %.2f ms, worst %.2f ms, screen update %.1f%%/frame, dirty misses %d\n", cnt, cnt ? sum / cnt : 0, worst, dirty_px_total * 100.0 / ((double)W * H * total), dirty_bad);
    return 0;
}
