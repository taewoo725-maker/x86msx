/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
// 도안(.dc.html) + 구운 그림(.png) → kernel/fe_kbdlay.inc  (사용: node tools/gen_kbdlay.js, 먼저 node tools/render_kbd.js)
const fs=require('fs'), path=require('path');
const D=path.join(__dirname,'kbd_design')+'/';
class DCLogic{ constructor(){ this.props={}; } }
function vals(file){
  const h=fs.readFileSync(D+file,'utf8');
  let js=h.split('<script type="text/x-dc" data-dc-script')[1].split('>').slice(1).join('>').split('</script>')[0];
  const C=new Function('DCLogic',js+'; return Component;')(DCLogic);
  return new C().renderVals();
}
const KP={'7':[10,2],'8':[10,3],'9':[10,4],'/':[9,2],'4':[9,7],'5':[10,0],'6':[10,1],'*':[9,0],'1':[9,4],'2':[9,5],'3':[9,6],'-':[10,5],'0':[9,3],'.':[10,7],',':[10,6],'+':[9,1]};
const M={'F1':[6,5],'F2':[6,6],'F3':[6,7],'F4':[7,0],'F5':[7,1],'ESC':[7,2],'TAB':[7,3],'STOP':[7,4],'BS':[7,5],'SELECT':[7,6],'RETURN':[7,7],'HOME':[8,1],'INS':[8,2],'DEL':[8,3],
 'SHIFT':[6,0],'CTRL':[6,1],'GRAPH':[6,2],'CAPS':[6,3],'かな':[6,4],'KANA':[6,4],'한글':[6,4],'SPACE':[8,0],
 '0':[0,0],'1':[0,1],'2':[0,2],'3':[0,3],'4':[0,4],'5':[0,5],'6':[0,6],'7':[0,7],'8':[1,0],'9':[1,1],'-':[1,2],'^':[1,3],'¥':[1,4],'₩':[1,4],'@':[1,5],'[':[1,6],';':[1,7],':':[2,0],']':[2,1],',':[2,2],'.':[2,3],'/':[2,4],'_':[2,5]};
const ARROW={u:[8,5],l:[8,4],r:[8,7],d:[8,6]};
const DPC_SYM={'-':[1,2],'=':[1,3],'\\':[1,4],'[':[1,5],']':[1,6],';':[1,7],"'":[2,0],'`':[2,1],',':[2,2],'.':[2,3],'/':[2,4],'₩':[2,5]};
// 모양 번호 (fe_keyed.inc 의 km_poly 와 같은 순서). 0 = 사각형
const FILE={JP1:'Default',DPC:'DPC-200',CPC:'CPC-300',A1:'FS-A1',WSX:'FS-A1WSX',CF:'CF-2000',GT:'FS-A1GT'};   // Icons\\Keyboard\\<이름>.png (assets/keyboard 에 복사)
const SH={rect:0,gt_u:1,gt_d:2,gt_l:3,gt_r:4,ws_u:5,ws_d:6,ws_l:7,ws_r:8,ret:9,diamond:10,ellipse:11};
function px(s,k){ return +new RegExp(k+':(-?\\d+)px').exec(s)[1]; }
const BOARDS=[
 {id:'JP1',file:'KeyboardBase.dc.html',name:'기본 키보드'},
 {id:'DPC',file:'KeyboardDpc200.dc.html',name:'Daewoo DPC-200'},
 {id:'CPC',file:'KeyboardCpc300.dc.html',name:'Daewoo CPC-300'},
 {id:'A1',file:'KeyboardA1.dc.html',name:'Panasonic FS-A1'},
 {id:'WSX',file:'KeyboardA1wsx.dc.html',name:'Panasonic FS-A1WSX'},
 {id:'CF',file:'KeyboardCf2000.dc.html',name:'National CF-2000'},
 {id:'GT',file:'Keyboard.dc.html',name:'Panasonic FS-A1GT'},
];
const res=[];
BOARDS.forEach(b=>{
  const v=vals(b.file); const keys=[];
  v.keys.forEach(q=>{
    const cls=q.cls.split(/\s+/);
    const x=px(q.pos,'left'),y=px(q.pos,'top'),w=px(q.pos,'width'),h=px(q.pos,'height');
    let l=q.label, code;
    const arrow=cls.includes('cu')?'u':cls.includes('cd')?'d':cls.includes('cl')?'l':cls.includes('cr')?'r':'';
    const big=cls.includes('big'), np=cls.includes('k-np');
    if(arrow) code=ARROW[arrow];
    else if(l&&'▲◀▶▼↑←↓→'.includes(l)) code=ARROW[{'▲':'u','↑':'u','◀':'l','←':'l','▶':'r','→':'r','▼':'d','↓':'d'}[l]];
    else if((big||np)&&KP[l]) code=KP[l];
    else if(b.id==='DPC'&&DPC_SYM[l]&&!/^[A-Z0-9]$/.test(l)) code=DPC_SYM[l];
    else {
      let ll=l; if(ll.length>1&&ll.includes('/')) ll=ll.split('/').pop();
      if(/^[A-Z]$/.test(ll)){ const ix=22+ll.charCodeAt(0)-65; code=[ix>>3,ix&7]; }
      else if(ll===''&&w>h*3) code=M['SPACE'];
      else code=M[ll];
    }
    if(!code) return;                                   // 꾸밈 키(PAUSE 등)는 그림에만 있다
    let shape=SH.rect;
    if(arrow&&(b.id==='GT'||b.id==='WSX')) shape=SH[(b.id==='GT'?'gt_':'ws_')+arrow];
    else if(arrow&&cls.includes('circ')) shape=SH.ellipse;
    else if(cls.includes('ret')) shape=SH.ret;
    keys.push({code:code[0]*8+code[1]+1,shape,x,y,w,h,hx:x,hy:y,hw:w,hh:h,arrow});
  });
  // 오각형 방향키(GT·WSX): 도안 사각형이 서로 겹치므로 누르는 자리는 가운데 위·아래 + 양옆으로 나눈다 (그림·강조 모양은 그대로)
  if(b.id==='GT'||b.id==='WSX'){
    const f=a=>keys.find(k=>k.arrow===a);
    const U=f('u'),L=f('l'),R=f('r'); const lw=Math.round(L.w*0.62);
    L.hx=U.x-4-lw; L.hw=lw; R.hx=U.x+U.w+4; R.hw=lw;
  }
  // 도안에 사각형이 없는 방향키: DPC-200 X자 판(칸마다 작은 자리), CF-2000 마름모
  if(b.id==='DPC'){
    [['u',851-22,140-20],['l',810-20,184-20],['r',892-20,184-20],['d',851-22,227-20]].forEach(([a,x,y])=>
      keys.push({code:ARROW[a][0]*8+ARROW[a][1]+1,shape:0,x:x,y:y,w:44,h:40,hx:x,hy:y,hw:44,hh:40,arrow:a}));
  }
  if(b.id==='CF'){
    const bx=776,by=105;
    [['u',80,44],['l',40,88],['r',120,88],['d',80,132]].forEach(([a,cx,cy])=>
      keys.push({code:ARROW[a][0]*8+ARROW[a][1]+1,shape:SH.diamond,x:Math.round(bx+cx-37),y:Math.round(by+cy-40),w:74,h:81,hx:Math.round(bx+cx-25),hy:Math.round(by+cy-25),hw:50,hh:50,arrow:a}));
  }
  console.log(b.name,'키',keys.length);
  res.push({b,keys});
});
let c='/* 자동 생성 (tools/gen_kbdlay.js): 기종 키보드 그림 7종. 그림 = Icons\\Keyboard\\<이름>.png (도안을 구운 964x358, assets/keyboard), 키 목록은 고르기·강조용.\n * { MSX 키 코드(1+행*8+비트), 모양, 강조 상자 x,y,w,h, 누르는 자리 x,y,w,h } 단위 = 도안 화소 */\n';
res.forEach(({b,keys})=>{
  fs.copyFileSync(D+b.id+'.png', path.join(__dirname,'..','assets','keyboard',FILE[b.id]+'.png'));
  c+=`static const KmKey km_lay_${b.id}[] = { /* ${b.name} */\n`;
  keys.forEach((k,j)=>{ c+=`{${k.code},${k.shape},${k.x},${k.y},${k.w},${k.h},${k.hx},${k.hy},${k.hw},${k.hh}},`+((j%4==3)?'\n':' '); });
  c+=`{255,0,${964-170},${358+6},160,24,${964-170},${358+6},160,24}\n};\n`;
});
c+='static const KmLay km_lays[] = {\n';
res.forEach(({b,keys})=>{ c+=`    { km_lay_${b.id}, ${keys.length+1}, 964, ${358+34}, "${FILE[b.id]}.png" },\n`; });
c+='};\n';
fs.writeFileSync(path.join(__dirname,'..','kernel','fe_kbdlay.inc'),c);
