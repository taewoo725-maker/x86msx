/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * headless.c - 호스트 개발용 실행기 (화면 없이 실행, 설계서 §9)
 *
 * 사용법:
 *   msxrun <프로필 폴더> <프레임 수> [옵션...]
 *     --shot <프레임> <파일.ppm>     해당 프레임 끝의 화면 저장 (테두리 포함, 2배)
 *     --type <프레임> <문자열>       해당 프레임부터 문자열 입력 ('\n' = RETURN)
 *     --text                         끝에서 텍스트 화면(이름 테이블) 출력
 */
#include "msx_host.h"
#include "../src/gmsynth.h"
#include "../src/mididev.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BORDER 16
#define SCALE 2

static void save_v9_ppm(const MSX *m, const char *path)      /* V9990 화면 (640 x rows) */
{
    int rows = m->v9->rows;
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", V9_FBW, rows);
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < V9_FBW; x++) {
            uint32_t c = m->v9->fb[y][x];
            uint8_t rgb[3] = { (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

static void save_ppm(const MSX *m, const char *path)
{
    if (m->v9 && v9990_visible(m->v9)) { save_v9_ppm(m, path); return; }
    int w = (VDP_W + BORDER * 2) * SCALE, h = (VDP_H + BORDER * 2) * SCALE;
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    uint8_t bd = vdp_backdrop(&m->vdp);
    for (int y = 0; y < h; y++) {
        int sy = y / SCALE - BORDER;
        for (int x = 0; x < w; x++) {
            int sx = x / SCALE - BORDER;
            uint8_t c = (sx >= 0 && sx < VDP_W && sy >= 0 && sy < VDP_H) ? m->vdp.fb[sy][sx] : bd;
            if (sx >= 0 && sx < VDP_W && sy >= 0 && sy < VDP_H && m->vdp.line_direct[sy] == 1) {   /* 4.0.129: SCREEN 8 직접색 (GGGRRRBB) */
                uint8_t rgb[3] = { (uint8_t)(((c >> 2) & 7) * 255 / 7), (uint8_t)(((c >> 5) & 7) * 255 / 7), (uint8_t)((c & 3) * 255 / 3) };
                fwrite(rgb, 1, 3, f);
            } else
            fwrite(m->vdp.palette[c], 1, 3, f);
        }
    }
    fclose(f);
}

/* DPC-200 키 매트릭스: 기호는 일본식 MSX 배열 (실행 시험으로 확인, 2026-10-01)
 *  행1: 8 9 - ^ \ @ [ ;    행2: : ] , . / _ A B
 *  Shift: 1! 2" 3# 4$ 5% 6& 7' 8( 9) -= ^~ \| @` [{ ;+ :* ]} ,< .> /? */
typedef struct { char ch; int row, bit, shift; } KeyMap;
static const KeyMap keymap[] = {
    {'0',0,0,0},{'1',0,1,0},{'2',0,2,0},{'3',0,3,0},{'4',0,4,0},{'5',0,5,0},{'6',0,6,0},{'7',0,7,0},
    {'8',1,0,0},{'9',1,1,0},{'-',1,2,0},{'^',1,3,0},{'\\',1,4,0},{'@',1,5,0},{'[',1,6,0},{';',1,7,0},
    {':',2,0,0},{']',2,1,0},{',',2,2,0},{'.',2,3,0},{'/',2,4,0},{'_',2,5,0},
    {'A',2,6,0},{'B',2,7,0},{'C',3,0,0},{'D',3,1,0},{'E',3,2,0},{'F',3,3,0},{'G',3,4,0},{'H',3,5,0},
    {'I',3,6,0},{'J',3,7,0},{'K',4,0,0},{'L',4,1,0},{'M',4,2,0},{'N',4,3,0},{'O',4,4,0},{'P',4,5,0},
    {'Q',4,6,0},{'R',4,7,0},{'S',5,0,0},{'T',5,1,0},{'U',5,2,0},{'V',5,3,0},{'W',5,4,0},{'X',5,5,0},
    {'Y',5,6,0},{'Z',5,7,0},{' ',8,0,0},{'\n',7,7,0},
    {'!',0,1,1},{'"',0,2,1},{'#',0,3,1},{'$',0,4,1},{'%',0,5,1},{'&',0,6,1},{'\'',0,7,1},
    {'(',1,0,1},{')',1,1,1},{'=',1,2,1},{'~',1,3,1},{'|',1,4,1},{'`',1,5,1},{'{',1,6,1},{'+',1,7,1},
    {'*',2,0,1},{'}',2,1,1},{'<',2,2,1},{'>',2,3,1},{'?',2,4,1},
};

static const KeyMap *find_key(char ch)
{
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
    for (size_t i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
        if (keymap[i].ch == ch) return &keymap[i];
    return NULL;
}

#define MAX_EVENTS 32
typedef struct { long frame; char *arg; int kind; } Event;   /* kind 0 shot, 1 type */

static long keys[64][4]; static int nkey;
int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: msxrun <profile_dir> <frames> [--shot F file] [--type F text] [--text]\n");
        return 2;
    }
    long frames = atol(argv[2]);
    Event ev[MAX_EVENTS];
    int nev = 0, want_text = 0;
    const char *wav_path = NULL;
    FILE *wav = NULL;
    uint32_t wav_samples = 0;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--shot") && i + 2 < argc && nev < MAX_EVENTS) {
            ev[nev++] = (Event){ atol(argv[i + 1]), argv[i + 2], 0 }; i += 2;
        } else if (!strcmp(argv[i], "--type") && i + 2 < argc && nev < MAX_EVENTS) {
            ev[nev++] = (Event){ atol(argv[i + 1]), argv[i + 2], 1 }; i += 2;
        } else if (!strcmp(argv[i], "--key") && i + 4 < argc && nkey < 64) {   /* --key 프레임 행 비트 길이 */
            keys[nkey][0] = atol(argv[i + 1]); keys[nkey][1] = atol(argv[i + 2]); keys[nkey][2] = atol(argv[i + 3]); keys[nkey][3] = atol(argv[i + 4]); nkey++; i += 4;
        } else if (!strcmp(argv[i], "--wav") && i + 1 < argc) {
            wav_path = argv[++i];
        } else if (!strcmp(argv[i], "--text")) {
            want_text = 1;
        }
    }

    static MSX m;
    static V9990 v9;
    char err[256];
    if (msx_host_init(&m, argv[1], err, sizeof err)) { fprintf(stderr, "init: %s\n", err); return 1; }
    if (getenv("V9990")) msx_v9990_enable(&m, &v9);          /* 4.0.29: Gfx9000 */
    if (getenv("AUDIO")) { msx_audio_enable(&m, 1); m.ch_vol[MSX_CH_MM] = 100; }
    if (getenv("MOON")) { static uint8_t wm[4u << 20]; opl4_set_memory(wm, 2u << 20); msx_moonsound_enable(&m, 1); }   /* 시험용 MoonSound (웨이브 롬은 0) */
    if (getenv("EXPAND")) msx_set_expand(&m, atoi(getenv("EXPAND")));   /* 확장기를 먼저 (카트리지 번호 SLOT1~5 를 서브슬롯에 꽂으려고) */
    if (getenv("CART")) {                               /* 3.4.4: 시험용 카트리지 (CART=경로, CARTMAP=매퍼 번호) */
        static uint8_t cbuf[MSX_ROM_BIG];  /* 4.0.129 */
        FILE *cf = fopen(getenv("CART"), "rb");
        size_t cn = cf ? fread(cbuf, 1, sizeof cbuf, cf) : 0;
        if (cf) fclose(cf);
        MapperType cmt = getenv("CARTMAP") ? (MapperType)atoi(getenv("CARTMAP")) : cart_guess_mapper(cbuf, (uint32_t)cn);
        if (cn) { uint32_t need = msx_cart_alloc_size(cmt, (uint32_t)cn); if (need > cn && need <= sizeof cbuf) { memset(cbuf + cn, 0xFF, need - cn); cn = need; } }   /* 플래시 칩 크기 (칩 부품이 정함, fe.c와 같게) */
        int ce = cn ? msx_insert_cart(&m, 0, cbuf, (uint32_t)cn, cmt) : -1;
        if (ce) { fprintf(stderr, "cart: %s\n", ce < 0 ? "read error" : msx_error(ce)); return 1; }
    }
    if (getenv("CART2")) {                              /* 두 번째 시험용 카트리지 (슬롯 B, CART2=경로 CART2MAP=매퍼 번호) — SCC 2개 시험 */
        static uint8_t cbuf2[MSX_ROM_BIG];
        FILE *cf = fopen(getenv("CART2"), "rb");
        size_t cn = cf ? fread(cbuf2, 1, sizeof cbuf2, cf) : 0;
        if (cf) fclose(cf);
        MapperType cmt = getenv("CART2MAP") ? (MapperType)atoi(getenv("CART2MAP")) : cart_guess_mapper(cbuf2, (uint32_t)cn);
        int ce = cn ? msx_insert_cart(&m, getenv("CART2IDX") ? atoi(getenv("CART2IDX")) : 1, cbuf2, (uint32_t)cn, cmt) : -1;
        if (ce) { fprintf(stderr, "cart2: %s\n", ce < 0 ? "read error" : msx_error(ce)); return 1; }
    }
    if (getenv("DISKA")) {                              /* 시험용 A 드라이브 디스크 */
        static uint8_t db[2u << 20]; FILE *df = fopen(getenv("DISKA"), "rb");
        if (df) { size_t dn = fread(db, 1, sizeof db, df); fclose(df); fdc_insert(&m.fdc, 0, db, (uint32_t)dn, 0); }
    }
    if (getenv("SC55DIR")) {                            /* 5.2.158 시험: SC-55mkII 실기 롬(Nuked-SC55)으로 GT 내장 MIDI 를 울림 */
        extern int sc55w_load(int, int, uint32_t); extern uint8_t *sc55w_tmp(void); extern int sc55w_start(int);
        extern const char *const sc55w_files[2][5];
        for (int f = 0; f < 5; f++) {
            char pth[512]; snprintf(pth, sizeof pth, "%s/%s", getenv("SC55DIR"), sc55w_files[0][f]);
            FILE *rf = fopen(pth, "rb"); if (!rf) { fprintf(stderr, "no %s\n", pth); return 1; }
            size_t n = fread(sc55w_tmp(), 1, 0x200000, rf); fclose(rf);
            if (sc55w_load(0, f, (uint32_t)n)) { fprintf(stderr, "bad size %s\n", pth); return 1; }
        }
        if (sc55w_start(0)) { fprintf(stderr, "sc55 start fail\n"); return 1; }
        gmi_set_mode(getenv("MIDIDEV") ? 0 : GM_INST_GT, GM_SC55);
    }
    if (getenv("MIDIDEV")) { int di = midibus_plug(MDK_GS, 0); if (di >= 0) midi_bus.dev[di].active = 1; if (getenv("SC55DIR")) gmi_set_mode(di, GM_SC55); }   /* 5.2.158 시험: 단자에 SC-55 기계를 꽂음 (실제 설정 경로) */
    if (getenv("MIDIGT")) { midi_bus.def_on = 1; if (!getenv("SC55DIR")) gmi_set_mode(GM_INST_GT, GM_FM); }   /* 5.2.158 시험: GT 내장 MIDI 소리를 WAV 에 넣음 */
    if (getenv("DISKB")) { static uint8_t db3[2u << 20]; FILE *df = fopen(getenv("DISKB"), "rb"); if (df) { size_t dn = fread(db3, 1, sizeof db3, df); fclose(df); fdc_insert(&m.fdc, 1, db3, (uint32_t)dn, 0); } }   /* 5.2.158 시험: B 드라이브 */
    if (getenv("MIDIOFF")) m.prof.midi = 0;             /* 내장 MIDI 끄기 시험 */
    if (getenv("RTC_NOW")) { int y, mo, d, h, mi, se; if (sscanf(getenv("RTC_NOW"), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se) == 6) msx_rtc_set(&m, msx_rtc_secs(y, mo, d, h, mi, se)); }
    printf("profile: %s (%s %s), regions %d, Z80 %u Hz, VDP %s\n", m.prof.id, m.prof.manufacturer,
           m.prof.model, m.prof.region_count, m.prof.z80_hz, m.prof.vdp);

    if (wav_path) {
        wav = fopen(wav_path, "wb");
        if (wav) fwrite("RIFF\0\0\0\0WAVEfmt \x10\0\0\0\x01\0\x01\0\x80\xbb\0\0\0\x77\x01\0\x02\0\x10\0data\0\0\0\0", 1, 44, wav);
    }

    /* 입력 큐: 글자마다 3프레임 누름, 3프레임 뗌 */
    const char *typing = NULL;
    long type_next = 0;
    int type_phase = 0;
    const KeyMap *cur = NULL;

    uint32_t typ_reads = 0;
    for (long f = 0; f < frames; f++) {
        for (int e = 0; e < nev; e++)
            if (ev[e].kind == 1 && ev[e].frame == f) { typing = ev[e].arg; type_next = f; type_phase = 0; }

        if (typing && f >= type_next) {
            if (type_phase == 0) {
                char ch = *typing;
                if (ch == '\\' && typing[1] == 'n') { ch = '\n'; typing++; }
                cur = find_key(ch);
                if (cur) {
                    if (cur->shift) msx_key(&m, 6, 0, 1);
                    msx_key(&m, cur->row, cur->bit, 1);
                }
                type_phase = 1; typ_reads = cur ? m.kbd_row_reads[cur->row] : 0;
                type_next = f + 3;
            } else if (getenv("KEYWAITREAD") && cur && f < type_next + 40 && m.kbd_row_reads[cur->row] == typ_reads) {
                /* 5.2.159 시험: 그 행을 Z80 이 한 번 읽을 때까지 유지 (입력 계층의 release_ok 와 같은 규칙) */
            } else {
                if (cur) {
                    msx_key(&m, cur->row, cur->bit, 0);
                    if (cur->shift) msx_key(&m, 6, 0, 0);
                }
                typing++;
                if (!*typing) typing = NULL;
                type_phase = 0;
                type_next = f + 3;
            }
        }

        for (int k = 0; k < nkey; k++) {
            if (f == keys[k][0]) msx_key(&m, (int)keys[k][1], (int)keys[k][2], 1);
            if (f == keys[k][0] + keys[k][3]) msx_key(&m, (int)keys[k][1], (int)keys[k][2], 0);
        }
        if (getenv("DISKSWAP") && getenv("DISKSWAP_F") && f == atol(getenv("DISKSWAP_F")) - 40) fdc_eject(&m.fdc, 0);
        if (getenv("DISKSWAP") && getenv("DISKSWAP_F") && f == atol(getenv("DISKSWAP_F"))) {   /* 5.2.158 시험: 그 프레임에 A 드라이브 디스크 바꿈 */
            static uint8_t db2[2u << 20]; FILE *df = fopen(getenv("DISKSWAP"), "rb");
            if (df) { size_t dn = fread(db2, 1, sizeof db2, df); fclose(df); fdc_insert(&m.fdc, 0, db2, (uint32_t)dn, 0); }
        }
        msx_run_frame(&m);
        if (getenv("MIDIDUMP")) { static FILE *mf; if (!mf) mf = fopen(getenv("MIDIDUMP"), "wb"); if (mf && m.midi_n) fwrite(m.midi_q, 1, m.midi_n, mf); if (mf) fflush(mf); }   /* 이번 프레임 MIDI 바이트 */
        if (getenv("TRACE") && f % atol(getenv("TRACE")) == 0) {
            printf("f=%ld PC=%04X SP=%04X slot=%02X IFF=%d halt=%d", f, m.cpu.pc, m.cpu.sp.w, m.primary, m.cpu.iff1, m.cpu.halted);
            for (int n = 0; n < 2; n++) if (m.cart[n].present) printf(" xb%d=%03X,%03X", n, m.cart[n].xbank[0], m.cart[n].xbank[1]);
            printf(" ss=%02X,%02X,%02X,%02X r0=%02X r1=%02X r9=%02X\n", m.subslot[0], m.subslot[1], m.subslot[2], m.subslot[3], m.vdp.reg[0], m.vdp.reg[1], m.vdp.reg[9]);
        }
        {   /* CHIPLOG=1: 음원 사용 여부가 바뀐 프레임 출력 (상태 아이콘 시험) */
            static int last_act = -1;
            if (getenv("CHIPLOG") && (m.chip_act | m.scc2_act << 8) != last_act) { printf("frame %ld chip_act %02X scc2 %d\n", f, m.chip_act, m.scc2_act); last_act = m.chip_act | m.scc2_act << 8; }
        }
        if (wav) {
            fwrite(m.psg.buf, 2, (size_t)m.psg.buf_n, wav);
            wav_samples += (uint32_t)m.psg.buf_n;
        }

        for (int e = 0; e < nev; e++)
            if (ev[e].kind == 0 && ev[e].frame == f) save_ppm(&m, ev[e].arg);
    }

    if (wav) {
        uint32_t data = wav_samples * 2, riff = data + 36;
        fseek(wav, 4, SEEK_SET); fwrite(&riff, 4, 1, wav);
        fseek(wav, 40, SEEK_SET); fwrite(&data, 4, 1, wav);
        fclose(wav);
        printf("wav: %u samples (%.2f s) -> %s\n", wav_samples, wav_samples / 48000.0, wav_path);
    }
    printf("ran %ld frames, PC=%04X, slot reg=%02X, cycles=%llu\n", frames, m.cpu.pc, m.primary,
           (unsigned long long)m.cpu.cycles);

    if (m.v9 && getenv("V9DBG")) {
        printf("v9990: mode %d sysreset %d disp_en %d shown %d pending %02X\nregs:", m.v9->mode, m.v9->sysreset, m.v9->disp_en, m.v9->shown, m.v9->pending);
        for (int i = 0; i < 29; i++) printf(" %02X", m.v9->regs[i]);
        printf("\n");
    }
    if (getenv("CART") && getenv("FLDIFF")) {             /* 플래시로 바뀐 8 KiB 조각 */
        FILE *cf = fopen(getenv("CART"), "rb"); static uint8_t ob[MSX_ROM_BIG]; size_t on = cf ? fread(ob, 1, sizeof ob, cf) : 0; if (cf) fclose(cf);
        for (size_t k = 0; k * 8192 < on; k++) if (memcmp(ob + k * 8192, m.cart[0].data + k * 8192, 8192)) printf("flash chunk %zu (off %06zX) changed\n", k, k * 8192);
    }
    if (getenv("RAMDUMP")) { FILE *rf = fopen(getenv("RAMDUMP"), "wb"); if (rf) { fwrite(m.mram, 1, m.mram_size, rf); fwrite(m.vdp.vram, 1, sizeof m.vdp.vram, rf); fclose(rf); } }   /* 5.2.141 시험 */
    { extern unsigned long msx_midi_drop; if (getenv("MIDIDROP")) printf("midi dropped bytes: %lu\n", msx_midi_drop); }
    if (getenv("SHOW_PAL")) printf("pal=%d lines=%d\n", msx_is_pal(&m), msx_frame_lines(&m));
    if (want_text) {
        int text = (m.vdp.reg[1] & 0x10) != 0;
        int cols = text ? 40 : 32;
        uint16_t nt = (uint16_t)((m.vdp.reg[2] & 0x0F) << 10);
        printf("--- name table (%s, %d cols) ---\n", text ? "SCREEN 0" : "SCREEN 1/2", cols);
        for (int r = 0; r < 24; r++) {
            for (int c = 0; c < cols; c++) {
                uint8_t ch = m.vdp.vram[nt + r * cols + c];
                putchar(ch >= 32 && ch < 127 ? ch : (ch == 0 ? ' ' : '.'));
            }
            putchar('\n');
        }
    }
    msx_host_free(&m);
    return 0;
}
