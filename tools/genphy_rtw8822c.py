# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
import re,struct,sys
src=open('/tmp/rtwref/t8822c.c').read()
def arr(name):
    m=re.search(r'static const u32 '+name+r'\[\] = \{(.*?)\n\};',src,re.S)
    body=re.sub(r'/\*.*?\*/','',m.group(1),flags=re.S)
    body=re.sub(r'//.*','',body)
    return [int(x,16) for x in re.findall(r'0x[0-9A-Fa-f]+',body)]
CUT,PKG,RFE,INTF=3,15,1,1
def check(c):
    cut=(c>>24)&15; pkg=(c>>12)&15; intf=(c>>8)&15; rfe=c&0xFF
    if cut and cut!=CUT: return False
    if pkg and pkg!=PKG: return False
    if intf and intf!=INTF: return False
    if rfe!=RFE: return False
    return True
def parse(a):
    out=[]; matched=True; skipped=False; pos=0
    for i in range(0,len(a),2):
        f,s=a[i],a[i+1]
        if f&0x80000000:
            br=(f>>28)&3
            if br==3: matched=True; skipped=False
            elif br==2: matched=False if skipped else True
            else: pos=f
        elif f&0x40000000:
            if not skipped:
                if check(pos): matched=True; skipped=True
                else: matched=False; skipped=False
            else: matched=False
        elif matched: out.append((f,s))
    return out
ops=[]
def sec(name,kind):
    r=[]
    for f,s in parse(arr(name)):
        if kind=='mac': r.append((1<<28|f&0xFFFF,s&0xFF))
        elif kind=='agc': r.append((f&0xFFFF,s))
        elif kind=='bb':
            d={0xfe:50000,0xfd:5000,0xfc:1000,0xfb:50,0xfa:5,0xf9:1}
            if f in d: r.append((3<<28,d[f]))
            else: r.append((f&0xFFFF,s))
        elif kind=='rf':
            if f==0xffe: r.append((3<<28,50000))
            elif f==0xfe: r.append((3<<28,100))
            else: r.append((2<<28|f&0xFF,s&0xFFFFF))
    return r
secs=[('mac','rtw8822c_mac'),('bb','rtw8822c_bb'),('agc','rtw8822c_agc'),('bb','rtw8822c_array_mp_cal_init'),('rf','rtw8822c_rf_b'),('rf','rtw8822c_rf_a')]
blob=b'RTWP\x01\x00\x00\x00'+struct.pack('<I',len(secs))
body=b''; 
for k,n in secs:
    r=sec(n,k); print(n,len(r),file=sys.stderr)
    blob+=struct.pack('<I',len(r))
    body+=b''.join(struct.pack('<II',a,d) for a,d in r)
open('rtw8822c_phy.bin','wb').write(blob+body)
print(len(blob+body),file=sys.stderr)
