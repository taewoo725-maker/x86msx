/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - lwIP 설정 (NO_SYS=1, raw API만). 메모리는 BSS에 고정 (kernel BSS 여유가 작아 합계 약 200 KiB 이내로) */
#ifndef LWIPOPTS_H
#define LWIPOPTS_H
#define NO_SYS                  1
#define SYS_LIGHTWEIGHT_PROT    0
#define LWIP_SOCKET             0
#define LWIP_NETCONN            0
#define LWIP_IPV4               1
#define LWIP_IPV6               0
#define LWIP_ARP                1
#define ARP_QUEUEING            1
#define LWIP_ETHERNET           1
#define LWIP_ICMP               1
#define LWIP_RAW                1
#define MEMP_NUM_RAW_PCB        2
#define LWIP_UDP                1
#define LWIP_TCP                1
#define LWIP_DHCP               1
#define LWIP_DNS                1
#define LWIP_AUTOIP             0
#define LWIP_IGMP               0
#define LWIP_STATS              0
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK   1
#define LWIP_CHECKSUM_ON_COPY   0
#ifdef __x86_64__
#define MEM_ALIGNMENT           8      /* UEFI(64비트) 판 — 포인터가 8바이트 */
#else
#define MEM_ALIGNMENT           4
#endif
#define MEM_SIZE                (96 * 1024)
#define MEMP_NUM_PBUF           16
#define MEMP_NUM_UDP_PCB        6
#define MEMP_NUM_TCP_PCB        6
#define MEMP_NUM_TCP_PCB_LISTEN 1
#define MEMP_NUM_TCP_SEG        40
#define MEMP_NUM_SYS_TIMEOUT    12
#define PBUF_POOL_SIZE          24
#define PBUF_POOL_BUFSIZE       1536
#define TCP_MSS                 1460
#define TCP_WND                 (6 * TCP_MSS)
#define TCP_SND_BUF             (6 * TCP_MSS)
#define TCP_SND_QUEUELEN        (4 * TCP_SND_BUF / TCP_MSS)
#define DNS_TABLE_SIZE          2
#define DNS_MAX_SERVERS         2
#define LWIP_DNS_SUPPORT_MDNS_QUERIES 0
#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS 0
#define LWIP_RAND()             (net_rand())
#define LWIP_NO_UNISTD_H        1
#define LWIP_NO_CTYPE_H         1
#define LWIP_PROVIDE_ERRNO      1
#define LWIP_DEBUG              0
#define LWIP_NETIF_HOSTNAME     1   /* DHCP 에 호스트 이름(옵션 12)을 넣음 — 윈도·리눅스 클라이언트와 같은 모양 */
#ifndef __ASSEMBLER__
extern unsigned int net_rand(void);
#endif
#endif
