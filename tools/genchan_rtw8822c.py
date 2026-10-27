#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# uefi/rtwchan.inc 생성: 리눅스 rtw88 의 채널 설정 함수(rtw8822c_set_channel 계열, rtw_set_channel_mac)와 그것이 쓰는 매크로 정의를 그대로 옮긴다.
# rtw88 : SPDX GPL-2.0 OR BSD-3-Clause (BSD-3-Clause 로 사용, THIRD-PARTY-NOTICES.txt 참조). Copyright(c) Realtek Corporation.
import re,sys,os
R='/tmp/rtwref/'
def func(fn,name):
    L=open(R+fn,encoding='utf-8',errors='replace').read().split('\n')
    a=next(i for i,l in enumerate(L) if re.match(r'(static )?(void|int|bool) '+name+r'\(',l))
    b=a
    while L[b]!='}': b+=1
    return '\n'.join(L[a:b+1])
code='\n\n'.join([func('rtw8822c.c','rtw8822c_rstb_3wire'),func('rtw8822c.c','rtw8822c_toggle_igi'),func('rtw8822c.c','rtw8822c_set_channel_rf'),
                  func('rtw8822c.c','rtw8822c_set_channel_bb'),func('mac.c','rtw_set_channel_mac'),func('rtw8822c.c','rtw8822c_set_channel')])
code=code.replace('void rtw_set_channel_mac','static void rtw_set_channel_mac')
defs={}
for fn in ['main.h','reg.h','rtw8822c.h','mac.h','phy.h']:
    if not os.path.exists(R+fn): continue
    t=open(R+fn,encoding='utf-8',errors='replace').read().replace('\\\n',' ')
    for m in re.finditer(r'^#define\s+(\w+(?:\([\w, ]*\))?)\s*(.*)$',t,re.M):
        nm=re.match(r'\w+',m.group(1)).group(0)
        if nm not in defs: defs[nm]=(m.group(1),m.group(2).strip())
have=set(re.findall(r'^#define\s+(\w+)',open('uefi/rtwshim.h',encoding='utf-8').read(),re.M))
out=[];seen=set()
def need(text):
    for w in sorted(set(re.findall(r'\b[A-Za-z_]\w*\b',text))):
        if w in defs and w not in seen and w not in have:
            seen.add(w); need(re.sub(r'/\*.*?\*/','',defs[w][1])); out.append('#define %s %s'%(defs[w][0],defs[w][1]))
need(code)
enums=[]
mh=open(R+'main.h',encoding='utf-8',errors='replace').read()
for key in ['RTW_CHANNEL_WIDTH_5','RTW_SC_20_UPPER']:
    m=re.search(r'enum\s+\w+\s*\{[^}]*\b'+key+r'\b[^}]*\};',mh,re.S); enums.append(m.group(0))
hdr='/* 자동 생성: tools/genchan_rtw8822c.py (원본 Linux rtw88, BSD-3-Clause 로 사용. Copyright(c) 2018-2019 Realtek Corporation) */\n'
open(sys.argv[1] if len(sys.argv)>1 else 'uefi/rtwchan.inc','w',encoding='utf-8').write(hdr+'\n'.join(out)+'\n\n'+'\n'.join(enums)+'\n\n'+code+'\n')
print(len(out),'macros')
