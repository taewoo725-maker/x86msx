; 만든이: KimTaeWoo
; 도와준이: Lee.J.H
; ======================================================================
; X86NTP.COM 1.0 - 인터넷 시간(SNTP)으로 시계 맞추기. MSX-DOS 1/2, Nextor 에서 실행.
;
;   X86NTP [서버이름] [/Z[+-]시[:분]] [/T] [/?]
;     서버이름  기본 kr.pool.ntp.org (실패하면 time.google.com, pool.ntp.org 도 차례로)
;     /Z        시간대. 기본 +9 (한국). 예: /Z-5   /Z+5:30
;     /T        시험: 시간만 보여 주고 시계는 안 바꿈
;
; 네트워크는 TCP/IP UNAPI (X86MSX 네트워크 카트리지)를 쓴다: DNS_Q·DNS_S·UDP_OPEN·UDP_SEND·UDP_RCV.
; 시계는 MSX-DOS 의 날짜·시간 설정(2Bh·2Dh)으로 고친다. X86MSX 는 MSX 에서 바꾼 시계를 PC 시계에도 반영한다
; (ini PcClockSet).
;
; 어셈블: pasmo x86ntp.asm X86NTP.COM        (시험용: pasmo -d -DTESTBUILD ... )
; ======================================================================
BDOS            equ     0005h
CALSLT          equ     001Ch
EXTBIO          equ     0FFCAh
ARG             equ     0F847h

F_PUTS          equ     09h
F_SDATE         equ     2Bh
F_STIME         equ     2Dh
F_TERM0         equ     00h

; ---- UNAPI TCP/IP 함수 번호 ----
U_NETSTATE      equ     3
U_DNSQ          equ     6
U_DNSS          equ     7
U_UDPOPEN       equ     8
U_UDPCLOSE      equ     9
U_UDPSEND       equ     11
U_UDPRCV        equ     12
U_WAIT          equ     29

        org     100h
start:
        ld      de,m_banner
        call    puts
        call    parse_args
        ld      a,(f_help)
        or      a
        jp      nz,show_help
        call    unapi_find
        jp      c,e_nounapi
        ; 네트워크 연결
        ld      a,U_NETSTATE
        call    ucall
        ld      a,b
        cp      2
        jp      nz,e_nonet
        ; 서버 목록: 사용자가 정했으면 그것 하나, 아니면 기본 세 개
        ld      a,(host_set)
        or      a
        jr      z,try_default
        ld      hl,host_buf
        call    try_server
        jp      nc,got_time
        jp      e_noreply
try_default:
        ld      hl,s_host1
        call    try_server
        jp      nc,got_time
        ld      hl,s_host2
        call    try_server
        jp      nc,got_time
        ld      hl,s_host3
        call    try_server
        jp      nc,got_time
        jp      e_noreply

; 받은 시간(NTP 초, T32) → 시간대 적용 → 날짜·시각
got_time:
        call    apply_tz
        ; 초·분·시·일수로 나눔
        ld      a,60
        call    div32by8
        ld      (v_sec),a
        ld      a,60
        call    div32by8
        ld      (v_min),a
        ld      a,24
        call    div32by8
        ld      (v_hour),a
        call    days_to_date
        call    show_time
        ld      a,(f_test)
        or      a
        jr      z,do_set
        ld      de,m_test
        call    puts
        jp      exit0
do_set:
        ; 날짜
        ld      hl,(v_year)
        ld      a,(v_mon)
        ld      d,a
        ld      a,(v_day)
        ld      e,a
        ld      c,F_SDATE
        call    BDOS
        or      a
        jr      nz,e_set
        ; 시각
        ld      a,(v_hour)
        ld      h,a
        ld      a,(v_min)
        ld      l,a
        ld      a,(v_sec)
        ld      d,a
        ld      e,0
        ld      c,F_STIME
        call    BDOS
        or      a
        jr      nz,e_set
        ld      de,m_done
        call    puts
exit0:  ld      c,F_TERM0
        jp      BDOS
e_set:  ld      de,m_eset
        jr      fail
e_nounapi:
        ld      de,m_nounapi
        jr      fail
e_nonet:
        ld      de,m_nonet
        jr      fail
e_noreply:
        ld      de,m_noreply
fail:   call    puts
        jr      exit0
show_help:
        ld      de,m_help
        call    puts
        jr      exit0

; ---------------------------------------------------------------------
; 서버 하나 시도: HL = 이름(0으로 끝). 성공하면 NC, T32 = NTP 초. 실패하면 C
; ---------------------------------------------------------------------
try_server:
        push    hl
        ld      de,m_try
        call    puts
        pop     hl
        push    hl
        ex      de,hl
        call    puts0                   ; 서버 이름
        ld      de,m_crlf
        call    puts
        pop     hl
        ; DNS 질의
        ld      b,0
        ld      a,U_DNSQ
        call    ucall
        or      a
        jr      nz,ts_dnsfail
        ld      de,600                  ; 최대 600 프레임 (약 10초)
ts_dwait:
        push    de
        ld      b,0
        ld      a,U_DNSS
        call    ucall
        pop     de
        or      a
        jr      nz,ts_dnsfail
        ld      a,b
        cp      2
        jr      z,ts_dnsok
        cp      1
        jr      nz,ts_dnsfail
        push    de
        ld      a,U_WAIT
        call    ucall
        pop     de
        dec     de
        ld      a,d
        or      e
        jr      nz,ts_dwait
ts_dnsfail:
        ld      de,m_dnsfail
        call    puts
        scf
        ret
ts_dnsok:                               ; L.H.E.D = IP
        ld      (pkt_ip),hl
        ld      (pkt_ip+2),de
        ld      hl,123
        ld      (pkt_port),hl
        ld      hl,48
        ld      (pkt_len),hl
        ; UDP 열기
        ld      hl,0FFFFh
        ld      b,1
        ld      a,U_UDPOPEN
        call    ucall
        or      a
        jp      nz,ts_fail
        ld      a,b
        ld      (udp_h),a
        ; 세 번 보내 보기
        ld      a,3
        ld      (tries),a
ts_send:
        call    make_request
        ld      a,(udp_h)
        ld      b,a
        ld      hl,req_buf
        ld      de,pkt_ip
        ld      a,U_UDPSEND
        call    ucall
        or      a
        jr      nz,ts_close_fail
        ld      de,300                  ; 최대 300 프레임 (약 5초)
ts_rwait:
        push    de
        ld      a,(udp_h)
        ld      b,a
        ld      hl,rsp_buf
        ld      de,128
        ld      a,U_UDPRCV
        call    ucall
        pop     de
        or      a
        jr      nz,ts_rnone
        ; 받음: 크기 BC. 48바이트 이상이고 서버 응답(모드 4)·층(stratum) 1~15·시각이 0이 아닌가
        ld      a,b
        or      a
        jr      nz,ts_check
        ld      a,c
        cp      48
        jr      c,ts_rnone              ; 너무 짧음: 무시하고 계속 기다림
ts_check:
        ld      a,(rsp_buf)
        and     07h
        cp      4
        jr      nz,ts_rnone
        ld      a,(rsp_buf+1)
        or      a
        jr      z,ts_rnone              ; stratum 0 = "KoD" (쓰지 말라는 응답)
        ld      hl,(rsp_buf+40)
        ld      de,(rsp_buf+42)
        ld      a,h
        or      l
        or      d
        or      e
        jr      z,ts_rnone
        ; NTP 시각은 큰 바이트부터: 40,41,42,43 → T32 (작은 바이트부터)
        ld      a,(rsp_buf+43)
        ld      (T32),a
        ld      a,(rsp_buf+42)
        ld      (T32+1),a
        ld      a,(rsp_buf+41)
        ld      (T32+2),a
        ld      a,(rsp_buf+40)
        ld      (T32+3),a
        call    udp_close
        or      a                       ; NC
        ret
ts_rnone:
        push    de
        ld      a,U_WAIT
        call    ucall
        pop     de
        dec     de
        ld      a,d
        or      e
        jr      nz,ts_rwait
        ld      a,(tries)
        dec     a
        ld      (tries),a
        jp      nz,ts_send
ts_close_fail:
        call    udp_close
ts_fail:
        scf
        ret
udp_close:
        ld      a,(udp_h)
        ld      b,a
        ld      a,U_UDPCLOSE
        call    ucall
        ret

; 48 바이트 SNTP 요청: LI=0, VN=4, Mode=3 → 23h, 나머지 0
make_request:
        ld      hl,req_buf
        ld      (hl),23h
        inc     hl
        ld      b,47
mr_lp:  ld      (hl),0
        inc     hl
        djnz    mr_lp
        ret

; ---------------------------------------------------------------------
; 시간대: T32 += (부호) tz_min * 60
; ---------------------------------------------------------------------
apply_tz:
        ld      hl,0
        ld      de,(tz_min)
        ld      b,60
at_mul: add     hl,de
        djnz    at_mul                  ; HL = 분 * 60 (크기, 최대 14시간 = 50400)
        ex      de,hl
        ld      a,d
        or      e
        ret     z
        ld      hl,T32
        ld      a,(tz_neg)
        or      a
        jr      nz,at_sub
        ld      a,(hl)
        add     a,e
        ld      (hl),a
        inc     hl
        ld      a,(hl)
        adc     a,d
        ld      (hl),a
        inc     hl
        ld      a,(hl)
        adc     a,0
        ld      (hl),a
        inc     hl
        ld      a,(hl)
        adc     a,0
        ld      (hl),a
        ret
at_sub: ld      a,(hl)
        sub     e
        ld      (hl),a
        inc     hl
        ld      a,(hl)
        sbc     a,d
        ld      (hl),a
        inc     hl
        ld      a,(hl)
        sbc     a,0
        ld      (hl),a
        inc     hl
        ld      a,(hl)
        sbc     a,0
        ld      (hl),a
        ret

; ---------------------------------------------------------------------
; 32비트 / 8비트 나눗셈. 입력 A = 나누는 수(1~255), T32 = 나뉘는 수. 결과 T32 = 몫, A = 나머지
; ---------------------------------------------------------------------
div32by8:
        ld      c,a
        ld      b,32
        ld      d,0                     ; 나머지
d32_lp: ld      hl,T32
        sla     (hl)
        inc     hl
        rl      (hl)
        inc     hl
        rl      (hl)
        inc     hl
        rl      (hl)                    ; 캐리 = 나뉘는 수의 맨 위 비트
        ld      a,d
        rla
        ld      d,a
        jr      c,d32_sub               ; 9비트가 된 경우: 반드시 c 보다 큼
        cp      c
        jr      c,d32_nx
d32_sub:
        sub     c
        ld      d,a
        ld      hl,T32
        set     0,(hl)
d32_nx: djnz    d32_lp
        ld      a,d
        ret

; ---------------------------------------------------------------------
; 일수 (1900-01-01 부터, T32 아래 16비트) → 년·월·일
; ---------------------------------------------------------------------
days_to_date:
        ld      hl,(T32)
        ld      de,1900
dd_yl:  call    year_days               ; BC = 이 해의 날수
        or      a
        sbc     hl,bc
        jr      c,dd_ydone
        inc     de
        jr      dd_yl
dd_ydone:
        add     hl,bc                   ; 되돌림: HL = 올해 안에서 지난 날수
        ld      (v_year),de
        call    is_leap                 ; DE = 년 → Z = 윤년 아님? (아래 정의: A=1 이면 윤년)
        ld      (v_leap),a
        ld      b,1                     ; 월
        ld      ix,mdays
dd_ml:  ld      c,(ix+0)
        ld      a,b
        cp      2
        jr      nz,dd_nf
        ld      a,(v_leap)
        add     a,c
        ld      c,a
dd_nf:  ld      a,h
        or      a
        jr      nz,dd_sub               ; 256 이상이면 더 빼야 함
        ld      a,l
        cp      c
        jr      c,dd_done               ; 이 달 안
dd_sub: ld      a,l
        sub     c
        ld      l,a
        ld      a,h
        sbc     a,0
        ld      h,a
        inc     ix
        inc     b
        jr      dd_ml
dd_done:
        ld      a,b
        ld      (v_mon),a
        ld      a,l
        inc     a
        ld      (v_day),a
        ret

; DE = 년 → BC = 날수 (365 / 366)
year_days:
        call    is_leap
        ld      bc,365
        or      a
        ret     z
        inc     bc
        ret
; DE = 년 → A = 1 윤년, 0 아님 (1900 은 윤년이 아님)
is_leap:
        ld      a,e
        and     3
        jr      nz,il_no
        ld      a,d
        cp      high 1900
        jr      nz,il_yes
        ld      a,e
        cp      low 1900
        jr      z,il_no
il_yes: ld      a,1
        ret
il_no:  xor     a
        ret

; ---------------------------------------------------------------------
; 화면에 시각 보이기: Time: YYYY-MM-DD HH:MM:SS (UTC+9)
; ---------------------------------------------------------------------
show_time:
        ld      de,m_time
        call    puts
        ld      hl,(v_year)
        call    put_dec16
        ld      a,'-'
        call    putc
        ld      a,(v_mon)
        call    put_dec2
        ld      a,'-'
        call    putc
        ld      a,(v_day)
        call    put_dec2
        ld      a,' '
        call    putc
        ld      a,(v_hour)
        call    put_dec2
        ld      a,':'
        call    putc
        ld      a,(v_min)
        call    put_dec2
        ld      a,':'
        call    putc
        ld      a,(v_sec)
        call    put_dec2
        ld      de,m_utc
        call    puts
        ld      a,(tz_neg)
        or      a
        ld      a,'+'
        jr      z,st_sg
        ld      a,'-'
st_sg:  call    putc
        ld      hl,(tz_min)
        ld      de,60
        ld      b,0                     ; 시
st_hl:  or      a
        sbc     hl,de
        jr      c,st_hd
        inc     b
        jr      st_hl
st_hd:  add     hl,de                   ; HL = 분
        push    hl
        ld      a,b
        call    put_dec1or2
        pop     hl
        ld      a,l
        or      a
        jr      z,st_end
        push    af
        ld      a,':'
        call    putc
        pop     af
        call    put_dec2
st_end: ld      de,m_rp
        call    puts
        ret

; ---------------------------------------------------------------------
; 출력
; ---------------------------------------------------------------------
puts:   ld      c,F_PUTS                ; DE = '$' 로 끝나는 글
        jp      BDOS
puts0:  ld      a,(de)                  ; DE = 0 으로 끝나는 글
        or      a
        ret     z
        push    de
        call    putc
        pop     de
        inc     de
        jr      puts0
putc:   ld      e,a
        ld      c,02h
        jp      BDOS
put_dec2:                               ; A (0~99) 두 자리
        ld      b,0
pd2_l:  cp      10
        jr      c,pd2_d
        sub     10
        inc     b
        jr      pd2_l
pd2_d:  push    af
        ld      a,b
        add     a,'0'
        call    putc
        pop     af
        add     a,'0'
        jp      putc
put_dec1or2:                            ; A (0~99) 한 자리 또는 두 자리
        cp      10
        jr      nc,put_dec2
        add     a,'0'
        jp      putc
put_dec16:                              ; HL 십진수 (0~9999, 네 자리 고정: 연도용)
        ld      de,1000
        call    pd16
        ld      de,100
        call    pd16
        ld      de,10
        call    pd16
        ld      a,l
        add     a,'0'
        jp      putc
pd16:   ld      b,'0'-1
pd16l:  inc     b
        or      a
        sbc     hl,de
        jr      nc,pd16l
        add     hl,de
        ld      a,b
        push    hl
        call    putc
        pop     hl
        ret

; ---------------------------------------------------------------------
; 명령줄: 0080h = 길이, 0081h~ = 글. argp = 지금 자리, argn = 남은 글자 수
; ---------------------------------------------------------------------
arg_peek:                               ; A = 지금 글자 (끝이면 0, 대문자로). HL 은 그대로
        ld      a,(argn)
        or      a
        ret     z
        push    hl
        ld      hl,(argp)
        ld      a,(hl)
        pop     hl
        cp      0Dh
        jr      nz,ap_u
        xor     a
        ret
ap_u:   cp      'a'
        ret     c
        cp      'z'+1
        ret     nc
        and     0DFh
        ret
arg_adv:                                ; 한 글자 넘김. A·플래그는 그대로 (HL 은 바뀜)
        push    af
        ld      a,(argn)
        or      a
        jr      z,aa_e
        dec     a
        ld      (argn),a
        ld      hl,(argp)
        inc     hl
        ld      (argp),hl
aa_e:   pop     af
        ret
arg_num:                                ; 십진수 → HL (자리가 없으면 0). 끝난 뒤 Z 가 아니라 CF=0
        ld      hl,0
an_lp:  call    arg_peek
        sub     '0'
        ret     c
        cp      10
        ret     nc
        push    af
        ld      d,h
        ld      e,l
        add     hl,hl
        add     hl,hl
        add     hl,de
        add     hl,hl                   ; *10
        pop     af
        ld      e,a
        ld      d,0
        add     hl,de
        push    hl
        call    arg_adv
        pop     hl
        jr      an_lp

parse_args:
        ld      hl,540                  ; 기본 시간대 +9시 = 540분
        ld      (tz_min),hl
        ld      a,(0080h)
        ld      (argn),a
        ld      hl,0081h
        ld      (argp),hl
pa_loop:
        call    arg_peek
        or      a
        jr      nz,pa_more
        ld      hl,(tz_min)             ; 시간대가 ±14시간을 넘으면 사용법을 보여 줌
        ld      de,841
        or      a
        sbc     hl,de
        ret     c
        jp      pa_help
pa_more:
        cp      ' '
        jr      z,pa_sk
        cp      09h
        jr      z,pa_sk
        cp      '/'
        jr      z,pa_opt
        ; 서버 이름 (원래 대소문자 그대로 복사)
        ld      de,host_buf
        ld      b,63
pa_hn:  ld      hl,(argp)
        ld      a,(argn)
        or      a
        jr      z,pa_hd
        ld      a,(hl)
        cp      ' '
        jr      z,pa_hd
        cp      0Dh
        jr      z,pa_hd
        cp      09h
        jr      z,pa_hd
        ld      (de),a
        inc     de
        push    de
        push    bc
        call    arg_adv
        pop     bc
        pop     de
        djnz    pa_hn
pa_hd:  xor     a
        ld      (de),a
        ld      a,1
        ld      (host_set),a
        jr      pa_loop
pa_sk:  call    arg_adv
        jr      pa_loop
pa_opt: call    arg_adv
        call    arg_peek
        call    arg_adv
        cp      'T'
        jr      nz,pa_o2
        ld      a,1
        ld      (f_test),a
        jr      pa_loop
pa_o2:  cp      '?'
        jr      z,pa_help
        cp      'H'
        jr      z,pa_help
        cp      'Z'
        jr      nz,pa_loop              ; 모르는 옵션은 무시
        xor     a
        ld      (tz_neg),a
        call    arg_peek
        cp      '+'
        jr      z,pa_zp
        cp      '-'
        jr      nz,pa_zh
        ld      a,1
        ld      (tz_neg),a
pa_zp:  call    arg_adv
pa_zh:  call    arg_num                 ; HL = 시
        ex      de,hl
        ld      hl,0
        ld      b,60
pa_z60: add     hl,de
        djnz    pa_z60                  ; HL = 시 * 60
        ld      (tz_min),hl
        call    arg_peek
        cp      ':'
        jp      nz,pa_loop
        call    arg_adv
        call    arg_num                 ; HL = 분
        ld      de,(tz_min)
        add     hl,de
        ld      (tz_min),hl
        jp      pa_loop
pa_help:
        ld      a,1
        ld      (f_help),a
        ret

; ---------------------------------------------------------------------
; UNAPI: TCP/IP 구현 찾기 → CF=0 이면 찾음 (ent_slot, ent_addr)
; ---------------------------------------------------------------------
unapi_find:
        ld      hl,s_spec
        ld      de,ARG
        ld      bc,7
        ldir                            ; 규격 식별자 "TCP/IP"를 ARG 에 복사
        ld      de,2222h
        xor     a                       ; A = 0: 구현 개수
        ld      b,0
        call    EXTBIO
        ld      a,b
        or      a
        jr      z,uf_no
        ld      de,2222h
        ld      a,1                     ; A = 1: 정보 (B = 첫 번째)
        ld      b,1
        call    EXTBIO                  ; 결과: A = 슬롯, B = 세그먼트(FFh = ROM), HL = 주소
        ld      (ent_iy+1),a
        ld      (ent_addr),hl
        or      a                       ; NC
        ret
uf_no:  scf
        ret

; A = 함수 번호, 나머지 레지스터는 그대로 구현에 넘김 (CALSLT: IX = 주소, IY 위쪽 = 슬롯)
ucall:
        push    ix
        push    iy
        ld      ix,(ent_addr)
        ld      iy,(ent_iy)
        call    CALSLT
        pop     iy
        pop     ix
        ei
        ret

; ---------------------------------------------------------------------
; 글과 자료
; ---------------------------------------------------------------------
s_spec:         db      "TCP/IP",0
s_host1:        db      "kr.pool.ntp.org",0
s_host2:        db      "time.google.com",0
s_host3:        db      "pool.ntp.org",0
m_banner:       db      "X86NTP 1.0 - Internet time (SNTP) for X86MSX",13,10,"$"
m_try:          db      "Server: $"
m_crlf:         db      13,10,"$"
m_time:         db      "Time:   $"
m_utc:          db      " (UTC$"
m_rp:           db      ")",13,10,"$"
m_done:         db      "Clock set.",13,10,"$"
m_test:         db      "Test mode: clock not changed.",13,10,"$"
m_eset:         db      "Could not set the clock (invalid date/time).",13,10,"$"
m_nounapi:      db      "No TCP/IP UNAPI found. Insert the network cartridge.",13,10,"$"
m_nonet:        db      "Network is not connected.",13,10,"$"
m_dnsfail:      db      "  DNS failed.",13,10,"$"
m_noreply:      db      "No reply from the time server.",13,10,"$"
m_help:         db      "Usage: X86NTP [server] [/Z+9] [/T]",13,10
                db      "  server  default kr.pool.ntp.org (then time.google.com, pool.ntp.org)",13,10
                db      "  /Z      time zone, default +9 (Korea). e.g. /Z-5  /Z+5:30",13,10
                db      "  /T      test: show the time, do not set the clock",13,10,"$"
mdays:          db      31,28,31,30,31,30,31,31,30,31,30,31

; ---- 변수 (COM 파일에는 들어가지 않음) ----
argp:           dw      0
argn:           db      0
f_help:         db      0
f_test:         db      0
host_set:       db      0
tz_min:         dw      540
tz_neg:         db      0
ent_iy:         db      0                       ; ent_iy+1 = 슬롯 (IY 위쪽)
ent_addr:       dw      0
udp_h:          db      0
tries:          db      0
T32:            ds      4
v_year:         dw      0
v_mon:          db      0
v_day:          db      0
v_hour:          db      0
v_min:          db      0
v_sec:          db      0
v_leap:         db      0
pkt_ip:         ds      4
pkt_port:       dw      0
pkt_len:        dw      0
host_buf:       ds      64
req_buf:        ds      48
rsp_buf:        ds      128
