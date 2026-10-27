/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * keymap.c - PC 키보드(세트 1, 미국식 기호) → DPC-200 MSX 키 매트릭스 (설계서 §4.6, FR-40b)
 *
 * 키 번호: 스캔 코드 하위 7비트, E0 확장 키는 0x80을 더함.
 * 글자·숫자·특수키는 위치 기준, 기호는 글자 기준으로 일본식 MSX 배열에 맞춘다.
 * MSX 행/비트: 행6 = SHIFT CTRL GRAPH CAPS CODE F1 F2 F3, 행7 = F4 F5 ESC TAB STOP BS SELECT RETURN,
 *             행8 = SPACE HOME INS DEL LEFT UP DOWN RIGHT
 */
#include "keymap.h"
#include <string.h>

#define NONE   { -1, 0, -1, 0 }
#define K(r,b) { r, b, -1, 0 }             /* Shift는 PC 그대로 */
#define KN(r,b) { r, b, 0, 0 }             /* MSX Shift 강제 해제 */
#define KS(r,b) { r, b, 1, 0 }             /* MSX Shift 강제 */

/* [키 번호][PC Shift 0/1] */
static MsxKey table[256][2];

static void set(int key, MsxKey normal, MsxKey shifted)
{
    table[key][0] = normal;
    table[key][1] = shifted;
}
static void set1(int key, MsxKey k) { set(key, k, k); }

static int layout;
void keymap_layout(int l) { layout = l; }
int keymap_altgr_layout(void) { return layout == KL_DE || layout == KL_FR; }   /* 3.2.12: 오른쪽 Alt = AltGr */
static MsxKey alt_table[256];                    /* 3.2.12: AltGr와 함께 누른 키 */
/* 3.2.12 KBD-02: 사용자 키 배치 (PC 키 → MSX 키, keymap_init 끝에 덮어씀) */
static uint8_t remap_to[256];                    /* 0 = 기본, 0xFF = MSX로 안 보냄, 그 밖 = 1 + 행*8 + 비트 */
void keymap_remap_clear(void) { memset(remap_to, 0, sizeof remap_to); }
void keymap_remap_set(int key, int code) { if (key >= 0 && key < 256) remap_to[key] = (uint8_t)code; }
int keymap_remap_get(int key) { return key >= 0 && key < 256 ? remap_to[key] : 0; }

/* MSX 기종 키보드 (2.13): 일본식(대우 포함)이 아니면 글자 기준으로 kbdmaps.c 표에서 찾는다 */
extern const uint8_t kbd_maps[9][95][2];
static int mkbd = KBD_JP;
void keymap_machine(int k) { mkbd = k; }
int  keymap_machine_get(void) { return mkbd; }
static int kbd_index(int k)
{
    switch (k) {
    case KBD_FR: return 1; case KBD_UK: return 2; case KBD_DE: return 3; case KBD_ES: return 4;
    case KBD_SE: return 5; case KBD_RU: return 6; case KBD_AR: return 7; case KBD_PL: return 8;
    default: return 0;
    }
}
const char *keymap_name(int k)
{
    switch (k) {
    case KBD_JP: return "Japanese-style"; case KBD_FR: return "French"; case KBD_UK: return "UK"; case KBD_DE: return "German";
    case KBD_ES: return "Spanish"; case KBD_SE: return "Swedish"; case KBD_DK: return "Danish"; case KBD_RU: return "Russian";
    case KBD_AR: return "Arabic"; case KBD_PL: return "Polish"; default: return "International";
    }
}
static int lower_eq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char c = *a; if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != *b) return 0;
    }
    return !*a && !*b;
}
int keymap_detect(const char *kl, int b2c)
{
    if (kl && lower_eq(kl, "russian")) return KBD_RU;
    if (kl && lower_eq(kl, "arabic")) return KBD_AR;
    if (kl && lower_eq(kl, "polish")) return KBD_PL;
    if (b2c >= 0) {
        switch (b2c & 15) {
        case 0: return KBD_JP; case 2: return KBD_FR; case 3: return KBD_UK; case 4: return KBD_DE;
        case 6: return KBD_ES; case 7: return KBD_SE; case 9: return KBD_DK; default: return KBD_INT;
        }
    }
    if (!kl) return KBD_JP;
    if (lower_eq(kl, "german")) return KBD_DE;
    if (lower_eq(kl, "french")) return KBD_FR;
    if (lower_eq(kl, "spanish")) return KBD_ES;
    if (lower_eq(kl, "swedish")) return KBD_SE;
    if (lower_eq(kl, "european2")) return KBD_UK;
    if (lower_eq(kl, "european") || lower_eq(kl, "brazilian") || lower_eq(kl, "estonian") || lower_eq(kl, "english")) return KBD_INT;
    return KBD_JP;
}
static const uint8_t jp_map[95][2] = {   /* 3.2.12: 일본식(대우 포함) MSX의 글자 → 행<<4|열, SHIFT (글자 기준 PC 배열용) */
    {0x80,0},{0x01,1},{0x02,1},{0x03,1},{0x04,1},{0x05,1},{0x06,1},{0x07,1},
    {0x10,1},{0x11,1},{0x20,1},{0x17,1},{0x22,0},{0x12,0},{0x23,0},{0x24,0},
    {0x00,0},{0x01,0},{0x02,0},{0x03,0},{0x04,0},{0x05,0},{0x06,0},{0x07,0},
    {0x10,0},{0x11,0},{0x20,0},{0x17,0},{0x22,1},{0x12,1},{0x23,1},{0x24,1},
    {0x15,0},{0x26,1},{0x27,1},{0x30,1},{0x31,1},{0x32,1},{0x33,1},{0x34,1},
    {0x35,1},{0x36,1},{0x37,1},{0x40,1},{0x41,1},{0x42,1},{0x43,1},{0x44,1},
    {0x45,1},{0x46,1},{0x47,1},{0x50,1},{0x51,1},{0x52,1},{0x53,1},{0x54,1},
    {0x55,1},{0x56,1},{0x57,1},{0x16,0},{0x14,0},{0x21,0},{0x13,0},{0x25,1},
    {0x15,1},{0x26,0},{0x27,0},{0x30,0},{0x31,0},{0x32,0},{0x33,0},{0x34,0},
    {0x35,0},{0x36,0},{0x37,0},{0x40,0},{0x41,0},{0x42,0},{0x43,0},{0x44,0},
    {0x45,0},{0x46,0},{0x47,0},{0x50,0},{0x51,0},{0x52,0},{0x53,0},{0x54,0},
    {0x55,0},{0x56,0},{0x57,0},{0x16,1},{0x14,1},{0x21,1},{0x13,1},
};
/* 키 편집기 그림 글자 — 그 기종 키보드에서 MSX 키(1+행*8+비트)를 Shift 없이 누르면 나오는 글자 (없으면 Shift 글자, 그래도 없으면 0) */
int keymap_legend(int kbd, int code)
{
    int row = (code - 1) >> 3, bit = (code - 1) & 7, best = 0;
    for (int pass = 0; pass < 2 && !best; pass++)
        for (int c = 0x21; c <= 0x7E; c++) {
            const uint8_t *e = kbd == KBD_JP ? jp_map[c - 0x20] : kbd_maps[kbd_index(kbd)][c - 0x20];
            if (e[0] == 0xFF) e = kbd_maps[0][c - 0x20];
            if (e[0] == 0xFF || (e[0] >> 4) != row || (e[0] & 15) != bit || ((e[1] >> 1) & 3) || (e[1] & 1) != pass) continue;
            int g = c >= 'a' && c <= 'z' ? c - 32 : c;
            if (!best || (g >= 'A' && g <= 'Z')) best = g;
        }
    return best;
}
static MsxKey char_key(int c, int letter)
{
    MsxKey k = { -1, 0, -1, 0 };
    if (c < 0x20 || c > 0x7E) return k;
    const uint8_t *e = mkbd == KBD_JP ? jp_map[c - 0x20] : kbd_maps[kbd_index(mkbd)][c - 0x20];
    if (e[0] == 0xFF) e = kbd_maps[0][c - 0x20];
    if (e[0] == 0xFF) return k;
    k.row = (int8_t)(e[0] >> 4); k.bit = (int8_t)(e[0] & 15);
    k.shift = letter ? -1 : (int8_t)(e[1] & 1);
    k.mods = (int8_t)((e[1] >> 1) & 3);
    return k;
}
/* PC 키 → 글자 (Shift 안 누름 / 누름). 0 = 글자 없음 */
static const struct { uint8_t sc; char lo, hi; } pc_us[] = {
    {0x02,'1','!'},{0x03,'2','@'},{0x04,'3','#'},{0x05,'4','$'},{0x06,'5','%'},{0x07,'6','^'},{0x08,'7','&'},
    {0x09,'8','*'},{0x0A,'9','('},{0x0B,'0',')'},{0x0C,'-','_'},{0x0D,'=','+'},{0x1A,'[','{'},{0x1B,']','}'},
    {0x27,';',':'},{0x28,'\'','"'},{0x29,'`','~'},{0x2B,'\\','|'},{0x33,',','<'},{0x34,'.','>'},{0x35,'/','?'},
}, pc_jp[] = {
    {0x02,'1','!'},{0x03,'2','"'},{0x04,'3','#'},{0x05,'4','$'},{0x06,'5','%'},{0x07,'6','&'},{0x08,'7','\''},
    {0x09,'8','('},{0x0A,'9',')'},{0x0B,'0',0},{0x0C,'-','='},{0x0D,'^','~'},{0x7D,'\\','|'},{0x1A,'@','`'},
    {0x1B,'[','{'},{0x27,';','+'},{0x28,':','*'},{0x2B,']','}'},{0x33,',','<'},{0x34,'.','>'},{0x35,'/','?'},
    {0x73,'\\','_'},
}, pc_uk[] = {
    {0x02,'1','!'},{0x03,'2','"'},{0x04,'3',0},{0x05,'4','$'},{0x06,'5','%'},{0x07,'6','^'},{0x08,'7','&'},
    {0x09,'8','*'},{0x0A,'9','('},{0x0B,'0',')'},{0x0C,'-','_'},{0x0D,'=','+'},{0x1A,'[','{'},{0x1B,']','}'},
    {0x27,';',':'},{0x28,'\'','@'},{0x29,'`',0},{0x2B,'#','~'},{0x56,'\\','|'},{0x33,',','<'},{0x34,'.','>'},{0x35,'/','?'},
}, pc_de[] = {                                  /* 3.2.12 독일 (QWERTZ). 글자에 없는 ä ö ü ß § ° ´는 비움 */
    {0x02,'1','!'},{0x03,'2','"'},{0x04,'3',0},{0x05,'4','$'},{0x06,'5','%'},{0x07,'6','&'},{0x08,'7','/'},
    {0x09,'8','('},{0x0A,'9',')'},{0x0B,'0','='},{0x0C,0,'?'},{0x0D,0,'`'},{0x1A,0,0},{0x1B,'+','*'},
    {0x27,0,0},{0x28,0,0},{0x29,'^',0},{0x2B,'#','\''},{0x56,'<','>'},{0x33,',',';'},{0x34,'.',':'},{0x35,'-','_'},
    {0x15,'z','Z'},{0x2C,'y','Y'},
}, pc_fr[] = {                                  /* 3.2.12 프랑스 (AZERTY) */
    {0x02,'&','1'},{0x03,0,'2'},{0x04,'"','3'},{0x05,'\'','4'},{0x06,'(','5'},{0x07,'-','6'},{0x08,0,'7'},
    {0x09,'_','8'},{0x0A,0,'9'},{0x0B,0,'0'},{0x0C,')',0},{0x0D,'=','+'},{0x1A,'^',0},{0x1B,'$',0},
    {0x27,'m','M'},{0x28,0,'%'},{0x29,0,0},{0x2B,'*',0},{0x56,'<','>'},{0x32,',','?'},{0x33,';','.'},{0x34,':','/'},{0x35,'!',0},
    {0x10,'a','A'},{0x1E,'q','Q'},{0x11,'z','Z'},{0x2C,'w','W'},
}, pc_de_alt[] = {                              /* AltGr */
    {0x08,'{',0},{0x09,'[',0},{0x0A,']',0},{0x0B,'}',0},{0x0C,'\\',0},{0x10,'@',0},{0x1B,'~',0},{0x56,'|',0},
}, pc_fr_alt[] = {
    {0x03,'~',0},{0x04,'#',0},{0x05,'{',0},{0x06,'[',0},{0x07,'|',0},{0x08,'`',0},{0x09,'\\',0},{0x0A,'^',0},
    {0x0B,'@',0},{0x0C,']',0},{0x0D,'}',0},
}, pc_pad[] = {
    {0x47,'7','7'},{0x48,'8','8'},{0x49,'9','9'},{0x4B,'4','4'},{0x4C,'5','5'},{0x4D,'6','6'},{0x4F,'1','1'},
    {0x50,'2','2'},{0x51,'3','3'},{0x52,'0','0'},{0x53,'.','.'},{0x4A,'-','-'},{0x4E,'+','+'},{0x37,'*','*'},
    {0xB5,'/','/'},
};
static const uint8_t pc_letters[26] = {     /* a~z의 PC 스캔 코드 */
    0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,0x31,0x18,0x19,0x10,0x13,0x1F,0x14,0x16,0x2F,0x11,0x2D,0x15,0x2C,
};
static void set(int key, MsxKey normal, MsxKey shifted);
#define NTAB(t) (int)(sizeof t / sizeof t[0])
static void char_layout(void)
{
    for (int i = 0; i < 26; i++) { MsxKey k = char_key('a' + i, 1); table[pc_letters[i]][0] = table[pc_letters[i]][1] = k; }
    const __typeof__(pc_us[0]) *t = pc_us, *alt = 0;
    int n = NTAB(pc_us), na = 0;
    if (layout == KL_JP) { t = pc_jp; n = NTAB(pc_jp); }
    else if (layout == KL_UK) { t = pc_uk; n = NTAB(pc_uk); }
    else if (layout == KL_DE) { t = pc_de; n = NTAB(pc_de); alt = pc_de_alt; na = NTAB(pc_de_alt); }
    else if (layout == KL_FR) { t = pc_fr; n = NTAB(pc_fr); alt = pc_fr_alt; na = NTAB(pc_fr_alt); }
    for (int i = 0; i < n; i++) {
        int letter = t[i].lo >= 'a' && t[i].lo <= 'z';       /* 글자 자리 바꿈(Y/Z, A/Q …): Shift는 PC 그대로 */
        if (letter) { MsxKey k = char_key(t[i].lo, 1); table[t[i].sc][0] = table[t[i].sc][1] = k; continue; }
        set(t[i].sc, char_key(t[i].lo, 0), char_key(t[i].hi, 0));
    }
    for (int i = 0; i < na; i++) alt_table[alt[i].sc] = char_key(alt[i].lo, 0);
    for (size_t i = 0; i < sizeof pc_pad / sizeof pc_pad[0]; i++) set(pc_pad[i].sc, char_key(pc_pad[i].lo, 0), char_key(pc_pad[i].hi, 0));
    if (layout == KL_JP) { table[0x29][0] = table[0x29][1] = (MsxKey)NONE; set1(0x70, (MsxKey)K(6,4)); }
    if (keymap_altgr_layout()) set1(0xDD, (MsxKey)K(6,4));    /* AltGr 배열: 메뉴 키(E0 5D) = CODE */
}

/* 3.2.12 KBD-02: 사용자 키 배치를 표에 덮어씀 (Shift는 PC 그대로, AltGr 표도 지움) */
static uint8_t base_code[256];                   /* 3.7.0: 바꾸기 전 기본 (Shift 없이 누른 MSX 키, 0 = 없음) */
int keymap_default_get(int key) { return key >= 0 && key < 256 ? base_code[key] : 0; }
static void apply_remap(void)
{
    for (int k = 0; k < 256; k++) base_code[k] = table[k][0].row >= 0 ? (uint8_t)(1 + table[k][0].row * 8 + table[k][0].bit) : 0;
    for (int k = 0; k < 256; k++) {
        if (!remap_to[k]) continue;
        MsxKey m = { -1, 0, -1, 0 };
        if (remap_to[k] != 0xFF) { m.row = (int8_t)((remap_to[k] - 1) >> 3); m.bit = (int8_t)((remap_to[k] - 1) & 7); }
        table[k][0] = table[k][1] = m;
        alt_table[k] = (MsxKey)NONE;
    }
}

void keymap_init(KeyState *ks)
{
    memset(ks, 0, sizeof *ks);
    for (int i = 0; i < 256; i++) { table[i][0] = table[i][1] = (MsxKey)NONE; alt_table[i] = (MsxKey)NONE; }

    /* 숫자 줄: 1~0과 Shift 기호 (PC 미국식 → MSX 일본식) */
    set(0x02, (MsxKey)KN(0,1), (MsxKey)KS(0,1));       /* 1 ! */
    set(0x03, (MsxKey)KN(0,2), (MsxKey)KN(1,5));       /* 2 @ */
    set(0x04, (MsxKey)KN(0,3), (MsxKey)KS(0,3));       /* 3 # */
    set(0x05, (MsxKey)KN(0,4), (MsxKey)KS(0,4));       /* 4 $ */
    set(0x06, (MsxKey)KN(0,5), (MsxKey)KS(0,5));       /* 5 % */
    set(0x07, (MsxKey)KN(0,6), (MsxKey)KN(1,3));       /* 6 ^ */
    set(0x08, (MsxKey)KN(0,7), (MsxKey)KS(0,6));       /* 7 & */
    set(0x09, (MsxKey)KN(1,0), (MsxKey)KS(2,0));       /* 8 * */
    set(0x0A, (MsxKey)KN(1,1), (MsxKey)KS(1,0));       /* 9 ( */
    set(0x0B, (MsxKey)KN(0,0), (MsxKey)KS(1,1));       /* 0 ) */
    set(0x0C, (MsxKey)KN(1,2), (MsxKey)KS(2,5));       /* - _ */
    set(0x0D, (MsxKey)KS(1,2), (MsxKey)KS(1,7));       /* = + */
    set(0x1A, (MsxKey)KN(1,6), (MsxKey)KS(1,6));       /* [ { */
    set(0x1B, (MsxKey)KN(2,1), (MsxKey)KS(2,1));       /* ] } */
    set(0x27, (MsxKey)KN(1,7), (MsxKey)KN(2,0));       /* ; : */
    set(0x28, (MsxKey)KS(0,7), (MsxKey)KS(0,2));       /* ' " */
    set(0x29, (MsxKey)KS(1,5), (MsxKey)KS(1,3));       /* ` ~ */
    set(0x2B, (MsxKey)KN(1,4), (MsxKey)KS(1,4));       /* \ | */
    set(0x33, (MsxKey)KN(2,2), (MsxKey)KS(2,2));       /* , < */
    set(0x34, (MsxKey)KN(2,3), (MsxKey)KS(2,3));       /* . > */
    set(0x35, (MsxKey)KN(2,4), (MsxKey)KS(2,4));       /* / ? */

    /* 글자 (Shift는 PC 그대로) */
    static const struct { uint8_t sc; int8_t row, bit; } letters[] = {
        {0x1E,2,6},{0x30,2,7},{0x2E,3,0},{0x20,3,1},{0x12,3,2},{0x21,3,3},{0x22,3,4},{0x23,3,5},
        {0x17,3,6},{0x24,3,7},{0x25,4,0},{0x26,4,1},{0x32,4,2},{0x31,4,3},{0x18,4,4},{0x19,4,5},
        {0x10,4,6},{0x13,4,7},{0x1F,5,0},{0x14,5,1},{0x16,5,2},{0x2F,5,3},{0x11,5,4},{0x2D,5,5},
        {0x15,5,6},{0x2C,5,7},
    };
    for (size_t i = 0; i < sizeof letters / sizeof letters[0]; i++)
        set1(letters[i].sc, (MsxKey)K(letters[i].row, letters[i].bit));

    /* 특수키 */
    set1(0x39, (MsxKey)K(8,0));         /* Space */
    set1(0x1C, (MsxKey)K(7,7));         /* Enter → RETURN */
    set1(0x9C, (MsxKey)K(7,7));         /* 키패드 Enter */
    set1(0x0E, (MsxKey)K(7,5));         /* Backspace → BS */
    set1(0x0F, (MsxKey)K(7,3));         /* Tab */
    set1(0x01, (MsxKey)K(7,2));         /* Esc */
    set1(0x1D, (MsxKey)K(6,1));         /* 왼쪽 Ctrl → CTRL */
    set1(0x38, (MsxKey)K(6,2));         /* 왼쪽 Alt → GRAPH */
    set1(0x3A, (MsxKey)K(6,3));         /* Caps Lock → CAPS */
    set1(0xB8, (MsxKey)K(6,4));         /* 한/영(E0 38) → CODE = DPC-200 한글 전환 (실기 확인 2026-10-01) */
    set1(0x9D, (MsxKey)K(6,1));         /* 한자(E0 1D) → CTRL */
    /* 기능키: F1~F5만 MSX로. 원래 MSX처럼 Shift+F1~F5 = F6~F10 (PC Shift 그대로 전달)
     * PC F6~F12는 MSX로 보내지 않음 (blueMSX 단축키, fe.c) */
    set1(0x3B, (MsxKey)K(6,5));
    set1(0x3C, (MsxKey)K(6,6));
    set1(0x3D, (MsxKey)K(6,7));
    set1(0x3E, (MsxKey)K(7,0));
    set1(0x3F, (MsxKey)K(7,1));
    /* 편집·방향 */
    set1(0xC7, (MsxKey)K(8,1));         /* Home */
    set1(0xD2, (MsxKey)K(8,2));         /* Ins */
    set1(0xD3, (MsxKey)K(8,3));         /* Del */
    set1(0xCB, (MsxKey)K(8,4));         /* Left */
    set1(0xC8, (MsxKey)K(8,5));         /* Up */
    set1(0xD0, (MsxKey)K(8,6));         /* Down */
    set1(0xCD, (MsxKey)K(8,7));         /* Right */
    set1(0xCF, (MsxKey)K(7,4));         /* End → STOP */
    set1(0xC9, (MsxKey)K(7,6));         /* Page Up → SELECT */
    /* 숫자 키패드 (Num Lock과 무관하게 숫자) */
    static const struct { uint8_t sc; int8_t row, bit; } pad[] = {
        {0x52,0,0},{0x4F,0,1},{0x50,0,2},{0x51,0,3},{0x4B,0,4},{0x4C,0,5},{0x4D,0,6},{0x47,0,7},
        {0x48,1,0},{0x49,1,1},
    };
    for (size_t i = 0; i < sizeof pad / sizeof pad[0]; i++)
        set1(pad[i].sc, (MsxKey)KN(pad[i].row, pad[i].bit));
    set1(0x4A, (MsxKey)KN(1,2));        /* KP - */
    set1(0x4E, (MsxKey)KS(1,7));        /* KP + */
    set1(0x37, (MsxKey)KS(2,0));        /* KP * */
    set1(0xB5, (MsxKey)KN(2,4));        /* KP / */
    set1(0x53, (MsxKey)KN(2,3));        /* KP . */

    if (mkbd != KBD_JP || layout >= KL_DE) { char_layout(); apply_remap(); return; }   /* 2.13: 일본식이 아닌 기종, 3.2.12: 독·프·영 PC 배열은 글자 기준 */
    if (layout == 2) {                  /* 일본 106키: 기호 자리가 MSX 일본식 배열과 같음 → 자리 그대로, Shift도 그대로 */
        static const struct { uint8_t sc; int8_t row, bit; } jp[] = {
            {0x02,0,1},{0x03,0,2},{0x04,0,3},{0x05,0,4},{0x06,0,5},{0x07,0,6},{0x08,0,7},{0x09,1,0},{0x0A,1,1},{0x0B,0,0},
            {0x0C,1,2},{0x0D,1,3},{0x7D,1,4},{0x1A,1,5},{0x1B,1,6},{0x27,1,7},{0x28,2,0},{0x2B,2,1},
            {0x33,2,2},{0x34,2,3},{0x35,2,4},{0x73,2,5},
        };
        for (size_t i = 0; i < sizeof jp / sizeof jp[0]; i++) set1(jp[i].sc, (MsxKey)K(jp[i].row, jp[i].bit));
        set1(0x29, (MsxKey)NONE);       /* 半角/全角 */
        set1(0x70, (MsxKey)K(6,4));     /* かな → KANA/CODE */
    }
    apply_remap();
}

static int is_shift(int key) { return key == 0x2A || key == 0x36; }

void keymap_event(KeyState *ks, int key, int pressed)
{
    key &= 0xFF;
    if (key == 0xAA || key == 0xB6) return;     /* 가짜 Shift (E0 2A / E0 36) */
    if (is_shift(key)) {
        ks->shift_held[key == 0x36] = (uint8_t)pressed;
        return;
    }
    if (key == 0xB8 && keymap_altgr_layout() && !remap_to[0xB8]) { ks->altgr_held = (uint8_t)pressed; return; }   /* 3.2.12 AltGr */
    if (pressed) {
        if (ks->held[key]) return;               /* 타이프매틱 반복 무시 */
        int sh = ks->shift_held[0] || ks->shift_held[1];
        ks->target[key] = ks->altgr_held && alt_table[key].row >= 0 ? alt_table[key] : table[key][sh];
        ks->held[key] = 1;
        ks->stamp[key] = ++ks->clock;
    } else {
        ks->held[key] = 0;
    }
}

/* 이 키를 지금 누르면 들어갈 MSX 행 (-1 없음). 입력 계층이 8행(스페이스·방향키) 판단에 씀 */
int keymap_press_row(const KeyState *ks, int key)
{
    key &= 0xFF;
    if (is_shift(key) || key == 0xAA || key == 0xB6) return -1;
    int sh = ks->shift_held[0] || ks->shift_held[1];
    MsxKey t = ks->altgr_held && alt_table[key].row >= 0 ? alt_table[key] : table[key][sh];
    return t.row;
}

void keymap_matrix(const KeyState *ks, uint8_t matrix[16])
{
    memset(matrix, 0xFF, 16);
    int override = -1;
    uint32_t best = 0;
    for (int k = 0; k < 256; k++) {
        if (!ks->held[k]) continue;
        MsxKey t = ks->target[k];
        if (t.row < 0) continue;
        matrix[t.row] &= (uint8_t)~(1 << t.bit);
        if (t.shift >= 0 && ks->stamp[k] >= best) { best = ks->stamp[k]; override = t.shift; }
        if (t.mods & 1) matrix[6] &= (uint8_t)~(1 << 2);   /* GRAPH */
        if (t.mods & 2) matrix[6] &= (uint8_t)~(1 << 4);   /* CODE */
    }
    int shift = override >= 0 ? override : (ks->shift_held[0] || ks->shift_held[1]);
    if (shift) matrix[6] &= (uint8_t)~1;
    else matrix[6] |= 1;
}
