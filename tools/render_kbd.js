/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
// 도안(.dc.html) → 964x358 PNG. 글꼴은 npm @fontsource/share-tech-mono, noto-sans-jp, noto-sans-kr 를 /tmp/kf 에 받아 쓴다. 도안 파일은 kbd_design/ 에 둔다.
const fs=require('fs'), path=require('path');
const {chromium}=require('playwright');
const SRC=__dirname+'/kbd_design/';
const FILES={JP1:'KeyboardBase',DPC:'KeyboardDpc200',CPC:'KeyboardCpc300',A1:'KeyboardA1',WSX:'KeyboardA1wsx',CF:'KeyboardCf2000',GT:'Keyboard'};
class DCLogic{constructor(){this.props={};}}
const FONT=`
@font-face{font-family:'Share Tech Mono';font-weight:400 700;src:url(file:///tmp/kf/node_modules/@fontsource/share-tech-mono/files/share-tech-mono-latin-400-normal.woff2)}
@font-face{font-family:'Noto Sans JP';font-weight:500;src:url(file:///tmp/kf/node_modules/@fontsource/noto-sans-jp/files/noto-sans-jp-japanese-500-normal.woff2)}
@font-face{font-family:'Noto Sans JP';font-weight:700;src:url(file:///tmp/kf/node_modules/@fontsource/noto-sans-jp/files/noto-sans-jp-japanese-700-normal.woff2)}
@font-face{font-family:'Noto Sans KR';font-weight:500;src:url(file:///tmp/kf/node_modules/@fontsource/noto-sans-kr/files/noto-sans-kr-korean-500-normal.woff2)}
@font-face{font-family:'Noto Sans KR';font-weight:700;src:url(file:///tmp/kf/node_modules/@fontsource/noto-sans-kr/files/noto-sans-kr-korean-700-normal.woff2)}
`;
function page(id){
  const h=fs.readFileSync(SRC+FILES[id]+'.dc.html','utf8');
  const js=h.split('<script type="text/x-dc" data-dc-script')[1].split('>').slice(1).join('>').split('</script>')[0];
  const C=new Function('DCLogic',js+'; return Component;')(DCLogic);
  const v=new C().renderVals();
  const style=(h.match(/<style>([\s\S]*?)<\/style>/)||['',''])[1];
  let body=h.split('<x-dc>')[1].split('</x-dc>')[0];
  body=body.replace(/<helmet>[\s\S]*?<\/helmet>/,'');
  // sc-for 확장
  body=body.replace(/<sc-for[^>]*>([\s\S]*?)<\/sc-for>/,(m,tpl)=>v.keys.map(k=>tpl.replace(/\{\{k\.(\w+)\}\}/g,(a,n)=>(k[n]==null?'':k[n]))).join('\n'));
  if(v.cf) body=body.replace(/\{\{cf\.(\w+)\}\}/g,(a,n)=>v.cf[n]);
  return `<!doctype html><html><head><meta charset="utf-8"><style>${FONT}${style}.k:hover{filter:none!important;transform:none!important}html,body{background:transparent!important;margin:0}</style></head><body>${body}</body></html>`;
}
(async()=>{
  const br=await chromium.launch(); const pg=await br.newPage({viewport:{width:1100,height:500},deviceScaleFactor:1});
  for(const id of Object.keys(FILES)){
    fs.writeFileSync(__dirname+'/kbd_design/'+id+'.html',page(id));
    await pg.goto('file://'+__dirname+'/kbd_design/'+id+'.html'); await pg.evaluate(()=>document.fonts.ready); await pg.waitForTimeout(300);
    const box=await pg.evaluate(()=>{const e=document.querySelector('[role=img]');const r=e.getBoundingClientRect();return [r.x,r.y,r.width,r.height];});
    // 안쪽 모서리(테두리 1px 안): 964x358
    await pg.screenshot({path:__dirname+'/kbd_design/'+id+'.png',clip:{x:box[0]+1,y:box[1]+1,width:964,height:358},omitBackground:true});
    console.log(id,box.map(Math.round).join(','));
  }
  await br.close();
})();
