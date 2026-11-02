#include "etc_oracle.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL ETC_ORACLE line=%d\n",__LINE__);return 1;}}while(0)
int main(void)
{
 uint8_t block[16]={0};float rgba[16][4];
 for(unsigned f=0;f<10;f++) {
  CHECK(etc_oracle_decode(f,block,rgba));
  for(unsigned i=0;i<16;i++) {
   if(f==0||f==4)for(unsigned c=0;c<3;c++)CHECK(fabsf(rgba[i][c]-2.0f/255.0f)<1e-8f);
   if(f==2||f==3)for(unsigned c=0;c<3;c++)CHECK(rgba[i][c]==0);
   if(f>=6) {float expected=(f&1)?-3.0f/1023.0f:1.0f/2047.0f;CHECK(rgba[i][0]==expected);if(f>=8)CHECK(rgba[i][1]==expected);CHECK(rgba[i][2]==0);}
   CHECK(rgba[i][3]==(f==4||f==5?0:1));
  }
 }
 block[0]=block[8]=0x80;
 for(unsigned f=7;f<=9;f+=2) {CHECK(etc_oracle_decode(f,block,rgba));for(unsigned i=0;i<16;i++)for(unsigned c=0;c<(f==9?2:1);c++)CHECK(rgba[i][c]==-1019.0f/1023.0f);}
 memset(block,0,sizeof(block));block[0]=10;block[1]=0x10;
 CHECK(etc_oracle_decode(6,block,rgba));for(unsigned i=0;i<16;i++)CHECK(rgba[i][0]==60.0f/2047.0f);
 CHECK(etc_oracle_decode(7,block,rgba));for(unsigned i=0;i<16;i++)CHECK(rgba[i][0]==56.0f/1023.0f);
 block[0]=246;
 CHECK(etc_oracle_decode(7,block,rgba));for(unsigned i=0;i<16;i++)CHECK(rgba[i][0]==-104.0f/1023.0f);
 memset(block,0,sizeof(block));block[1]=0x20;
 uint64_t selectors=0;for(unsigned i=0;i<16;i++)selectors|=4ull<<(45-3*i);
 for(unsigned i=0;i<6;i++)block[2+i]=(uint8_t)(selectors>>(40-8*i));
 CHECK(etc_oracle_decode(6,block,rgba));for(unsigned i=0;i<16;i++)CHECK(rgba[i][0]==36.0f/2047.0f);
 CHECK(etc_oracle_decode(7,block,rgba));for(unsigned i=0;i<16;i++)CHECK(rgba[i][0]==32.0f/1023.0f);
 memset(block,0,sizeof(block));block[0]=block[1]=block[2]=0x88;
 CHECK(etc_oracle_decode(1,block,rgba));
 double encoded=138.0/255.0;float linear=(float)pow((encoded+0.055)/1.055,2.4);
 for(unsigned i=0;i<16;i++){for(unsigned c=0;c<3;c++)CHECK(fabsf(rgba[i][c]-linear)<1e-7f);CHECK(rgba[i][3]==1);}
 memset(block,0,sizeof(block));block[4]=block[5]=255;
 for(unsigned f=2;f<=3;f++){CHECK(etc_oracle_decode(f,block,rgba));for(unsigned i=0;i<16;i++)for(unsigned c=0;c<4;c++)CHECK(rgba[i][c]==0);}
 uint32_t state=0x731aa512u;
 for(unsigned trial=0;trial<4096;trial++) {
  for(unsigned j=0;j<16;j++){state=1664525u*state+1013904223u;block[j]=state>>24;}
  for(unsigned f=0;f<10;f++){CHECK(etc_oracle_decode(f,block,rgba));for(unsigned i=0;i<16;i++)for(unsigned c=0;c<4;c++)CHECK(isfinite(rgba[i][c])&&rgba[i][c]>=-1&&rgba[i][c]<=1);}
 }
 puts("PASS ETC_ORACLE knownsigned/unsigned/punchthrough/default/sRGBdomain+40960decodedblocks;CPUonly");return 0;
}
