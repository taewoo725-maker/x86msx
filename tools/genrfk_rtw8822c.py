#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# uefi/rtwrfk.inc 생성: 리눅스 rtw88 rtw8822c.c 의 RF 초기화 함수들(DAC 보정, 열/전력 보정, PA 바이어스)을 줄 단위로 그대로 가져온다.
# rtw88 : SPDX GPL-2.0 OR BSD-3-Clause (여기서는 BSD-3-Clause 로 사용, THIRD-PARTY-NOTICES.txt 참조). Copyright(c) Realtek Corporation.
import re,sys
src=open(sys.argv[1] if len(sys.argv)>1 else '/tmp/rtwref/rtw8822c.c',encoding='utf-8',errors='replace').read().split('\n')
a=next(i for i,l in enumerate(src) if l.startswith('static void rtw8822c_dac_backup_reg'))
b=next(i for i,l in enumerate(src) if l.startswith('static void rtw8822c_pa_bias'))
while src[b]!='}': b+=1
body='\n'.join(src[a:b+1])
# 사용하지 않는 디버그 출력 제거는 shim 의 rtw_dbg 매크로로 처리
hdr='/* 자동 생성: tools/genrfk_rtw8822c.py  (원본 Linux rtw88 rtw8822c.c, BSD-3-Clause 로 사용. Copyright(c) 2018-2019 Realtek Corporation) */\n'
open(sys.argv[2] if len(sys.argv)>2 else 'uefi/rtwrfk.inc','w',encoding='utf-8').write(hdr+body+'\n')
print('lines',b-a+1)
