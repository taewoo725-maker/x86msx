/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX 네트워크 카트리지 - 5단계: DNS, TCP 클라이언트, UDP 명령
 * 설계서: x86MSX 네트워크 카트리지 설계서 (v0.1 기준, 변경점은 버전기록.md 참고)
 *
 * 이 모듈은 x86MSX의 다른 소스에 의존하지 않는다. 필요한 것은 <stdint.h>, <string.h>뿐이다.
 * 사이클 값은 호출하는 쪽의 CPU 사이클 카운터(예: m->cpu.cycles)를 그대로 넘긴다.
 */
#ifndef NETCARD_H
#define NETCARD_H

#include <stdint.h>

#define NETCARD_DEFAULT_BASE  0x28   /* 포트 +0 ~ +3 사용, +4 ~ +7 예약 (8포트 단위로 정렬) */
#define NETCARD_ARG_MAX       260    /* DNS_START: 길이 1바이트 + 이름 255바이트까지 */
#define NETCARD_RES_MAX       64
#define NETCARD_DATA_MAX      2048
#define NETCARD_MAX_CONN      4      /* TCP 핸들 0~3 */
#define NETCARD_MAX_UDP       4      /* UDP 핸들 0~3 (TCP와 따로 센다) */
#define NETCARD_UDP_MAX       1472   /* UDP 자료 최대 길이 (이더넷 MTU 1500 - IP 20 - UDP 8) */

/* 상태 포트(+0) 비트 */
#define NC_ST_BUSY   0x80
#define NC_ST_DONE   0x40
#define NC_ST_IRQ    0x20
#define NC_ST_NETUP  0x10
#define NC_ST_ERR    0x08

/* 인터럽트 포트(+3) 쓰기 비트 */
#define NC_INT_ENABLE 0x01
#define NC_INT_ACK    0x02   /* 1을 쓰면 IRQ 깃발을 지운다 */

/* 명령 코드 */
#define NC_CMD_RESET       0x01
#define NC_CMD_NET_STATE   0x02
#define NC_CMD_GET_IPINFO  0x03
#define NC_CMD_DNS_START   0x10
#define NC_CMD_DNS_RESULT  0x11
#define NC_CMD_DNS_CANCEL  0x12   /* 진행 중인 질의를 취소하고, 끝난 결과는 버린다. 상태는 IDLE이 된다 */
#define NC_CMD_DNS_SETIP   0x13   /* 5.0.131 인자: IP(4). IP 주소를 직접 질의했을 때 결과를 저장 (DNS_RESULT가 OK로 돌려줌). DNS_START·DNS_CANCEL·리셋이 지움 */
#define NC_CMD_TCP_OPEN    0x20
#define NC_CMD_TCP_CLOSE   0x21
#define NC_CMD_TCP_ABORT   0x22
#define NC_CMD_TCP_STATE   0x23
#define NC_CMD_TCP_SEND    0x24
#define NC_CMD_TCP_RECV    0x25
#define NC_CMD_TCP_FLUSH   0x26
#define NC_CMD_TCP_INFO    0x27   /* 인자: 핸들. 결과 8바이트: 상대 IP(4), 상대 포트(2, LE), 내 포트(2, LE) */
#define NC_CMD_UDP_OPEN    0x30   /* 인자: 내 포트(2, LE; 0 = 장치가 고름). 결과: 핸들 */
#define NC_CMD_UDP_CLOSE   0x31   /* 인자: 핸들 */
#define NC_CMD_UDP_STATE   0x32   /* 인자: 핸들. 결과 5바이트: 받은 개수(1), 가장 오래된 것의 크기(2, LE), 내 포트(2, LE) */
#define NC_CMD_UDP_SEND    0x33   /* 인자: 핸들, 상대 IP(4), 상대 포트(2, LE), 길이(2, LE). 자료는 미리 +1 포트로 쓴다 */
#define NC_CMD_UDP_RECV    0x34   /* 인자: 핸들, 최대 길이(2, LE). 결과 8바이트: 보낸 IP(4), 보낸 포트(2, LE), 받은 길이(2, LE). 자료는 +1 포트에서 읽음 */

#define NC_CMD_ECHO_SEND   0x40   /* 5.0.131 ICMP 에코(PING) 보내기. 인자 11바이트: 상대 IP(4), TTL(1), 식별자(2, LE), 순번(2, LE), 자료 길이(2, LE). 자료는 0,1,2..255 되풀이 */
#define NC_CMD_ECHO_RECV   0x41   /* 가장 오래된 응답 하나를 꺼냄. 결과 11바이트: 보낸 IP(4), TTL(1), 식별자(2, LE), 순번(2, LE), 자료 길이(2, LE). 없으면 NODATA */

/* 오류 코드 (+3 읽기) */
#define NC_E_OK        0
#define NC_E_BADCMD    1
#define NC_E_BADARG    2
#define NC_E_NOHANDLE  3
#define NC_E_NETDOWN   4
#define NC_E_TIMEOUT   5
#define NC_E_HOST      6
#define NC_E_NOSLOT    7     /* 슬롯이 가득 찼거나 DNS 질의가 이미 진행 중, UDP 보내기가 아직 앞 전송 중 */
#define NC_E_INUSE     8     /* 같은 포트가 이미 쓰이고 있음 */
#define NC_E_NODATA    9     /* 받을 자료 없음 (UDP_RECV) */

/* NET_STATE 결과 */
#define NC_NET_DOWN    0
#define NC_NET_SETUP   1
#define NC_NET_UP      2

/* DNS_RESULT 상태 */
#define NC_DNS_PENDING 0
#define NC_DNS_OK      1
#define NC_DNS_FAIL    2
#define NC_DNS_IDLE    3   /* 질의한 적이 없거나, 취소해서 버린 뒤 */

/* DNS_RESULT 여섯째 바이트(실패 때만 뜻이 있음): TCP/IP UNAPI의 DNS 오류 번호와 같다 */
#define NC_DNSERR_UNKNOWN   0
#define NC_DNSERR_SERVFAIL  2
#define NC_DNSERR_NXDOMAIN  3
#define NC_DNSERR_NOREPLY   16   /* 서버가 답하지 않음 */
#define NC_DNSERR_TIMEOUT   17   /* 전체 제한 시간 초과 */
#define NC_DNSERR_CANCELLED 18
#define NC_DNSERR_NETLOST   19

/* TCP_STATE 연결 상태 (UEFI Tcp4 상태 번호와 같다) */
#define NC_TCP_CLOSED       0
#define NC_TCP_SYN_SENT     2
#define NC_TCP_ESTABLISHED  4
#define NC_TCP_FIN_WAIT1    5
#define NC_TCP_CLOSE_WAIT   9

/* TCP_STATE 닫힘 이유 */
#define NC_REASON_NONE      0   /* 정상 또는 아직 열려 있음 */
#define NC_REASON_REFUSED   1   /* 상대가 거부하거나 리셋 */
#define NC_REASON_TIMEOUT   2
#define NC_REASON_UNREACH   3   /* 네트워크, 호스트, 포트에 닿지 않음 */
#define NC_REASON_OTHER     4
#define NC_REASON_NETLOST   5   /* 주소를 잃었거나 바뀜 */

/* TCP_OPEN 플래그 */
#define NC_OPEN_PASSIVE     0x01   /* 3단계에서는 지원하지 않음 (BADARG) */

/* 호스트(UEFI) 쪽 연결부. 칸이 NULL이면 그 기능은 "네트워크 없음"으로 본다.
 * 아래 3단계 칸의 반환값은 0 = 성공, 그 외 = NC_E_* 오류 코드이다. */
typedef struct NetHost {
    void *ctx;
    int (*net_state)(void *ctx);                   /* NC_NET_* 중 하나 */
    int (*get_ipinfo)(void *ctx, uint8_t out[20]); /* 로컬 IP, 서브넷, 게이트웨이, DNS1, DNS2 (각 4바이트). 0 = 성공 */

    int (*dns_start)(void *ctx, const char *name, int len);
    int (*dns_result)(void *ctx, uint8_t *status, uint8_t ip[4], uint8_t *err);
    int (*dns_cancel)(void *ctx);
    int (*tcp_open)(void *ctx, uint16_t lport, const uint8_t rip[4], uint16_t rport, uint8_t flags, uint8_t *handle);
    int (*tcp_close)(void *ctx, int handle, int abort);
    int (*tcp_state)(void *ctx, int handle, uint8_t *state, uint16_t *rx_avail, uint16_t *tx_free, uint8_t *reason);
    int (*tcp_send)(void *ctx, int handle, const uint8_t *data, int len, int push, int *accepted);
    int (*tcp_recv)(void *ctx, int handle, uint8_t *dst, int maxlen, int *got);
    int (*tcp_flush)(void *ctx, int handle);
    int (*tcp_info)(void *ctx, int handle, uint8_t out[8]);

    int (*udp_open)(void *ctx, uint16_t lport, uint8_t *handle);
    int (*udp_close)(void *ctx, int handle);
    int (*udp_state)(void *ctx, int handle, uint8_t *count, uint16_t *oldest, uint16_t *lport);
    int (*udp_send)(void *ctx, int handle, const uint8_t rip[4], uint16_t rport, const uint8_t *data, int len);
    /* 받은 자료가 없으면 NC_E_NODATA. 자료가 maxlen보다 길면 앞부분만 주고 나머지는 버린다 */
    int (*udp_recv)(void *ctx, int handle, uint8_t *dst, int maxlen, uint8_t srcip[4], uint16_t *srcport, int *got);
    /* ICMP 에코. 칸이 NULL이면 "네트워크 없음" */
    int (*echo_send)(void *ctx, const uint8_t rip[4], uint8_t ttl, uint16_t id, uint16_t seq, uint16_t len);
    int (*echo_recv)(void *ctx, uint8_t out[11]);   /* 응답이 없으면 NC_E_NODATA */
} NetHost;

typedef struct NetCard {
    uint8_t  base;            /* 8의 배수 */
    int      enabled;
    NetHost  host;

    /* 상태 */
    int      busy, done, err, irq, irq_enable;
    int      dns_ip_valid;    /* DNS_SETIP로 저장한 IP */
    uint8_t  dns_ip[4];
    uint8_t  errcode;
    uint64_t busy_until;      /* 이 사이클이 되면 BUSY가 풀린다 */

    /* 명령 틀 */
    uint8_t  cmd;
    uint8_t  args[NETCARD_ARG_MAX];
    int      nargs;
    uint8_t  res[NETCARD_RES_MAX];
    int      nres, res_pos;

    /* 데이터 창 (+1) */
    uint8_t  tx[NETCARD_DATA_MAX];
    int      tx_len;
    uint8_t  rx[NETCARD_DATA_MAX];
    int      rx_len, rx_pos;
} NetCard;

void    netcard_init(NetCard *nc, uint8_t base);                 /* 전원 투입. 사용 안 함(enabled=0) 상태 */
void    netcard_set_host(NetCard *nc, const NetHost *host);      /* host가 NULL이면 연결 해제 */
void    netcard_reset(NetCard *nc);                              /* MSX 리셋 */
int     netcard_owns(const NetCard *nc, uint16_t port);          /* 이 포트가 이 장치 것인가 (정렬 8포트 중 +0~+3) */
uint8_t netcard_io_read(NetCard *nc, uint16_t port, uint64_t cycles);
void    netcard_io_write(NetCard *nc, uint16_t port, uint8_t v, uint64_t cycles);
extern uint32_t netcard_act;                             /* 실행한 명령 수 */
void    netcard_tick(NetCard *nc, uint64_t cycles);              /* 프레임마다 한 번 이상 호출 (BUSY 해제용) */
int     netcard_irq_line(const NetCard *nc);                     /* 1이면 MSX 인터럽트 선을 올린다 */
void    netcard_raise_irq(NetCard *nc);                          /* 호스트 쪽 비동기 이벤트용 */

#endif
