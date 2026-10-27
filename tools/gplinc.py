#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# LICENSE(GPL v3 원문) -> kernel/gpl3.inc : 문단으로 이어 붙여 C 문자열로 (정보 창 표시용)
import re,sys
src=open('LICENSE',encoding='utf-8').read().replace('\r','').split('\n')
paras=[];cur=[]
def flush():
    global cur
    if cur: paras.append(' '.join(x.strip() for x in cur)); cur=[]
for ln in src:
    if not ln.strip(): flush(); continue
    ind=len(ln)-len(ln.lstrip(' '))
    if ind>=12: flush(); paras.append('# '+ln.strip()); continue
    if ind>=2: flush()
    cur.append(ln)
flush()
import zlib
txt=''
for p in paras:
    p=re.sub(r' {2,}',' ',p)
    if p.startswith('# GNU GENERAL') or p.startswith('# Version 3'): p=p[2:]
    if not p.startswith('# ') and p[:1] in '-[': p=' '+p
    txt+=p+'\n\n'
raw=txt.encode('utf-8')
c=zlib.compressobj(9,zlib.DEFLATED,-15); z=c.compress(raw)+c.flush()
body=',\n'.join(','.join('%d'%b for b in z[i:i+32]) for i in range(0,len(z),32))
open('kernel/gpl3.inc','w',encoding='utf-8').write('/* tools/gplinc.py 가 LICENSE에서 만듦: GNU GPL v3 원문을 문단으로 이어 deflate로 줄인 것 */\n#define GPL3_RAW %d\nstatic const unsigned char gpl3_z[] = {\n%s};\n'%(len(raw),body))
print(len(paras),'문단',len(raw),'->',len(z))
