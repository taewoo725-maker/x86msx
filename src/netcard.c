/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX 네트워크 카트리지 - 5단계 구현 (netcard.h 참고) */
#include "netcard.h"
#include <string.h>

/* 명령 하나가 BUSY로 보이는 시간 (Z80 T-state). 명령이 몇 사이클 뒤 끝나는 것으로 보이게 한다. */
#define NC_BUSY_CYCLES 64

void netcard_init(NetCard *nc, uint8_t base)
{
    memset(nc, 0, sizeof *nc);
    nc->base = (uint8_t)(base & 0xF8);
    nc->enabled = 0;
}

void netcard_set_host(NetCard *nc, const NetHost *host)
{
    if (host) nc->host = *host;
    else memset(&nc->host, 0, sizeof nc->host);
}

static void clear_cmd(NetCard *nc)
{
    nc->cmd = 0;
    nc->nargs = 0;
    nc->nres = nc->res_pos = 0;
    nc->tx_len = 0;
    nc->rx_len = nc->rx_pos = 0;
}

void netcard_reset(NetCard *nc)
{
    nc->busy = nc->done = nc->err = nc->irq = nc->irq_enable = 0;
    nc->errcode = NC_E_OK;
    nc->busy_until = 0;
    nc->dns_ip_valid = 0;
    clear_cmd(nc);
}

int netcard_owns(const NetCard *nc, uint16_t port)
{
    return nc->enabled && (port & 0xF8) == nc->base && (port & 7) < 4;
}

static int host_state(const NetCard *nc)
{
    return nc->host.net_state ? nc->host.net_state(nc->host.ctx) : NC_NET_DOWN;
}

void netcard_tick(NetCard *nc, uint64_t cycles)
{
    if (nc->busy && cycles >= nc->busy_until) {
        nc->busy = 0;
        if (nc->errcode != NC_E_OK) nc->err = 1;
        else nc->done = 1;
    }
}

static void fail(NetCard *nc, uint8_t code)
{
    nc->errcode = code;
    nc->nres = nc->res_pos = 0;
}

/* 호스트 호출의 반환값(0 = 성공)을 오류 코드로 옮긴다. 1 이상 255 이하가 아니면 호스트 오류로 본다. */
static void host_rc(NetCard *nc, int rc)
{
    if (rc != 0) fail(nc, (rc > 0 && rc < 256) ? (uint8_t)rc : NC_E_HOST);
}

static int handle_ok(NetCard *nc, int h)
{
    if (h < 0 || h >= NETCARD_MAX_CONN) { fail(nc, NC_E_NOHANDLE); return 0; }
    return 1;
}

static int udp_handle_ok(NetCard *nc, int h)
{
    if (h < 0 || h >= NETCARD_MAX_UDP) { fail(nc, NC_E_NOHANDLE); return 0; }
    return 1;
}

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

/* 명령 실행: 결과를 바로 계산해 두고, BUSY만 시간을 두고 푼다. */
uint32_t netcard_act;                                   /* 명령을 실행할 때마다 늘어남 (네트워크 아이콘이 '쓰는 중'을 알아봄) */
static void run_cmd(NetCard *nc, uint8_t cmd)
{
    netcard_act++;
    const NetHost *h = &nc->host;
    nc->cmd = cmd;
    nc->errcode = NC_E_OK;
    nc->nres = nc->res_pos = 0;
    switch (cmd) {
    case NC_CMD_RESET:
        if (nc->nargs != 0) { fail(nc, NC_E_BADARG); break; }
        {   /* 하드웨어 리셋과 달리 인터럽트 마스크 설정은 둔다 */
            int en = nc->irq_enable;
            netcard_reset(nc);
            nc->irq_enable = en;
            nc->cmd = cmd;
        }
        break;
    case NC_CMD_NET_STATE:
        if (nc->nargs != 0) { fail(nc, NC_E_BADARG); break; }
        nc->res[0] = (uint8_t)host_state(nc);
        nc->nres = 1;
        break;
    case NC_CMD_GET_IPINFO:
        if (nc->nargs != 0) { fail(nc, NC_E_BADARG); break; }
        if (host_state(nc) != NC_NET_UP || !h->get_ipinfo) { fail(nc, NC_E_NETDOWN); break; }
        if (h->get_ipinfo(h->ctx, nc->res) != 0) { fail(nc, NC_E_HOST); break; }
        nc->nres = 20;
        break;

    case NC_CMD_DNS_START: {
        int len = nc->nargs >= 1 ? nc->args[0] : 0;
        if (len < 1 || nc->nargs != 1 + len) { fail(nc, NC_E_BADARG); break; }
        nc->dns_ip_valid = 0;
        if (host_state(nc) != NC_NET_UP || !h->dns_start) { fail(nc, NC_E_NETDOWN); break; }
        host_rc(nc, h->dns_start(h->ctx, (const char *)&nc->args[1], len));
        break;
    }
    case NC_CMD_DNS_RESULT: {
        uint8_t st = NC_DNS_FAIL, ip[4] = {0, 0, 0, 0}, err = 0;
        if (nc->nargs != 0) { fail(nc, NC_E_BADARG); break; }
        if (nc->dns_ip_valid) { nc->res[0] = NC_DNS_OK; memcpy(&nc->res[1], nc->dns_ip, 4); nc->res[5] = 0; nc->nres = 6; break; }   /* IP 직접 질의의 저장된 결과 */
        if (!h->dns_result) { fail(nc, NC_E_NETDOWN); break; }
        if (h->dns_result(h->ctx, &st, ip, &err) != 0) { fail(nc, NC_E_HOST); break; }
        nc->res[0] = st;
        memcpy(&nc->res[1], ip, 4);
        nc->res[5] = st == NC_DNS_FAIL ? err : 0;
        nc->nres = 6;
        break;
    }

    case NC_CMD_DNS_SETIP:
        if (nc->nargs != 4) { fail(nc, NC_E_BADARG); break; }
        memcpy(nc->dns_ip, nc->args, 4); nc->dns_ip_valid = 1;
        break;
    case NC_CMD_DNS_CANCEL:
        if (nc->nargs != 0) { fail(nc, NC_E_BADARG); break; }
        nc->dns_ip_valid = 0;
        if (!h->dns_cancel) { fail(nc, NC_E_NETDOWN); break; }
        host_rc(nc, h->dns_cancel(h->ctx));
        break;

    case NC_CMD_TCP_OPEN: {
        uint8_t handle = 0;
        unsigned lport, rport;
        if (nc->nargs != 9) { fail(nc, NC_E_BADARG); break; }
        lport = (unsigned)nc->args[0] | ((unsigned)nc->args[1] << 8);
        rport = (unsigned)nc->args[6] | ((unsigned)nc->args[7] << 8);
        if (rport == 0 || (nc->args[8] & NC_OPEN_PASSIVE)) { fail(nc, NC_E_BADARG); break; }
        if (host_state(nc) != NC_NET_UP || !h->tcp_open) { fail(nc, NC_E_NETDOWN); break; }
        {
            int rc = h->tcp_open(h->ctx, (uint16_t)lport, &nc->args[2], (uint16_t)rport, nc->args[8], &handle);
            if (rc != 0) { host_rc(nc, rc); break; }
        }
        nc->res[0] = handle;
        nc->nres = 1;
        break;
    }
    case NC_CMD_TCP_CLOSE:
    case NC_CMD_TCP_ABORT:
        if (nc->nargs != 1) { fail(nc, NC_E_BADARG); break; }
        if (!handle_ok(nc, nc->args[0])) break;
        if (!h->tcp_close) { fail(nc, NC_E_NETDOWN); break; }
        host_rc(nc, h->tcp_close(h->ctx, nc->args[0], cmd == NC_CMD_TCP_ABORT));
        break;
    case NC_CMD_TCP_STATE: {
        uint8_t state = 0, reason = 0;
        uint16_t rxa = 0, txf = 0;
        if (nc->nargs != 1) { fail(nc, NC_E_BADARG); break; }
        if (!handle_ok(nc, nc->args[0])) break;
        if (!h->tcp_state) { fail(nc, NC_E_NETDOWN); break; }
        {
            int rc = h->tcp_state(h->ctx, nc->args[0], &state, &rxa, &txf, &reason);
            if (rc != 0) { host_rc(nc, rc); break; }
        }
        nc->res[0] = state;
        put16(&nc->res[1], rxa);
        put16(&nc->res[3], txf);
        nc->res[5] = reason;
        nc->nres = 6;
        break;
    }
    case NC_CMD_TCP_SEND: {
        int len, accepted = 0;
        if (nc->nargs != 4) { fail(nc, NC_E_BADARG); break; }
        if (!handle_ok(nc, nc->args[0])) break;
        len = (int)nc->args[1] | ((int)nc->args[2] << 8);
        /* 보낼 자료는 미리 데이터 포트(+1)로 써 둔 것의 앞쪽 len 바이트이다 */
        if (len < 1 || len > nc->tx_len) { fail(nc, NC_E_BADARG); break; }
        if (!h->tcp_send) { fail(nc, NC_E_NETDOWN); break; }
        {
            int rc = h->tcp_send(h->ctx, nc->args[0], nc->tx, len, nc->args[3] & 1, &accepted);
            if (rc != 0) host_rc(nc, rc);
            else if (accepted < 0 || accepted > len) fail(nc, NC_E_HOST);
            else { put16(nc->res, (unsigned)accepted); nc->nres = 2; }
        }
        break;
    }
    case NC_CMD_TCP_RECV: {
        int maxlen, got = 0;
        if (nc->nargs != 3) { fail(nc, NC_E_BADARG); break; }
        if (!handle_ok(nc, nc->args[0])) break;
        maxlen = (int)nc->args[1] | ((int)nc->args[2] << 8);
        if (maxlen < 1 || maxlen > NETCARD_DATA_MAX) { fail(nc, NC_E_BADARG); break; }
        if (!h->tcp_recv) { fail(nc, NC_E_NETDOWN); break; }
        nc->rx_len = nc->rx_pos = 0;
        {
            int rc = h->tcp_recv(h->ctx, nc->args[0], nc->rx, maxlen, &got);
            if (rc != 0) host_rc(nc, rc);
            else if (got < 0 || got > maxlen) fail(nc, NC_E_HOST);
            else { nc->rx_len = got; put16(nc->res, (unsigned)got); nc->nres = 2; }
        }
        break;
    }
    case NC_CMD_TCP_FLUSH:
        if (nc->nargs != 1) { fail(nc, NC_E_BADARG); break; }
        if (!handle_ok(nc, nc->args[0])) break;
        if (!h->tcp_flush) { fail(nc, NC_E_NETDOWN); break; }
        host_rc(nc, h->tcp_flush(h->ctx, nc->args[0]));
        break;

    case NC_CMD_TCP_INFO:
        if (nc->nargs != 1) { fail(nc, NC_E_BADARG); break; }
        if (!handle_ok(nc, nc->args[0])) break;
        if (!h->tcp_info) { fail(nc, NC_E_NETDOWN); break; }
        if (h->tcp_info(h->ctx, nc->args[0], nc->res) != 0) { fail(nc, NC_E_NOHANDLE); break; }
        nc->nres = 8;
        break;

    case NC_CMD_UDP_OPEN: {
        uint8_t handle = 0;
        if (nc->nargs != 2) { fail(nc, NC_E_BADARG); break; }
        if (host_state(nc) != NC_NET_UP || !h->udp_open) { fail(nc, NC_E_NETDOWN); break; }
        {
            int rc = h->udp_open(h->ctx, (uint16_t)((unsigned)nc->args[0] | ((unsigned)nc->args[1] << 8)), &handle);
            if (rc != 0) { host_rc(nc, rc); break; }
        }
        nc->res[0] = handle;
        nc->nres = 1;
        break;
    }
    case NC_CMD_UDP_CLOSE:
        if (nc->nargs != 1) { fail(nc, NC_E_BADARG); break; }
        if (!udp_handle_ok(nc, nc->args[0])) break;
        if (!h->udp_close) { fail(nc, NC_E_NETDOWN); break; }
        host_rc(nc, h->udp_close(h->ctx, nc->args[0]));
        break;
    case NC_CMD_UDP_STATE: {
        uint8_t count = 0;
        uint16_t oldest = 0, lport = 0;
        if (nc->nargs != 1) { fail(nc, NC_E_BADARG); break; }
        if (!udp_handle_ok(nc, nc->args[0])) break;
        if (!h->udp_state) { fail(nc, NC_E_NETDOWN); break; }
        {
            int rc = h->udp_state(h->ctx, nc->args[0], &count, &oldest, &lport);
            if (rc != 0) { host_rc(nc, rc); break; }
        }
        nc->res[0] = count;
        put16(&nc->res[1], oldest);
        put16(&nc->res[3], lport);
        nc->nres = 5;
        break;
    }
    case NC_CMD_UDP_SEND: {
        unsigned rport, len;
        if (nc->nargs != 9) { fail(nc, NC_E_BADARG); break; }
        if (!udp_handle_ok(nc, nc->args[0])) break;
        rport = (unsigned)nc->args[5] | ((unsigned)nc->args[6] << 8);
        len = (unsigned)nc->args[7] | ((unsigned)nc->args[8] << 8);
        /* 자료는 미리 데이터 포트(+1)로 써 둔 것의 앞쪽 len 바이트. 길이 0인 데이터그램도 허용 */
        if (rport == 0 || len > NETCARD_UDP_MAX || (int)len > nc->tx_len) { fail(nc, NC_E_BADARG); break; }
        if (host_state(nc) != NC_NET_UP || !h->udp_send) { fail(nc, NC_E_NETDOWN); break; }
        host_rc(nc, h->udp_send(h->ctx, nc->args[0], &nc->args[1], (uint16_t)rport, nc->tx, (int)len));
        break;
    }
    case NC_CMD_UDP_RECV: {
        int maxlen, got = 0;
        uint8_t sip[4] = {0, 0, 0, 0};
        uint16_t sport = 0;
        if (nc->nargs != 3) { fail(nc, NC_E_BADARG); break; }
        if (!udp_handle_ok(nc, nc->args[0])) break;
        maxlen = (int)nc->args[1] | ((int)nc->args[2] << 8);
        if (maxlen > NETCARD_DATA_MAX) { fail(nc, NC_E_BADARG); break; }
        if (!h->udp_recv) { fail(nc, NC_E_NETDOWN); break; }
        nc->rx_len = nc->rx_pos = 0;
        {
            int rc = h->udp_recv(h->ctx, nc->args[0], nc->rx, maxlen, sip, &sport, &got);
            if (rc != 0) { host_rc(nc, rc); break; }
        }
        if (got < 0 || got > maxlen) { fail(nc, NC_E_HOST); break; }
        nc->rx_len = got;
        memcpy(nc->res, sip, 4);
        put16(&nc->res[4], sport);
        put16(&nc->res[6], (unsigned)got);
        nc->nres = 8;
        break;
    }

    case NC_CMD_ECHO_SEND: {
        unsigned id, seq, len;
        if (nc->nargs != 11) { fail(nc, NC_E_BADARG); break; }
        id = (unsigned)nc->args[5] | ((unsigned)nc->args[6] << 8);
        seq = (unsigned)nc->args[7] | ((unsigned)nc->args[8] << 8);
        len = (unsigned)nc->args[9] | ((unsigned)nc->args[10] << 8);
        if (len > 1400) { fail(nc, NC_E_BADARG); break; }
        if (host_state(nc) != NC_NET_UP || !h->echo_send) { fail(nc, NC_E_NETDOWN); break; }
        host_rc(nc, h->echo_send(h->ctx, &nc->args[0], nc->args[4], (uint16_t)id, (uint16_t)seq, (uint16_t)len));
        break;
    }
    case NC_CMD_ECHO_RECV:
        if (nc->nargs != 0) { fail(nc, NC_E_BADARG); break; }
        if (!h->echo_recv) { fail(nc, NC_E_NETDOWN); break; }
        {
            int rc = h->echo_recv(h->ctx, nc->res);
            if (rc != 0) { host_rc(nc, rc); break; }
        }
        nc->nres = 11;
        break;

    default:
        fail(nc, NC_E_BADCMD);
        break;
    }
    /* 데이터 창으로 쓴 자료는 SEND 한 번에만 쓰인다. 성공이든 실패든, 남은 자료는 MSX가 다시 쓴다 */
    if (cmd != NC_CMD_RESET) nc->tx_len = 0;
}

uint8_t netcard_io_read(NetCard *nc, uint16_t port, uint64_t cycles)
{
    netcard_tick(nc, cycles);
    switch (port & 3) {
    case 0: {
        uint8_t s = 0;
        if (nc->busy) s |= NC_ST_BUSY;
        if (nc->done) s |= NC_ST_DONE;
        if (nc->irq)  s |= NC_ST_IRQ;
        if (host_state(nc) == NC_NET_UP) s |= NC_ST_NETUP;
        if (nc->err)  s |= NC_ST_ERR;
        return s;
    }
    case 1:  return nc->rx_pos < nc->rx_len ? nc->rx[nc->rx_pos++] : 0xFF;
    case 2:  return nc->res_pos < nc->nres ? nc->res[nc->res_pos++] : 0xFF;
    default: return nc->errcode;
    }
}

void netcard_io_write(NetCard *nc, uint16_t port, uint8_t v, uint64_t cycles)
{
    netcard_tick(nc, cycles);
    switch (port & 3) {
    case 0:
        if (nc->busy) return;                           /* BUSY 동안 명령 쓰기는 무시 */
        nc->done = nc->err = 0;
        run_cmd(nc, v);
        nc->busy = 1;
        nc->busy_until = cycles + NC_BUSY_CYCLES;
        nc->nargs = 0;                                  /* 다음 명령의 인자를 새로 받는다 (결과는 남긴다) */
        break;
    case 1:
        if (!nc->busy && nc->tx_len < NETCARD_DATA_MAX) nc->tx[nc->tx_len++] = v;
        break;
    case 2:
        if (!nc->busy && nc->nargs < NETCARD_ARG_MAX) nc->args[nc->nargs++] = v;
        break;
    default:
        nc->irq_enable = (v & NC_INT_ENABLE) ? 1 : 0;
        if (v & NC_INT_ACK) nc->irq = 0;
        break;
    }
}

int netcard_irq_line(const NetCard *nc)
{
    return nc->enabled && nc->irq && nc->irq_enable;
}

void netcard_raise_irq(NetCard *nc)
{
    nc->irq = 1;
}
