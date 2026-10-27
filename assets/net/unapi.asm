; 만든이: KimTaeWoo
; 도와준이: Lee.J.H
; ======================================================================
; X86MSX 네트워크 카트리지 - TCP/IP UNAPI 1.1 구현 ROM (16KB, 페이지 1)
;
; 규격: MSX UNAPI 1.1 / TCP/IP UNAPI 1.1 (Konamiman). 규격 식별자 "TCP/IP".
; 장치: netcard.c의 포트 NETPORT+0..+3 (config.inc). 명령 번호는 netcard.h 참고.
;
; 구현한 함수: 0~18, 25, 27(읽기만), 28(읽기만), 29   (5단계에서 UDP 8~12를 더했다)
; ERR_NOT_IMP를 돌려주는 함수: 19(DISCARD), 20~24(RAW), 26
;
; 어셈블: pasmo --bin unapi.asm unapi.rom unapi.sym
; ======================================================================
        include "config.inc"

P0              equ     NETPORT         ; 상태 / 명령
P1              equ     NETPORT+1       ; 데이터 창
P2              equ     NETPORT+2       ; 인자 / 결과
P3              equ     NETPORT+3       ; 오류 코드 / 인터럽트

; ---- MSX 시스템 영역 ----
RSLREG          equ     0138h
EXPTBL          equ     0FCC1h
SLTTBL          equ     0FCC5h
HIMEM           equ     0FC4Ah
HOKVLD          equ     0FB20h
EXTBIO          equ     0FFCAh
OLDH            equ     7FF0h           ; 카트리지 안의 RAM(7F00h~7FFFh). 이전 EXTBIO 후크 5바이트 사본
ARG             equ     0F847h

; ---- 장치 명령 ----
C_NET_STATE     equ     02h
C_GET_IPINFO    equ     03h
C_DNS_START     equ     10h
C_DNS_RESULT    equ     11h
C_DNS_CANCEL    equ     12h
C_UDP_OPEN      equ     30h
C_UDP_CLOSE     equ     31h
C_UDP_STATE     equ     32h
C_UDP_SEND      equ     33h
C_UDP_RECV      equ     34h
C_TCP_OPEN      equ     20h
C_TCP_CLOSE     equ     21h
C_TCP_ABORT     equ     22h
C_TCP_STATE     equ     23h
C_TCP_SEND      equ     24h
C_TCP_RECV      equ     25h
C_TCP_INFO      equ     27h

; ---- UNAPI 오류 코드 ----
ERR_OK          equ     0
ERR_NOT_IMP     equ     1
ERR_NO_NETWORK  equ     2
ERR_NO_DATA     equ     3
ERR_INV_PARAM   equ     4
ERR_QUERY_EXISTS equ    5
ERR_INV_IP      equ     6
ERR_NO_DNS      equ     7
ERR_DNS         equ     8
ERR_NO_FREE_CONN equ    9
ERR_CONN_EXISTS equ     10
ERR_NO_CONN     equ     11
ERR_CONN_STATE  equ     12
ERR_BUFFER      equ     13
ERR_LARGE_DGRAM equ     14

NCONN           equ     4               ; 연결 수 (netcard.h의 NETCARD_MAX_CONN)
NUDP            equ     4               ; UDP 연결 수 (netcard.h의 NETCARD_MAX_UDP). CONNH가 NCONN으로 검사하므로 같아야 한다
UDP_RETRIES     equ     8               ; UDP_SEND가 앞 전송이 끝나기를 기다리는 횟수 (프레임 단위)
UDP_MAXDG       equ     1472            ; 보낼 수 있는 데이터그램 최대 크기
UDP_MAXRX       equ     1500            ; 한 번에 받을 수 있는 최대 크기
DNS_RETRIES     equ     8

        org     4000h
; ---- 카트리지 머리말 ----
        db      "AB"
        dw      INIT2                   ; INIT (아래). HIMEM 을 건드리지 않는다 (예전 RAM 조각 방식 삭제)
        dw      0                       ; STATEMENT
        dw      0                       ; DEVICE
        dw      0                       ; TEXT
        ds      6,0

; 페이지 1이 있는 슬롯을 CALSLT 형식(F000SSPP)으로 돌려준다.
GETSLOT:
        in      a,(0A8h)                ; BIOS(0138h) 대신 포트를 직접 읽는다. DOS 중에는 페이지 0이 RAM이라 BIOS를 부를 수 없다
        nop
        rrca
        rrca
        and     3
        ld      c,a
        ld      b,0
        ld      hl,EXPTBL
        add     hl,bc
        ld      a,(hl)
        and     80h
        or      c
        ld      c,a
        inc     hl
        inc     hl
        inc     hl
        inc     hl                      ; SLTTBL = EXPTBL + 4
        ld      a,(hl)
        and     0Ch
        or      c
        ret

; ======================================================================
; 진입점: A = 함수 번호
; ======================================================================
ENTRY:
        cp      30
        ret     nc                      ; 배정되지 않은 번호: 아무것도 하지 않음
        push    hl
        ld      hl,JTAB
        add     a,a
        add     a,l
        ld      l,a
        jr      nc,en1
        inc     h
en1:    ld      a,(hl)
        inc     hl
        ld      h,(hl)
        ld      l,a
        ex      (sp),hl                 ; HL 복원, 스택 맨 위는 목적지
        ret

JTAB:   dw      R_INFO                  ; 0
        dw      R_CAPAB                 ; 1
        dw      R_IPINFO                ; 2
        dw      R_NETSTATE              ; 3
        dw      R_SENDECHO              ; 4  SEND_ECHO (5.0.131)
        dw      R_RCVECHO               ; 5  RCV_ECHO (5.0.131)
        dw      R_DNSQ                  ; 6
        dw      R_DNSS                  ; 7
        dw      R_UDPOPEN               ; 8
        dw      R_UDPCLOSE              ; 9
        dw      R_UDPSTATE              ; 10
        dw      R_UDPSEND               ; 11
        dw      R_UDPRCV                ; 12
        dw      R_TCPOPEN               ; 13
        dw      R_TCPCLOSE              ; 14
        dw      R_TCPABORT              ; 15
        dw      R_TCPSTATE              ; 16
        dw      R_TCPSEND               ; 17
        dw      R_TCPRCV                ; 18
        dw      R_NOTIMP                ; 19 TCP_DISCARD
        dw      R_NOTIMP                ; 20 RAW_OPEN
        dw      R_NOTIMP                ; 21
        dw      R_NOTIMP                ; 22
        dw      R_NOTIMP                ; 23
        dw      R_NOTIMP                ; 24
        dw      R_CFGAUTO               ; 25
        dw      R_NOTIMP                ; 26 CONFIG_IP
        dw      R_CFGTTL                ; 27
        dw      R_CFGPING               ; 28
        dw      R_WAIT                  ; 29

; ---- 공통 ----
R_NOTIMP:
        ld      a,ERR_NOT_IMP
        ret
RT_INV:
        ld      a,ERR_INV_PARAM
        ret

; 장치 명령 실행: A = 명령. BUSY가 풀리기를 기다려 A = 장치 오류 코드(0 = 성공)를 돌려준다.
; BC, DE, HL은 건드리지 않는다. 호출한 쪽이 OR A로 확인한다.
DEVCMD:
        out     (P0),a
dc1:    in      a,(P0)
        bit     7,a
        jr      nz,dc1
        bit     3,a
        ld      a,0
        ret     z
        in      a,(P3)
        ret

; 장치 오류 코드(A)를 UNAPI 오류로 바꾼다. HL = 표. 8 이상은 ERR_NO_NETWORK.
MAPERR:
        cp      10
        jr      c,mp1
        ld      a,ERR_NO_NETWORK
        ret
mp1:    add     a,l
        ld      l,a
        jr      nc,mp2
        inc     h
mp2:    ld      a,(hl)
        ret

;            장치: 0 OK, 1 BADCMD, 2 BADARG, 3 NOHANDLE, 4 NETDOWN, 5 TIMEOUT, 6 HOST, 7 NOSLOT, 8 INUSE, 9 NODATA
TBL_TCP: db     0, ERR_NOT_IMP, ERR_INV_PARAM, ERR_NO_CONN, ERR_NO_NETWORK, ERR_NO_NETWORK, ERR_NO_NETWORK, ERR_NO_FREE_CONN, ERR_CONN_EXISTS, ERR_NO_DATA
TBL_DNS: db     0, ERR_NOT_IMP, ERR_INV_PARAM, ERR_INV_PARAM, ERR_NO_NETWORK, ERR_NO_NETWORK, ERR_NO_DNS, ERR_QUERY_EXISTS, ERR_INV_PARAM, ERR_NO_NETWORK

TCP_ERR:
        ld      hl,TBL_TCP
        jp      MAPERR

; B = 연결 번호(1~4) -> A = 핸들(0~3). 잘못되면 A = ERR_NO_CONN, 캐리 세트.
CONNH:
        ld      a,b
        dec     a
        cp      NCONN
        jr      nc,ch_bad
        or      a
        ret
ch_bad: ld      a,ERR_NO_CONN
        scf
        ret

; 한 프레임(약 8만 T-state)쯤 기다린다. BC 보존.
DELAY:
        push    bc
        ld      b,20
dl1:    ld      c,0
dl2:    dec     c
        jr      nz,dl2
        djnz    dl1
        pop     bc
        ret

; ---- 0: 구현 정보 ----
R_INFO:
        ld      hl,NAME
        ld      de,0101h                ; 규격 1.1
        ld      bc,0006h                ; 구현 0.6
        xor     a
        ret
NAME:   db      "x86MSX UEFI TCP/IP",0

; ---- 1: 기능 ----
R_CAPAB:
        ld      a,b
        dec     a
        jr      z,cap1
        dec     a
        jr      z,cap2
        dec     a
        jr      z,cap3
        dec     a
        jr      z,cap4
        ld      a,ERR_INV_PARAM
        ret
cap1:   ld      hl,0C48Dh               ; PING(비트0), DNS, UDP, 능동 TCP, 명시적 PUSH, 자동 IP(옛 비트), TTL/ToS 읽기
        ld      de,0010h                ; 하드웨어(UEFI)가 TCP/IP를 처리
        ld      b,0                     ; 링크 종류: 기타
        xor     a
        ret
cap2:   ld      d,0                     ; 빈 TCP 연결 수를 센다
        ld      e,0
c2l:    ld      a,e
        out     (P2),a
        ld      a,C_TCP_STATE
        call    DEVCMD
        or      a
        jr      z,c2n
        inc     d
c2n:    inc     e
        ld      a,e
        cp      NCONN
        jr      c,c2l
        ld      e,0                     ; 빈 UDP 연결 수를 센다
        ld      h,0
c2u:    ld      a,h
        out     (P2),a
        ld      a,C_UDP_STATE
        call    DEVCMD
        or      a
        jr      z,c2v
        inc     e
c2v:    inc     h
        ld      a,h
        cp      NUDP
        jr      c,c2u
        ld      b,NCONN
        ld      c,NUDP
        ld      hl,0
        xor     a
        ret
cap3:   ld      hl,UDP_MAXRX
        ld      de,UDP_MAXRX
        xor     a
        ret
cap4:   ld      hl,0003h                ; 자동 로컬 IP, 자동 DNS
        ld      de,0
        xor     a
        ret

; ---- 2: IP 정보 ----
R_IPINFO:
        ld      a,b
        or      a
        jp      z,RT_INV
        cp      7
        jp      nc,RT_INV
        ld      c,a                     ; 번호 1~6
        ld      a,C_GET_IPINFO
        call    DEVCMD
        or      a
        jr      nz,ip_zero
        ld      a,c
        cp      1
        jr      z,ip_rd
        cp      2
        jr      z,ip_zero               ; 상대방 IP: 점대점이 아니므로 0.0.0.0
        sub     2
        add     a,a
        add     a,a                     ; (번호-2) * 4바이트를 건너뛴다
        ld      b,a
ip_sk:  in      a,(P2)
        djnz    ip_sk
ip_rd:  ld      c,P2
        in      l,(c)
        in      h,(c)
        in      e,(c)
        in      d,(c)
        xor     a
        ret
ip_zero:
        ld      hl,0
        ld      d,h
        ld      e,l
        xor     a
        ret

; ---- 3: 네트워크 상태 ----
R_NETSTATE:
        ld      a,C_NET_STATE
        call    DEVCMD
        or      a
        jr      nz,ns_unk
        in      a,(P2)
        ld      b,a                     ; 0 닫힘, 1 여는 중, 2 열림: 번호가 UNAPI와 같다
        xor     a
        ret
ns_unk: ld      b,255
        xor     a
        ret

; ---- 6: DNS 질의 ----
; HL = 이름, B = 플래그 (bit0 현재 질의만 취소, bit1 IP 주소여야 함, bit2 기존 질의를 취소하지 않음)
R_DNSQ:
        ld      a,b
        and     0F8h
        jp      nz,RT_INV
        bit     0,b
        jr      z,dq_nab
        ld      a,C_DNS_CANCEL
        call    DEVCMD
        ld      b,0
        xor     a
        ret
dq_nab: push    bc
        push    hl
        call    PARSEIP                 ; A=0: IP 주소(L.H.E.D)  1: 호스트 이름  2: IP 모양인데 잘못됨
        or      a
        jr      z,dq_ip
        cp      2
        jr      z,dq_badip
        pop     hl
        pop     bc
        bit     1,b
        ld      a,ERR_INV_IP
        ret     nz
        push    hl                      ; 이름 길이를 센다
        ld      c,0
dq_len: ld      a,(hl)
        or      a
        jr      z,dq_ldone
        inc     hl
        inc     c
        jr      nz,dq_len
        pop     hl                      ; 255자를 넘음
        jp      RT_INV
dq_ldone:
        pop     hl
        ld      a,c
        or      a
        jp      z,RT_INV                ; 빈 이름
        bit     2,b
        jr      nz,dq_keep
        push    bc                      ; 기존 질의를 취소한다
        push    hl
        ld      a,C_DNS_CANCEL
        call    DEVCMD
        pop     hl
        pop     bc
        jr      dq_go
dq_keep:
        push    bc                      ; 질의가 진행 중이면 ERR_QUERY_EXISTS
        push    hl
        ld      a,C_DNS_RESULT
        call    DEVCMD
        or      a
        jr      nz,dq_kerr
        in      a,(P2)
        pop     hl
        pop     bc
        or      a
        ld      a,ERR_QUERY_EXISTS
        ret     z
        jr      dq_go
dq_kerr:
        pop     hl
        pop     bc
        ld      hl,TBL_DNS
        jp      MAPERR
dq_go:  ld      d,DNS_RETRIES
dq_try: push    bc
        push    hl
        ld      a,c
        out     (P2),a
        ld      b,c
        ld      c,P2
        otir                            ; 이름을 보낸다
        ld      a,C_DNS_START
        call    DEVCMD
        pop     hl
        pop     bc
        or      a
        jr      z,dq_ok
        cp      7                       ; NOSLOT: 취소가 아직 끝나지 않았을 수 있다
        jr      nz,dq_err
        bit     2,b
        jr      nz,dq_err
        dec     d
        jr      z,dq_err
        call    DELAY
        jr      dq_try
dq_ok:  ld      b,0                     ; 질의 진행 중
        xor     a
        ret
dq_err: ld      hl,TBL_DNS
        jp      MAPERR
dq_ip:  jp      DQ_IP2                  ; 결과를 저장해야 해서 ROM 끝쪽 루틴으로 (뒤 주소들을 안 바꾸려고 자리만 채움)
        db      01h,0AFh,0C9h           ; 예전 코드의 남은 바이트 (실행되지 않음)
dq_badip:
        pop     bc
        pop     bc
        ld      a,ERR_INV_IP
        ret

; HL = 0으로 끝나는 문자열. 점으로 나눈 10진수 네 개이면 A=0, L.H.E.D = 주소.
; 숫자와 점 말고 다른 글자가 있으면 A=1 (호스트 이름). 숫자와 점뿐인데 모양이 틀리면 A=2.
PARSEIP:
        ld      a,(hl)
        or      a
        jr      z,pi_not
        push    hl
pi_1:   ld      a,(hl)
        or      a
        jr      z,pi_1ok
        cp      '.'
        jr      z,pi_1n
        sub     '0'
        cp      10
        jr      nc,pi_not1
pi_1n:  inc     hl
        jr      pi_1
pi_not1:
        pop     hl
pi_not: ld      a,1
        ret
pi_1ok: pop     hl
        push    bc                      ; 결과 네 바이트를 스택에 쌓는다
        push    bc
        ld      iy,0
        add     iy,sp
        ld      c,0                     ; 몇 번째 수인가
pi_oct: ld      b,0                     ; 자릿수
        ld      d,0                     ; 값
pi_dig: ld      a,(hl)
        sub     '0'
        cp      10
        jr      nc,pi_end
        ld      e,a
        ld      a,d
        cp      26
        jp      nc,pi_bad
        cp      25
        jr      nz,pi_m
        ld      a,e
        cp      6
        jr      nc,pi_bad               ; 255 초과
pi_m:   ld      a,d                     ; D = D*10 + E
        add     a,a
        ld      d,a
        add     a,a
        add     a,a
        add     a,d
        add     a,e
        ld      d,a
        inc     b
        ld      a,b
        cp      4
        jr      nc,pi_bad               ; 네 자리 이상
        inc     hl
        jr      pi_dig
pi_end: ld      a,b
        or      a
        jr      z,pi_bad                ; 빈 칸
        ld      a,d
        push    hl
        ld      l,c                     ; (IY + C) = D
        ld      h,0
        push    iy
        pop     de
        add     hl,de
        ld      (hl),a
        pop     hl
        ld      a,(hl)
        inc     c
        or      a
        jr      z,pi_fin
        cp      '.'
        jr      nz,pi_bad
        inc     hl
        ld      a,c
        cp      4
        jr      c,pi_oct
pi_bad: pop     bc
        pop     bc
        ld      a,2
        ret
pi_fin: ld      a,c
        cp      4
        jr      nz,pi_bad
        pop     hl                      ; L = 첫째 수, H = 둘째 수
        pop     de                      ; E = 셋째 수, D = 넷째 수
        xor     a
        ret

; ---- 7: DNS 상태 ----
R_DNSS:
        push    bc                      ; B = 플래그 (bit0: 읽은 뒤 결과 지움)
        ld      a,C_DNS_RESULT
        call    DEVCMD
        or      a
        jr      nz,ds_err
        ld      c,P2
        in      a,(c)                   ; 상태
        ld      b,a
        in      l,(c)
        in      h,(c)
        in      e,(c)
        in      d,(c)
        ld      a,b
        pop     bc
        or      a
        jr      z,ds_pend
        cp      1
        jr      z,ds_ok
        cp      2
        jr      z,ds_fail
        ld      b,0                     ; 질의 없음
        ld      c,0
        xor     a
        ret
ds_pend:
        ld      b,1
        ld      c,0
        xor     a
        ret
ds_ok:  bit     0,b
        jr      z,dsk
        ld      a,C_DNS_CANCEL
        call    DEVCMD
dsk:    ld      b,2
        ld      c,0
        xor     a
        ret
ds_fail:
        in      a,(P2)                  ; 장치가 알려 주는 실패 이유 (UNAPI DNS 오류 코드)
        ld      c,a
        bit     0,b
        jr      z,dsf
        ld      a,C_DNS_CANCEL          ; DEVCMD는 BC를 건드리지 않는다
        call    DEVCMD
dsf:    ld      b,c                     ; B = DNS 오류 코드
        ld      c,0
        ld      a,ERR_DNS
        ret
ds_err: pop     bc
        ld      hl,TBL_DNS
        jp      MAPERR

; ---- 13: TCP 열기 ----
; HL = 변수 블록: +0 상대 IP(4) +4 상대 포트(2) +6 내 포트(2) +8 시간 제한(2) +10 플래그 +11 이름 주소(2)
R_TCPOPEN:
        push    hl
        pop     ix
        ld      a,(ix+10)
        and     0F0h
        jp      nz,RT_INV
        ld      a,(ix+10)
        and     0Dh                     ; 수동, TLS, 인증서 확인
        jp      nz,R_NOTIMP
        ld      e,(ix+8)
        ld      d,(ix+9)
        ld      a,d
        or      e
        jr      z,to_ok                 ; 0 = 기본값
        ld      a,d
        and     e
        inc     a
        jr      z,to_ok                 ; FFFFh = 무한
                                ; 1080초를 넘는 시간 제한은 거절하지 않고 받아들인다 (FH 가 7363 같은 값을 넘김 - 실제 장치들도 너그럽게 받음)
to_ok:  ld      a,(ix+0)
        or      (ix+1)
        or      (ix+2)
        or      (ix+3)
        jp      z,RT_INV                ; 능동 모드에서 0.0.0.0은 안 됨
        ld      e,(ix+6)
        ld      d,(ix+7)
        ld      a,d
        and     e
        inc     a
        jr      nz,lp_ok
        ld      de,0                    ; FFFFh = 임의 포트: 장치가 고른다
lp_ok:  ld      a,e
        out     (P2),a
        ld      a,d
        out     (P2),a
        ld      a,(ix+0)
        out     (P2),a
        ld      a,(ix+1)
        out     (P2),a
        ld      a,(ix+2)
        out     (P2),a
        ld      a,(ix+3)
        out     (P2),a
        ld      a,(ix+4)
        out     (P2),a
        ld      a,(ix+5)
        out     (P2),a
        xor     a
        out     (P2),a                  ; 장치 플래그: 능동
        ld      a,C_TCP_OPEN
        call    DEVCMD
        or      a
        jp      nz,TCP_ERR
        in      a,(P2)
        inc     a
        ld      b,a                     ; 연결 번호 = 핸들 + 1
        xor     a
        ret

; ---- 14, 15: TCP 닫기 / 끊기 (B = 0이면 모두) ----
R_TCPCLOSE:
        ld      c,C_TCP_CLOSE
        jr      tc_com
R_TCPABORT:
        ld      c,C_TCP_ABORT
tc_com: ld      a,b
        or      a
        jr      z,tc_all
        call    CONNH
        ret     c
        out     (P2),a
        ld      a,c
        call    DEVCMD
        or      a
        jp      nz,TCP_ERR
        ret
tc_all: ld      e,0
tca:    ld      a,e
        out     (P2),a
        ld      a,c
        call    DEVCMD
        inc     e
        ld      a,e
        cp      NCONN
        jr      c,tca
        xor     a
        ret

; ---- 16: TCP 상태 ----
; B = 연결 번호, HL = 정보 블록(8바이트) 주소 또는 0
; 결과: B = 상태, C = 플래그(0) 또는 닫힌 이유, HL = 받을 수 있는 자료, DE = 0, IX = 보낼 수 있는 공간
R_TCPSTATE:
        call    CONNH
        ret     c
        ld      e,a                     ; E = 핸들
        ld      a,h
        or      l
        jr      z,ts_noinfo
        ld      a,e
        out     (P2),a
        ld      a,C_TCP_INFO
        call    DEVCMD
        or      a
        jp      nz,TCP_ERR
        ld      b,8
        ld      c,P2
        inir
ts_noinfo:
        ld      a,e
        out     (P2),a
        ld      a,C_TCP_STATE
        call    DEVCMD
        or      a
        jp      nz,TCP_ERR
        in      a,(P2)                  ; 장치 상태 (UEFI 번호)
        cp      11
        jr      c,ts1
        xor     a
ts1:    ld      e,a
        ld      d,0
        ld      hl,STMAP
        add     hl,de
        ld      d,(hl)                  ; D = UNAPI 상태
        in      a,(P2)
        ld      l,a
        in      a,(P2)
        ld      h,a                     ; HL = 받을 수 있는 자료
        in      a,(P2)
        ld      c,a
        in      a,(P2)
        ld      b,a
        push    bc
        pop     ix                      ; IX = 보낼 수 있는 공간
        in      a,(P2)                  ; 닫힌 이유 (장치 번호)
        ld      c,0
        ld      e,a
        ld      a,d
        or      a
        jr      nz,ts_out               ; 닫힌 상태가 아니면 C = 0
        ld      a,e
        cp      6
        jr      c,ts2
        ld      a,4
ts2:    ld      e,a
        ld      d,0
        push    hl
        ld      hl,RSNMAP
        add     hl,de
        ld      c,(hl)
        pop     hl
        ld      d,0
        xor     a
        ld      b,a                     ; B = 닫힘(0)
        ld      e,0
        ret
ts_out: ld      b,d
        ld      d,0
        ld      e,0
        xor     a
        ret

;        UEFI 상태: 0 닫힘 1 LISTEN 2 SYN-SENT 3 SYN-RCVD 4 ESTABLISHED 5 FIN-WAIT1 6 FIN-WAIT2 7 CLOSING 8 TIME-WAIT 9 CLOSE-WAIT 10 LAST-ACK
STMAP:  db      0, 1, 2, 3, 4, 5, 6, 8, 10, 7, 9
;        장치 이유: 0 없음(정상 종료) 1 거부/리셋 2 시간 초과 3 닿지 않음 4 기타 5 망 끊김 -> UNAPI 닫힌 이유
RSNMAP: db      2, 4, 6, 8, 0, 7        ; (5 = 망 끊김 -> 7)

; ---- 17: TCP 보내기 ----
; B = 연결, DE = 자료 주소, HL = 길이, C = 플래그 (bit0 PUSH, bit1 긴급(무시))
R_TCPSEND:
        ld      a,c
        and     0FCh
        jp      nz,RT_INV
        call    CONNH
        ret     c
        ld      b,a                     ; B = 핸들
        ld      a,c
        and     1
        ld      c,a                     ; C = PUSH
        push    bc                      ; IX+4: PUSH, IX+5: 핸들
        push    hl                      ; IX+2: 길이
        push    de                      ; IX+0: 자료 주소
        ld      ix,0
        add     ix,sp
        ld      a,b
        out     (P2),a
        ld      a,C_TCP_STATE
        call    DEVCMD
        or      a
        jp      nz,sd_deverr
        in      a,(P2)
        ld      b,a                     ; 장치 상태
        in      a,(P2)
        in      a,(P2)
        in      a,(P2)
        ld      e,a
        in      a,(P2)
        ld      d,a                     ; DE = 보낼 수 있는 공간
        ld      a,b
        cp      4
        jr      z,sd_st
        cp      9
        ld      a,ERR_CONN_STATE
        jr      nz,sd_exit
sd_st:  ld      l,(ix+2)
        ld      h,(ix+3)
        ld      a,h
        or      l
        jr      z,sd_zero
        or      a
        sbc     hl,de                   ; 길이 - 공간
        jr      z,sd_fit
        jr      nc,sd_big
sd_fit: ld      l,(ix+0)
        ld      h,(ix+1)
        ld      e,(ix+2)
        ld      d,(ix+3)
sd_lp:  ld      a,d
        or      e
        jr      z,sd_sent
        ld      a,d
        or      a
        jr      nz,sd_256
        ld      b,e
        jr      sd_go
sd_256: ld      b,0
sd_go:  ld      c,P1
        otir
        ld      a,d
        or      a
        jr      z,sd_lo
        dec     d
        jr      sd_lp
sd_lo:  ld      e,0
        jr      sd_lp
sd_sent:
        ld      a,(ix+5)
        out     (P2),a
        ld      a,(ix+2)
        out     (P2),a
        ld      a,(ix+3)
        out     (P2),a
        ld      a,(ix+4)
        out     (P2),a
        ld      a,C_TCP_SEND
        call    DEVCMD
        or      a
        jr      nz,sd_deverr
        in      a,(P2)
        ld      c,a
        in      a,(P2)
        ld      b,a                     ; BC = 장치가 받은 바이트 수
        ld      l,(ix+2)
        ld      h,(ix+3)
        or      a
        sbc     hl,bc
        ld      a,ERR_BUFFER
        jr      nz,sd_exit
sd_zero:
        xor     a
        jr      sd_exit
sd_big: ld      a,ERR_BUFFER
        jr      sd_exit
sd_deverr:
        ld      hl,TBL_TCP
        call    MAPERR
sd_exit:
        pop     bc
        pop     bc
        pop     bc
        ret

; ---- 18: TCP 받기 ----
; B = 연결, DE = 목적지 주소, HL = 최대 길이. 결과: BC = 받은 바이트 수, HL = 0 (긴급 자료 없음)
R_TCPRCV:
        call    CONNH
        ret     c
        push    de                      ; 목적지
        ld      d,a                     ; D = 핸들
        ld      a,h
        cp      8
        jr      c,rv_lim
        ld      hl,0800h                ; 한 번에 2048바이트까지
rv_lim: ld      a,h
        or      l
        jr      z,rv_none
        ld      a,d
        out     (P2),a
        ld      a,l
        out     (P2),a
        ld      a,h
        out     (P2),a
        ld      a,C_TCP_RECV
        call    DEVCMD
        or      a
        jr      nz,rv_err
        in      a,(P2)
        ld      e,a
        in      a,(P2)
        ld      d,a                     ; DE = 받은 바이트 수
        ld      b,d
        ld      c,e
        pop     hl                      ; 목적지
        push    bc
rv_lp:  ld      a,d
        or      e
        jr      z,rv_done
        ld      a,d
        or      a
        jr      nz,rv_256
        ld      b,e
        jr      rv_go
rv_256: ld      b,0
rv_go:  ld      c,P1
        inir
        ld      a,d
        or      a
        jr      z,rv_lo
        dec     d
        jr      rv_lp
rv_lo:  ld      e,0
        jr      rv_lp
rv_done:
        pop     bc
        ld      hl,0
        xor     a
        ret
rv_none:
        pop     de
        ld      bc,0
        ld      hl,0
        xor     a
        ret
rv_err: pop     de
        jp      TCP_ERR

; ======================================================================
; UDP (5단계)
; ======================================================================

; ---- 8: UDP 열기 ----
; HL = 내 포트 (FFFFh = 임의), B = 수명 (모두 임시로 취급). 결과: B = 연결 번호
R_UDPOPEN:
        ld      a,h
        cp      0FFh
        jr      nz,uo_chk
        ld      a,l
        cp      0F0h
        jr      c,uo_chk                ; FFxx이지만 FFF0 미만: 보통 포트
        inc     a
        jr      z,uo_rand               ; FFFF: 임의 포트
        jp      RT_INV                  ; FFF0~FFFE
uo_chk: ld      a,h
        or      l
        jp      z,RT_INV                ; 0
        jr      uo_go
uo_rand:
        ld      hl,0
uo_go:  ld      a,l
        out     (P2),a
        ld      a,h
        out     (P2),a
        ld      a,C_UDP_OPEN
        call    DEVCMD
        or      a
        jp      nz,TCP_ERR
        in      a,(P2)
        inc     a
        ld      b,a
        xor     a
        ret

; ---- 9: UDP 닫기 ----
; B = 연결 번호, 0 = 모든 임시 연결
R_UDPCLOSE:
        ld      a,b
        or      a
        jr      z,uc_all
        call    CONNH
        ret     c
        out     (P2),a
        ld      a,C_UDP_CLOSE
        call    DEVCMD
        or      a
        ret     z
        jp      TCP_ERR
uc_all: ld      e,0
uca:    ld      a,e
        out     (P2),a
        ld      a,C_UDP_CLOSE
        call    DEVCMD                  ; 빈 슬롯의 오류는 무시
        inc     e
        ld      a,e
        cp      NUDP
        jr      c,uca
        xor     a
        ret

; ---- 10: UDP 상태 ----
; B = 연결 번호. 결과: HL = 내 포트, B = 받은 데이터그램 수, DE = 가장 오래된 것의 크기
R_UDPSTATE:
        call    CONNH
        ret     c
        out     (P2),a
        ld      a,C_UDP_STATE
        call    DEVCMD
        or      a
        jp      nz,TCP_ERR
        ld      c,P2
        in      b,(c)
        in      e,(c)
        in      d,(c)
        in      l,(c)
        in      h,(c)
        xor     a
        ret

; ---- 11: UDP 보내기 ----
; B = 연결 번호, HL = 자료 주소, DE = 변수 블록: +0 상대 IP(4) +4 상대 포트(2) +6 길이(2)
R_UDPSEND:
        call    CONNH
        ret     c
        ld      c,a                     ; C = 핸들
        ld      b,UDP_RETRIES           ; B = 남은 시도 횟수
        push    bc                      ; IX+4: 핸들, IX+5: 시도 횟수
        push    de                      ; IX+2: 변수 블록
        push    hl                      ; IX+0: 자료 주소
        ld      ix,0
        add     ix,sp
        ld      l,(ix+2)
        ld      h,(ix+3)
        ld      de,6
        add     hl,de
        ld      e,(hl)
        inc     hl
        ld      d,(hl)                  ; DE = 길이
        ld      hl,UDP_MAXDG
        or      a
        sbc     hl,de
        ld      a,ERR_LARGE_DGRAM
        jr      c,us_exit
us_try: ld      l,(ix+2)                ; 데이터 창에 자료를 쓴다 (장치는 SEND 한 번에만 쓰므로 시도마다 다시)
        ld      h,(ix+3)
        ld      de,6
        add     hl,de
        ld      e,(hl)
        inc     hl
        ld      d,(hl)
        ld      l,(ix+0)
        ld      h,(ix+1)
us_lp:  ld      a,d
        or      e
        jr      z,us_w
        ld      a,d
        or      a
        jr      nz,us_256
        ld      b,e
        jr      us_go
us_256: ld      b,0
us_go:  ld      c,P1
        otir
        ld      a,d
        or      a
        jr      z,us_lo
        dec     d
        jr      us_lp
us_lo:  ld      e,0
        jr      us_lp
us_w:   ld      a,(ix+4)
        out     (P2),a                  ; 핸들
        ld      l,(ix+2)
        ld      h,(ix+3)
        ld      b,8                     ; IP(4) + 포트(2) + 길이(2)가 장치 인자 순서와 같다
        ld      c,P2
        otir
        ld      a,C_UDP_SEND
        call    DEVCMD
        or      a
        jr      z,us_ok
        cp      7                       ; NOSLOT: 앞 전송이 아직 안 끝났다
        jr      nz,us_dev
        dec     (ix+5)
        jr      z,us_buf
        call    DELAY
        jr      us_try
us_buf: ld      a,ERR_BUFFER
        jr      us_exit
us_dev: ld      hl,TBL_TCP
        call    MAPERR
        jr      us_exit
us_ok:  xor     a
us_exit:
        pop     hl
        pop     hl
        pop     hl
        ret

; ---- 12: UDP 받기 ----
; B = 연결 번호, HL = 목적지, DE = 최대 길이 (FFFFh = 전부). 결과: L.H.E.D = 보낸 IP, IX = 보낸 포트, BC = 크기
R_UDPRCV:
        call    CONNH
        ret     c
        push    hl                      ; 목적지
        ld      hl,UDP_MAXRX
        or      a
        sbc     hl,de
        jr      nc,ur_lim               ; DE <= 최대
        ld      de,UDP_MAXRX
ur_lim: out     (P2),a                  ; 핸들 (A는 그대로)
        ld      a,e
        out     (P2),a
        ld      a,d
        out     (P2),a
        ld      a,C_UDP_RECV
        call    DEVCMD
        or      a
        jr      nz,ur_err
        ld      c,P2
        in      l,(c)
        in      h,(c)
        in      e,(c)
        in      d,(c)
        push    hl                      ; IP 앞 두 바이트
        push    de                      ; IP 뒤 두 바이트
        in      l,(c)
        in      h,(c)
        push    hl                      ; 포트
        in      e,(c)
        in      d,(c)                   ; DE = 받은 크기
        push    de
        ld      hl,8
        add     hl,sp
        ld      a,(hl)
        inc     hl
        ld      h,(hl)
        ld      l,a                     ; HL = 목적지
ur_lp:  ld      a,d
        or      e
        jr      z,ur_done
        ld      a,d
        or      a
        jr      nz,ur_256
        ld      b,e
        jr      ur_go
ur_256: ld      b,0
ur_go:  ld      c,P1
        inir
        ld      a,d
        or      a
        jr      z,ur_lo
        dec     d
        jr      ur_lp
ur_lo:  ld      e,0
        jr      ur_lp
ur_done:
        pop     bc                      ; BC = 크기
        pop     ix                      ; IX = 보낸 포트
        pop     de
        pop     hl
        pop     af                      ; 목적지는 버린다
        xor     a
        ret
ur_err: pop     hl
        jp      TCP_ERR

; ---- 25: 자동 설정 (읽기만) ----
R_CFGAUTO:
        ld      a,b
        or      a
        jr      z,ca_get
        cp      1
        jp      z,R_NOTIMP
        jp      RT_INV
ca_get: ld      c,3                     ; 자동 IP + 자동 DNS
        xor     a
        ret

; ---- 27: TTL / ToS (읽기만) ----
R_CFGTTL:
        ld      a,b
        or      a
        jr      z,ct_get
        cp      1
        jp      z,R_NOTIMP
        jp      RT_INV
ct_get: ld      d,64
        ld      e,0
        xor     a
        ret

; ---- 28: 자동 PING 응답 (읽기만) ----
R_CFGPING:
        ld      a,b
        or      a
        jr      z,cp_get
        cp      1
        jp      z,R_NOTIMP
        jp      RT_INV
cp_get: ld      c,0
        xor     a
        ret

; ---- 29: 처리 한 번이 끝나기를 기다린다 ----
; 에뮬레이터는 프레임마다 네트워크를 처리하므로, 다음 인터럽트(프레임)까지 기다린다.
R_WAIT:
        ei
        halt
        xor     a
        ret

; ======================================================================
; INIT2 - 헤더가 가리키는 새 INIT.
; 기존 INIT은 EXTBIO 처리부를 RAM에 두려고 HIMEM을 낮췄는데, A1GT 같은 기종은 HIMEM이 F380이 아니면
; 디스크 롬 초기화를 건너뛰어 Disk BASIC이 뜨지 않았다. 새 INIT은 RAM을 쓰지 않는다:
; EXTBIO 후크를 슬롯 간 호출(RST 30h, 슬롯, 주소)로 걸어 ROM의 EXT_RT가 바로 처리한다.
; 이전 후크가 없을 때(HOKVLD 0비트가 0 = RET 다섯 개)만 이 방식을 쓰고, 이미 다른 후크가 있으면
; 이어 붙일 곳이 필요하므로 예전 INIT(RAM 조각)을 그대로 쓴다.
; ======================================================================
INIT2:
        in      a,(P0)
        cp      0FFh
        ret     z                       ; 장치가 없으면 아무것도 하지 않는다
        ld      a,(HOKVLD)
        bit     0,a
        jr      nz,I2_HV
        or      1                       ; 후크 칸이 아직 초기화 전: RET 다섯 개로 채운다
        ld      (HOKVLD),a
        ld      hl,EXTBIO
        ld      (hl),0C9h
        ld      de,EXTBIO+1
        ld      bc,4
        ldir
I2_HV:  ld      a,(EXTBIO)              ; 이미 우리 후크면(INIT 이 두 번 불림) 사본을 그대로 둔다
        cp      0F7h
        jr      nz,I2_SAVE
        ld      hl,(EXTBIO+2)
        ld      de,EXT_RT
        or      a
        sbc     hl,de
        jr      z,I2_SET
I2_SAVE:
        ld      hl,EXTBIO               ; 이전 후크를 카트리지 RAM 에 보관 (HIMEM 을 낮추지 않는다:
        ld      de,OLDH                 ; A1GT 같은 기종의 디스크 롬은 HIMEM 이 F380h 가 아니면 초기화를 건너뜀)
        ld      bc,5
        ldir
I2_SET: call    GETSLOT                 ; A = 이 ROM의 슬롯 (CALSLT 형식)
        ld      hl,EXTBIO
        ld      (hl),0F7h               ; RST 30h
        inc     hl
        ld      (hl),a                  ; 슬롯
        inc     hl
        ld      (hl),EXT_RT & 0FFh
        inc     hl
        ld      (hl),EXT_RT >> 8
        inc     hl
        ld      (hl),0C9h               ; RET
        ret

; EXTBIO 처리 (슬롯 간 호출로 들어옴). 규격은 RAM 조각(TPL)과 같고, 이어 붙일 후크가 없어 그냥 RET.
EXT_RT:
        push    af
        ld      a,d
        cp      22h
        jr      nz,ER_PASS
        ld      a,e
        cp      22h
        jr      nz,ER_PASS
        pop     af
        cp      0FFh
        jp      z,OLDH                  ; 이전 후크로 (카트리지 RAM 의 사본을 그대로 실행)
        push    af
        push    bc
        push    hl
        ld      hl,ARG
        ld      de,ER_ID
ER_CMP: ld      a,(hl)
        cp      'a'
        jr      c,ER_C1
        cp      'z'+1
        jr      nc,ER_C1
        sub     20h
ER_C1:  ld      c,a
        ld      a,(de)
        cp      c
        jr      nz,ER_NO
        or      a
        jr      z,ER_YES
        inc     hl
        inc     de
        jr      ER_CMP
ER_NO:  pop     hl
        pop     bc
        pop     af
        ld      de,2222h
        jp      OLDH
ER_YES: pop     hl
        pop     bc
        pop     af
        ld      de,2222h
        or      a
        jr      z,ER_CNT
        cp      1
        jr      z,ER_INFO
        dec     a
        jp      OLDH
ER_CNT: inc     b
        jp      OLDH
ER_INFO:
        call    GETSLOT
        ld      b,0FFh                  ; 매핑 RAM이 아니라 ROM
        ld      hl,ENTRY
        ret
ER_PASS:
        pop     af
        jp      OLDH
ER_ID:  db      "TCP/IP",0

; ======================================================================
; ICMP 에코(PING). 4 = SEND_ECHO, 5 = RCV_ECHO (규격 1.1)
; ======================================================================
CMD_ECHO_SEND     equ     40h
CMD_ECHO_RECV     equ     41h
;            장치: 0 OK, 1 BADCMD, 2 BADARG, 3 NOHANDLE, 4 NETDOWN, 5 TIMEOUT, 6 HOST, 7 NOSLOT, 8 INUSE, 9 NODATA
TBL_ECHO: db    0, ERR_NOT_IMP, ERR_LARGE_DGRAM, ERR_NO_NETWORK, ERR_NO_NETWORK, ERR_NO_NETWORK, ERR_NO_NETWORK, ERR_NO_NETWORK, ERR_NO_NETWORK, ERR_NO_DATA
ECHO_ERR:
        ld      hl,TBL_ECHO
        jp      MAPERR

; HL = 변수 블록: +0 IP(4) +4 TTL +5 식별자(2) +7 순번(2) +9 자료 길이(2)
R_SENDECHO:
        ld      b,11
        ld      c,P2
        otir
        ld      a,CMD_ECHO_SEND
        call    DEVCMD
        or      a
        jr      nz,ECHO_ERR
        ret

; HL = 변수 블록을 받을 자리 (같은 모양)
R_RCVECHO:
        ld      a,CMD_ECHO_RECV
        call    DEVCMD
        or      a
        jr      nz,ECHO_ERR
        ld      b,11
        ld      c,P2
        inir
        xor     a
        ret

; IP 주소를 직접 질의했을 때: 규격은 결과를 저장해 두었다가 DNS_S가 같은 값을 돌려주게 한다.
; (PING 등은 DNS_Q 뒤에 DNS_S가 "끝남"을 돌려줄 때까지 기다린다.) 장치에 DNS_SETIP로 맡긴다.
C_DNS_SETIP     equ     13h
DQ_IP2: pop     bc                      ; 이름 주소와 플래그를 버린다 (HL, DE는 IP 주소 L.H.E.D)
        pop     bc
        ld      a,l
        out     (P2),a
        ld      a,h
        out     (P2),a
        ld      a,e
        out     (P2),a
        ld      a,d
        out     (P2),a
        ld      a,C_DNS_SETIP
        call    DEVCMD                  ; HL, DE, BC는 그대로
        ld      b,1                     ; 이름이 IP 주소였음
        xor     a
        ret

ROMEND:
        ds      8000h-ROMEND,0FFh
        end
