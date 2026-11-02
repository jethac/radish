#include "etc_oracle.h"
#include "vendor/detex.h"
#include <math.h>
#include <string.h>

static float srgb_linear(unsigned byte)
{
 double x=(double)byte/255.0;
 return (float)(x<=0.04045?x/12.92:pow((x+0.055)/1.055,2.4));
}
bool etc_oracle_decode(unsigned format,const uint8_t input[16],float rgba[16][4])
{
 if(format>=10)return false;
 uint8_t block[16]; memcpy(block,input,16);
 if(format<6) {
  uint32_t pixels[16]; bool ok;
  if(format<2)ok=detexDecompressBlockETC2(block,DETEX_MODE_MASK_ALL_MODES_ETC2,0,(uint8_t *)pixels);
  else if(format<4)ok=detexDecompressBlockETC2_PUNCHTHROUGH(block,DETEX_MODE_MASK_ALL_MODES_ETC2_PUNCHTHROUGH,0,(uint8_t *)pixels);
  else ok=detexDecompressBlockETC2_EAC(block,DETEX_MODE_MASK_ALL_MODES_ETC2,0,(uint8_t *)pixels);
  if(!ok)return false;
  for(unsigned i=0;i<16;i++) {
   unsigned bytes[4]={detexPixel32GetR8(pixels[i]),detexPixel32GetG8(pixels[i]),detexPixel32GetB8(pixels[i]),detexPixel32GetA8(pixels[i])};
   for(unsigned c=0;c<4;c++)rgba[i][c]=(format&1)&&c<3?srgb_linear(bytes[c]):bytes[c]/255.0f;
  }
 } else {
  bool sign=format==7||format==9;unsigned channels=format>=8?2:1;
  if(sign)for(unsigned c=0;c<channels;c++)if(block[8*c]==0x80)block[8*c]=0x81;
  uint16_t values[32]; bool ok;
  if(format==6)ok=detexDecompressBlockEAC_R11(block,0,0,(uint8_t *)values);
  else if(format==7)ok=detexDecompressBlockEAC_SIGNED_R11(block,0,0,(uint8_t *)values);
  else if(format==8)ok=detexDecompressBlockEAC_RG11(block,0,0,(uint8_t *)values);
  else ok=detexDecompressBlockEAC_SIGNED_RG11(block,0,0,(uint8_t *)values);
  if(!ok)return false;
  for(unsigned i=0;i<16;i++) {
   rgba[i][0]=rgba[i][1]=rgba[i][2]=0;rgba[i][3]=1;
   for(unsigned c=0;c<channels;c++) {
    unsigned raw=values[i*channels+c];
    if(sign) {int value=(raw&0x8000)?(int)raw-65536:(int)raw;int eleven=value<0?-((-value)>>5):value>>5;rgba[i][c]=eleven/1023.0f;}
    else rgba[i][c]=(raw>>5)/2047.0f;
   }
  }
 }
 return true;
}
