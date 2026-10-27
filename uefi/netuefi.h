/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX 네트워크 카트리지 - UEFI 연결: 2단계 (IP4Config2로 DHCP 주소) + 3단계 (DNS4, TCP4 클라이언트) + 5단계 (UDP4, 망 끊김)
 *
 * netcard의 NetHost 칸을 UEFI 펌웨어로 채운다.
 * 부트 서비스를 끝내지 않는 x86MSX UEFI 판에서만 쓴다. 이 파일은 uefi/ 폴더에 둔다 ("efi.h"는 같은 폴더의 것).
 *
 * 사용법:
 *   static NetUefi nu;                         (구조체가 크고 UEFI가 안의 주소를 계속 쓰므로 정적 변수로, 옮기지 않는다)
 *   NetHost host;
 *   netuefi_init(&nu, SystemTable);
 *   netuefi_host(&nu, &host);
 *   netcard_set_host(&m->net, &host);
 *   ... 프레임마다:  if (netuefi_poll(&nu)) { netcard_raise_irq(&m->net); sync_int(m); }
 *
 * 모든 호출은 막지 않는다. netuefi_poll은 대부분의 프레임에서 카운터만 올리고 돌아온다.
 * 단, 처음 NIC를 연결할 때(ConnectController)와 연결 하나를 만들 때(CreateChild, Configure)는
 * 펌웨어에 따라 잠깐 걸릴 수 있다.
 */
#ifndef NETUEFI_H
#define NETUEFI_H

#include "efi.h"
#include "../src/netcard.h"

/* ====== UEFI 규격 정의 (edk2 MdePkg/Include/Protocol 의 Ip4Config2.h, Tcp4.h, Dns4.h, Udp4.h, ServiceBinding.h) ====== */

/* --- IP4Config2 --- */
#define NU_CFG2_INTERFACE_INFO 0
#define NU_CFG2_POLICY         1
#define NU_CFG2_MANUAL_ADDRESS 2
#define NU_CFG2_GATEWAY        3
#define NU_CFG2_DNS_SERVER     4

#define NU_POLICY_STATIC 0
#define NU_POLICY_DHCP   1

typedef struct NU_IP4_CONFIG2_PROTOCOL {
    EFI_STATUS (*SetData)(struct NU_IP4_CONFIG2_PROTOCOL *, uint32_t type, UINTN size, void *data);
    EFI_STATUS (*GetData)(struct NU_IP4_CONFIG2_PROTOCOL *, uint32_t type, UINTN *size, void *data);
    void *RegisterDataNotify, *UnregisterDataNotify;
} NU_IP4_CONFIG2_PROTOCOL;

typedef struct {
    CHAR16   Name[32];
    uint8_t  IfType;
    uint32_t HwAddressSize;
    uint8_t  HwAddress[32];
    uint8_t  StationAddress[4];
    uint8_t  SubnetMask[4];
    uint32_t RouteTableSize;
    void    *RouteTable;
} NU_IP4_CONFIG2_INTERFACE_INFO;

_Static_assert(offsetof(NU_IP4_CONFIG2_INTERFACE_INFO, HwAddressSize) == 68, "layout");
_Static_assert(offsetof(NU_IP4_CONFIG2_INTERFACE_INFO, StationAddress) == 104, "layout");
_Static_assert(offsetof(NU_IP4_CONFIG2_INTERFACE_INFO, RouteTableSize) == 112, "layout");

/* --- 서비스 바인딩 --- */
typedef struct NU_SERVICE_BINDING {
    EFI_STATUS (*CreateChild)(struct NU_SERVICE_BINDING *, EFI_HANDLE *child);
    EFI_STATUS (*DestroyChild)(struct NU_SERVICE_BINDING *, EFI_HANDLE child);
} NU_SERVICE_BINDING;

/* --- TCP4 --- */
typedef struct {
    uint8_t  UseDefaultAddress;
    uint8_t  StationAddress[4];
    uint8_t  SubnetMask[4];
    uint16_t StationPort;
    uint8_t  RemoteAddress[4];
    uint16_t RemotePort;
    uint8_t  ActiveFlag;
} NU_TCP4_ACCESS_POINT;

typedef struct {
    uint8_t  TypeOfService;
    uint8_t  TimeToLive;
    NU_TCP4_ACCESS_POINT AccessPoint;
    void    *ControlOption;
} NU_TCP4_CONFIG_DATA;

typedef struct { void *Event; EFI_STATUS Status; } NU_TCP4_COMPLETION_TOKEN;
typedef struct { NU_TCP4_COMPLETION_TOKEN CompletionToken; } NU_TCP4_CONNECTION_TOKEN;
typedef struct { NU_TCP4_COMPLETION_TOKEN CompletionToken; void *Packet; } NU_TCP4_IO_TOKEN;
typedef struct { NU_TCP4_COMPLETION_TOKEN CompletionToken; uint8_t AbortOnClose; } NU_TCP4_CLOSE_TOKEN;

typedef struct { uint32_t FragmentLength; void *FragmentBuffer; } NU_TCP4_FRAGMENT;
typedef struct { uint8_t UrgentFlag; uint32_t DataLength; uint32_t FragmentCount; NU_TCP4_FRAGMENT FragmentTable[1]; } NU_TCP4_RECEIVE_DATA;
typedef struct { uint8_t Push; uint8_t Urgent; uint32_t DataLength; uint32_t FragmentCount; NU_TCP4_FRAGMENT FragmentTable[1]; } NU_TCP4_TRANSMIT_DATA;

typedef struct NU_TCP4_PROTOCOL {
    void *GetModeData;
    EFI_STATUS (*Configure)(struct NU_TCP4_PROTOCOL *, NU_TCP4_CONFIG_DATA *);
    void *Routes;
    EFI_STATUS (*Connect)(struct NU_TCP4_PROTOCOL *, NU_TCP4_CONNECTION_TOKEN *);
    void *Accept;
    EFI_STATUS (*Transmit)(struct NU_TCP4_PROTOCOL *, NU_TCP4_IO_TOKEN *);
    EFI_STATUS (*Receive)(struct NU_TCP4_PROTOCOL *, NU_TCP4_IO_TOKEN *);
    EFI_STATUS (*Close)(struct NU_TCP4_PROTOCOL *, NU_TCP4_CLOSE_TOKEN *);
    EFI_STATUS (*Cancel)(struct NU_TCP4_PROTOCOL *, NU_TCP4_COMPLETION_TOKEN *);
    EFI_STATUS (*Poll)(struct NU_TCP4_PROTOCOL *);
} NU_TCP4_PROTOCOL;

_Static_assert(sizeof(NU_TCP4_ACCESS_POINT) == 20, "layout");
_Static_assert(offsetof(NU_TCP4_CONFIG_DATA, ControlOption) == 24, "layout");

/* --- DNS4 --- */
typedef struct {
    UINTN    DnsServerListCount;
    uint8_t *DnsServerList;           /* EFI_IPv4_ADDRESS 배열 */
    uint8_t  UseDefaultSetting;
    uint8_t  EnableDnsCache;
    uint8_t  Protocol;                /* 17 = UDP */
    uint8_t  StationIp[4];
    uint8_t  SubnetMask[4];
    uint16_t LocalPort;
    uint32_t RetryCount;
    uint32_t RetryInterval;
} NU_DNS4_CONFIG_DATA;

typedef struct { uint32_t IpCount; uint8_t *IpList; } NU_DNS_HOST_TO_ADDR_DATA;

typedef struct {
    void *Event;
    EFI_STATUS Status;
    uint32_t RetryCount;
    uint32_t RetryInterval;
    void *RspData;                    /* 공용체: H2AData / A2HData / GLookupData 중 하나를 가리킴 */
} NU_DNS4_COMPLETION_TOKEN;

typedef struct NU_DNS4_PROTOCOL {
    void *GetModeData;
    EFI_STATUS (*Configure)(struct NU_DNS4_PROTOCOL *, NU_DNS4_CONFIG_DATA *);
    EFI_STATUS (*HostNameToIp)(struct NU_DNS4_PROTOCOL *, CHAR16 *, NU_DNS4_COMPLETION_TOKEN *);
    void *IpToHostName, *GeneralLookUp, *UpdateDnsCache;
    EFI_STATUS (*Poll)(struct NU_DNS4_PROTOCOL *);
    EFI_STATUS (*Cancel)(struct NU_DNS4_PROTOCOL *, NU_DNS4_COMPLETION_TOKEN *);
} NU_DNS4_PROTOCOL;

_Static_assert(sizeof(NU_DNS4_CONFIG_DATA) == 40, "layout");

/* --- UDP4 --- */
typedef struct {
    uint8_t  AcceptBroadcast, AcceptPromiscuous, AcceptAnyPort, AllowDuplicatePort;
    uint8_t  TypeOfService, TimeToLive, DoNotFragment;
    uint32_t ReceiveTimeout, TransmitTimeout;
    uint8_t  UseDefaultAddress;
    uint8_t  StationAddress[4];
    uint8_t  SubnetMask[4];
    uint16_t StationPort;
    uint8_t  RemoteAddress[4];
    uint16_t RemotePort;
} NU_UDP4_CONFIG_DATA;

typedef struct { uint8_t SourceAddress[4]; uint16_t SourcePort; uint8_t DestinationAddress[4]; uint16_t DestinationPort; } NU_UDP4_SESSION_DATA;
typedef struct { uint32_t FragmentLength; void *FragmentBuffer; } NU_UDP4_FRAGMENT_DATA;
typedef struct {
    NU_UDP4_SESSION_DATA *UdpSessionData;
    uint8_t *GatewayAddress;
    uint32_t DataLength;
    uint32_t FragmentCount;
    NU_UDP4_FRAGMENT_DATA FragmentTable[1];
} NU_UDP4_TRANSMIT_DATA;
typedef struct {
    uint8_t  TimeStamp[16];                      /* EFI_TIME */
    void    *RecycleSignal;
    NU_UDP4_SESSION_DATA UdpSession;
    uint32_t DataLength;
    uint32_t FragmentCount;
    NU_UDP4_FRAGMENT_DATA FragmentTable[1];
} NU_UDP4_RECEIVE_DATA;
typedef struct {
    void *Event;
    EFI_STATUS Status;
    union { NU_UDP4_RECEIVE_DATA *RxData; NU_UDP4_TRANSMIT_DATA *TxData; } Packet;
} NU_UDP4_COMPLETION_TOKEN;

typedef struct NU_UDP4_PROTOCOL {
    void *GetModeData;
    EFI_STATUS (*Configure)(struct NU_UDP4_PROTOCOL *, NU_UDP4_CONFIG_DATA *);
    void *Groups, *Routes;
    EFI_STATUS (*Transmit)(struct NU_UDP4_PROTOCOL *, NU_UDP4_COMPLETION_TOKEN *);
    EFI_STATUS (*Receive)(struct NU_UDP4_PROTOCOL *, NU_UDP4_COMPLETION_TOKEN *);
    EFI_STATUS (*Cancel)(struct NU_UDP4_PROTOCOL *, NU_UDP4_COMPLETION_TOKEN *);
    EFI_STATUS (*Poll)(struct NU_UDP4_PROTOCOL *);
} NU_UDP4_PROTOCOL;

_Static_assert(sizeof(NU_UDP4_CONFIG_DATA) == 36, "layout");
_Static_assert(offsetof(NU_UDP4_CONFIG_DATA, StationPort) == 26, "layout");
_Static_assert(sizeof(NU_UDP4_SESSION_DATA) == 12, "layout");
_Static_assert(offsetof(NU_UDP4_RECEIVE_DATA, UdpSession) == 24, "layout");

/* ====== 이 모듈의 상태 ====== */

enum { NU_STAGE_IDLE = 0, NU_STAGE_WAIT = 1, NU_STAGE_UP = 2 };

#define NU_POLL_SETUP   30     /* 주소 기다리는 중: 30번 poll마다 확인 (60 fps면 0.5초) */
#define NU_POLL_UP      300    /* 주소를 받은 뒤: 300번마다 확인 (5초) */
#define NU_RETRY_IDLE   1800   /* NIC를 못 찾았을 때 다시 시도하는 간격 (30초) */

#define NU_RXRAW        2048   /* UEFI에 한 번에 걸어 두는 Receive 버퍼 */
#define NU_TXBUF        2048   /* 연결당 한 번에 보내는 최대 크기 */
#define NU_FIFO         4096   /* 연결당 받은 자료 대기열 */
#define NU_DNS_TMO      5400   /* DNS 질의 제한 시간 (60 fps에서 90초). 펌웨어가 자체 재시도를 하므로 넉넉하게 */
#define NU_CLOSE_TMO    600    /* 닫기 준비 단계 제한 (10초). 넘으면 강제 닫기로 바꾼다 */
#define NU_CLOSED_TMO   1800   /* 닫기 명령 뒤 완료 제한 (30초). 넘으면 연결을 버린다 */
#define NU_UDP_BUF      1500   /* 데이터그램 하나를 담는 크기 */
#define NU_UDP_Q        4      /* UDP 소켓마다 받은 데이터그램을 쌓는 칸 수 (가득 차면 새로 온 것을 버림) */

#define NU_EV_RX        1      /* netuefi_poll 반환값: 받은 자료 도착 또는 상대가 닫음 */
#define NU_EV_STATE     2      /* 연결 상태 바뀜 (연결됨, 끊김 등) */
/* UDP 데이터그램 도착은 NU_EV_RX로 알린다 */

/* 연결 슬롯 상태 */
enum { NU_CS_FREE = 0, NU_CS_CONNECTING, NU_CS_OPEN, NU_CS_CLOSING, NU_CS_DEAD };

typedef struct NuConn {
    int cs;
    EFI_HANDLE child;
    NU_TCP4_PROTOCOL *tcp;
    void *ev_conn, *ev_rx, *ev_tx, *ev_close;
    volatile int f_conn, f_rx, f_tx, f_close;      /* 이벤트 콜백이 올리는 깃발 */
    NU_TCP4_CONNECTION_TOKEN conn_tok;
    NU_TCP4_IO_TOKEN rx_tok, tx_tok;
    NU_TCP4_CLOSE_TOKEN close_tok;
    NU_TCP4_RECEIVE_DATA rxd;
    NU_TCP4_TRANSMIT_DATA txd;
    int rx_pending, tx_pending, conn_pending;
    int close_req, abort_req, fail_req, cancel_sent, close_issued, peer_fin;   /* fail_req: 연결이 깨져서 우리가 정리 중 (이유와 자료는 남김) */
    uint64_t close_since;
    uint8_t reason;
    uint8_t rip[4];               /* TCP_INFO용: 열 때 받은 값 */
    uint16_t rport, lport;
    int q_head, q_count;
    uint8_t rxraw[NU_RXRAW];
    uint8_t txbuf[NU_TXBUF];
    uint8_t fifo[NU_FIFO];
} NuConn;

/* UDP 소켓 상태. DOWN = 주소를 잃어서 UEFI 쪽을 내려놓음 (핸들, 포트, 받은 자료는 유지, 주소가 돌아오면 다시 붙임) */
enum { NU_US_FREE = 0, NU_US_OPEN, NU_US_DOWN };

typedef struct { uint8_t ip[4]; uint16_t port; uint16_t len; uint8_t data[NU_UDP_BUF]; } NuDgram;

typedef struct NuUdp {
    int us;
    EFI_HANDLE child;
    NU_UDP4_PROTOCOL *udp;
    void *ev_rx, *ev_tx;
    volatile int f_rx, f_tx;
    NU_UDP4_COMPLETION_TOKEN rx_tok, tx_tok;
    NU_UDP4_TRANSMIT_DATA txd;
    NU_UDP4_SESSION_DATA txs;
    int rx_pending, tx_pending;
    uint16_t lport;
    uint64_t next_try;
    int q_head, q_count;
    NuDgram q[NU_UDP_Q];
    uint8_t txbuf[NU_UDP_BUF];
} NuUdp;

typedef struct NetUefi {
    EFI_SYSTEM_TABLE *st;
    EFI_HANDLE nic;
    NU_IP4_CONFIG2_PROTOCOL *cfg;
    int stage;
    uint64_t ticks, next;
    int connect_tries;            /* 진단용: ConnectController를 시도한 횟수 */
    EFI_STATUS last_status;       /* 진단용: 마지막으로 실패한 UEFI 호출의 상태 */
    uint8_t info[20];             /* 로컬 IP, 서브넷, 게이트웨이, DNS1, DNS2 */
    uint64_t buf[128];            /* 정렬된 작업 버퍼 (1024바이트) */

    /* TCP */
    NU_SERVICE_BINDING *tcp_sb;
    NuConn conn[NETCARD_MAX_CONN];

    /* UDP */
    NU_SERVICE_BINDING *udp_sb;
    NuUdp udp[NETCARD_MAX_UDP];
    uint16_t eph_udp;
    int pending_ev;               /* netuefi_poll이 다음에 돌려줄 NU_EV_* (망 끊김 등) */

    /* DNS */
    NU_SERVICE_BINDING *dns_sb;
    EFI_HANDLE dns_child;
    NU_DNS4_PROTOCOL *dns;
    int dns_configured;
    uint8_t dns_sig[20];          /* 마지막 Configure 때의 info */
    void *ev_dns;
    volatile int f_dns;
    NU_DNS4_COMPLETION_TOKEN dns_tok;
    int dns_pending, dns_timed_out;
    uint64_t dns_since;
    uint8_t dns_state;            /* NC_DNS_* */
    uint8_t dns_err;              /* NC_DNSERR_* (dns_state가 FAIL일 때) */
    uint16_t eph_next;            /* 내 포트를 0으로 열 때 고르는 임시 포트의 다음 후보 */
    uint8_t dns_ip[4];
    uint8_t dns_servers[8];       /* Configure에 넘기는 목록 (최대 2개) */

    /* 주소 설정 (설정 창). manual = 0 DHCP, 1 수동 */
    int cfg_manual, cfg_dirty;
    uint8_t cfg_ip[4], cfg_mask[4], cfg_gw[4], cfg_dns[4];
    CHAR16  dns_name[256];
} NetUefi;

void netuefi_init(NetUefi *nu, EFI_SYSTEM_TABLE *st);
int  netuefi_poll(NetUefi *nu);                          /* NU_EV_* 비트를 돌려준다. 0이 아니면 netcard_raise_irq */
int  netuefi_state(const NetUefi *nu);                   /* NC_NET_DOWN / SETUP / UP */
int  netuefi_get_ipinfo(const NetUefi *nu, uint8_t out[20]);
void netuefi_set_config(NetUefi *nu, int manual, const uint8_t ip[4], const uint8_t mask[4], const uint8_t gw[4], const uint8_t dns[4]);   /* 5.0.57 */
int  netuefi_online(const NetUefi *nu);
int  netuefi_has_nic(const NetUefi *nu);                 /* UEFI가 랜카드(IP4Config2)를 찾았으면 1 */                  /* 주소를 받아 쓸 수 있으면 1 */
void netuefi_host(NetUefi *nu, NetHost *out);            /* netcard_set_host에 넘길 연결부를 채운다 */

#endif
