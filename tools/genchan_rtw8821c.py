#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# uefi/rtwchan21.inc 생성: 리눅스 rtw88 의 RTL8821C 채널 설정 함수(rtw8821c_set_channel 계열)와 그것이 쓰는 매크로 정의를 그대로 옮긴다.
# 공용 부분(rtw_set_channel_mac, 폭 열거형, 공통 매크로)은 8822C 용 uefi/rtwchan.inc 에 이미 있으므로 다시 만들지 않는다.
# rtw88 : SPDX GPL-2.0 OR BSD-3-Clause (BSD-3-Clause 로 사용, THIRD-PARTY-NOTICES.txt 참조). Copyright(c) Realtek Corporation.
import re,sys,os
R='/tmp/w21/'
def func(fn,name):
    L=open(R+fn,encoding='utf-8',errors='replace').read().split('\n')
    a=next(i for i,l in enumerate(L) if re.match(r'(static )?(void|int|bool|u32) '+name+r'\(',l))
    b=a
    while L[b]!='}': b+=1
    return '\n'.join(L[a:b+1])
names=['coex_cfg_init','switch_rf_set','set_channel_rf','set_channel_rxdfir','cck_tx_filter_srrc','set_channel_bb','get_bb_swing','set_channel_bb_swing','set_channel']
code='\n\n'.join(func('rtw8821c.c','rtw8821c_'+n) for n in names)
code=code.replace('struct rtw_efuse efuse = rtwdev->efuse;','struct rtw_efuse efuse = rtwdev->efuse;')
defs={}
for fn in ['main.h','reg.h','rtw8821c.h']:
    t=open(R+fn,encoding='utf-8',errors='replace').read().replace('\\\n',' ')
    for m in re.finditer(r'^#define\s+(\w+(?:\([\w, ]*\))?)\s*(.*)$',t,re.M):
        nm=re.match(r'\w+',m.group(1)).group(0)
        if nm not in defs: defs[nm]=(m.group(1),m.group(2).strip())
# 8822C 쪽(rtwshim.h, rtwchan.inc)에 같은 이름이 이미 있으면: 값이 같으면 다시 만들지 않고, 값이 다르면(예: RF18_BW_20M 은 8822C 는 BIT(13)|BIT(12), 8821C 는 BIT(11)|BIT(10))
# 8821C 것을 이름 뒤에 _21 을 붙여 따로 만든다. (8.0.211 에는 이 구분이 없어 8822C 값이 섞여 들어가 RF 0x18 채널 값이 틀렸다)
def norm(v): return re.sub(r'\s+',' ',re.sub(r'/\*.*?\*/','',v)).strip()
have={}
for fn in ['uefi/rtwshim.h','uefi/rtwchan.inc']:
    for m in re.finditer(r'^#define\s+(\w+)(?:\([^)]*\))?\s*(.*)$',open(fn,encoding='utf-8').read(),re.M):
        have.setdefault(m.group(1),norm(m.group(2)))
closure=[];seen=set()
def need(text):
    for w in sorted(set(re.findall(r'\b[A-Za-z_]\w*\b',text))):
        if w in defs and w not in seen:
            seen.add(w); need(re.sub(r'/\*.*?\*/','',defs[w][1])); closure.append(w)
need(code)
need('REG_LED_CFG BIT_DPDT_SEL_EN BIT_DPDT_WL_SEL REG_RFE_CTRL8 BIT_MASK_RFE_SEL89 DPDT_CTRL_PIN BIT_MASK_R_RFE_SEL_15 REG_CTRL_TYPE BIT_CTRL_TYPE1 BIT_CTRL_TYPE2')   # 손으로 쓴 안테나 스위치(rtwdet.c)용
renamed=set(w for w in closure if w in have and have[w]!=norm(defs[w][1]))
def ren(t): return re.sub(r'\b('+'|'.join(sorted(renamed,key=len,reverse=True))+r')\b',r'\1_21',t) if renamed else t
out=[]
for w in closure:
    if w in have and w not in renamed: continue
    head,val=defs[w]
    out.append('#define %s %s'%(ren(head),ren(val)))
code=ren(code)
print('renamed:',sorted(renamed))
enums=[]
for fn in ['rtw8821c.c']:
    t=open(R+fn,encoding='utf-8',errors='replace').read()
    for m in re.finditer(r'enum\s*\w*\s*\{[^}]*\};',t,re.S):
        if 'SWITCH_TO_BTG' in m.group(0): enums.append(m.group(0))
hdr='/* 자동 생성: tools/genchan_rtw8821c.py (원본 Linux rtw88, BSD-3-Clause 로 사용. Copyright(c) 2018-2019 Realtek Corporation) */\n'
open(sys.argv[1] if len(sys.argv)>1 else 'uefi/rtwchan21.inc','w',encoding='utf-8').write(hdr+'\n'.join(out)+'\n\n'+'\n'.join(enums)+'\n\n'+code+'\n')
print(len(out),'macros',len(enums),'enums')
