/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* v9difftest - 8.0.147 mmv9_diffuse8 (V9990 화면 14 의 오차 확산): 정확히 낼 수 있는 색은 그대로, 그라데이션 평균이 원본과 같음, 글자 줄(plain)은 오차를 안 퍼뜨림 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "../src/mmv9.h"
/* 8.0.147 의 원래 계산 (점마다 나눗셈). 8.0.155 의 표 방식이 이것과 같은 결과를 내는지 보는 기준 */
static int qlv(int v,int n){return (v*n*2+255)/510;}
static int lvv(int q,int n){return (q*255*2+n)/(2*n);}
static void ref_diffuse8(const uint32_t *src,int w,int h,uint8_t *dst,int plain_top,int plain_bot){
    static const int levels[3]={7,7,3}; static int er[2][3][770];
    for(int k=0;k<2;k++)for(int c=0;c<3;c++)for(int x=0;x<w+2;x++)er[k][c][x]=0;
    for(int y=0;y<h;y++){ int (*cur)[770]=er[y&1],(*nxt)[770]=er[(y&1)^1]; int plain=y<plain_top||y>=plain_bot,dir=(y&1)?-1:1;
        for(int c=0;c<3;c++)for(int x=0;x<w+2;x++)nxt[c][x]=0;
        for(int i=0;i<w;i++){ int x=dir>0?i:w-1-i; uint32_t px=src[(size_t)y*(size_t)w+(size_t)x]; int comp[3]={(int)((px>>16)&255),(int)((px>>8)&255),(int)(px&255)},q[3];
            for(int c=0;c<3;c++){ int n=levels[c],v16=comp[c]*16+(plain?0:cur[c][x+1]); if(v16<0)v16=0; else if(v16>255*16)v16=255*16; q[c]=qlv((v16+8)>>4,n);
                if(!plain){ int e=v16-lvv(q[c],n)*16; cur[c][x+1+dir]+=(e*7)>>4; nxt[c][x+1-dir]+=(e*3)>>4; nxt[c][x+1]+=(e*5)>>4; nxt[c][x+1+dir]+=e-((e*7)>>4)-((e*3)>>4)-((e*5)>>4);} }
            dst[(size_t)y*(size_t)w+(size_t)x]=(uint8_t)((q[1]<<5)|(q[0]<<2)|q[2]); } } }
int main(void){
    enum{W=768,H=240}; static uint32_t src[W*H]; static uint8_t dst[W*H]; int fails=0;
    /* 1) 단계 값(정확히 낼 수 있는 색)은 그대로 */
    for(int y=0;y<H;y++)for(int x=0;x<W;x++){int r=((x/96)*510+7)/14,g=((y/30)*510+7)/14,b=((x/192)*510+3)/6; src[y*W+x]=(uint32_t)(r<<16|g<<8|b);}
    mmv9_diffuse8(src,W,H,dst,0,H);
    for(int y=0;y<H;y++)for(int x=0;x<W;x++){int q=((x/96)),gg=(y/30),bb=(x/192); if(dst[y*W+x]!=(uint8_t)(gg<<5|q<<2|bb)){fails++; if(fails<4)printf("exact fail %d,%d %02X\n",x,y,dst[y*W+x]);}}
    /* 2) 부드러운 그라데이션: 4x16 블록 평균이 원본에 가까움 */
    for(int y=0;y<H;y++)for(int x=0;x<W;x++){int v=x*255/(W-1); src[y*W+x]=(uint32_t)(v<<16|v<<8|v);}
    mmv9_diffuse8(src,W,H,dst,0,H);
    double worst=0;
    for(int y=0;y+16<=H;y+=16)for(int x=0;x+16<=W;x+=16){double a=0,b=0;for(int j=0;j<16;j++)for(int i=0;i<16;i++){uint8_t d=dst[(y+j)*W+x+i];int r=(d>>2)&7,g=d>>5,bl=d&3; a+=(r*255.0/7+g*255.0/7)/2; b+=(src[(y+j)*W+x+i]&255);} a/=256;b/=256; double e=fabs(a-b); if(e>worst)worst=e;}
    printf("gradient worst 16x16 mean error %.2f (of 255)\n",worst); if(worst>6){fails++;printf("FAIL gradient\n");}
    /* 3) plain 줄: 오차 안 퍼짐 → 같은 색 줄은 항상 같은 결과 */
    for(int y=0;y<H;y++)for(int x=0;x<W;x++)src[y*W+x]=0x10D040u;
    mmv9_diffuse8(src,W,H,dst,24,H-40);
    for(int x=0;x<W;x++) if(dst[x]!=dst[0]||dst[5*W+x]!=dst[0]){fails++;printf("plain row varies\n");break;}
    /* 4) 8.0.155: 표 방식으로 바꾼 계산이 8.0.147 의 나눗셈 계산(아래 ref_diffuse8)과 바이트까지 같음 (무작위·그라데이션·2색·단계색, 글자 줄 여러 모양) */
    for(int t=0;t<24;t++){ static uint8_t ref[W*H];
        for(int i=0;i<W*H;i++){ int x=i%W,y=i/W; if(t%4==0) src[i]=(uint32_t)(rand()&0xFFFFFF); else if(t%4==1) src[i]=(uint32_t)(((x*255/(W-1))<<16)|((255-x*255/(W-1))<<8)|(y&255)); else if(t%4==2) src[i]=(rand()&1)?0xFFFFFFu:0u; else src[i]=(uint32_t)(((rand()%8)*36<<16)|((rand()%8)*36<<8)|((rand()%4)*85)); }
        int pt=(t%3)?24:0, pb=(t%3)==2?H:H-40; if(t%5==0){pt=0;pb=H;}
        ref_diffuse8(src,W,H,ref,pt,pb); mmv9_diffuse8(src,W,H,dst,pt,pb);
        if(memcmp(ref,dst,(size_t)W*H)){fails++;printf("FAIL: 표 방식이 옛 계산과 다름 (시험 %d)\n",t);} }
    printf(fails?"difftest %d FAIL\n":"difftest: all passed\n",fails); return fails!=0;
}
