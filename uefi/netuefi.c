/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX 네트워크 카트리지 - UEFI 연결 구현: 2단계 (주소) + 3단계 (DNS4, TCP4 클라이언트) + 5단계 (UDP4, 망 끊김). netuefi.h 참고 */
#include "netuefi.h"
#include <string.h>

/* GUID: edk2 MdePkg/Include/Protocol 의 Ip4Config2.h, Tcp4.h, Dns4.h, Udp4.h 와 대조함. SNP 값만 규격 대조 없음. */
static const EFI_GUID gSnp     = { 0xA19832B9, 0xAC25, 0x11D3, { 0x9A, 0x2D, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D } };
static const EFI_GUID gIp4Cfg2 = { 0x5B446ED1, 0xE30B, 0x4FAA, { 0x87, 0x1A, 0x36, 0x54, 0xEC, 0xA3, 0x60, 0x80 } };
static const EFI_GUID gTcp4Sb  = { 0x00720665, 0x67EB, 0x4A99, { 0xBA, 0xF7, 0xD3, 0xC3, 0x3A, 0x1C, 0x7C, 0xC9 } };
static const EFI_GUID gTcp4    = { 0x65530BC7, 0xA359, 0x410F, { 0xB0, 0x10, 0x5A, 0xAD, 0xC7, 0xEC, 0x2B, 0x62 } };
static const EFI_GUID gDns4Sb  = { 0xB625B186, 0xE063, 0x44F7, { 0x89, 0x05, 0x6A, 0x74, 0xDC, 0x6F, 0x52, 0xB4 } };
static const EFI_GUID gUdp4Sb  = { 0x83F01464, 0x99BD, 0x45E5, { 0xB3, 0x83, 0xAF, 0x63, 0x05, 0xD8, 0xE9, 0xE6 } };
static const EFI_GUID gUdp4    = { 0x3AD9DF29, 0x4501, 0x478D, { 0xB1, 0xF8, 0x7F, 0x7F, 0xE7, 0x0E, 0x50, 0xF3 } };
static const EFI_GUID gDns4    = { 0xAE3D28CC, 0xE05B, 0x4FA1, { 0xA0, 0x11, 0x7E, 0xB5, 0x5A, 0x3F, 0x14, 0x01 } };

/* 상태 코드: EFIERR(n) = 최상위 비트 + n (edk2 UefiBaseType.h, Base.h) */
#define EFIERR_(n)            (0x8000000000000000ull | (n))
#define EFI_ALREADY_STARTED_  EFIERR_(20)
#define EFI_NOT_FOUND_        EFIERR_(14)
#define EFI_ACCESS_DENIED_    EFIERR_(15)
#define EFI_TIMEOUT_          EFIERR_(18)
#define EFI_ABORTED_          EFIERR_(21)
#define EFI_NET_UNREACH_      EFIERR_(100)
#define EFI_HOST_UNREACH_     EFIERR_(101)
#define EFI_PORT_UNREACH_     EFIERR_(103)
#define EFI_CONN_FIN_         EFIERR_(104)
#define EFI_CONN_RESET_       EFIERR_(105)
#define EFI_CONN_REFUSED_     EFIERR_(106)

#define LOCATE_BY_PROTOCOL    2
#define EVT_NOTIFY_SIGNAL     0x00000200u
#define TPL_CALLBACK_         8

void netuefi_init(NetUefi *nu, EFI_SYSTEM_TABLE *st)
{
    memset(nu, 0, sizeof *nu);
    nu->st = st;
    nu->stage = NU_STAGE_IDLE;
    nu->dns_state = NC_DNS_IDLE;
}

/* ====== 2단계: 주소 ====== */

/* IP4Config2를 가진 핸들을 찾아 첫 번째를 쓴다. */
static int find_cfg(NetUefi *nu)
{
    EFI_BOOT_SERVICES *bs = nu->st->BootServices;
    UINTN n = 0;
    EFI_HANDLE *h = NULL;
    EFI_STATUS s = bs->LocateHandleBuffer(LOCATE_BY_PROTOCOL, &gIp4Cfg2, NULL, &n, &h);
    if (EFI_ERROR(s) || !h) { nu->last_status = s; return 0; }
    int found = 0;
    for (UINTN i = 0; i < n && !found; i++) {
        void *p = NULL;
        if (!EFI_ERROR(bs->HandleProtocol(h[i], &gIp4Cfg2, &p)) && p) {
            nu->nic = h[i];
            nu->cfg = p;
            found = 1;
        }
    }
    bs->FreePool(h);
    return found;
}

/* 네트워크 카드(SNP)마다 드라이버를 이어 붙여 IP4 계층이 올라오게 한다. */
static void connect_all(NetUefi *nu)
{
    EFI_BOOT_SERVICES *bs = nu->st->BootServices;
    UINTN n = 0;
    EFI_HANDLE *h = NULL;
    EFI_STATUS s = bs->LocateHandleBuffer(LOCATE_BY_PROTOCOL, &gSnp, NULL, &n, &h);
    if (EFI_ERROR(s) || !h) { nu->last_status = s; return; }
    for (UINTN i = 0; i < n; i++) {
        nu->connect_tries++;
        EFI_STATUS c = bs->ConnectController(h[i], NULL, NULL, 1);
        if (EFI_ERROR(c)) nu->last_status = c;
    }
    bs->FreePool(h);
}

static void apply_config(NetUefi *nu);

static void try_open(NetUefi *nu)
{
    if (!find_cfg(nu)) {
        connect_all(nu);
        if (!find_cfg(nu)) { nu->next = nu->ticks + NU_RETRY_IDLE; return; }
    }
    apply_config(nu);                                    /* DHCP 또는 수동 */
    nu->stage = NU_STAGE_WAIT;
    nu->next = nu->ticks + 1;
}

static int nonzero4(const uint8_t *p) { return (p[0] | p[1] | p[2] | p[3]) != 0; }

/* 설정 창의 값을 IP4Config2에 넣는다. 자동 = DHCP (이미 DHCP이면 ALREADY_STARTED가 오며 정상). 수동 = 정적 + 주소·마스크, 게이트웨이, DNS */
static void apply_config(NetUefi *nu)
{
    nu->cfg_dirty = 0;
    if (!nu->cfg) return;
    if (!nu->cfg_manual || !nonzero4(nu->cfg_ip) || !nonzero4(nu->cfg_mask)) {
        uint32_t policy = NU_POLICY_DHCP;
        EFI_STATUS s = nu->cfg->SetData(nu->cfg, NU_CFG2_POLICY, sizeof policy, &policy);
        if (EFI_ERROR(s) && s != EFI_ALREADY_STARTED_) nu->last_status = s;
        return;
    }
    uint32_t policy = NU_POLICY_STATIC;
    EFI_STATUS s = nu->cfg->SetData(nu->cfg, NU_CFG2_POLICY, sizeof policy, &policy);
    if (EFI_ERROR(s) && s != EFI_ALREADY_STARTED_) nu->last_status = s;
    uint8_t ma[8];
    memcpy(ma, nu->cfg_ip, 4); memcpy(ma + 4, nu->cfg_mask, 4);
    s = nu->cfg->SetData(nu->cfg, NU_CFG2_MANUAL_ADDRESS, sizeof ma, ma);
    if (EFI_ERROR(s)) nu->last_status = s;
    if (nonzero4(nu->cfg_gw)) {
        uint8_t g[4]; memcpy(g, nu->cfg_gw, 4);
        s = nu->cfg->SetData(nu->cfg, NU_CFG2_GATEWAY, sizeof g, g);
        if (EFI_ERROR(s)) nu->last_status = s;
    }
    if (nonzero4(nu->cfg_dns)) {
        uint8_t d[4]; memcpy(d, nu->cfg_dns, 4);
        s = nu->cfg->SetData(nu->cfg, NU_CFG2_DNS_SERVER, sizeof d, d);
        if (EFI_ERROR(s)) nu->last_status = s;
    }
}

void netuefi_set_config(NetUefi *nu, int manual, const uint8_t ip[4], const uint8_t mask[4], const uint8_t gw[4], const uint8_t dns[4])
{
    nu->cfg_manual = manual;
    memcpy(nu->cfg_ip, ip, 4); memcpy(nu->cfg_mask, mask, 4); memcpy(nu->cfg_gw, gw, 4); memcpy(nu->cfg_dns, dns, 4);
    nu->cfg_dirty = 1;
}

int netuefi_online(const NetUefi *nu) { return nu->stage == NU_STAGE_UP; }
int netuefi_has_nic(const NetUefi *nu) { return nu->cfg != NULL; }

/* 주소가 있으면 info를 채우고 1, 없으면 info를 비우고 0 */
static int read_info(NetUefi *nu)
{
    UINTN size = sizeof nu->buf;
    EFI_STATUS s = nu->cfg->GetData(nu->cfg, NU_CFG2_INTERFACE_INFO, &size, nu->buf);
    if (EFI_ERROR(s) || size < sizeof(NU_IP4_CONFIG2_INTERFACE_INFO)) {
        if (EFI_ERROR(s)) nu->last_status = s;
        memset(nu->info, 0, sizeof nu->info);
        return 0;
    }
    const NU_IP4_CONFIG2_INTERFACE_INFO *ii = (const NU_IP4_CONFIG2_INTERFACE_INFO *)nu->buf;
    if (!nonzero4(ii->StationAddress)) { memset(nu->info, 0, sizeof nu->info); return 0; }

    uint8_t info[20];
    memset(info, 0, sizeof info);
    memcpy(info + 0, ii->StationAddress, 4);
    memcpy(info + 4, ii->SubnetMask, 4);

    uint8_t tmp[64];
    size = sizeof tmp;                                   /* 게이트웨이: IPv4 주소 배열, 첫 번째만 쓴다 */
    if (!EFI_ERROR(nu->cfg->GetData(nu->cfg, NU_CFG2_GATEWAY, &size, tmp)) && size >= 4) memcpy(info + 8, tmp, 4);
    size = sizeof tmp;                                   /* DNS: 최대 두 개 */
    if (!EFI_ERROR(nu->cfg->GetData(nu->cfg, NU_CFG2_DNS_SERVER, &size, tmp))) {
        if (size >= 4) memcpy(info + 12, tmp, 4);
        if (size >= 8) memcpy(info + 16, tmp + 4, 4);
    }
    memcpy(nu->info, info, sizeof info);
    return 1;
}

static void net_lost(NetUefi *nu);

static void stage_poll(NetUefi *nu)
{
    if (nu->cfg_dirty && nu->cfg) {                      /* 설정이 바뀜 — 바로 다시 적용하고 주소를 기다린다 */
        int was_up = nu->stage == NU_STAGE_UP;
        apply_config(nu);
        nu->stage = NU_STAGE_WAIT;
        nu->next = nu->ticks + 1;
        memset(nu->info, 0, sizeof nu->info);
        if (was_up) net_lost(nu);
        return;
    }
    if (nu->ticks < nu->next) return;
    switch (nu->stage) {
    case NU_STAGE_IDLE:
        try_open(nu);
        break;
    case NU_STAGE_WAIT:
        nu->next = nu->ticks + NU_POLL_SETUP;
        if (read_info(nu)) { nu->stage = NU_STAGE_UP; nu->next = nu->ticks + NU_POLL_UP; }
        break;
    case NU_STAGE_UP:
        nu->next = nu->ticks + NU_POLL_UP;
        {
            uint8_t old[4];
            memcpy(old, nu->info, 4);
            if (!read_info(nu)) { nu->stage = NU_STAGE_WAIT; nu->next = nu->ticks + NU_POLL_SETUP; net_lost(nu); }
            else if (memcmp(old, nu->info, 4) != 0) net_lost(nu);       /* 주소가 바뀜: 옛 주소의 연결은 못 쓴다 */
        }
        break;
    default:
        break;
    }
}

int netuefi_state(const NetUefi *nu)
{
    return nu->stage == NU_STAGE_UP ? NC_NET_UP : nu->stage == NU_STAGE_WAIT ? NC_NET_SETUP : NC_NET_DOWN;
}

int netuefi_get_ipinfo(const NetUefi *nu, uint8_t out[20])
{
    if (nu->stage != NU_STAGE_UP) return -1;
    memcpy(out, nu->info, 20);
    return 0;
}

/* ====== 이벤트 도우미 ====== */

static void on_signal(void *ev, void *ctx) { (void)ev; *(volatile int *)ctx = 1; }

static int mk_event(NetUefi *nu, volatile int *flag, void **ev)
{
    void *e = NULL;
    *flag = 0;
    EFI_STATUS s = nu->st->BootServices->CreateEvent(EVT_NOTIFY_SIGNAL, TPL_CALLBACK_, on_signal, (void *)flag, &e);
    if (EFI_ERROR(s) || !e) { nu->last_status = s; return 0; }
    *ev = e;
    return 1;
}

static void rm_event(NetUefi *nu, void **ev)
{
    if (*ev) {
        ((EFI_STATUS (*)(void *))nu->st->BootServices->CloseEvent)(*ev);
        *ev = NULL;
    }
}

/* ====== 3단계: TCP ====== */

static uint8_t map_reason(EFI_STATUS s)
{
    if (s == EFI_CONN_FIN_) return NC_REASON_NONE;
    if (s == EFI_CONN_RESET_ || s == EFI_CONN_REFUSED_) return NC_REASON_REFUSED;
    if (s == EFI_TIMEOUT_) return NC_REASON_TIMEOUT;
    if (s == EFI_NET_UNREACH_ || s == EFI_HOST_UNREACH_ || s == EFI_PORT_UNREACH_) return NC_REASON_UNREACH;
    return NC_REASON_OTHER;
}

static int get_tcp_sb(NetUefi *nu)
{
    void *p = NULL;
    if (nu->tcp_sb) return 1;
    EFI_STATUS s = nu->st->BootServices->HandleProtocol(nu->nic, &gTcp4Sb, &p);
    if (EFI_ERROR(s) || !p) { nu->last_status = s; return 0; }
    nu->tcp_sb = (NU_SERVICE_BINDING *)p;
    return 1;
}

/* 자식과 이벤트를 모두 돌려준다. 슬롯의 나머지 내용(대기열, 이유)은 그대로 둔다. */
static void conn_release(NetUefi *nu, NuConn *c)
{
    if (c->child && nu->tcp_sb) nu->tcp_sb->DestroyChild(nu->tcp_sb, c->child);
    c->child = NULL;
    c->tcp = NULL;
    rm_event(nu, &c->ev_conn);
    rm_event(nu, &c->ev_rx);
    rm_event(nu, &c->ev_tx);
    rm_event(nu, &c->ev_close);
}

/* 연결이 끝났다. MSX가 닫기를 요청했으면 슬롯을 비우고, 아니면 이유와 남은 자료를 두고 DEAD로 둔다. */
static void conn_die(NetUefi *nu, NuConn *c, int *ev)
{
    int wanted = c->close_req || c->abort_req;
    conn_release(nu, c);
    if (wanted) memset(c, 0, sizeof *c);
    else c->cs = NU_CS_DEAD;
    *ev |= NU_EV_STATE | NU_EV_RX;
}

static int open_fail(NetUefi *nu, NuConn *c, EFI_STATUS s)
{
    nu->last_status = s;
    conn_release(nu, c);
    memset(c, 0, sizeof *c);
    return s == EFI_ACCESS_DENIED_ ? NC_E_INUSE : NC_E_HOST;
}

/* 내 포트가 0이면 16384~32767 사이에서, 지금 열려 있는 연결이 쓰지 않는 포트를 고른다 (UNAPI의 "임의 포트"). */
static uint16_t pick_ephemeral(NetUefi *nu)
{
    for (int tries = 0; tries < 64; tries++) {
        if (nu->eph_next < 16384 || nu->eph_next > 32767)
            nu->eph_next = (uint16_t)(16384 + (nu->ticks * 2654435761u >> 8) % 16384);
        uint16_t p = nu->eph_next++;
        int used = 0;
        for (int i = 0; i < NETCARD_MAX_CONN; i++)
            if (nu->conn[i].cs != NU_CS_FREE && nu->conn[i].lport == p) used = 1;
        if (!used) return p;
    }
    return nu->eph_next;
}

static int tcp_open_cb(void *ctx, uint16_t lport, const uint8_t rip[4], uint16_t rport, uint8_t flags, uint8_t *handle)
{
    NetUefi *nu = (NetUefi *)ctx;
    if (nu->stage != NU_STAGE_UP) return NC_E_NETDOWN;
    if (flags & NC_OPEN_PASSIVE) return NC_E_BADARG;
    if (!get_tcp_sb(nu)) return NC_E_HOST;

    int i;
    for (i = 0; i < NETCARD_MAX_CONN; i++) if (nu->conn[i].cs == NU_CS_FREE) break;
    if (i == NETCARD_MAX_CONN) return NC_E_NOSLOT;

    NuConn *c = &nu->conn[i];
    EFI_STATUS s;
    void *p = NULL;
    memset(c, 0, sizeof *c);
    if (!mk_event(nu, &c->f_conn, &c->ev_conn) || !mk_event(nu, &c->f_rx, &c->ev_rx) ||
        !mk_event(nu, &c->f_tx, &c->ev_tx) || !mk_event(nu, &c->f_close, &c->ev_close))
        return open_fail(nu, c, nu->last_status);

    s = nu->tcp_sb->CreateChild(nu->tcp_sb, &c->child);
    if (EFI_ERROR(s)) { c->child = NULL; return open_fail(nu, c, s); }
    s = nu->st->BootServices->HandleProtocol(c->child, &gTcp4, &p);
    if (EFI_ERROR(s) || !p) return open_fail(nu, c, s);
    c->tcp = (NU_TCP4_PROTOCOL *)p;

    if (lport == 0) lport = pick_ephemeral(nu);
    memcpy(c->rip, rip, 4);
    c->rport = rport;
    c->lport = lport;

    NU_TCP4_CONFIG_DATA cd;
    memset(&cd, 0, sizeof cd);
    cd.TimeToLive = 64;
    cd.AccessPoint.UseDefaultAddress = 1;            /* DHCP로 받은 주소를 쓴다 */
    cd.AccessPoint.StationPort = lport;
    memcpy(cd.AccessPoint.RemoteAddress, rip, 4);
    cd.AccessPoint.RemotePort = rport;
    cd.AccessPoint.ActiveFlag = 1;
    s = c->tcp->Configure(c->tcp, &cd);
    if (EFI_ERROR(s)) return open_fail(nu, c, s);

    c->conn_tok.CompletionToken.Event = c->ev_conn;
    c->conn_tok.CompletionToken.Status = 0;
    s = c->tcp->Connect(c->tcp, &c->conn_tok);
    if (EFI_ERROR(s)) return open_fail(nu, c, s);
    c->conn_pending = 1;
    c->cs = NU_CS_CONNECTING;
    *handle = (uint8_t)i;
    return 0;
}

static int tcp_close_cb(void *ctx, int h, int abort)
{
    NetUefi *nu = (NetUefi *)ctx;
    NuConn *c = &nu->conn[h];
    if (c->cs == NU_CS_FREE) return 0;
    if (c->cs == NU_CS_DEAD) { memset(c, 0, sizeof *c); return 0; }
    c->close_req = 1;
    if (abort) c->abort_req = 1;
    return 0;
}

static int tcp_state_cb(void *ctx, int h, uint8_t *state, uint16_t *rx_avail, uint16_t *tx_free, uint8_t *reason)
{
    NetUefi *nu = (NetUefi *)ctx;
    const NuConn *c = &nu->conn[h];
    if (c->cs == NU_CS_FREE) return NC_E_NOHANDLE;
    switch (c->cs) {
    case NU_CS_CONNECTING: *state = NC_TCP_SYN_SENT; break;
    case NU_CS_OPEN:       *state = c->peer_fin ? NC_TCP_CLOSE_WAIT : NC_TCP_ESTABLISHED; break;
    case NU_CS_CLOSING:    *state = NC_TCP_FIN_WAIT1; break;
    default:               *state = NC_TCP_CLOSED; break;
    }
    *rx_avail = (uint16_t)c->q_count;
    *tx_free = (c->cs == NU_CS_OPEN && !c->close_req && !c->abort_req && !c->fail_req && !c->tx_pending) ? NU_TXBUF : 0;
    *reason = c->reason;
    return 0;
}

static int tcp_send_cb(void *ctx, int h, const uint8_t *data, int len, int push, int *accepted)
{
    NetUefi *nu = (NetUefi *)ctx;
    NuConn *c = &nu->conn[h];
    *accepted = 0;
    if (c->cs != NU_CS_OPEN || c->close_req || c->abort_req || c->fail_req) return NC_E_NOHANDLE;
    if (c->tx_pending) return 0;                     /* 앞선 전송이 안 끝났다. 0바이트 받음 */
    int n = len < NU_TXBUF ? len : NU_TXBUF;
    memcpy(c->txbuf, data, (size_t)n);
    c->txd.Push = push ? 1 : 0;
    c->txd.Urgent = 0;
    c->txd.DataLength = (uint32_t)n;
    c->txd.FragmentCount = 1;
    c->txd.FragmentTable[0].FragmentLength = (uint32_t)n;
    c->txd.FragmentTable[0].FragmentBuffer = c->txbuf;
    c->tx_tok.CompletionToken.Event = c->ev_tx;
    c->tx_tok.CompletionToken.Status = 0;
    c->tx_tok.Packet = &c->txd;
    c->f_tx = 0;
    EFI_STATUS s = c->tcp->Transmit(c->tcp, &c->tx_tok);
    if (EFI_ERROR(s)) {
        nu->last_status = s;
        c->reason = map_reason(s);
        c->fail_req = 1;                               /* 연결이 깨진 것으로 보고 정리한다 */
        return NC_E_HOST;
    }
    c->tx_pending = 1;
    *accepted = n;
    return 0;
}

static void fifo_put(NuConn *c, const uint8_t *src, int n)
{
    int space = NU_FIFO - c->q_count;
    if (n > space) n = space;
    for (int i = 0; i < n; i++) c->fifo[(c->q_head + c->q_count + i) % NU_FIFO] = src[i];
    c->q_count += n;
}

static int tcp_recv_cb(void *ctx, int h, uint8_t *dst, int maxlen, int *got)
{
    NetUefi *nu = (NetUefi *)ctx;
    NuConn *c = &nu->conn[h];
    if (c->cs == NU_CS_FREE) return NC_E_NOHANDLE;
    int n = c->q_count < maxlen ? c->q_count : maxlen;
    for (int i = 0; i < n; i++) dst[i] = c->fifo[(c->q_head + i) % NU_FIFO];
    c->q_head = (c->q_head + n) % NU_FIFO;
    c->q_count -= n;
    *got = n;
    return 0;
}

static int tcp_flush_cb(void *ctx, int h)
{
    NetUefi *nu = (NetUefi *)ctx;
    return nu->conn[h].cs == NU_CS_FREE ? NC_E_NOHANDLE : 0;   /* 전송은 SEND 때 바로 시작하므로 더 할 일이 없다 */
}

static int tcp_info_cb(void *ctx, int h, uint8_t out[8])
{
    NetUefi *nu = (NetUefi *)ctx;
    const NuConn *c = &nu->conn[h];
    if (c->cs == NU_CS_FREE) return NC_E_NOHANDLE;
    memcpy(out, c->rip, 4);
    out[4] = (uint8_t)c->rport; out[5] = (uint8_t)(c->rport >> 8);
    out[6] = (uint8_t)c->lport; out[7] = (uint8_t)(c->lport >> 8);
    return 0;
}

static void post_rx(NetUefi *nu, NuConn *c)
{
    if (c->cs != NU_CS_OPEN || c->rx_pending || c->peer_fin || c->close_req || c->abort_req || c->fail_req) return;
    if (NU_FIFO - c->q_count < NU_RXRAW) return;      /* 대기열이 차 있으면 걸지 않는다 (MSX가 읽을 때까지 기다림) */
    c->rxd.UrgentFlag = 0;
    c->rxd.DataLength = NU_RXRAW;
    c->rxd.FragmentCount = 1;
    c->rxd.FragmentTable[0].FragmentLength = NU_RXRAW;
    c->rxd.FragmentTable[0].FragmentBuffer = c->rxraw;
    c->rx_tok.CompletionToken.Event = c->ev_rx;
    c->rx_tok.CompletionToken.Status = 0;
    c->rx_tok.Packet = &c->rxd;
    c->f_rx = 0;
    EFI_STATUS s = c->tcp->Receive(c->tcp, &c->rx_tok);
    if (!EFI_ERROR(s)) { c->rx_pending = 1; return; }
    if (s == EFI_CONN_FIN_) { c->peer_fin = 1; return; }       /* 상대가 이미 닫았고 남은 자료도 없음 */
    nu->last_status = s;
    c->reason = map_reason(s);
    c->fail_req = 1;
}

/* 닫기 절차: 걸려 있는 토큰을 취소 → 다 끝나면 Close → 완료되면 정리. 오래 걸리면 강제로 넘어간다. */
static void conn_close_step(NetUefi *nu, NuConn *c, int *ev)
{
    uint64_t now = nu->ticks;
    if (!c->close_since) c->close_since = now;

    if (c->cs == NU_CS_CONNECTING) {
        if (!c->cancel_sent) { c->cancel_sent = 1; c->tcp->Cancel(c->tcp, &c->conn_tok.CompletionToken); }
        if (!c->conn_pending || now - c->close_since > NU_CLOSE_TMO) conn_die(nu, c, ev);
        return;
    }
    if (c->cs == NU_CS_OPEN) c->cs = NU_CS_CLOSING;
    if (c->cs != NU_CS_CLOSING) return;

    if (!c->close_issued) {
        int waited_long = (now - c->close_since) > NU_CLOSE_TMO;
        if (!c->cancel_sent) {
            c->cancel_sent = 1;
            if (c->abort_req || c->fail_req) c->tcp->Cancel(c->tcp, NULL);
            else if (c->rx_pending) c->tcp->Cancel(c->tcp, &c->rx_tok.CompletionToken);
        }
        if ((!c->rx_pending && !c->tx_pending) || waited_long) {
            c->close_tok.CompletionToken.Event = c->ev_close;
            c->close_tok.CompletionToken.Status = 0;
            c->close_tok.AbortOnClose = (c->abort_req || c->fail_req || waited_long) ? 1 : 0;
            c->f_close = 0;
            EFI_STATUS s = c->tcp->Close(c->tcp, &c->close_tok);
            if (EFI_ERROR(s)) { nu->last_status = s; conn_die(nu, c, ev); return; }
            c->close_issued = 1;
            c->close_since = now;
        }
    } else if (c->f_close || now - c->close_since > NU_CLOSED_TMO) {
        conn_die(nu, c, ev);
    }
}

static int conn_poll(NetUefi *nu, NuConn *c)
{
    int ev = 0;
    if (c->cs == NU_CS_FREE || c->cs == NU_CS_DEAD) return 0;
    if (c->tcp && c->tcp->Poll) c->tcp->Poll(c->tcp);

    if (c->f_conn) {
        c->f_conn = 0;
        c->conn_pending = 0;
        EFI_STATUS s = c->conn_tok.CompletionToken.Status;
        if (c->cs == NU_CS_CONNECTING) {
            if (!EFI_ERROR(s)) { c->cs = NU_CS_OPEN; ev |= NU_EV_STATE; }
            else if (c->close_req || c->abort_req) { conn_die(nu, c, &ev); return ev; }   /* 우리가 취소한 연결 */
            else { c->reason = map_reason(s); conn_die(nu, c, &ev); return ev; }
        }
    }
    if (c->f_rx) {
        c->f_rx = 0;
        c->rx_pending = 0;
        EFI_STATUS s = c->rx_tok.CompletionToken.Status;
        if (!EFI_ERROR(s)) {
            int n = (int)c->rxd.DataLength;
            if (n > NU_RXRAW) n = NU_RXRAW;
            fifo_put(c, c->rxraw, n);
            if (n) ev |= NU_EV_RX;
        } else if (s == EFI_CONN_FIN_) {
            c->peer_fin = 1;
            ev |= NU_EV_RX | NU_EV_STATE;
        } else if (!c->close_req && !c->abort_req && !c->fail_req) {
            c->reason = map_reason(s);
            c->fail_req = 1;
            ev |= NU_EV_RX | NU_EV_STATE;
        }
    }
    if (c->f_tx) {
        c->f_tx = 0;
        c->tx_pending = 0;
        EFI_STATUS s = c->tx_tok.CompletionToken.Status;
        if (EFI_ERROR(s) && !c->close_req && !c->abort_req && !c->fail_req) {
            c->reason = map_reason(s);
            c->fail_req = 1;
            ev |= NU_EV_STATE;
        }
    }
    if (c->close_req || c->abort_req || c->fail_req) conn_close_step(nu, c, &ev);
    else post_rx(nu, c);
    return ev;
}

/* ====== 3단계: DNS ====== */

static int dns_ensure(NetUefi *nu)
{
    EFI_BOOT_SERVICES *bs = nu->st->BootServices;
    EFI_STATUS s;
    void *p = NULL;

    if (!nu->dns) {
        s = bs->HandleProtocol(nu->nic, &gDns4Sb, &p);
        if (EFI_ERROR(s) || !p) { nu->last_status = s; return 0; }
        nu->dns_sb = (NU_SERVICE_BINDING *)p;
        s = nu->dns_sb->CreateChild(nu->dns_sb, &nu->dns_child);
        if (EFI_ERROR(s)) { nu->last_status = s; nu->dns_child = NULL; return 0; }
        p = NULL;
        s = bs->HandleProtocol(nu->dns_child, &gDns4, &p);
        if (EFI_ERROR(s) || !p) { nu->last_status = s; return 0; }
        nu->dns = (NU_DNS4_PROTOCOL *)p;
        if (!nu->ev_dns && !mk_event(nu, &nu->f_dns, &nu->ev_dns)) return 0;
    }
    if (!nu->dns_configured || memcmp(nu->dns_sig, nu->info, sizeof nu->dns_sig) != 0) {
        int count = 0;
        memset(nu->dns_servers, 0, sizeof nu->dns_servers);
        if (nonzero4(nu->info + 12)) { memcpy(nu->dns_servers + 4 * count, nu->info + 12, 4); count++; }
        if (nonzero4(nu->info + 16)) { memcpy(nu->dns_servers + 4 * count, nu->info + 16, 4); count++; }
        if (count == 0) return 0;                     /* DHCP가 DNS 서버를 주지 않았다 */
        if (nu->dns_configured) { nu->dns->Configure(nu->dns, NULL); nu->dns_configured = 0; }
        NU_DNS4_CONFIG_DATA cd;
        memset(&cd, 0, sizeof cd);
        cd.DnsServerListCount = (UINTN)count;
        cd.DnsServerList = nu->dns_servers;
        cd.UseDefaultSetting = 0;
        cd.EnableDnsCache = 1;
        cd.Protocol = 17;                             /* UDP */
        memcpy(cd.StationIp, nu->info + 0, 4);
        memcpy(cd.SubnetMask, nu->info + 4, 4);
        s = nu->dns->Configure(nu->dns, &cd);
        if (EFI_ERROR(s)) { nu->last_status = s; return 0; }
        nu->dns_configured = 1;
        memcpy(nu->dns_sig, nu->info, sizeof nu->dns_sig);
    }
    return 1;
}

static int dns_start_cb(void *ctx, const char *name, int len)
{
    NetUefi *nu = (NetUefi *)ctx;
    if (nu->stage != NU_STAGE_UP) return NC_E_NETDOWN;
    if (nu->dns_pending) return NC_E_NOSLOT;
    if (len < 1 || len > 255) return NC_E_BADARG;
    for (int i = 0; i < len; i++) {
        uint8_t ch = (uint8_t)name[i];
        if (ch < 0x21 || ch > 0x7E) return NC_E_BADARG;     /* 영문 도메인 이름만. 한글 도메인(punycode 변환)은 MSX 쪽에서 */
    }
    if (!dns_ensure(nu)) return NC_E_HOST;
    for (int i = 0; i < len; i++) nu->dns_name[i] = (CHAR16)(uint8_t)name[i];
    nu->dns_name[len] = 0;
    nu->dns_tok.Event = nu->ev_dns;
    nu->dns_tok.Status = 0;
    nu->dns_tok.RetryCount = 0;
    nu->dns_tok.RetryInterval = 0;
    nu->dns_tok.RspData = NULL;
    nu->f_dns = 0;
    EFI_STATUS s = nu->dns->HostNameToIp(nu->dns, nu->dns_name, &nu->dns_tok);
    if (EFI_ERROR(s)) { nu->last_status = s; return NC_E_HOST; }
    nu->dns_pending = 1;
    nu->dns_err = NC_DNSERR_UNKNOWN;
    nu->dns_timed_out = 0;
    nu->dns_since = nu->ticks;
    nu->dns_state = NC_DNS_PENDING;
    return 0;
}

static int dns_result_cb(void *ctx, uint8_t *status, uint8_t ip[4], uint8_t *err)
{
    NetUefi *nu = (NetUefi *)ctx;
    *status = (nu->dns_pending && !nu->dns_timed_out) ? NC_DNS_PENDING : nu->dns_state;
    memcpy(ip, nu->dns_ip, 4);
    if (*status != NC_DNS_OK) memset(ip, 0, 4);
    *err = *status == NC_DNS_FAIL ? nu->dns_err : 0;
    return 0;
}

/* UEFI 상태를 UNAPI DNS 오류 번호로 (규격 문서의 번호). 실제 펌웨어가 어떤 상태를 주는지는 확인하지 못했다. */
static uint8_t dns_err_from(EFI_STATUS s)
{
    if (s == EFI_NOT_FOUND_) return NC_DNSERR_NXDOMAIN;
    if (s == EFI_TIMEOUT_) return NC_DNSERR_NOREPLY;
    if (s == EFI_ABORTED_) return NC_DNSERR_CANCELLED;
    return NC_DNSERR_SERVFAIL;
}

static void dns_poll(NetUefi *nu);

static int dns_cancel_cb(void *ctx)
{
    NetUefi *nu = (NetUefi *)ctx;
    if (nu->dns_pending && !nu->dns_timed_out) {
        if (nu->dns) nu->dns->Cancel(nu->dns, &nu->dns_tok);
        nu->dns_timed_out = 1;                        /* 늦게 도착하는 결과는 버린다 */
        nu->dns_since = nu->ticks - 2 * NU_DNS_TMO + NU_CLOSE_TMO;   /* 취소가 안 돌아오면 NU_CLOSE_TMO 뒤에 포기 */
        dns_poll(nu);                                 /* 펌웨어가 바로 끝냈다면 여기서 거둔다 */
    }
    nu->dns_state = NC_DNS_IDLE;
    return 0;
}

static void dns_poll(NetUefi *nu)
{
    if (!nu->dns_pending) return;
    if (nu->dns && nu->dns->Poll) nu->dns->Poll(nu->dns);
    if (nu->f_dns) {
        NU_DNS4_COMPLETION_TOKEN *t = &nu->dns_tok;
        NU_DNS_HOST_TO_ADDR_DATA *d = (NU_DNS_HOST_TO_ADDR_DATA *)t->RspData;
        nu->f_dns = 0;
        nu->dns_pending = 0;
        if (!nu->dns_timed_out) {
            if (!EFI_ERROR(t->Status) && d && d->IpCount > 0 && d->IpList) {
                memcpy(nu->dns_ip, d->IpList, 4);
                nu->dns_state = NC_DNS_OK;
            } else {
                if (EFI_ERROR(t->Status)) nu->last_status = t->Status;
                nu->dns_state = NC_DNS_FAIL;
                nu->dns_err = EFI_ERROR(t->Status) ? dns_err_from(t->Status) : NC_DNSERR_NXDOMAIN;
            }
        }
        if (d) {                                      /* 결과 버퍼는 우리가 돌려준다 */
            if (d->IpList) nu->st->BootServices->FreePool(d->IpList);
            nu->st->BootServices->FreePool(d);
        }
        t->RspData = NULL;
    } else if (!nu->dns_timed_out && nu->ticks - nu->dns_since > NU_DNS_TMO) {
        if (nu->dns) nu->dns->Cancel(nu->dns, &nu->dns_tok);
        nu->dns_timed_out = 1;
        nu->dns_state = NC_DNS_FAIL;
        nu->dns_err = NC_DNSERR_TIMEOUT;
    } else if (nu->dns_timed_out && nu->ticks - nu->dns_since > 2 * NU_DNS_TMO) {
        nu->dns_pending = 0;                          /* 취소가 끝내 안 돌아오면 포기 (토큰은 버림) */
    }
}

/* ====== 5단계: UDP ====== */

static int get_udp_sb(NetUefi *nu)
{
    void *p = NULL;
    if (nu->udp_sb) return 1;
    EFI_STATUS s = nu->st->BootServices->HandleProtocol(nu->nic, &gUdp4Sb, &p);
    if (EFI_ERROR(s) || !p) { nu->last_status = s; return 0; }
    nu->udp_sb = (NU_SERVICE_BINDING *)p;
    return 1;
}

/* UEFI 쪽만 내려놓는다 (자식 파괴). 포트, 받은 자료, 이벤트는 그대로 둔다. */
static void udp_unbind(NetUefi *nu, NuUdp *u)
{
    if (u->udp && u->udp->Cancel) u->udp->Cancel(u->udp, NULL);
    if (u->child && nu->udp_sb) nu->udp_sb->DestroyChild(nu->udp_sb, u->child);
    u->child = NULL;
    u->udp = NULL;
    u->rx_pending = u->tx_pending = 0;
}

/* 자식을 만들고 u->lport로 Configure한다. 성공하면 0, 아니면 NC_E_* */
static int udp_bind(NetUefi *nu, NuUdp *u)
{
    void *p = NULL;
    EFI_STATUS s;
    if (!get_udp_sb(nu)) return NC_E_HOST;
    if (!u->ev_rx && !mk_event(nu, &u->f_rx, &u->ev_rx)) return NC_E_HOST;
    if (!u->ev_tx && !mk_event(nu, &u->f_tx, &u->ev_tx)) return NC_E_HOST;
    s = nu->udp_sb->CreateChild(nu->udp_sb, &u->child);
    if (EFI_ERROR(s)) { nu->last_status = s; u->child = NULL; return NC_E_HOST; }
    s = nu->st->BootServices->HandleProtocol(u->child, &gUdp4, &p);
    if (EFI_ERROR(s) || !p) { nu->last_status = s; udp_unbind(nu, u); return NC_E_HOST; }
    u->udp = (NU_UDP4_PROTOCOL *)p;

    NU_UDP4_CONFIG_DATA cd;
    memset(&cd, 0, sizeof cd);
    cd.TimeToLive = 64;
    cd.UseDefaultAddress = 1;                        /* DHCP로 받은 주소 */
    cd.StationPort = u->lport;                       /* 상대 주소와 포트는 0: 아무한테서나 받는다 */
    s = u->udp->Configure(u->udp, &cd);
    if (EFI_ERROR(s)) { nu->last_status = s; udp_unbind(nu, u); return s == EFI_ACCESS_DENIED_ ? NC_E_INUSE : NC_E_HOST; }
    u->f_rx = u->f_tx = 0;
    return 0;
}

static void udp_free(NetUefi *nu, NuUdp *u)
{
    udp_unbind(nu, u);
    rm_event(nu, &u->ev_rx);
    rm_event(nu, &u->ev_tx);
    memset(u, 0, sizeof *u);
}

/* 내 포트가 0이면 16384~32767에서 열려 있는 UDP 소켓이 쓰지 않는 포트를 고른다. */
static uint16_t pick_udp_port(NetUefi *nu)
{
    for (int tries = 0; tries < 64; tries++) {
        if (nu->eph_udp < 16384 || nu->eph_udp > 32767)
            nu->eph_udp = (uint16_t)(16384 + (nu->ticks * 2246822519u >> 8) % 16384);
        uint16_t p = nu->eph_udp++;
        int used = 0;
        for (int i = 0; i < NETCARD_MAX_UDP; i++) if (nu->udp[i].us != NU_US_FREE && nu->udp[i].lport == p) used = 1;
        if (!used) return p;
    }
    return nu->eph_udp;
}

static int udp_open_cb(void *ctx, uint16_t lport, uint8_t *handle)
{
    NetUefi *nu = (NetUefi *)ctx;
    if (nu->stage != NU_STAGE_UP) return NC_E_NETDOWN;
    int i;
    for (i = 0; i < NETCARD_MAX_UDP; i++) if (nu->udp[i].us == NU_US_FREE) break;
    if (i == NETCARD_MAX_UDP) return NC_E_NOSLOT;
    if (lport == 0) lport = pick_udp_port(nu);
    else for (int k = 0; k < NETCARD_MAX_UDP; k++) if (nu->udp[k].us != NU_US_FREE && nu->udp[k].lport == lport) return NC_E_INUSE;

    NuUdp *u = &nu->udp[i];
    memset(u, 0, sizeof *u);
    u->lport = lport;
    int rc = udp_bind(nu, u);
    if (rc != 0) { udp_free(nu, u); return rc; }
    u->us = NU_US_OPEN;
    *handle = (uint8_t)i;
    return 0;
}

static int udp_close_cb(void *ctx, int h)
{
    NetUefi *nu = (NetUefi *)ctx;
    if (nu->udp[h].us == NU_US_FREE) return NC_E_NOHANDLE;
    udp_free(nu, &nu->udp[h]);
    return 0;
}

static int udp_state_cb(void *ctx, int h, uint8_t *count, uint16_t *oldest, uint16_t *lport)
{
    NetUefi *nu = (NetUefi *)ctx;
    const NuUdp *u = &nu->udp[h];
    if (u->us == NU_US_FREE) return NC_E_NOHANDLE;
    *count = (uint8_t)u->q_count;
    *oldest = u->q_count ? u->q[u->q_head].len : 0;
    *lport = u->lport;
    return 0;
}

static int udp_send_cb(void *ctx, int h, const uint8_t rip[4], uint16_t rport, const uint8_t *data, int len)
{
    NetUefi *nu = (NetUefi *)ctx;
    NuUdp *u = &nu->udp[h];
    if (u->us == NU_US_FREE) return NC_E_NOHANDLE;
    if (u->us != NU_US_OPEN || nu->stage != NU_STAGE_UP) return NC_E_NETDOWN;
    if (len < 0 || len > NU_UDP_BUF) return NC_E_BADARG;
    if (u->tx_pending) return NC_E_NOSLOT;           /* 앞 전송이 아직 안 끝남. 호출한 쪽이 조금 있다 다시 */
    memcpy(u->txbuf, data, (size_t)len);
    memset(&u->txs, 0, sizeof u->txs);
    memcpy(u->txs.DestinationAddress, rip, 4);
    u->txs.DestinationPort = rport;
    u->txd.UdpSessionData = &u->txs;
    u->txd.GatewayAddress = NULL;
    u->txd.DataLength = (uint32_t)len;
    u->txd.FragmentCount = 1;
    u->txd.FragmentTable[0].FragmentLength = (uint32_t)len;
    u->txd.FragmentTable[0].FragmentBuffer = u->txbuf;
    u->tx_tok.Event = u->ev_tx;
    u->tx_tok.Status = 0;
    u->tx_tok.Packet.TxData = &u->txd;
    u->f_tx = 0;
    EFI_STATUS s = u->udp->Transmit(u->udp, &u->tx_tok);
    if (EFI_ERROR(s)) { nu->last_status = s; return s == EFI_NET_UNREACH_ || s == EFI_HOST_UNREACH_ ? NC_E_NETDOWN : NC_E_HOST; }
    u->tx_pending = 1;
    return 0;
}

static int udp_recv_cb(void *ctx, int h, uint8_t *dst, int maxlen, uint8_t srcip[4], uint16_t *srcport, int *got)
{
    NetUefi *nu = (NetUefi *)ctx;
    NuUdp *u = &nu->udp[h];
    if (u->us == NU_US_FREE) return NC_E_NOHANDLE;
    if (u->q_count == 0) return NC_E_NODATA;
    NuDgram *d = &u->q[u->q_head];
    int n = d->len < maxlen ? d->len : maxlen;       /* 짧게 받으면 나머지는 버린다 (UDP 규칙) */
    memcpy(dst, d->data, (size_t)n);
    memcpy(srcip, d->ip, 4);
    *srcport = d->port;
    *got = n;
    u->q_head = (u->q_head + 1) % NU_UDP_Q;
    u->q_count--;
    return 0;
}

static void udp_post_rx(NetUefi *nu, NuUdp *u)
{
    if (u->rx_pending) return;
    u->rx_tok.Event = u->ev_rx;
    u->rx_tok.Status = 0;
    u->rx_tok.Packet.RxData = NULL;
    u->f_rx = 0;
    EFI_STATUS s = u->udp->Receive(u->udp, &u->rx_tok);
    if (!EFI_ERROR(s)) u->rx_pending = 1;
    else nu->last_status = s;                        /* 다음 poll에 다시 건다 */
}

static int udp_poll(NetUefi *nu, NuUdp *u)
{
    int ev = 0;
    if (u->us == NU_US_FREE) return 0;
    if (u->us == NU_US_DOWN) {                       /* 주소가 돌아오면 같은 포트로 다시 붙인다 */
        if (nu->stage == NU_STAGE_UP && nu->ticks >= u->next_try) {
            if (udp_bind(nu, u) == 0) { u->us = NU_US_OPEN; ev |= NU_EV_STATE; }
            else u->next_try = nu->ticks + NU_POLL_UP;
        }
        return ev;
    }
    if (u->udp && u->udp->Poll) u->udp->Poll(u->udp);
    if (u->f_rx) {
        u->f_rx = 0;
        u->rx_pending = 0;
        if (!EFI_ERROR(u->rx_tok.Status)) {
            NU_UDP4_RECEIVE_DATA *d = u->rx_tok.Packet.RxData;
            if (d) {
                if (u->q_count < NU_UDP_Q) {
                    NuDgram *q = &u->q[(u->q_head + u->q_count) % NU_UDP_Q];
                    uint32_t n = 0;
                    for (uint32_t f = 0; f < d->FragmentCount && n < NU_UDP_BUF; f++) {
                        uint32_t take = d->FragmentTable[f].FragmentLength;
                        if (take > NU_UDP_BUF - n) take = NU_UDP_BUF - n;
                        if (take && d->FragmentTable[f].FragmentBuffer) memcpy(q->data + n, d->FragmentTable[f].FragmentBuffer, take);
                        n += take;
                    }
                    memcpy(q->ip, d->UdpSession.SourceAddress, 4);
                    q->port = d->UdpSession.SourcePort;
                    q->len = (uint16_t)n;
                    u->q_count++;
                    ev |= NU_EV_RX;
                }                                    /* 가득 찼으면 버린다 */
                if (d->RecycleSignal) ((EFI_STATUS (*)(void *))nu->st->BootServices->SignalEvent)(d->RecycleSignal);
            }
        }
    }
    if (u->f_tx) { u->f_tx = 0; u->tx_pending = 0; }
    udp_post_rx(nu, u);
    return ev;
}

/* ====== 5단계: 망 끊김 ====== */

/* 주소를 잃었거나 바뀌었다. 옛 주소를 쓰던 것들을 정리한다. */
static void net_lost(NetUefi *nu)
{
    for (int i = 0; i < NETCARD_MAX_CONN; i++) {
        NuConn *c = &nu->conn[i];
        if ((c->cs == NU_CS_CONNECTING || c->cs == NU_CS_OPEN) && !c->close_req && !c->abort_req && !c->fail_req) {
            c->reason = NC_REASON_NETLOST;
            c->fail_req = 1;                          /* 이유와 남은 자료는 남기고 정리 (conn_poll이 진행) */
            nu->pending_ev |= NU_EV_STATE | NU_EV_RX;
        }
    }
    if (nu->dns_pending && !nu->dns_timed_out) {
        if (nu->dns) nu->dns->Cancel(nu->dns, &nu->dns_tok);
        nu->dns_timed_out = 1;
        nu->dns_since = nu->ticks - 2 * NU_DNS_TMO + NU_CLOSE_TMO;
        nu->dns_state = NC_DNS_FAIL;
        nu->dns_err = NC_DNSERR_NETLOST;
    }
    for (int i = 0; i < NETCARD_MAX_UDP; i++) {
        NuUdp *u = &nu->udp[i];
        if (u->us == NU_US_OPEN) {
            udp_unbind(nu, u);
            u->us = NU_US_DOWN;
            u->next_try = nu->ticks;
            nu->pending_ev |= NU_EV_STATE;
        }
    }
}

/* ====== 공통 ====== */

int netuefi_poll(NetUefi *nu)
{
    int ev = 0;
    nu->ticks++;
    stage_poll(nu);
    ev |= nu->pending_ev;
    nu->pending_ev = 0;
    for (int i = 0; i < NETCARD_MAX_CONN; i++) ev |= conn_poll(nu, &nu->conn[i]);
    for (int i = 0; i < NETCARD_MAX_UDP; i++) ev |= udp_poll(nu, &nu->udp[i]);
    dns_poll(nu);
    return ev;
}

static int host_state(void *ctx) { return netuefi_state((const NetUefi *)ctx); }
static int host_ipinfo(void *ctx, uint8_t out[20]) { return netuefi_get_ipinfo((const NetUefi *)ctx, out); }

void netuefi_host(NetUefi *nu, NetHost *out)
{
    memset(out, 0, sizeof *out);
    out->ctx = nu;
    out->net_state = host_state;
    out->get_ipinfo = host_ipinfo;
    out->dns_start = dns_start_cb;
    out->dns_result = dns_result_cb;
    out->dns_cancel = dns_cancel_cb;
    out->tcp_open = tcp_open_cb;
    out->tcp_close = tcp_close_cb;
    out->tcp_state = tcp_state_cb;
    out->tcp_send = tcp_send_cb;
    out->tcp_recv = tcp_recv_cb;
    out->tcp_flush = tcp_flush_cb;
    out->tcp_info = tcp_info_cb;
    out->udp_open = udp_open_cb;
    out->udp_close = udp_close_cb;
    out->udp_state = udp_state_cb;
    out->udp_send = udp_send_cb;
    out->udp_recv = udp_recv_cb;
}
