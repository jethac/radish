#include "etc_oracle.h"
#include "vendor/detex.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#define ETC_SIDE 16
#define REQUIRE(x) do {if(!(x))return 1;} while(0)
static uint32_t etc_rand(uint32_t *s){*s=*s*1664525u+1013904223u;return *s;}
static int etc_block(unsigned f,unsigned m,unsigned layer,unsigned bx,unsigned by,unsigned round,uint8_t block[16]) {
 unsigned width=ETC_SIDE>>m,nb=(width+3)/4,id=round*97+m*13+layer*nb*nb+by*nb+bx;
 uint32_t state=0x17135a29u^f*5317u^id*1234567u;
 for(unsigned attempt=0;attempt<4096;attempt++) {
  for(unsigned i=0;i<16;i++)block[i]=etc_rand(&state)>>24;
  if(f>=6){if((f==7||f==9)&&m==1&&layer==1&&!bx&&!by)block[0]=0x80;return 0;}
  uint8_t *color=block+(f>=4?8:0);unsigned mode=f==2||f==3?detexGetModeETC2_PUNCHTHROUGH(color):detexGetModeETC2(color);
  unsigned desired=f==2||f==3?1+id%4:id%5;
  if(mode!=desired)continue;
  if(f==2||f==3){if(id&1)color[3]|=2;else color[3]&=~2;}
  return 0;
 }
 REQUIRE(0);return 1;
}
int main(void) {
 const unsigned f=1,m=1,layer=2,round=0;
 double sum[4]={0};
 for(unsigned y=4;y<=5;y++)for(unsigned x=3;x<=4;x++) {
  uint8_t block[16];float rgba[16][4];
  if(etc_block(f,m,layer,x/4,y/4,round,block)||!etc_oracle_decode(f,block,rgba))return 1;
  printf("TAP x=%u y=%u mode=%u block=",x,y,detexGetModeETC2(block));
  for(unsigned i=0;i<8;i++)printf("%02x",block[i]);
  printf(" linear=");for(unsigned i=0;i<4;i++){double v=rgba[(y%4)*4+x%4][i];sum[i]+=v;printf(" %.12g",v);}
  putchar('\n');
 }
 printf("MEAN");for(unsigned i=0;i<4;i++)printf(" %.12g",sum[i]/4);putchar('\n');
 return 0;
}
