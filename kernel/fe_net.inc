/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fe_net.inc - 5.0.57 네트워크 설정 창 (대화 창 18). fe.c가 fe_edit.inc 다음에 포함한다.
 * 옵션 > 네트워크: 켜기/끄기(네트워크 카트리지), 연결 방식(유선·무선), 무선 네트워크(SSID)·암호, 주소(자동 DHCP · 수동 IP·마스크·게이트웨이·DNS), 상태.
 * 값은 설정 파일(NetWifi NetManual NetIP NetMask NetGW NetDNS NetSSID NetPass)에 저장하고, UEFI 쪽(netuefi)에 넘긴다.
 * 인터넷이 잡혔는지(주소를 받음)는 왼쪽 아래 인터넷 아이콘으로 — 잡힐 때만 보인다.
 * 무선 찾기·연결은 UEFI 펌웨어의 Wi-Fi 지원에 달려 있어 uefi/netwifi.c 의 호출을 통한다 (없으면 "무선 어댑터 없음"). */

#ifdef FE_UEFI
extern int uefi_net_online(void);
extern int uefi_net_has_nic(void);
extern void uefi_net_lease_set(const uint8_t ip[4]);                   /* 마지막으로 받은 주소를 알려 줌 (DHCP INIT-REBOOT) */
extern void uefi_net_select(int wired);                               /* 사용자가 유선을 골랐는지 (USB 랜 드라이버를 쓸지 정함) */
extern int uefi_net_wifi_live(void);                                  /* 1 = 무선 실시간(lwIP) 모드 */
extern void uefi_net_diag(char *o, int max);
extern int uefi_net_ipinfo(uint8_t out[20]);
extern void uefi_net_config(int manual, const uint8_t ip[4], const uint8_t mask[4], const uint8_t gw[4], const uint8_t dns[4]);
extern int uefi_wired_present(void);                                  /* 1 = 유선 랜카드가 있음 */
static void net_renew_call(void) { }
extern int uefi_wifi_present(void);                                   /* 1 = 무선 어댑터와 Wi-Fi 지원이 있음 */
extern int uefi_wifi_scan_begin(void);                                /* 찾기 시작 (약 20초 계속) */
extern int uefi_wifi_scanning(void);
extern void uefi_wifi_scan_stop(void);
extern int uefi_wifi_scan_get(char ssid[][36], int *sig, int *sec, int max);   /* 지금까지 찾은 수, 오류는 음수 */
extern int uefi_wifi_connect(const char *ssid, const char *pass);     /* 0 = 요청함 */
extern int uefi_wifi_state(void);                                      /* 0 미지정 1 연결 중 2 연결됨 3 공유기 못 찾음 4 접속 실패 */
#else
/* 5.0.108 레거시(BIOS) 판: kernel/net/netstack.c(lwIP + 유선 드라이버)를 부른다. 시험 틀(fehost)에는 없으므로 약한 연결 → 없으면 0 */
extern int net_online(void) __attribute__((weak));
extern void net_renew(void) __attribute__((weak));
extern int net_has_nic(void) __attribute__((weak));
extern int net_hw_present(void) __attribute__((weak));
extern void net_diag_text(char *o, int max) __attribute__((weak));
extern int net_ipinfo(uint8_t out[20]) __attribute__((weak));
extern void net_config(int manual, const uint8_t ip[4], const uint8_t mask[4], const uint8_t gw[4], const uint8_t dns[4]) __attribute__((weak));
static int uefi_net_online(void) { return net_online ? net_online() : 0; }
static int uefi_net_has_nic(void) { return net_hw_present ? net_hw_present() : 0; }
static int uefi_net_wifi_live(void) { return 0; }
static void uefi_net_select(int wired) { (void)wired; }
extern void net_lease_set(const uint8_t ip[4]) __attribute__((weak));
static void uefi_net_lease_set(const uint8_t ip[4]) { if (net_lease_set) net_lease_set(ip); }
static void uefi_net_diag(char *o, int max) { if (net_diag_text) net_diag_text(o, max); else copy_str(o, max, "(no NIC)"); }
static int uefi_net_ipinfo(uint8_t out[20]) { return net_ipinfo ? net_ipinfo(out) : -1; }
static void uefi_net_config(int manual, const uint8_t ip[4], const uint8_t mask[4], const uint8_t gw[4], const uint8_t dns[4]) { if (net_config) net_config(manual, ip, mask, gw, dns); }
static int uefi_wired_present(void) { return net_hw_present ? net_hw_present() : 0; }
static void net_renew_call(void) { if (net_renew) net_renew(); }
static int uefi_wifi_present(void) { return 0; }
static int uefi_wifi_scan_begin(void) { return -1; }
static int uefi_wifi_scanning(void) { return 0; }
static void uefi_wifi_scan_stop(void) { }
static int uefi_wifi_scan_get(char ssid[][36], int *sig, int *sec, int max) { (void)ssid; (void)sig; (void)sec; (void)max; return -1; }
static int uefi_wifi_connect(const char *ssid, const char *pass) { (void)ssid; (void)pass; return -1; }
static int uefi_wifi_state(void) { return 0; }
#endif

/* 시험 틀(fehost)만 이 값을 정의한다: 1 = 유선, 2 = 무선, 3 = 둘 다 있는 것처럼 */
extern int fe_test_net __attribute__((weak));
static int net_has_wired(void) { return uefi_wired_present() || (&fe_test_net && (fe_test_net & 1)); }
static int net_has_wifi(void) { return uefi_wifi_present() || (&fe_test_net && (fe_test_net & 2)); }
static int net_any_adapter(void) { return net_has_wired() || net_has_wifi(); }
static int net_dmode;                                   /* 지금 열린 창 = 0 유선 네트워크 / 1 무선 네트워크 (메뉴가 따로) */
static int net_wifi, net_manual;                        /* 연결 방식(우선): 0 유선 / 1 무선, 0 자동(DHCP) / 1 수동 */
static int net_mem_wifi = 1;                            /* 무선을 마지막으로 켰나(1)/껐나(0) — 카트리지를 다시 꽂을 때 되살림. 유선은 꽂으면 항상 켜짐 */
static uint32_t net_lease[2]; static char net_lease_ssid[36];   /* 마지막으로 받은 주소 [0 유선, 1 무선] (+무선은 그때 공유기 이름) */
static int net_wired_on, net_wifi_on;                   /* 유선·무선 각각 켜기/끄기 (하나라도 켜져 있으면 네트워크 카트리지가 꽂힘) */
static char net_ip[16], net_mask[16] = "255.255.255.0", net_gw[16], net_dns[16], net_ssid[36], net_pass[64];
static int net_sel, net_edit = -1, net_elen, net_scanning;   /* net_edit = 글자 고치는 줄 (-1 없음) */
static char net_ebuf[64];
static char net_ap_ssid[64][36]; static int net_ap_sig[64], net_ap_sec[64], net_ap_n, net_ap_sel, net_ap_busy, net_ap_top;
static char net_sv_ssid[8][36], net_sv_pass[8][64]; static int net_sv_wired, net_sv_wifi;
static int net_sv_n, net_sv_sel, net_ap_mode, net_ap_vis = 8;   /* 저장한 공유기 목록. net_ap_mode: 0 목록 1 암호 입력 2 접속 중 3 저장 목록(Del)
*/
static char net_pssid[36], net_ppass[64]; static int net_pend;    /* 고르는 중인 공유기·암호 (0 없음 1 암호 입력 중 2 접속 중). 접속에 성공해야 net_ssid/net_pass 에 저장 */

static const char *nt(const char *ko, const char *en, const char *ja) { return lang == 0 ? ko : lang == 2 ? ja : en; }

static int net_parse_ip(const char *s, uint8_t out[4])  /* "a.b.c.d" → 1 */
{
    int part = 0, val = 0, digs = 0;
    uint8_t o[4] = { 0, 0, 0, 0 };
    for (const char *p = s;; p++) {
        if (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); if (++digs > 3 || val > 255) return 0; }
        else if (*p == '.' || *p == 0) {
            if (!digs || part > 3) return 0;
            o[part++] = (uint8_t)val; val = 0; digs = 0;
            if (!*p) break;
        } else return 0;
    }
    if (part != 4) return 0;
    memcpy(out, o, 4);
    return 1;
}

static void net_apply(void)                             /* 값을 UEFI 쪽에 넘긴다 */
{
    uint8_t ip[4] = { 0, 0, 0, 0 }, mk[4] = { 0, 0, 0, 0 }, gw[4] = { 0, 0, 0, 0 }, dn[4] = { 0, 0, 0, 0 };
    int ok = net_manual && net_parse_ip(net_ip, ip) && net_parse_ip(net_mask, mk);
    if (net_manual) { net_parse_ip(net_gw, gw); net_parse_ip(net_dns, dn); }
    uefi_net_config(ok ? 1 : 0, ip, mk, gw, dn);
}

static void net_sv_add(const char *ss, const char *pw)   /* 맨 앞에 넣는다 (같은 이름은 갱신) */
{
    int at = net_sv_n;
    for (int i = 0; i < net_sv_n; i++) if (!strcmp(net_sv_ssid[i], ss)) { at = i; break; }
    if (at == net_sv_n) { if (net_sv_n < 8) net_sv_n++; else at = 7; }
    for (int i = at; i > 0; i--) { copy_str(net_sv_ssid[i], 36, net_sv_ssid[i - 1]); copy_str(net_sv_pass[i], 64, net_sv_pass[i - 1]); }
    copy_str(net_sv_ssid[0], 36, ss); copy_str(net_sv_pass[0], 64, pw);
}
static const char *net_sv_find(const char *ss) { for (int i = 0; i < net_sv_n; i++) if (!strcmp(net_sv_ssid[i], ss)) return net_sv_pass[i]; return 0; }

static void net_wifi_push(void)                          /* 고른 공유기·암호로 (다시) 접속 */
{
    if (!(net_wifi && net_wifi_on)) return;
    if (net_pend == 2) uefi_wifi_connect(net_pssid, net_ppass);
    else uefi_wifi_connect(net_ssid, net_pass);
}

static int net_cart_slot(void)
{
    for (int k = 0; k < MSX_CART_SLOTS; k++) if (M->cart[k].present && M->cart[k].mapper == MAPPER_NET) return k;
    return -1;
}
static void net_cart_ensure(void)                       /* 스위치를 켜면 네트워크 카트리지가 없을 때 알아서 꽂는다 */
{
    if (net_cart_slot() >= 0) return;
    int f = free_cart_slot(); if (f >= 0) special_apply(f, special_kind(159), 1);   /* 번호를 SPK_NET(만드는 법 26)로 넘겨 엉뚱한 카트리지가 꽂히거나 안 꽂히던 것 → 카탈로그 번호 59 (코드 159) */
}
/* 줄: 0 유선 켜기 1 무선 켜기 2 연결 방식 [3 무선 이름 4 암호] 5 주소 6 IP 7 마스크 8 게이트웨이 9 DNS */
static int net_rows(int *r)
{
    int n = 0;                                           /* 카드가 꽂혀 있을 때만 이 창이 열리므로 켜기·끄기 줄을 다시 보인다 (끄면 네트워크만 멈추고 카드는 그대로) */
    if (!net_dmode) r[n++] = 0;                          /* 유선·무선 창을 따로 — 유선 창은 켜기 + 주소, 무선 창은 켜기 + 네트워크·암호 + 주소 */
    else { r[n++] = 1; r[n++] = 3; r[n++] = 4; }
    r[n++] = 5; r[n++] = 6; r[n++] = 7; r[n++] = 8; r[n++] = 9;
    return n;
}
static char *net_field(int row) { return row == 3 ? (net_pend ? net_pssid : net_ssid) : row == 4 ? (net_pend ? net_ppass : net_pass) : row == 6 ? net_ip : row == 7 ? net_mask : row == 8 ? net_gw : net_dns; }
static int net_field_max(int row) { return row == 3 ? 32 : row == 4 ? 63 : 15; }
static int net_win_on(void) { return net_dmode ? net_wifi_on : net_wired_on; }   /* 이 창(무선/유선)의 스위치 */
static int net_row_enabled(int row) { if (row <= 1) return 1; if (!net_win_on()) return 0; return row < 5 ? 1 : (row == 5 || net_manual); }   /* 꺼져 있으면 켜기·끄기 줄만 살아 있고 나머지는 흐리게 */

static void net_status_text(char *t, int max)
{
    t[0] = 0;
    if (!net_has_wifi()) net_wifi = net_wifi_on = 0;   /* 무선 어댑터가 없으면 무선은 꺼진 것으로 */
    if (!net_has_wired()) net_wired_on = 0;             /* 유선 랜카드가 없으면 유선도 꺼진 것으로 */
    if (!(net_dmode ? net_wifi_on : net_wired_on)) { copy_str(t, max, nt("꺼짐", "Off", "オフ")); return; }   /* 이 창(무선/유선)의 스위치가 꺼져 있으면 꺼짐 */
    if (net_cart_slot() < 0) { copy_str(t, max, nt("꺼짐 (네트워크 카트리지를 끼우지 못함)", "Off (cartridge not inserted)", "オフ (カートリッジ未挿入)")); return; }
    uint8_t ii[20];
    if (uefi_net_online() && uefi_net_ipinfo(ii) == 0) {
        copy_str(t, max, nt("연결됨", "Connected", "接続済み"));
        if (net_wifi_on && !net_wired_on && (net_pend ? net_pssid[0] : net_ssid[0])) { str_append(t, max, " - "); str_append(t, max, net_pend ? net_pssid : net_ssid); }
        return;
    }
    if (net_wifi_on && !net_wired_on && uefi_net_wifi_live()) {                          /* 윈도우처럼 단계별 안내 */
        int ws = uefi_wifi_state();
        if (net_pend == 1) copy_str(t, max, nt("암호를 입력하고 Enter", "Type the password and press Enter", "パスワードを入力して Enter"));
        else if (ws == 0) copy_str(t, max, nt("무선 네트워크를 고르세요 (무선 네트워크 줄에서 Enter)", "Choose a wireless network (press Enter on the network row)", "無線ネットワークを選んでください (ネットワーク行で Enter)"));
        else if (ws == 2) copy_str(t, max, nt("연결됨 - 주소를 받는 중", "Connected - getting an address", "接続済み - アドレス取得中"));
        else if (ws == 3) copy_str(t, max, nt("공유기를 찾지 못함 (이름 확인. 2.4GHz WPA2 만 지원)", "Network not found (2.4GHz WPA2 only)", "ネットワークが見つかりません (2.4GHz WPA2 のみ)"));
        else if (ws == 4) copy_str(t, max, nt("접속하지 못함 (암호를 확인하세요)", "Could not connect (check the password)", "接続できません (パスワードを確認)"));
        else copy_str(t, max, nt("연결하는 중...", "Connecting...", "接続中..."));
        return;
    }
    if (net_wifi_on && !net_wired_on) { copy_str(t, max, nt("무선 카드를 찾았지만 아직 연결 기능이 없음 (노트북 무선 스위치·Fn 키로 켜 두세요)", "Wi-Fi card found, but connecting is not available yet (turn the laptop radio on with its switch / Fn key)", "無線カードあり、接続機能は未対応 (ノートPCの無線スイッチ/Fnキーでオン)")); return; }
    if (!uefi_net_has_nic() && net_has_wired()) { copy_str(t, max, nt("유선 랜 장치를 찾았지만 X86MSX 에 이 장치용 드라이버가 아직 없음 (케이블 문제가 아님)", "Wired adapter found, but there is no driver for it yet", "有線LAN機器は見つかりましたが、この機器のドライバーはまだありません")); return; }
    if (!uefi_net_has_nic()) { copy_str(t, max, nt("유선 랜카드를 찾지 못함 (펌웨어가 이 랜카드를 지원하지 않음)", "No wired network card found (firmware does not support it)", "有線LANカードが見つかりません (ファームウェア未対応)")); return; }
    copy_str(t, max, nt("연결 안 됨 (케이블·공유기를 확인하세요, 주소를 기다리는 중)", "Not connected (waiting for an address)", "未接続 (アドレス待ち)"));
}

static void open_net(int mode) { net_dmode = mode < 0 ? (net_has_wifi() && (net_wifi || !net_has_wired())) : mode != 0; net_edit = -1; if (net_scanning) uefi_wifi_scan_stop(); net_scanning = 0; net_ap_mode = 0; if (net_pend == 1) net_pend = 0; open_dialog(18); }

static void net_tick(void)                              /* 왼쪽 아래 인터넷 아이콘: 주소를 받으면 보이고, 못 받으면 사라짐 */
{
    uefi_net_select(net_wired_on && !net_wifi_on);             /* 5.2.110 */
    {   /* DHCP 규격(RFC 2131 INIT-REBOOT): 마지막으로 받은 주소를 기억해 두었다가 다음에 먼저 요청 (윈도우처럼). 유선·무선 따로, 무선은 같은 공유기일 때만 */
        int k = net_wired_on ? 0 : net_wifi_on ? 1 : -1;
        if (k >= 0 && !net_manual) {
            uint8_t ii[20];
            if (uefi_net_online() && uefi_net_ipinfo(ii) == 0 && (ii[0] | ii[1] | ii[2] | ii[3])) {
                uint32_t v; memcpy(&v, ii, 4);
                if (v != net_lease[k] || (k == 1 && strcmp(net_lease_ssid, net_ssid))) { net_lease[k] = v; if (k == 1) copy_str(net_lease_ssid, sizeof net_lease_ssid, net_ssid); settings_changed(); settings_flush(); }
            }
            uint32_t use = (k == 1 && strcmp(net_lease_ssid, net_ssid)) ? 0 : net_lease[k];
            uint8_t b4[4]; memcpy(b4, &use, 4); uefi_net_lease_set(b4);
        }
    }
    { static int boot_chk;                                     /* 전체 상태 저장 OFF 이면 켤 때 네트워크 카드가 빠져 있고 유선·무선도 꺼진 채로 시작 */
      if (!boot_chk) { boot_chk = 1;
          if (!global_save) { int k = net_cart_slot(); if (k >= 0) { cart_remove(k); cart_special[k] = 0; cart_path[k][0] = 0; cart_name[k][0] = 0; cart_mapper[k] = -1; cart_size[k] = 0; } net_wired_on = net_wifi_on = 0; } } }
    { static int had = -1; int cs = net_cart_slot() >= 0;      /* 카드를 꽂으면 켜짐으로 시작, 빼면 꺼짐. 꽂혀 있는 동안은 창에서 켜고 끌 수 있다 */
      if (!net_has_wired() && net_has_wifi()) net_wifi = 1;
      if (had < 0) had = cs;
      if (cs && !had) {
          int w = net_has_wired(), f = !w && net_has_wifi() && net_mem_wifi;   /* 유선은 꽂으면 항상 켜짐, 유선이 없으면 무선은 전에 켰나/껐나를 따름 */
          if (w) net_wifi = 0; else if (f) net_wifi = 1;
          if (w != net_wired_on || f != net_wifi_on) { net_wired_on = w; net_wifi_on = f; settings_changed(); }
          net_renew_call();                                    /* 꽂으면 주소 다시 받기 */
      }
      if (!cs && (net_wired_on || net_wifi_on)) { if (net_wifi_on) net_mem_wifi = 1; net_wired_on = net_wifi_on = 0; settings_changed(); }
      had = cs; }
    { static int pushed, last_ws = -1;                    /* 카드를 꽂으면 저장된 공유기로 접속, 상태가 바뀌면 알림·창 다시 그림 */
      int cs2 = net_cart_slot() >= 0;
      if (!cs2) pushed = 0;
      else if (!pushed && net_wifi && net_wifi_on && (net_ssid[0] || net_sv_n) && !net_pend && uefi_net_wifi_live()) {
          /* 저장한 네트워크가 여럿이면 잠깐(약 4초) 찾아서 지금 보이는 것 중 신호가 가장 센 곳에 붙는다 (윈도처럼). 하나면 바로 */
          static uint32_t t0; static int scanning_auto;
          if (net_sv_n <= 1) { if (!net_ssid[0] && net_sv_n) { copy_str(net_ssid, sizeof net_ssid, net_sv_ssid[0]); copy_str(net_pass, sizeof net_pass, net_sv_pass[0]); } pushed = 1; net_wifi_push(); }
          else if (!scanning_auto) { scanning_auto = 1; t0 = fe_frame_count; if (uefi_wifi_scan_begin() < 0) t0 = fe_frame_count - 1000; }
          else if (fe_frame_count - t0 > 240 || !uefi_wifi_scanning()) {
              char ss[64][36]; int sg[64], sc[64]; int n = uefi_wifi_scan_get(ss, sg, sc, 64);
              int bi = -1, bs = -1;
              for (int i = 0; i < n; i++) { const char *pw = net_sv_find(ss[i]); if (pw && sg[i] > bs) { bs = sg[i]; bi = i; } }
              if (bi >= 0) { copy_str(net_ssid, sizeof net_ssid, ss[bi]); copy_str(net_pass, sizeof net_pass, net_sv_find(ss[bi])); }
              else if (!net_ssid[0]) { copy_str(net_ssid, sizeof net_ssid, net_sv_ssid[0]); copy_str(net_pass, sizeof net_pass, net_sv_pass[0]); }
              if (!net_scanning) uefi_wifi_scan_stop();
              scanning_auto = 0; pushed = 1; net_wifi_push();
          }
      }
      if (cs2 && net_wifi_on && uefi_net_wifi_live()) {
          int ws = uefi_wifi_state();
          if (net_pend == 2 && ws == 2) {                         /* 윈도우처럼 — 접속에 성공해야 공유기·암호를 저장하고 전체 화면으로 */
              net_sv_add(net_pssid, net_ppass);
              copy_str(net_ssid, sizeof net_ssid, net_pssid); copy_str(net_pass, sizeof net_pass, net_ppass);
              net_pend = 0; net_ppass[0] = 0; net_ap_mode = 0; if (net_scanning) { uefi_wifi_scan_stop(); net_scanning = 0; }
              settings_changed(); menu_dirty = 1; full_redraw();
              toast(nt("연결됨 - 이 공유기와 암호를 저장했습니다", "Connected - network and password saved", "接続済み - ネットワークとパスワードを保存しました"), 0);
          } else if (net_pend == 2 && (ws == 3 || ws == 4)) {      /* 실패: 저장하지 않는다. 찾기 창이 열려 있으면 암호를 다시 입력 */
              toast(ws == 3 ? nt("공유기를 찾지 못했습니다", "Network not found", "ネットワークが見つかりません") : nt("암호가 맞지 않습니다. 다시 입력하세요", "Wrong password. Try again", "パスワードが違います。もう一度"), 0);
              if (net_scanning) { net_pend = 1; net_ap_mode = 1; net_elen = 0; net_ebuf[0] = 0; }
              else net_pend = 0;
              net_ppass[0] = 0; menu_dirty = 1; full_redraw();
              net_wifi_push();                                     /* 이전에 저장한 공유기가 있으면 다시 접속, 없으면 비움 */
              ws = uefi_wifi_state(); last_ws = ws;
          }
          if (ws != last_ws) {
              if (last_ws >= 0 && !net_pend && (ws == 3 || ws == 4)) toast(ws == 3 ? nt("공유기를 찾지 못했습니다", "Network not found", "ネットワークが見つかりません") : nt("접속하지 못했습니다 (암호를 확인하세요)", "Could not connect (check the password)", "接続できません (パスワードを確認)"), 0);
              if (last_ws >= 0 && ws == 2) toast(nt("무선 연결됨", "Wi-Fi connected", "無線接続済み"), 0);
              last_ws = ws; menu_dirty = 1;
          }
      } else last_ws = -1;
    }
    if (!net_sv_n && net_ssid[0]) net_sv_add(net_ssid, net_pass);   /* 예전 설정 파일의 한 개를 목록으로 */
    if (net_scanning && net_ap_mode == 0) {                      /* 찾는 동안 목록이 채워진다 (센 것부터) */
        char ss[64][36]; int sg[64], sc[64];
        int n = uefi_wifi_scan_get(ss, sg, sc, 64), busy = uefi_wifi_scanning(), ch = (busy != net_ap_busy);
        if (n < 0) n = 0;
        char keep[36]; copy_str(keep, sizeof keep, net_ap_n > 0 ? net_ap_ssid[net_ap_sel] : "");
        if (n != net_ap_n) ch = 1;
        for (int i = 0; i < n && !ch; i++) if (strcmp(ss[i], net_ap_ssid[i]) || sg[i] / 5 != net_ap_sig[i] / 5 || sc[i] != net_ap_sec[i]) ch = 1;
        if (ch) {
            net_ap_n = n;
            for (int i = 0; i < n; i++) { copy_str(net_ap_ssid[i], sizeof net_ap_ssid[i], ss[i]); net_ap_sig[i] = sg[i]; net_ap_sec[i] = sc[i]; }
            net_ap_sel = 0; if (keep[0]) for (int i = 0; i < n; i++) if (!strcmp(keep, ss[i])) net_ap_sel = i;
            net_ap_busy = busy; full_redraw(); menu_dirty = 1;
        }
    }
    {   /* 다른 아이콘과 같은 규칙 — 연결됐고 쓰는 중(주소를 받거나 명령을 실행)이면 3초, 메뉴·창이 열렸거나 멈췄을 때는 계속, F6·ROM 게임 중엔 숨김 */
        static uint32_t act, last_cnt; static int was;
        uint32_t now = P.ms();
        int on = ms_st[MS_NET] == SS_ON && (net_wired_on || net_wifi_on) && uefi_net_online();   /* 무선·유선을 모두 끄면 주소가 남아 있어도 아이콘을 숨김 */   /* 카드가 꽂혀 있나는 장치 선언 (src/status.c), 연결됐나만 호스트 */
        if (on && !was) act = now | 1;
        was = on;
        if (netcard_act != last_cnt) { last_cnt = netcard_act; if (on) act = now | 1; }
        uint8_t v = (uint8_t)(on && badge_vis() && ((act && now - act < 3000) || status_show_all()));
        if (v != net_vis_now) { net_vis_now = v; margin_bd = -1; menu_dirty = 1; }
    }
}

static void draw_net(void)
{
    UiRect o;
    char st[96];
    net_status_text(st, sizeof st);
    UiRect b = ui_full_card(net_dmode ? nt("무선 네트워크", "Wireless network", "無線ネットワーク") : nt("유선 네트워크", "Wired network", "有線ネットワーク"), 0, 0, &o);
    hit_add(hc_none, 0, 0, 0, 0, o.x, o.y, o.w, o.h);
    uint32_t cy = dlg_cmd_y(), rh = ui_row_h(), x = b.x + 12 * S, w = b.w > 24 * S ? b.w - 24 * S : b.w, vx = b.x + b.w * 42 / 100, y = b.y + 8 * S;
    if (net_scanning && net_ap_mode == 3) {              /* 저장한 공유기 목록 (Del 로 지움) */
        text_clip(x + 6 * S, y, nt("저장한 무선 네트워크", "Saved wireless networks", "保存したネットワーク"), C_MUTED, 0xFF, w);
        y += LH + 8 * S;
        if (net_sv_n <= 0) text_clip(x + 18 * S, y + 6 * S, nt("저장한 네트워크가 없습니다", "No saved networks", "保存したネットワークなし"), C_FAINT, 0xFF, w);
        for (int i = 0; i < net_sv_n; i++, y += rh) {
            ui_list_row_bg(x, y, w, rh, i == net_sv_sel, i & 1, 1);
            char t[48]; copy_str(t, sizeof t, net_sv_ssid[i]); if (!strcmp(net_sv_ssid[i], net_ssid)) str_append(t, sizeof t, "  *");
            text_clip(x + 16 * S, ui_ty(y, rh), t, C_TEXT, 0xFF, w * 90 / 100);
        }
        ui_cmdbar(o.x + S, cy, o.w - 2 * S, nt("Del 지우기   ↑↓ 고르기   Esc 돌아가기", "Del remove   Up/Down select   Esc back", "Del 削除   ↑↓ 選択   Esc 戻る"));
        return;
    }
    if (net_scanning) {                                  /* 무선 네트워크 목록 */
        text_clip(x + 6 * S, y, net_ap_busy ? nt("무선 네트워크 - 찾는 중... (목록이 채워집니다)", "Wireless networks - searching... (list fills in)", "無線ネットワーク - 検索中...") : nt("무선 네트워크 (신호 세기)", "Wireless networks (signal)", "無線ネットワーク (電波強度)"), C_MUTED, 0xFF, w);
        y += LH + 8 * S;
        if (net_ap_n <= 0) text_clip(x + 18 * S, y + 6 * S, st[0] && !net_has_wifi() ? nt("무선 어댑터 없음", "No wireless adapter", "無線アダプターなし") : net_ap_busy ? nt("찾는 중...", "Searching...", "検索中...") : nt("찾은 네트워크가 없습니다 (F5 = 다시 찾기)", "No networks found (F5 = rescan)", "ネットワークなし (F5 = 再検索)"), C_FAINT, 0xFF, w);
        { int vis = (int)((cy > y + 2 * rh ? cy - y : rh) / rh) - 1; if (vis < 3) vis = 3;      /* 화면에 들어가는 줄 수 (암호 줄 한 칸 남김) */
          if (net_ap_sel < net_ap_top) net_ap_top = net_ap_sel; if (net_ap_sel >= net_ap_top + vis) net_ap_top = net_ap_sel - vis + 1; if (net_ap_top < 0) net_ap_top = 0;
          net_ap_vis = vis; }
        for (int i = net_ap_top; i < net_ap_n && i < net_ap_top + net_ap_vis; i++, y += rh) {
            ui_list_row_bg(x, y, w, rh, i == net_ap_sel, i & 1, 1);
            char t[48]; copy_str(t, sizeof t, net_ap_ssid[i]); if (net_sv_find(net_ap_ssid[i])) str_append(t, sizeof t, "  (saved)");
            text_clip(x + 16 * S, ui_ty(y, rh), t, C_TEXT, 0xFF, w * 55 / 100);
            char q[24] = ""; num_append(q, sizeof q, (uint32_t)net_ap_sig[i], 0, ' '); str_append(q, sizeof q, "%");
            if (net_ap_sec[i] == 2) str_append(q, sizeof q, "  \xF0\x9F\x94\x92"); else str_append(q, sizeof q, net_ap_sec[i] ? "  WPA/WPA3 (x)" : "  open (x)");
            text_clip(x + w * 62 / 100, ui_ty(y, rh), q, C_ACC, 0xFF, w * 35 / 100);
            if (i == net_ap_sel && net_ap_mode) {          /* 고른 줄 바로 밑: 암호 입력 / 접속 중 */
                y += rh;
                char vb[96] = "";
                if (net_ap_mode == 1) { str_append(vb, sizeof vb, nt("암호: ", "Password: ", "パスワード: ")); int l = (int)strlen(vb); for (int k = 0; k < net_elen && k < 60; k++) vb[l + k] = '*'; vb[l + (net_elen < 60 ? net_elen : 60)] = 0; str_append(vb, sizeof vb, "_"); }
                else copy_str(vb, sizeof vb, nt("연결하는 중...", "Connecting...", "接続中..."));
                fe_fill(x + 12 * S, y, w - 12 * S, rh, C_LINE);
                text_clip(x + 28 * S, ui_ty(y, rh), vb, C_OK, 0xFF, w - 40 * S);
            }
        }
        ui_cmdbar(o.x + S, cy, o.w - 2 * S, net_ap_mode == 1 ? nt("Enter 연결   Esc 취소   Backspace 지우기", "Enter connect   Esc cancel   Backspace delete", "Enter 接続   Esc 取消   Backspace 削除")
            : net_ap_mode == 2 ? nt("연결하는 중...   Esc 취소", "Connecting...   Esc cancel", "接続中...   Esc 取消")
            : nt("Enter 고르기   Del 저장한 목록·지우기   F5 다시 찾기   Esc 돌아가기", "Enter select   Del saved list   F5 rescan   Esc back", "Enter 選択   Del 保存一覧   F5 再検索   Esc 戻る"));
        return;
    }
    int rows[10], n = net_rows(rows);
    char got[4][20]; int have = 0;                       /* 자동(DHCP)이면 받아 온 주소를 칸에 보여 준다 */
    if (!net_manual && (net_wired_on || net_wifi_on) && net_cart_slot() >= 0 && uefi_net_online()) { uint8_t ii[20]; if (uefi_net_ipinfo(ii) == 0) { have = 1;
        for (int k = 0; k < 4; k++) { got[k][0] = 0; for (int i = 0; i < 4; i++) { char q[8]; int_to_str(q, sizeof q, ii[k * 4 + i]); str_append(got[k], 20, q); if (i < 3) str_append(got[k], 20, "."); }
                                      if (!(ii[k*4] | ii[k*4+1] | ii[k*4+2] | ii[k*4+3])) copy_str(got[k], 20, "-"); } } }
    for (int i = 0; i < n; i++, y += rh) {
        int r = rows[i], foc = i == net_sel, en = net_row_enabled(r);
        const char *lab = "", *val = "";
        char vb[80] = "";
        int kind = UV_CHOICE;
        switch (r) {
        case 0: lab = nt("유선 (이더넷)", "Wired (Ethernet)", "有線 (イーサネット)"); kind = UV_TOGGLE; break;
        case 1: lab = nt("무선 (Wi-Fi)", "Wireless (Wi-Fi)", "無線 (Wi-Fi)"); kind = UV_TOGGLE; break;
        case 2: lab = nt("연결 방식", "Connection", "接続方式"); val = net_wifi ? nt("무선 (Wi-Fi)", "Wireless (Wi-Fi)", "無線 (Wi-Fi)") : nt("유선 (이더넷)", "Wired (Ethernet)", "有線 (イーサネット)"); break;
        case 3: lab = nt("무선 네트워크", "Wireless network", "無線ネットワーク"); val = net_pend ? net_pssid : net_ssid[0] ? net_ssid : nt("(Enter: 찾기)", "(Enter: scan)", "(Enter: 検索)"); kind = UV_ACTION; break;
        case 4: lab = nt("암호", "Password", "パスワード"); { const char *pp = net_pend ? net_ppass : net_pass; for (int k = 0; pp[k] && k < 40; k++) vb[k] = '*'; } val = vb; kind = UV_TEXT; break;
        case 5: lab = nt("주소 설정", "Address", "アドレス設定"); val = net_manual ? nt("수동", "Manual", "手動") : nt("자동 (DHCP)", "Automatic (DHCP)", "自動 (DHCP)"); break;
        case 6: lab = "IP"; val = net_ip; kind = UV_TEXT; break;
        case 7: lab = nt("서브넷 마스크", "Subnet mask", "サブネットマスク"); val = net_mask; kind = UV_TEXT; break;
        case 8: lab = nt("게이트웨이", "Gateway", "ゲートウェイ"); val = net_gw; kind = UV_TEXT; break;
        default: lab = "DNS"; val = net_dns; kind = UV_TEXT; break;
        }
        if (have && r >= 6) val = got[r - 6];
        if (net_edit == r) { copy_str(vb, sizeof vb, ""); for (int k = 0; k < net_elen && k < 70; k++) vb[k] = r == 4 ? '*' : net_ebuf[k]; vb[net_elen < 70 ? net_elen : 70] = 0; str_append(vb, sizeof vb, "_"); val = vb; kind = UV_TEXT; }
        ui_row(x, y, w, vx, lab, val, 0, kind, r == 0 ? net_wired_on : r == 1 ? net_wifi_on : 0, foc, en);
    }
    y += 6 * S;
    fe_fill(x, y, w, S, C_LINE); y += 10 * S;
    text_clip(x + 18 * S, y, nt("상태", "Status", "状態"), C_MUTED, 0xFF, vx - x - 26 * S);
    text_clip(vx, y, st, net_vis_now ? C_OK : C_TEXT, 0xFF, b.x + b.w - vx - 14 * S);
    { char dg[96]; uefi_net_diag(dg, sizeof dg); y += rh;                          /* 진단 줄: 실기에서 왜 안 잡히는지 볼 수 있게 */
      text_clip(x + 18 * S, y, nt("진단", "Diagnostics", "診断"), C_FAINT, 0xFF, vx - x - 26 * S);
      text_clip(vx, y, dg, C_FAINT, 0xFF, b.x + b.w - vx - 14 * S); }
    ui_cmdbar(o.x + S, cy, o.w - 2 * S, net_edit >= 0 ? nt("Enter 저장   Esc 취소   Backspace 지우기", "Enter save   Esc cancel   Backspace delete", "Enter 保存   Esc 取消   Backspace 削除")
                                                      : nt("↑↓ 고르기   ←→ 바꾸기   Enter 고치기   Esc 닫기(저장)", "Up/Down select   Left/Right change   Enter edit   Esc close (saved)", "↑↓ 選択   ←→ 変更   Enter 編集   Esc 閉じる(保存)"));
}

static void net_scan_now(void)
{
    net_ap_n = 0; net_ap_sel = 0; net_ap_top = 0; net_ap_busy = 1;
    if (uefi_wifi_scan_begin() < 0) net_ap_busy = 0;
}

static void netdlg_key(int key)
{
    int rows[10], n = net_rows(rows);
    if (net_sel >= n) net_sel = n - 1;
    if (!net_win_on()) net_sel = 0;                      /* 꺼져 있으면 포인터는 켜기·끄기 줄에만 */
    if (!net_win_on() && (key == 0xC8 || key == 0xD0)) return;
    if (key & 0x80 && key != 0x9C && key != 0xC8 && key != 0xD0 && key != 0xCB && key != 0xCD) { if (key >= 0xE0) return; }
    int r = rows[net_sel];
    if (net_scanning) {
        if (net_ap_mode == 3) {                                                  /* 저장한 목록: Del 로 지움 */
            if (key == 0x01) net_ap_mode = 0;
            else if (key == 0xC8 && net_sv_sel > 0) net_sv_sel--;
            else if (key == 0xD0 && net_sv_sel + 1 < net_sv_n) net_sv_sel++;
            else if (key == 0xD3 && net_sv_n > 0) {
                int was_cur = !strcmp(net_sv_ssid[net_sv_sel], net_ssid);
                for (int i = net_sv_sel; i + 1 < net_sv_n; i++) { copy_str(net_sv_ssid[i], 36, net_sv_ssid[i + 1]); copy_str(net_sv_pass[i], 64, net_sv_pass[i + 1]); }
                net_sv_n--; if (net_sv_sel >= net_sv_n && net_sv_sel > 0) net_sv_sel--;
                if (was_cur) {                                                   /* 지금 쓰는 것을 지우면 연결을 끊고 (다음 저장분이 있으면 그걸로) */
                    copy_str(net_ssid, sizeof net_ssid, net_sv_n ? net_sv_ssid[0] : ""); copy_str(net_pass, sizeof net_pass, net_sv_n ? net_sv_pass[0] : "");
                    uefi_wifi_connect(net_ssid, net_pass);
                }
                settings_changed();
                toast(nt("저장한 네트워크를 지웠습니다", "Saved network removed", "保存したネットワークを削除しました"), 0);
            }
        } else if (net_ap_mode == 1) {                                           /* 고른 줄 밑에서 암호 입력 */
            if (key == 0x01) { net_ap_mode = 0; net_pend = 0; }
            else if (key == 0x1C || key == 0x9C) {
                net_ebuf[net_elen] = 0; copy_str(net_ppass, sizeof net_ppass, net_ebuf); net_pend = 2; net_ap_mode = 2; net_sv_add(net_pssid, net_ppass); settings_changed();   /* 암호를 치면 접속 결과와 상관없이 목록에 저장 */
                net_wifi_push(); toast(nt("연결하는 중...", "Connecting...", "接続中..."), 0);
            } else if (key == 0x0E) { if (net_elen > 0) net_elen--; }
            else { char c = key_char(key, (mods & 0x03) != 0); if (c && net_elen < 63) net_ebuf[net_elen++] = c; }
            net_ebuf[net_elen] = 0;
        } else if (net_ap_mode == 2) {
            if (key == 0x01) { net_pend = 0; net_ap_mode = 0; net_wifi_push(); }   /* 접속 취소 → 이전 공유기로 */
        } else {
            if (key == 0x01) { uefi_wifi_scan_stop(); net_scanning = 0; }
            else if (key == 0xC8 && net_ap_sel > 0) net_ap_sel--;
            else if (key == 0xD0 && net_ap_sel + 1 < net_ap_n) net_ap_sel++;
            else if (key == 0x3F) net_scan_now();                                    /* F5 */
            else if (key == 0xD3) { net_ap_mode = 3; net_sv_sel = 0; }               /* Del = 저장한 목록 */
            else if ((key == 0x1C || key == 0x9C) && net_ap_n > 0) {
                if (net_ap_sec[net_ap_sel] != 2) { toast(nt("이 공유기는 지원하지 않는 방식입니다 (WPA2 만 연결 가능)", "Not supported (WPA2 only)", "未対応方式です (WPA2 のみ)"), 0); full_redraw(); menu_dirty = 1; return; }
                const char *sp = net_sv_find(net_ap_ssid[net_ap_sel]);
                copy_str(net_pssid, sizeof net_pssid, net_ap_ssid[net_ap_sel]);
                uefi_wifi_scan_stop();
                if (sp) { copy_str(net_ppass, sizeof net_ppass, sp); net_pend = 2; net_ap_mode = 2; net_wifi_push(); }   /* 저장한 공유기는 저장한 암호로 */
                else { net_ppass[0] = 0; net_pend = 1; net_ap_mode = 1; net_elen = 0; net_ebuf[0] = 0; }                 /* 처음이면 바로 밑에 암호 입력 */
            }
        }
        full_redraw(); menu_dirty = 1; return;
    }
    if (net_edit >= 0) {
        if (key == 0x01) { net_edit = -1; if (net_pend == 1) net_pend = 0; }
        else if (key == 0x1C || key == 0x9C) {
            net_ebuf[net_elen] = 0;
            uint8_t t[4];
            if (net_edit >= 6 && net_elen && !net_parse_ip(net_ebuf, t)) { toast(nt("주소 형식이 맞지 않습니다 (예: 192.168.0.10)", "Bad address (e.g. 192.168.0.10)", "アドレス形式が違います (例: 192.168.0.10)"), 0); full_redraw(); return; }
            copy_str(net_field(net_edit), net_edit == 3 ? 36 : net_edit == 4 ? 64 : 16, net_ebuf);
            { int was = net_edit; net_edit = -1; settings_changed(); net_apply();
              if (was == 3 || was == 4) {
                  if (net_pend == 1 && was == 4) { net_pend = 2; net_sv_add(net_pssid, net_ppass); settings_changed(); }                      /* 고른 공유기 + 이 암호로 접속 시도 */
                  else if (!net_pend) { copy_str(net_pssid, sizeof net_pssid, net_ssid); copy_str(net_ppass, sizeof net_ppass, net_pass); net_pend = 2; }   /* 저장된 값을 손으로 고친 경우도 성공해야 저장 */
                  if (net_pend == 2) { net_wifi_push(); toast(nt("연결하는 중...", "Connecting...", "接続中..."), 0); }
              } }
        } else if (key == 0x0E) { if (net_elen > 0) net_elen--; }
        else {
            char c = key_char(key, (mods & 0x03) != 0);
            if (c && net_elen < net_field_max(net_edit) && (net_edit < 6 || (c >= '0' && c <= '9') || c == '.')) net_ebuf[net_elen++] = c;
        }
        net_ebuf[net_elen] = 0;
        full_redraw(); menu_dirty = 1; return;
    }
    switch (key) {
    case 0x01: net_apply(); settings_changed(); dialog_back(); return;
    case 0xC8: if (net_sel > 0) net_sel--; break;
    case 0xD0: if (net_sel + 1 < n) net_sel++; break;
    case 0xCB: case 0xCD: case 0x39: case 0x1C: case 0x9C:
        if (r == 0) { net_wired_on = !net_wired_on; if (net_wired_on) { net_wifi = 0; net_wifi_on = 0; } settings_changed(); if (net_wired_on) { net_cart_ensure(); net_renew_call(); } }   /* 한 번에 하나만 (유선을 켜면 무선이 꺼짐) */
        else if (r == 1) { net_wifi_on = !net_wifi_on; net_mem_wifi = net_wifi_on; if (net_wifi_on) { net_wifi = 1; net_wired_on = 0; net_cart_ensure(); } settings_changed(); }
        else if (r == 2) { net_wifi = !net_wifi; settings_changed(); if (!net_wifi && rows[net_sel] >= 3 && rows[net_sel] <= 4) for (int q = 0; q < n; q++) if (rows[q] == 2) net_sel = q; }
        else if (r == 5) {
            if (!net_manual) {                                   /* 자동 → 수동: 지금 받아 온 값을 그대로 채워 둔다 (5.0.109) */
                uint8_t ii[20];
                if (uefi_net_online() && uefi_net_ipinfo(ii) == 0) {
                    char *f[4] = { net_ip, net_mask, net_gw, net_dns };
                    for (int k = 0; k < 4; k++) { f[k][0] = 0; if (ii[k*4] | ii[k*4+1] | ii[k*4+2] | ii[k*4+3])
                        for (int i = 0; i < 4; i++) { char q[8]; int_to_str(q, sizeof q, ii[k * 4 + i]); str_append(f[k], 16, q); if (i < 3) str_append(f[k], 16, "."); } }
                }
            }
            net_manual = !net_manual; settings_changed(); net_apply(); }
        else if (r == 3 && net_row_enabled(r) && (key == 0x1C || key == 0x9C)) { net_scanning = 1; net_ap_mode = 0; net_scan_now(); }
        else if (r >= 3 && net_row_enabled(r) && (key == 0x1C || key == 0x9C)) { net_edit = r; copy_str(net_ebuf, sizeof net_ebuf, net_field(r)); net_elen = (int)strlen(net_ebuf); }
        break;
    case 0x3F: if (r == 3 && net_wifi_on) { net_scanning = 1; net_ap_mode = 0; net_scan_now(); } break;
    default: break;
    }
    full_redraw(); menu_dirty = 1;
}

/* 설정 파일 */
static void net_save_keys(char *b)
{
    if (global_save) { net_sv_wired = net_wired_on; net_sv_wifi = net_wifi_on; }          /* OFF 인 동안 켜고 끈 값은 저장하지 않음 (ON 으로 돌아오면 마지막 ON 때의 값) */
    ini_num(b, "NetWifiMem", net_mem_wifi); ini_num(b, "NetLeaseW", (int)net_lease[0]); ini_num(b, "NetLeaseF", (int)net_lease[1]); ini_put(b, "NetLeaseSSID", net_lease_ssid);
    ini_num(b, "NetWifi", net_wifi); ini_num(b, "NetWiredOn", net_sv_wired); ini_num(b, "NetWifiOn", net_sv_wifi); ini_num(b, "NetManual", net_manual);
    ini_put(b, "NetIP", net_ip); ini_put(b, "NetMask", net_mask); ini_put(b, "NetGW", net_gw); ini_put(b, "NetDNS", net_dns);
    ini_put(b, "NetSSID", net_ssid); ini_put(b, "NetPass", net_pass);
    for (int i = 0; i < net_sv_n; i++) { char k[12] = "NetSv0S"; k[5] = (char)('0' + i); ini_put(b, k, net_sv_ssid[i]); k[6] = 'P'; ini_put(b, k, net_sv_pass[i]); }
}
static int net_load_key(const char *k, const char *v)
{
    if (!strcmp(k, "NetWifi")) net_wifi = ini_long(v) != 0;
    else if (!strcmp(k, "NetWifiMem")) net_mem_wifi = ini_long(v) != 0;
    else if (!strcmp(k, "NetLeaseW")) net_lease[0] = (uint32_t)ini_long(v);
    else if (!strcmp(k, "NetLeaseF")) net_lease[1] = (uint32_t)ini_long(v);
    else if (!strcmp(k, "NetLeaseSSID")) copy_str(net_lease_ssid, sizeof net_lease_ssid, v);
    else if (!strcmp(k, "NetWiredOn")) net_wired_on = net_sv_wired = ini_long(v) != 0;
    else if (!strcmp(k, "NetWifiOn")) net_wifi_on = net_sv_wifi = ini_long(v) != 0;
    else if (!strcmp(k, "NetManual")) net_manual = ini_long(v) != 0;
    else if (!strcmp(k, "NetIP")) copy_str(net_ip, sizeof net_ip, v);
    else if (!strcmp(k, "NetMask")) copy_str(net_mask, sizeof net_mask, v);
    else if (!strcmp(k, "NetGW")) copy_str(net_gw, sizeof net_gw, v);
    else if (!strcmp(k, "NetDNS")) copy_str(net_dns, sizeof net_dns, v);
    else if (!strcmp(k, "NetSSID")) copy_str(net_ssid, sizeof net_ssid, v);
    else if (!strcmp(k, "NetPass")) copy_str(net_pass, sizeof net_pass, v);
    else if (!strncmp(k, "NetSv", 5) && k[5] >= '0' && k[5] <= '7' && (k[6] == 'S' || k[6] == 'P') && !k[7]) {
        int i = k[5] - '0'; if (i >= net_sv_n) net_sv_n = i + 1;
        if (k[6] == 'S') copy_str(net_sv_ssid[i], 36, v); else copy_str(net_sv_pass[i], 64, v);
    }
    else return 0;
    return 1;
}

/* 네트워크 카트리지가 슬롯에 있나 (레거시 판 kmain이 랜카드·lwIP를 돌릴지 정한다. 없으면 네트워크 쪽 코드는 전혀 실행하지 않는다) */
int fe_net_cart_present(void) { return net_cart_slot() >= 0 && (net_wired_on || net_wifi_on); }   /* 카드가 있고 켜져 있을 때만 네트워크 소스가 돈다 */
static int net_cart_present(void) { return net_cart_slot() >= 0; }
