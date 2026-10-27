/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fe_basic.inc - 3.2.7 BASIC 텍스트 넣기·저장 (fe.c가 포함)
 * 넣기: USB의 TXT(ASCII BASIC 소스 등)를 MSX 키 버퍼(KEYBUF FBF0h, 40바이트, PUTPNT F3F8h / GETPNT F3FAh)에
 *       빈 자리만큼 매 프레임 넣는다 — 손으로 친 것과 같음. 줄 끝 CR LF·LF·CR → CR, 탭은 그대로, 1Ah(EOF)에서 끝, 다른 제어 문자는 버림.
 * 저장: 메모리의 BASIC 프로그램(TXTTAB F676h)을 basic_detok로 LIST 모양 글로 바꿔 BASIC\BASIC_0001.txt … 에 씀. */
#define BAS_MAX (64u * 1024u)
#define KB_BUF 0xFBF0u
#define KB_END 0xFC18u                                  /* 40바이트 */
#define KB_PUT 0xF3F8u
#define KB_GET 0xF3FAu
static uint8_t bas_buf[BAS_MAX];
static char bas_name[64];

static uint16_t bas_rd16(uint16_t a) { return (uint16_t)(msx_peek(M, a) | msx_peek(M, (uint16_t)(a + 1)) << 8); }
static int bas_ram(uint16_t a) { return M->rd[a >> 13] == M->wr[a >> 13]; }          /* 그 자리가 RAM */
static void bas_wr(uint16_t a, uint8_t v) { if (bas_ram(a)) M->wr[a >> 13][a & 0x1FFF] = v; }

static int bas_kb_ok(void)
{
    uint16_t pu = bas_rd16(KB_PUT), ge = bas_rd16(KB_GET);
    return bas_ram(KB_BUF) && pu >= KB_BUF && pu < KB_END && ge >= KB_BUF && ge < KB_END;
}

static void bas_stop(int user)
{
    if (!bas_len) return;
    bas_len = 0;
    if (user) toast(T(S_BAS_STOPPED), bas_name);
}

/* 토큰 BAS 파일(SAVE "x", 첫 바이트 FFh)을 글로 되돌림. 파일은 8001h부터 올라간 모양 */
typedef struct { const uint8_t *b; uint32_t n; } BasFileMem;
static uint8_t bas_file_peek(void *ctx, uint16_t a)
{
    const BasFileMem *m = (const BasFileMem *)ctx;
    uint32_t i = (uint32_t)a - 0x8001u + 1u;
    return a >= 0x8001u && i < m->n ? m->b[i] : 0;
}
static int bas_untoken(uint32_t *n)
{
    if (!P.scratch || P.scratch_size < (256u << 10)) return -1;
    BasFileMem m = { bas_buf, *n };
    int lines = 0, mx = (int)(P.scratch_size < (1u << 20) ? P.scratch_size : (1u << 20));
    int k = basic_detok_at(bas_file_peek, &m, 0x8001, (char *)P.scratch, mx, &lines);
    if (k <= 0 || (uint32_t)k > BAS_MAX - 1) return -1;
    memcpy(bas_buf, P.scratch, (size_t)k);
    *n = (uint32_t)k;
    return 0;
}

static void bas_paste_file(const char *path, const char *name)
{
    uint32_t n = 0;
    char zn[64];
    if (is_zip(path)) { if (zip_read_member(path, "txt|asc|bas", bas_buf, BAS_MAX - 1, &n, zn, sizeof zn, 0)) return; name = zn; }   /* 3.9.6: zip 안의 텍스트 */
    else if (!P.read_file || P.read_file(path, bas_buf, BAS_MAX - 1, &n) || !n) { toast(T(S_LOAD_ERROR), name); return; }
    if (bas_buf[0] == 0xFF && bas_untoken(&n)) { toast(T(S_BAS_TOKEN), name); return; }       /* 토큰 BAS (SAVE "x"로 저장한 것): 5.0.38 글로 되돌려 넣음, 안 되면 알림 */
    /* 글 다듬기: UTF-8 BOM 빼기, 줄 끝을 CR 하나로, EOF(1Ah)에서 끝 */
    uint32_t i = 0, k = 0;
    if (n >= 3 && bas_buf[0] == 0xEF && bas_buf[1] == 0xBB && bas_buf[2] == 0xBF) i = 3;
    int lines = 0;
    for (; i < n; i++) {
        uint8_t c = bas_buf[i];
        if (c == 0x1A) break;
        if (c == '\r') { bas_buf[k++] = '\r'; lines++; if (i + 1 < n && bas_buf[i + 1] == '\n') i++; continue; }
        if (c == '\n') { bas_buf[k++] = '\r'; lines++; continue; }
        if (c < 0x20 && c != '\t') continue;
        bas_buf[k++] = c;
    }
    if (k && bas_buf[k - 1] != '\r') { bas_buf[k++] = '\r'; lines++; }  /* 마지막 줄도 입력 */
    if (!k) { toast(T(S_LOAD_ERROR), name); return; }
    if (!bas_kb_ok()) { toast(T(S_BAS_NOTREADY), 0); return; }
    copy_str(bas_name, sizeof bas_name, name);
    bas_len = (int)k; bas_pos = 0; bas_lines = lines;
    if (run_state != RUN_ON) do_action(A_RUN_PAUSE);
    toast(T(S_BAS_TYPING), bas_name);
}

/* 매 프레임 (MSX가 돌기 전): 키 버퍼 빈 자리만큼 */
static void bas_feed(void)
{
    if (!bas_kb_ok()) return;                           /* 지금은 BASIC이 아님 (게임 등): 기다림 */
    uint16_t pu = bas_rd16(KB_PUT), ge = bas_rd16(KB_GET);
    int put = 0;
    while (bas_pos < bas_len) {
        uint16_t nx = (uint16_t)((uint32_t)pu + 1u >= KB_END ? KB_BUF : (uint32_t)pu + 1u);
        if (nx == ge) break;                            /* 가득 참 (한 칸은 비워 둠) */
        uint8_t c = bas_buf[bas_pos++];
        bas_wr(pu, c);
        pu = nx; put++;
        if (c == '\r') break;                           /* 한 프레임에 한 줄: BASIC이 줄을 처리할 틈 */
    }
    if (put) { bas_wr(KB_PUT, (uint8_t)pu); bas_wr((uint16_t)(KB_PUT + 1), (uint8_t)(pu >> 8)); }
    if (bas_pos >= bas_len) {
        char t[80];
        copy_str(t, sizeof t, bas_name); str_append(t, sizeof t, " (");
        num_append(t, sizeof t, (uint32_t)bas_lines, 0, ' '); str_append(t, sizeof t, lang == 0 ? "줄)" : " lines)");
        bas_len = 0;
        full_redraw();                                  /* 앞 알림(넣는 중)이 겹쳐 남지 않게 */
        toast(T(S_BAS_DONE), t);
    }
}

static uint8_t bas_peek_cb(void *ctx, uint16_t a) { (void)ctx; return msx_peek(M, a); }

/* 3.2.9 BASIC TXT변환 창 열기 (넣기·저장 한 창, 마지막 탭으로) */
static void bas_open(void)
{
    open_browser(bas_mode_save ? 15 : 14);
    bas_set_mode(bas_mode_save);
}

/* 저장 창에서 Enter: 지금 폴더 + 이름 칸 (확장자가 없으면 .txt) */
static void bas_save_to(void)
{
    if (!bas_fname[0]) { toast(T(S_BAS_NONAME), 0); return; }
    if (!cur_path[0]) { toast(T(S_BAS_SAVEFAIL), 0); return; }  /* 드라이브 목록에서는 저장할 수 없음 */
    char name[72], path[PATH_MAX_FE];
    copy_str(name, sizeof name, bas_fname);
    int dot = 0;
    for (const char *q = name; *q; q++) if (*q == '.') dot = 1;
    if (!dot) str_append(name, sizeof name, ".txt");
    copy_str(path, sizeof path, cur_path); str_append(path, sizeof path, "\\"); str_append(path, sizeof path, name);
    static uint8_t tiny[8];
    uint32_t got = 0;
    if (!P.read_file(path, tiny, sizeof tiny, &got) && !bas_over) { bas_over = 1; toast(T(S_BAS_EXISTS), name); return; }   /* 있음: 한 번 묻기 */
    int lines = 0;
    char *out = (char *)P.scratch;
    int max = (int)(P.scratch_size < (1u << 20) ? P.scratch_size : (1u << 20));
    int n = basic_detok(bas_peek_cb, 0, out, max, &lines);
    if (n <= 0) { toast(T(S_BAS_NOPROG), 0); return; }
    if (P.create(path, 0) || P.write_file(path, (const uint8_t *)out, (uint32_t)n)) { toast(T(S_BAS_SAVEFAIL), name); return; }
    copy_str(bas_fname, sizeof bas_fname, name);
    copy_str(bas_sel, sizeof bas_sel, name);
    browse_remember();
    dialog_done();
    char t[96];
    copy_str(t, sizeof t, name); str_append(t, sizeof t, " (");
    num_append(t, sizeof t, (uint32_t)lines, 0, ' '); str_append(t, sizeof t, lang == 0 ? "줄)" : " lines)");
    toast(T(S_BAS_SAVED), t);
}
