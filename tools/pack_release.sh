#!/bin/bash
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# usage: pack.sh PREV NEW "row text" "req line (optional)"
set -e
A=$1; B=$2; ROW=$3; REQ=$4
cd /home/claude/msxemu
for f in build/kernel.bin build/BOOTX64.EFI; do grep -q "$B" $f || { echo "STALE $f (판 $B 없음)"; exit 1; }; done
O=/mnt/user-data/outputs/v${B//./}; I=/mnt/user-data/outputs/v${A//./}
mkdir -p $O
mkdir -p /tmp/pk/usb/Icons/Keyboard; cp assets/keyboard/*.png /tmp/pk/usb/Icons/Keyboard/
mkdir -p /tmp/pk/usb/Icons/Machines; cp assets/machines/* /tmp/pk/usb/Icons/Machines/   # 5.3.19 시스템 보기 본체 사진
mkdir -p /tmp/pk/usb/Icons/Sysview; cp assets/sysview/* /tmp/pk/usb/Icons/Sysview/   # 5.3.48 시스템 보기 주변기기 그림 (모니터·드라이브·하드·MIDI·조이스틱·확장 박스·카트리지)
mkdir -p /tmp/pk/usb/fonts; cp third_party/noto/NotoSansKR.x8f /tmp/pk/usb/fonts/; cp third_party/noto/LICENSE.txt /tmp/pk/usb/fonts/LICENSE-NotoSansCJK.txt; cp third_party/gothic/GothicKR.x8f /tmp/pk/usb/fonts/; cp third_party/gothic/OFL.txt /tmp/pk/usb/fonts/LICENSE-GothicKR.txt; cp third_party/happy/HappyKR.x8f /tmp/pk/usb/fonts/; cp third_party/happy/README.txt /tmp/pk/usb/fonts/LICENSE-HappyKR.txt; cp third_party/happy/LICENSE-HappyKR.pdf /tmp/pk/usb/fonts/
cp build/BOOTX64.EFI /tmp/pk/usb/EFI/BOOT/BOOTX64.EFI; cp build/kernel.bin /tmp/pk/usb/X86MSX.BIN
cp build/BOOTX64.EFI $O/; cp build/kernel.bin $O/X86MSX.BIN; cp $I/RTW.INI $O/
if [ -z "$SKIPIMG" ]; then cp build/msx1.img /tmp/pk/X86MSX_v$B.img; (cd /tmp/pk && rm -f $O/X86MSX_v$B.img.zip && zip -q $O/X86MSX_v$B.img.zip X86MSX_v$B.img && rm -f X86MSX_v$B.img); fi   # SKIPIMG=1: 이미지 없이 묶기 (mkfs.fat 없는 환경)
# USB추가파일 에는 지난 판과 달라진 파일만 넣는다 (전체 = FULL=1 이면 USB전체파일.zip 도 따로). 기준 = 지난 판 USB추가파일.zip 안 파일들의 SHA-1
rm -f /tmp/pk/usb/BOOTX64.EFI                           # 옛 찌꺼기 (EFI/BOOT/BOOTX64.EFI 만 쓰임)
python3 - "$A" "$B" "$I" "$O" <<'PY2'
import zipfile,hashlib,os,sys,json
A,B,I,O=sys.argv[1:5]
I=I.rstrip('/')+'/'; O=O.rstrip('/')+'/'
usb='/tmp/pk/usb'
base={}
mf='/mnt/user-data/outputs/.usb_manifest_v%s.json'%A.replace('.','')
if os.path.exists(mf): base=json.load(open(mf))
else:
    zp='%sX86MSX_v%s_USB추가파일.zip'%(I,A)
    if os.path.exists(zp):
        for it in zipfile.ZipFile(zp).infolist():
            if not it.filename.endswith('/'): base[it.filename]=hashlib.sha1(zipfile.ZipFile(zp).read(it.filename)).hexdigest()
cur={}
for r,_,fs in os.walk(usb):
    for f in fs:
        fp=os.path.join(r,f); rel=os.path.relpath(fp,usb).replace(os.sep,'/')
        cur[rel]=hashlib.sha1(open(fp,'rb').read()).hexdigest()
json.dump(cur,open('/mnt/user-data/outputs/.usb_manifest_v%s.json'%B.replace('.',''),'w'))
ch=sorted(k for k in cur if base.get(k)!=cur[k]); gone=sorted(k for k in base if k not in cur)
zo=zipfile.ZipFile(O+'X86MSX_v%s_USB추가파일.zip'%B,'w',zipfile.ZIP_DEFLATED)
for k in ch: zo.write(os.path.join(usb,k),k)
note='X86MSX %s USB 추가파일 (바뀐 파일만)\n지난 판 %s 과 달라졌거나 새로 생긴 파일 %d개만 들어 있습니다. USB에 폴더째 덮어 복사하세요.\n\n'%(B,A,len(ch))+'\n'.join(ch)
if gone: note+='\n\n지난 판에는 있었지만 지금은 없는 파일 (지워도 됨):\n'+'\n'.join(gone)
zo.writestr('변경목록.txt',note); zo.close()
if os.environ.get('FULL'):
    zf=zipfile.ZipFile(O+'X86MSX_v%s_USB전체파일.zip'%B,'w',zipfile.ZIP_DEFLATED)
    for k in sorted(cur): zf.write(os.path.join(usb,k),k)
    zf.close()
print('USB 변경 %d / 전체 %d 파일'%(len(ch),len(cur)))
PY2
(cd /home/claude && rm -f $O/X86MSX_src_v$B.zip && zip -qr $O/X86MSX_src_v$B.zip msxemu -x 'msxemu/build/*' -x 'msxemu/*.o' -x 'msxemu/tests/regress/local/*.rom' -x 'msxemu/tests/regress/local/machines/*' -x 'msxemu/tests/regress/local/configs/*')
python3 - "$A" "$B" "$ROW" "$REQ" <<'PY'
import zipfile,os,json,sys,datetime
A,B,row,req=sys.argv[1:5]
I='/mnt/user-data/outputs/v'+A.replace('.','')+'/'; O='/mnt/user-data/outputs/v'+B.replace('.','')+'/'
d=datetime.date.today().strftime('%Y%m%d')
row='| **%s** | %s | 판올림 %s — %s |\n'%(B,datetime.date.today().isoformat(),B,row)
for n in ('X86MSX_설계서_v%s.md','X86MSX_요구사항서_v%s.md'):
    s=open(I+n%A,encoding='utf-8').read().replace('**문서 버전:** '+A,'**문서 버전:** '+B,1)
    i=s.index('| **%s** |'%A); s=s[:i]+row+s[i:]
    if '요구사항서' in n and req:
        j=s.index('| ARCH-0'); s=s[:j]+req+'\n'+s[j:]
    open(O+n%B,'w',encoding='utf-8').write(s)
open(O+'X86MSX_구성도_v%s.html'%B,'w',encoding='utf-8').write(open(I+'X86MSX_구성도_v%s.html'%A,encoding='utf-8').read().replace(A,B))
for n,e in (('미비점목록','xlsx'),('사용설명서','docx')):
    zi=zipfile.ZipFile(f'{I}X86MSX_{n}_v{A}.{e}'); zo=zipfile.ZipFile(f'{O}X86MSX_{n}_v{B}.{e}','w',zipfile.ZIP_DEFLATED)
    for it in zi.infolist():
        dd=zi.read(it.filename)
        if it.filename.endswith('.xml'): dd=dd.replace(A.encode(),B.encode())
        zo.writestr(it,dd)
    zo.close()
out=[]
for f in sorted(os.listdir(O)):
    n=f
    if f not in('BOOTX64.EFI','X86MSX.BIN','RTW.INI'):
        b,e=os.path.splitext(f); n=f[:-8]+'.img_%s.zip'%d if f.endswith('.img.zip') else b+'_'+d+e
    out.append({"devicePath":'H:\\msx\\X86MSX %s\\'%B+n,"stagedPath":O+f})
json.dump(out,open('/tmp/commit.json','w'),ensure_ascii=False)
print(len(out),'files ready')
PY
