#include "test_common.h"
#include "rd_private.h"
#include <inttypes.h>

enum { NX=17, NY=5, N=NX*NY, GUARD=16, FIELDS=36, BUFFER_COUNT=9 };
enum { OUTPUT, UNIFORMS, SEEDS, SOURCE_TEXELS, TARGET_TEXELS, IMAGE_UPLOAD,
       SAMPLE_UPLOAD, IMAGE_READBACK, COPY_READBACK };
struct storage {
   VkBuffer buffer; VkDeviceMemory memory;
   uint32_t *map, *saved;
   size_t words, binding_words;
};
struct image {
   VkImage image; VkImageView view; VkDeviceMemory memory;
   uint32_t *map; size_t words, binding_words;
   uint32_t width,height,row_words;
};
struct array_image { struct image image; VkSubresourceLayout layout[12][2]; uint32_t *saved; };
static uint32_t array_value(unsigned source,unsigned layer,unsigned lod,unsigned x,unsigned y)
{ return 0x61000000u+source*0x01000000u+layer*0x10000u+lod*0x1000u+y*16u+x; }
static float filtered_value(unsigned source,unsigned layer,unsigned lod,int x,int y)
{ return source*16.0f+layer*4.0f+lod*2.0f+x*0.25f+y*0.5f; }
static int floor_int(float x) { int i=(int)x; return i>x?i-1:i; }
static int wrap_index(int i,int n,unsigned wrap)
{
   if (wrap==1) return i<0?0:i>=n?n-1:i;
   int period=wrap==2?n*2:n; i%=period; if (i<0) i+=period;
   return wrap==2 && i>=n?period-1-i:i;
}
static float filtered_pixel(unsigned source,unsigned layer,unsigned lod,int x,int y,unsigned wrap)
{
   int n=4>>lod;
   if (wrap==3 && (x<0 || y<0 || x>=n || y>=n)) return 1.0f;
   return filtered_value(source,layer,lod,wrap_index(x,n,wrap),wrap_index(y,n,wrap));
}
static float filtered_linear(unsigned source,unsigned layer,unsigned lod,float s,float t,unsigned wrap)
{
   int n=4>>lod; float u=s*n-0.5f,v=t*n-0.5f;
   int x=floor_int(u),y=floor_int(v); float a=u-x,b=v-y;
   float lo=filtered_pixel(source,layer,lod,x,y,wrap)*(1-a)+filtered_pixel(source,layer,lod,x+1,y,wrap)*a;
   float hi=filtered_pixel(source,layer,lod,x,y+1,wrap)*(1-a)+filtered_pixel(source,layer,lod,x+1,y+1,wrap)*a;
   return lo*(1-b)+hi*b;
}

static const int cube_normal[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
static const int cube_s[6][3]={{0,0,-1},{0,0,1},{1,0,0},{1,0,0},{1,0,0},{-1,0,0}};
static const int cube_t[6][3]={{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}};
static float cube_value(unsigned source,unsigned face,unsigned lod,int x,int y)
{ return source*16.0f+face*2.0f+lod*0.5f+x*0.0625f+y*0.125f; }
static double cube_dot(const int a[3],const double b[3])
{ return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static unsigned cube_project(const double direction[3],double *s,double *t)
{
   unsigned face=5; double major=cube_dot(cube_normal[face],direction);
   for (int f=4;f>=0;f--) { double d=cube_dot(cube_normal[f],direction); if (d>major) { face=f; major=d; } }
   *s=0.5*(cube_dot(cube_s[face],direction)/major+1.0);
   *t=0.5*(cube_dot(cube_t[face],direction)/major+1.0);
   return face;
}
static float cube_pixel(unsigned source,unsigned face,unsigned lod,int x,int y)
{
   int n=4>>lod;
   if (x<0 || y<0 || x>=n || y>=n) {
      double u=(2.0*x+1.0)/n-1.0,v=(2.0*y+1.0)/n-1.0,direction[3],s,t;
      for (unsigned c=0;c<3;c++) direction[c]=cube_normal[face][c]+u*cube_s[face][c]+v*cube_t[face][c];
      face=cube_project(direction,&s,&t); x=(int)(s*n); y=(int)(t*n);
      if (x<0) x=0; if (y<0) y=0; if (x>=n) x=n-1; if (y>=n) y=n-1;
   }
   return cube_value(source,face,lod,x,y);
}
static float cube_linear(unsigned source,unsigned face,unsigned lod,double s,double t)
{
   int n=4>>lod; double u=s*n-0.5,v=t*n-0.5;
   int x=(int)u,y=(int)v; if (x>u) x--; if (y>v) y--;
   double a=u-x,b=v-y;
   double lo=cube_pixel(source,face,lod,x,y)*(1-a)+cube_pixel(source,face,lod,x+1,y)*a;
   double hi=cube_pixel(source,face,lod,x,y+1)*(1-a)+cube_pixel(source,face,lod,x+1,y+1)*a;
   return (float)(lo*(1-b)+hi*b);
}
static void cube_direction(unsigned face,unsigned row,double direction[3])
{
   static const double us[5]={0.0,0.875,0.875,-0.875,0.125};
   static const double vs[5]={0.0,0.125,0.875,0.125,-0.875};
   for (unsigned c=0;c<3;c++) direction[c]=cube_normal[face][c]+us[row]*cube_s[face][c]+vs[row]*cube_t[face][c];
}
static uint32_t poison(size_t i,unsigned id,unsigned round)
{ return 0x9a000000u ^ (uint32_t)(i*103u) ^ (id*0x00110000u) ^ (round*0x20000000u); }
static uint32_t float_word(float f) { uint32_t u; memcpy(&u,&f,4); return u; }
static uint32_t source_texel(unsigned i) { return 0x45000000u+i*13u; }
static uint32_t seed_value(unsigned i) { return 0x34000000u+i*11u; }
static uint32_t image_value(unsigned i) { return 0x23000000u+i*7u; }
static uint32_t target_texel(unsigned i) { return 0x12000000u+i*3u; }
static float sample_value(unsigned x,unsigned y) { return (4*y+x)*0.25f; }
static unsigned memory_type(const VkMemoryRequirements *r,
                            const VkPhysicalDeviceMemoryProperties *p)
{
   for (unsigned i=0;i<p->memoryTypeCount;i++)
      if ((r->memoryTypeBits&(1u<<i)) && (p->memoryTypes[i].propertyFlags &
          (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
          (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) return i;
   return UINT32_MAX;
}
static int storage_create(const struct vk_device_dispatch_table *d,VkDevice device,
   const VkPhysicalDeviceMemoryProperties *properties,VkDeviceSize bytes,
   VkBufferUsageFlags usage,struct storage *s)
{
   VkBufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=bytes,.usage=usage};
   SUCCESS(d->CreateBuffer(device,&ci,NULL,&s->buffer));
   VkMemoryRequirements r; d->GetBufferMemoryRequirements(device,s->buffer,&r);
   REQUIRE(r.size>=bytes && r.size<1024*1024 && r.alignment && !(r.alignment%4));
   unsigned type=memory_type(&r,properties); REQUIRE(type!=UINT32_MAX);
   VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize=r.alignment+r.size+64,.memoryTypeIndex=type};
   REQUIRE(!(ai.allocationSize%4));
   SUCCESS(d->AllocateMemory(device,&ai,NULL,&s->memory));
   SUCCESS(d->BindBufferMemory(device,s->buffer,s->memory,r.alignment));
   SUCCESS(d->MapMemory(device,s->memory,0,VK_WHOLE_SIZE,0,(void **)&s->map));
   s->words=ai.allocationSize/4; s->binding_words=r.alignment/4;
   s->saved=malloc(ai.allocationSize); REQUIRE(s->saved);
   return 0;
}
static int image_create(const struct vk_device_dispatch_table *d,VkDevice device,
   const VkPhysicalDeviceMemoryProperties *properties,VkFormat format,unsigned width,
   unsigned height,VkImageUsageFlags usage,struct image *image,bool split)
{
   VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
      .format=format,.extent={width,height,1},.mipLevels=1,.arrayLayers=1,
      .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_LINEAR,
      .usage=usage|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
   bool depth=format==VK_FORMAT_D16_UNORM || format==VK_FORMAT_D32_SFLOAT;
   VkImageAspectFlags aspect=depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT;
   if (depth) ci.initialLayout=VK_IMAGE_LAYOUT_PREINITIALIZED;
   ci.flags=depth?0:VK_IMAGE_CREATE_ALIAS_BIT|VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
   if (format==VK_FORMAT_R32_UINT) ci.format=VK_FORMAT_R32_SFLOAT;
   if (split) { ci.flags|=VK_IMAGE_CREATE_SPLIT_INSTANCE_BIND_REGIONS_BIT; ci.tiling=VK_IMAGE_TILING_OPTIMAL; }
   SUCCESS(d->CreateImage(device,&ci,NULL,&image->image));
   VkImageMemoryRequirementsInfo2 ri={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,.image=image->image};
   VkMemoryRequirements2 rr={.sType=VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
   d->GetImageMemoryRequirements2(device,&ri,&rr); VkMemoryRequirements r=rr.memoryRequirements;
   uint32_t sparse=UINT32_MAX; d->GetImageSparseMemoryRequirements(device,image->image,&sparse,NULL); REQUIRE(!sparse);
   VkImageSparseMemoryRequirementsInfo2 si={.sType=VK_STRUCTURE_TYPE_IMAGE_SPARSE_MEMORY_REQUIREMENTS_INFO_2,.image=image->image};
   sparse=UINT32_MAX; d->GetImageSparseMemoryRequirements2(device,&si,&sparse,NULL); REQUIRE(!sparse);
   unsigned type=memory_type(&r,properties); REQUIRE(type!=UINT32_MAX);
   REQUIRE(r.alignment && !(r.alignment%4) && r.size<1024*1024);
   VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize=r.alignment+r.size+64,.memoryTypeIndex=type};
   SUCCESS(d->AllocateMemory(device,&ai,NULL,&image->memory));
   VkRect2D rectangle={{0,0},{width,height}};
   VkBindImageMemoryDeviceGroupInfo group={.sType=VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_DEVICE_GROUP_INFO,
      .splitInstanceBindRegionCount=split?1:0,.pSplitInstanceBindRegions=split?&rectangle:NULL};
   VkBindImageMemoryInfo bind={.sType=VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,.pNext=&group,.image=image->image,.memory=image->memory,.memoryOffset=r.alignment};
   SUCCESS(d->BindImageMemory2(device,1,&bind));
   SUCCESS(d->MapMemory(device,image->memory,0,VK_WHOLE_SIZE,0,(void **)&image->map));
   image->words=ai.allocationSize/4; image->binding_words=r.alignment/4;
   VkImageSubresource sub={.aspectMask=aspect}; VkSubresourceLayout layout;
   if (!split) { d->GetImageSubresourceLayout(device,image->image,&sub,&layout); } else {
      struct rd_image *internal=rd_image_from_handle(image->image);
      layout=(VkSubresourceLayout){.offset=internal->levels[0].offset,.rowPitch=internal->levels[0].row_pitch};
   }
   REQUIRE(layout.offset==0 && !(layout.rowPitch%4) && layout.rowPitch>=width*(format==VK_FORMAT_D16_UNORM?2:4));
   image->width=width; image->height=height; image->row_words=layout.rowPitch/4;
   VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image->image,
      .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=format,
      .subresourceRange={aspect,0,1,0,1}};
   SUCCESS(d->CreateImageView(device,&vi,NULL,&image->view));
   return 0;
}
static int array_image_create(const struct vk_device_dispatch_table *d,VkDevice device,
   const VkPhysicalDeviceMemoryProperties *properties,struct array_image *array,unsigned source,VkFormat format,bool cube)
{
   struct image *image=&array->image;
   VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
      .flags=cube?VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT:0,
      .format=format,.extent={4,4,1},.mipLevels=2,.arrayLayers=cube?12:2,
      .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_LINEAR,
      .usage=VK_IMAGE_USAGE_SAMPLED_BIT,.initialLayout=VK_IMAGE_LAYOUT_PREINITIALIZED};
   SUCCESS(d->CreateImage(device,&ci,NULL,&image->image));
   VkMemoryRequirements r; d->GetImageMemoryRequirements(device,image->image,&r);
   unsigned type=memory_type(&r,properties); REQUIRE(type!=UINT32_MAX);
   REQUIRE(r.size<1024*1024 && r.alignment && !(r.alignment%4));
   VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize=r.alignment+r.size+64,.memoryTypeIndex=type};
   SUCCESS(d->AllocateMemory(device,&ai,NULL,&image->memory));
   VkBindImageMemoryInfo bind={.sType=VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,
      .image=image->image,.memory=image->memory,.memoryOffset=r.alignment};
   SUCCESS(d->BindImageMemory2(device,1,&bind));
   SUCCESS(d->MapMemory(device,image->memory,0,VK_WHOLE_SIZE,0,(void **)&image->map));
   image->words=ai.allocationSize/4; image->binding_words=r.alignment/4;
   for (size_t i=0;i<image->words;i++) image->map[i]=poison(i,BUFFER_COUNT+6+source,0);
   for (unsigned layer=cube?6:0;layer<(cube?12:2);layer++) for (unsigned lod=0;lod<2;lod++) {
      VkImageSubresource sub={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.mipLevel=lod,.arrayLayer=layer};
      VkSubresourceLayout *layout=&array->layout[layer][lod];
      d->GetImageSubresourceLayout(device,image->image,&sub,layout);
      REQUIRE(!(layout->offset%4) && !(layout->rowPitch%4) && layout->rowPitch>=(4u>>lod)*4u);
      for (unsigned y=0;y<(4u>>lod);y++) for (unsigned x=0;x<(4u>>lod);x++) {
         size_t word=image->binding_words+layout->offset/4+y*(layout->rowPitch/4)+x;
         REQUIRE(word<image->words);
         image->map[word]=cube?float_word(cube_value(source,layer-6,lod,x,y)):
            format==VK_FORMAT_R32_UINT?array_value(source,layer,lod,x,y):
            float_word(filtered_value(source,layer,lod,x,y));
      }
   }
   array->saved=malloc(ai.allocationSize); REQUIRE(array->saved);
   memcpy(array->saved,image->map,ai.allocationSize);
   VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image->image,
      .viewType=cube?VK_IMAGE_VIEW_TYPE_CUBE:VK_IMAGE_VIEW_TYPE_2D_ARRAY,.format=format,
      .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,2,cube?6:0,cube?6:2}};
   SUCCESS(d->CreateImageView(device,&vi,NULL,&image->view));
   return 0;
}

static int buffer_view(const struct vk_device_dispatch_table *d,VkDevice device,
                      VkBuffer buffer,VkBufferView *view)
{
   VkBufferViewCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
      .buffer=buffer,.format=VK_FORMAT_R32_UINT,.offset=GUARD*4,.range=VK_WHOLE_SIZE};
   SUCCESS(d->CreateBufferView(device,&ci,NULL,view));
   REQUIRE(rd_buffer_view_from_handle(*view)->vk.elements==N);
   return 0;
}
static uint32_t *read_spirv(const char *path,size_t *size)
{
   FILE *f=fopen(path,"rb"); if (!f) return NULL;
   if (fseek(f,0,SEEK_END)) { fclose(f); return NULL; }
   long n=ftell(f); if (n<=0 || n%4 || n>1024*1024) { fclose(f); return NULL; }
   rewind(f); uint32_t *p=malloc(n);
   if (!p || fread(p,1,n,f)!=(size_t)n) { free(p); fclose(f); return NULL; }
   fclose(f); *size=n; return p;
}
static void barriers(const struct vk_device_dispatch_table *d,VkCommandBuffer command,
                     struct image images[2],bool before)
{
   VkImageMemoryBarrier b[2];
   for (unsigned i=0;i<2;i++) b[i]=(VkImageMemoryBarrier){
      .sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask=before?VK_ACCESS_TRANSFER_WRITE_BIT:VK_ACCESS_SHADER_WRITE_BIT,
      .dstAccessMask=before?VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT:VK_ACCESS_TRANSFER_READ_BIT,
      .oldLayout=VK_IMAGE_LAYOUT_GENERAL,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
      .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
      .image=images[i].image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
   d->CmdPipelineBarrier(command,before?VK_PIPELINE_STAGE_TRANSFER_BIT:VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      before?VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,2,b);
}
static void descriptor_buffers(const struct vk_device_dispatch_table *d,VkDevice device,
   VkDescriptorSet set,struct storage stores[BUFFER_COUNT],unsigned round,bool initial)
{
   VkDescriptorBufferInfo bi[4]={
      {stores[OUTPUT].buffer,GUARD*4,N*FIELDS*4},
      {stores[UNIFORMS].buffer,initial?0:round?48:32,16},
      {stores[UNIFORMS].buffer,initial?16:round?0:32,16},
      {stores[SEEDS].buffer,16,N*4}};
   uint32_t bindings[4]={0,2,5,7};
   VkDescriptorType types[4]={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC};
   VkWriteDescriptorSet writes[4];
   for (unsigned i=0;i<4;i++) writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet=set,.dstBinding=bindings[i],.descriptorCount=1,.descriptorType=types[i],.pBufferInfo=&bi[i]};
   d->UpdateDescriptorSets(device,4,writes,0,NULL);
}
static int verify(struct storage stores[BUFFER_COUNT],struct image images[6],struct array_image arrays[2],struct array_image filtered[2],struct array_image cubes[2],unsigned round)
{
   uint32_t a=0x10000000u+(round?3:2)*0x100u;
   uint32_t b=0x10000000u+(round?1:3)*0x100u;
   for (unsigned i=0;i<N;i++) {
      unsigned x=i%NX,y=i/NX,tx=x%8,ty=y%4;
      unsigned x0=tx<4?tx:tx&1?3:0,x1=tx<4?(tx<3?tx+1:3):x0;
      unsigned y1=ty<3?ty+1:3;
      float linear=(sample_value(x0,ty)+sample_value(x1,ty)+sample_value(x0,y1)+sample_value(x1,y1))*0.25f;
      uint32_t value=image_value(i)^a^b^seed_value(i)^source_texel(i);
      uint32_t record[FIELDS]={a,b,seed_value(i),source_texel(i),target_texel(i),image_value(i),
         float_word(sample_value(x0,ty)),float_word(linear),0,0,1,value,NX,NY,N,N,
         float_word((float)((x%4+y%4)&1)),float_word((float)((x%4+y%4)&1)),
         float_word(((x%4+y%4)%4+1)*0.25f),float_word((x%4+y%4)%4?1.0f:0.0f),
         array_value(round,(x>>1)&1,x&1,x%(4u>>(x&1)),y%(4u>>(x&1))),1};
      int fx=(int)(x%8)-2,fy=(int)(y%5)-1; unsigned layer=(x>>2)&1;
      float fs=(fx+1.0f)*0.25f,ft=(fy+1.0f)*0.25f;
      record[22]=float_word(filtered_pixel(round,layer,0,fx,fy,0));
      record[23]=float_word((x&1)?(filtered_linear(round,layer,0,fs,ft,1)+
         filtered_linear(round,layer,1,fs,ft,1))*0.5f:filtered_pixel(round,layer,0,fx+1,fy+1,1));
      record[24]=float_word(filtered_linear(round,layer,1,fs,ft,2));
      record[25]=float_word(filtered_pixel(round,layer,0,fx,fy+1,3));
      record[26]=float_word(filtered_pixel(round,layer,0,fx+1,fy+1,3));
      record[27]=float_word(filtered_pixel(round,layer,0,fx+1,fy,3));
      record[28]=float_word(filtered_pixel(round,layer,0,fx,fy,3));
      double direction[3],cs,ct; cube_direction(x%6,y,direction);
      unsigned face=cube_project(direction,&cs,&ct);
      int cx=(int)(cs*4),cy=(int)(ct*4); if (cx>3) cx=3; if (cy>3) cy=3;
      record[29]=float_word(cube_value(round,face,0,cx,cy));
      record[30]=float_word((cube_linear(round,face,0,cs,ct)+cube_linear(round,face,1,cs,ct))*0.5f);
      record[31]=float_word(cube_linear(round,face,1,cs,ct));
      cx=floor_int((float)(cs*4-0.5)); cy=floor_int((float)(ct*4-0.5));
      record[32]=float_word(cube_pixel(round,face,0,cx,cy+1));
      record[33]=float_word(cube_pixel(round,face,0,cx+1,cy+1));
      record[34]=float_word(cube_pixel(round,face,0,cx+1,cy));
      record[35]=float_word(cube_pixel(round,face,0,cx,cy));
      memcpy(stores[OUTPUT].saved+stores[OUTPUT].binding_words+GUARD+i*FIELDS,record,sizeof(record));
      stores[TARGET_TEXELS].saved[stores[TARGET_TEXELS].binding_words+GUARD+i]=target_texel(i)+a+b+source_texel(i);
      stores[IMAGE_READBACK].saved[stores[IMAGE_READBACK].binding_words+GUARD+i]=value;
   }
   for (unsigned y=0;y<NY;y++) for (unsigned x=0;x<NX;x++) {
      uint32_t expected=0x013579bdu;
      if (x>=3 && x<14 && y>=1 && y<4) {
         unsigned n=y*NX+x-1;
         expected=image_value(n)^a^b^seed_value(n)^source_texel(n);
      }
      stores[COPY_READBACK].saved[stores[COPY_READBACK].binding_words+GUARD+y*NX+x]=expected;
   }
   for (unsigned s=0;s<BUFFER_COUNT;s++) for (size_t i=0;i<stores[s].words;i++) {
      if (stores[s].map[i]!=stores[s].saved[i]) {
         fprintf(stderr,"FAIL round=%u buffer=%u word=%zu actual=%08x expected=%08x\n",
            round,s,i,stores[s].map[i],stores[s].saved[i]); return 1;
      }
   }
   for (unsigned s=0;s<3;s++) for (size_t i=0;i<images[s].words;i++) {
      uint32_t expected=poison(i,BUFFER_COUNT+s,round);
      if (i>=images[s].binding_words) {
         size_t rel=i-images[s].binding_words; unsigned y=rel/images[s].row_words,x=rel%images[s].row_words;
         if (x<images[s].width && y<images[s].height) {
            unsigned n=y*NX+x;
            if (s==0) expected=image_value(n)^a^b^seed_value(n)^source_texel(n);
            else if (s==1) expected=float_word(sample_value(x,y));
            else {
               expected=0x013579bdu;
               if (x>=3 && x<14 && y>=1 && y<4) {
                  n=y*NX+x-1;
                  expected=image_value(n)^a^b^seed_value(n)^source_texel(n);
               }
            }
         }
      }
      if (images[s].map[i]!=expected) {
         fprintf(stderr,"FAIL round=%u image=%u word=%zu actual=%08x expected=%08x\n",
            round,s,i,images[s].map[i],expected); return 1;
      }
   }
   for (unsigned s=3;s<6;s++) for (size_t i=0;i<images[s].words;i++) {
      uint32_t expected=poison(i,BUFFER_COUNT+s,0);
      if (i>=images[s].binding_words) {
         size_t rel=i-images[s].binding_words;
         unsigned y=rel/images[s].row_words,x=rel%images[s].row_words;
         if (y<4 && x<(s==3?2:4)) {
            if (s==3) {
               unsigned pixel=x*2;
               expected=((pixel+y)&1?65535u:0u)|(((pixel+1+y)&1?65535u:0u)<<16);
            } else expected=float_word(((x+y)%4+1)*0.25f);
         }
      }
      if (images[s].map[i]!=expected) {
         fprintf(stderr,"FAIL depth image=%u word=%zu actual=%08x expected=%08x\n",
            s,i,images[s].map[i],expected); return 1;
      }
   }
   for (unsigned source=0;source<2;source++) for (size_t i=0;i<arrays[source].image.words;i++) {
      if (arrays[source].image.map[i]!=arrays[source].saved[i]) {
         fprintf(stderr,"FAIL array source=%u word=%zu actual=%08x expected=%08x\n",
            source,i,arrays[source].image.map[i],arrays[source].saved[i]); return 1;
      }
   }
   for (unsigned source=0;source<2;source++) for (size_t i=0;i<filtered[source].image.words;i++) {
      if (filtered[source].image.map[i]!=filtered[source].saved[i]) {
         fprintf(stderr,"FAIL filtered source=%u word=%zu actual=%08x expected=%08x\n",
            source,i,filtered[source].image.map[i],filtered[source].saved[i]); return 1;
      }
   }
   for (unsigned source=0;source<2;source++) for (size_t i=0;i<cubes[source].image.words;i++) {
      if (cubes[source].image.map[i]!=cubes[source].saved[i]) {
         fprintf(stderr,"FAIL cube source=%u word=%zu actual=%08x expected=%08x\n",
            source,i,cubes[source].image.map[i],cubes[source].saved[i]); return 1;
      }
   }
   return 0;
}
int main(int argc,char **argv)
{
   if (argc!=3 && !(argc==4 && !strcmp(argv[3],"--compile-only"))) {
      fprintf(stderr,"Usage: %s ICD-library resources.spv [--compile-only]\n",argv[0]); return 1;
   }
   REQUIRE(!setenv("RADISH_SW_TEXEL_FETCH","1",1));
   puts("ARRAY_FETCH_PRIVATE_FORCE set=1 before_CreateInstance=1");
   REQUIRE(!getenv("RADISH_BATCH_CHUNKS") || !strcmp(getenv("RADISH_BATCH_CHUNKS"),"1"));
   REQUIRE(!getenv("RADISH_TILED_GROUPS_X") || !strcmp(getenv("RADISH_TILED_GROUPS_X"),"1"));
   void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); if (!lib) { fprintf(stderr,"%s\n",dlerror()); return 1; }
   PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr"); REQUIRE(gipa);
   VkInstance instance=VK_NULL_HANDLE; INSTANCE_FN(CreateInstance);
   VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_0};
   const char *iex[]={VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,VK_KHR_DEVICE_GROUP_CREATION_EXTENSION_NAME};
   VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app,.enabledExtensionCount=2,.ppEnabledExtensionNames=iex};
   SUCCESS(CreateInstance(&ici,NULL,&instance));
   INSTANCE_FN(EnumeratePhysicalDevices); INSTANCE_FN(GetPhysicalDeviceMemoryProperties);
   INSTANCE_FN(GetPhysicalDeviceProperties); INSTANCE_FN(GetPhysicalDeviceFormatProperties);
   INSTANCE_FN(CreateDevice); INSTANCE_FN(DestroyInstance);
   VkPhysicalDevice physical; uint32_t count=1;
   SUCCESS(EnumeratePhysicalDevices(instance,&count,&physical)); REQUIRE(count==1);
   VkPhysicalDeviceMemoryProperties memory_properties; GetPhysicalDeviceMemoryProperties(physical,&memory_properties);
   VkPhysicalDeviceProperties properties; GetPhysicalDeviceProperties(physical,&properties);
   REQUIRE(properties.apiVersion==VK_API_VERSION_1_0);
   VkFormatProperties fp; GetPhysicalDeviceFormatProperties(physical,VK_FORMAT_R32_SFLOAT,&fp);
   REQUIRE((fp.linearTilingFeatures & (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))==
      (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT));
   GetPhysicalDeviceFormatProperties(physical,VK_FORMAT_R32_UINT,&fp);
   REQUIRE(fp.linearTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT);
   REQUIRE((fp.bufferFeatures & (VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT|VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT))==
      (VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT|VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT));
   VkFormat depth_formats[2]={VK_FORMAT_D16_UNORM,VK_FORMAT_D32_SFLOAT};
   for (unsigned i=0;i<2;i++) {
      GetPhysicalDeviceFormatProperties(physical,depth_formats[i],&fp);
      REQUIRE((fp.linearTilingFeatures & (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
         VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT)) ==
         (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
          VK_FORMAT_FEATURE_TRANSFER_DST_BIT));
      REQUIRE(!(fp.linearTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) && !fp.bufferFeatures);
   }
   float priority=1; VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueCount=1,.pQueuePriorities=&priority};
   const char *dex[]={VK_KHR_BIND_MEMORY_2_EXTENSION_NAME,VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,VK_KHR_DEVICE_GROUP_EXTENSION_NAME};
   VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qci,.enabledExtensionCount=3,.ppEnabledExtensionNames=dex};
   VkDevice device; SUCCESS(CreateDevice(physical,&dci,NULL,&device));
   struct rd_device *inside=rd_device_from_handle(device);
   PFN_vkGetDeviceProcAddr gdpa=(PFN_vkGetDeviceProcAddr)gipa(instance,"vkGetDeviceProcAddr"); REQUIRE(gdpa);
   struct vk_device_dispatch_table aliases=inside->vk.dispatch_table;
   aliases.BindImageMemory2=(PFN_vkBindImageMemory2)gdpa(device,"vkBindImageMemory2KHR");
   aliases.GetImageMemoryRequirements2=(PFN_vkGetImageMemoryRequirements2)gdpa(device,"vkGetImageMemoryRequirements2KHR");
   aliases.GetImageSparseMemoryRequirements2=(PFN_vkGetImageSparseMemoryRequirements2)gdpa(device,"vkGetImageSparseMemoryRequirements2KHR");
   REQUIRE(aliases.BindImageMemory2 && aliases.GetImageMemoryRequirements2 && aliases.GetImageSparseMemoryRequirements2);
   const struct vk_device_dispatch_table *d=&aliases;
   VkSampler samplers[11];
   for (unsigned i=0;i<3;i++) {
      VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
         .magFilter=i==1?VK_FILTER_LINEAR:VK_FILTER_NEAREST,.minFilter=i==1?VK_FILTER_LINEAR:VK_FILTER_NEAREST,
         .mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         .addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
      if (i==2) { si.compareEnable=VK_TRUE; si.compareOp=VK_COMPARE_OP_LESS_OR_EQUAL; }
      SUCCESS(d->CreateSampler(device,&si,NULL,&samplers[i]));
   }
   for (unsigned i=3;i<7;i++) {
      VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
         .magFilter=i==5?VK_FILTER_LINEAR:VK_FILTER_NEAREST,
         .minFilter=(i==4 || i==5)?VK_FILTER_LINEAR:VK_FILTER_NEAREST,
         .mipmapMode=i==4?VK_SAMPLER_MIPMAP_MODE_LINEAR:VK_SAMPLER_MIPMAP_MODE_NEAREST,
         .addressModeU=i==3?VK_SAMPLER_ADDRESS_MODE_REPEAT:i==5?VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
            i==6?VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         .mipLodBias=i==5?0.75f:0.0f,.maxLod=i==6?0.0f:1.0f,
         .borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE};
      si.addressModeV=si.addressModeW=si.addressModeU;
      SUCCESS(d->CreateSampler(device,&si,NULL,&samplers[i]));
   }
   for (unsigned i=7;i<11;i++) {
      VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
         .magFilter=i==7?VK_FILTER_NEAREST:VK_FILTER_LINEAR,
         .minFilter=i==7?VK_FILTER_NEAREST:VK_FILTER_LINEAR,
         .mipmapMode=i==8?VK_SAMPLER_MIPMAP_MODE_LINEAR:VK_SAMPLER_MIPMAP_MODE_NEAREST,
         .addressModeU=VK_SAMPLER_ADDRESS_MODE_REPEAT,.addressModeV=VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
         .addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,.borderColor=VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
         .maxLod=(i==7 || i==10)?0.0f:1.0f};
      SUCCESS(d->CreateSampler(device,&si,NULL,&samplers[i]));
   }
   uint32_t numbers[22]={19,7,0,13,2,17,9,5,11,23,29,31,37,41,43,47,53,59,61,67,71,73};
   VkDescriptorType types[22]={VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
   VkDescriptorSetLayoutBinding bindings[22];
   for (unsigned i=0;i<22;i++) bindings[i]=(VkDescriptorSetLayoutBinding){.binding=numbers[i],.descriptorType=types[i],
      .descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,.pImmutableSamplers=numbers[i]==17?&samplers[0]:NULL};
   VkDescriptorSetLayout sl[3]; VkDescriptorSetLayoutCreateInfo sci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
   SUCCESS(d->CreateDescriptorSetLayout(device,&sci,NULL,&sl[0])); sci.bindingCount=22; sci.pBindings=bindings;
   SUCCESS(d->CreateDescriptorSetLayout(device,&sci,NULL,&sl[1]));
   SUCCESS(d->CreateDescriptorSetLayout(device,&sci,NULL,&sl[2]));
   VkPipelineLayout layouts[2];
   for (unsigned i=0;i<2;i++) {
      VkDescriptorSetLayout sets[2]={sl[0],sl[1+i]};
      VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=2,.pSetLayouts=sets};
      SUCCESS(d->CreatePipelineLayout(device,&li,NULL,&layouts[i]));
   }
   size_t bytes; uint32_t *code=read_spirv(argv[2],&bytes); REQUIRE(code);
   VkShaderModuleCreateInfo mi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=bytes,.pCode=code};
   VkShaderModule module; SUCCESS(d->CreateShaderModule(device,&mi,NULL,&module)); free(code);
   VkComputePipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"},
      .layout=layouts[0]};
   VkPipeline pipeline; SUCCESS(d->CreateComputePipelines(device,VK_NULL_HANDLE,1,&pi,NULL,&pipeline));
   d->DestroyShaderModule(device,module,NULL);
   struct rd_pipeline *p=rd_pipeline_from_handle(pipeline);
   printf("RESOURCE_COMPILED logical=%u textures=%u samplers=%u images=%u native=%u image_constants=%u aliases=%u\n",
      p->descriptor_count,p->resource_usage.texture_count,p->resource_usage.sampler_count,p->resource_usage.image_count,
      p->variant->instrlen,ir3_const_state(p->variant)->image_dims.count,p->variant->image_mapping.num_tex); fflush(stdout);
   if (argc==4) {
      d->DestroyPipeline(device,pipeline,NULL);
      for (unsigned i=0;i<2;i++) d->DestroyPipelineLayout(device,layouts[i],NULL);
      for (unsigned i=0;i<3;i++) d->DestroyDescriptorSetLayout(device,sl[i],NULL);
      for (unsigned i=0;i<11;i++) d->DestroySampler(device,samplers[i],NULL);
      d->DestroyDevice(device,NULL); DestroyInstance(instance,NULL); dlclose(lib);
      puts("PASS RESOURCE compile-only: all pipelines created before buffers/images/commands/dispatch"); return 0;
   }
   struct storage stores[BUFFER_COUNT]={0};
   VkDeviceSize sizes[BUFFER_COUNT]={GUARD*4+N*FIELDS*4+64,64,32+N*4+64,GUARD*4+N*4+3,
      GUARD*4+N*4+3,GUARD*4+N*4+64,GUARD*4+16*4+64,GUARD*4+N*4+64,GUARD*4+N*4+64};
   VkBufferUsageFlags usages[BUFFER_COUNT]={VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_BUFFER_USAGE_TRANSFER_DST_BIT};
   for (unsigned i=0;i<BUFFER_COUNT;i++) REQUIRE(storage_create(d,device,&memory_properties,sizes[i],usages[i],&stores[i])==0);
   struct image images[6]={0};
   REQUIRE(image_create(d,device,&memory_properties,VK_FORMAT_R32_UINT,NX,NY,VK_IMAGE_USAGE_STORAGE_BIT,&images[0],false)==0);
   REQUIRE(image_create(d,device,&memory_properties,VK_FORMAT_R32_SFLOAT,4,4,VK_IMAGE_USAGE_SAMPLED_BIT,&images[1],false)==0);
   REQUIRE(image_create(d,device,&memory_properties,VK_FORMAT_R32_UINT,NX,NY,VK_IMAGE_USAGE_STORAGE_BIT,&images[2],true)==0);
   REQUIRE(image_create(d,device,&memory_properties,VK_FORMAT_D16_UNORM,4,4,VK_IMAGE_USAGE_SAMPLED_BIT,&images[3],false)==0);
   REQUIRE(image_create(d,device,&memory_properties,VK_FORMAT_D32_SFLOAT,4,4,VK_IMAGE_USAGE_SAMPLED_BIT,&images[4],false)==0);
   REQUIRE(image_create(d,device,&memory_properties,VK_FORMAT_D32_SFLOAT,4,4,VK_IMAGE_USAGE_SAMPLED_BIT,&images[5],false)==0);
   struct array_image arrays[2]={0};
   for (unsigned i=0;i<2;i++) REQUIRE(array_image_create(d,device,&memory_properties,&arrays[i],i,VK_FORMAT_R32_UINT,false)==0);
   struct array_image filtered[2]={0};
   for (unsigned i=0;i<2;i++) REQUIRE(array_image_create(d,device,&memory_properties,&filtered[i],i,VK_FORMAT_R32_SFLOAT,false)==0);
   struct array_image cubes[2]={0};
   for (unsigned i=0;i<2;i++) REQUIRE(array_image_create(d,device,&memory_properties,&cubes[i],i,VK_FORMAT_R32_SFLOAT,true)==0);
   VkBufferView views[2]; REQUIRE(buffer_view(d,device,stores[SOURCE_TEXELS].buffer,&views[0])==0);
   REQUIRE(buffer_view(d,device,stores[TARGET_TEXELS].buffer,&views[1])==0);
   VkDescriptorPoolSize pools[8]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,1},
      {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,1},{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,1},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,15}};
   VkDescriptorPoolCreateInfo dpi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=8,.pPoolSizes=pools};
   VkDescriptorPool pool; SUCCESS(d->CreateDescriptorPool(device,&dpi,NULL,&pool));
   VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&sl[1]};
   VkDescriptorSet set; SUCCESS(d->AllocateDescriptorSets(device,&ai,&set));
   descriptor_buffers(d,device,set,stores,0,true);
   VkDescriptorImageInfo ii[3]={{.imageView=images[0].view,.imageLayout=VK_IMAGE_LAYOUT_GENERAL},
      {.sampler=VK_NULL_HANDLE,.imageView=images[1].view,.imageLayout=VK_IMAGE_LAYOUT_GENERAL},
      {.sampler=samplers[1],.imageView=images[1].view,.imageLayout=VK_IMAGE_LAYOUT_GENERAL}};
   VkWriteDescriptorSet writes[5];
   for (unsigned i=0;i<5;i++) writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,
      .dstBinding=i<2?9+2*i:i==2?13:i==3?17:19,.descriptorCount=1,
      .descriptorType=i<2?(i?VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER):
                           i==2?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .pTexelBufferView=i<2?&views[i]:NULL,.pImageInfo=i>=2?&ii[i-2]:NULL};
   d->UpdateDescriptorSets(device,5,writes,0,NULL);
   VkDescriptorImageInfo depth_info[4]; VkWriteDescriptorSet depth_writes[4];
   uint32_t depth_bindings[4]={23,29,31,37};
   for (unsigned i=0;i<4;i++) {
      depth_info[i]=(VkDescriptorImageInfo){.sampler=samplers[(i&1)?2:0],
         .imageView=images[i<2?3:5].view,.imageLayout=VK_IMAGE_LAYOUT_GENERAL};
      depth_writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstSet=set,.dstBinding=depth_bindings[i],.descriptorCount=1,
         .descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&depth_info[i]};
   }
   d->UpdateDescriptorSets(device,4,depth_writes,0,NULL);
   VkCommandPoolCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; VkCommandPool command_pool;
   SUCCESS(d->CreateCommandPool(device,&ci,NULL,&command_pool));
   VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=command_pool,
      .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1}; VkCommandBuffer command;
   SUCCESS(d->AllocateCommandBuffers(device,&ca,&command));
   VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};


   VkQueue queue; d->GetDeviceQueue(device,0,0,&queue);
   VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence;
   SUCCESS(d->CreateFence(device,&fi,NULL,&fence));
   VkMappedMemoryRange ranges[BUFFER_COUNT+3];
   for (unsigned i=0;i<BUFFER_COUNT+3;i++) ranges[i]=(VkMappedMemoryRange){.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
      .memory=i<BUFFER_COUNT?stores[i].memory:images[i-BUFFER_COUNT].memory,.size=VK_WHOLE_SIZE};
   for (unsigned s=3;s<6;s++) {
      for (size_t i=0;i<images[s].words;i++) images[s].map[i]=poison(i,BUFFER_COUNT+s,0);
      if (s==5) continue;
      for (unsigned y=0;y<4;y++) for (unsigned x=0;x<4;x++) {
         size_t word=images[s].binding_words+y*images[s].row_words;
         if (s==3) ((uint16_t *)(images[s].map+word))[x]=((x+y)&1)?65535:0;
         else images[s].map[word+x]=float_word(((x+y)%4+1)*0.25f);
      }
   }
   VkMappedMemoryRange depth_ranges[9];
   for (unsigned i=0;i<3;i++) depth_ranges[i]=(VkMappedMemoryRange){
      .sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=images[i+3].memory,.size=VK_WHOLE_SIZE};
   for (unsigned i=0;i<2;i++) depth_ranges[3+i]=(VkMappedMemoryRange){
      .sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=arrays[i].image.memory,.size=VK_WHOLE_SIZE};
   for (unsigned i=0;i<2;i++) depth_ranges[5+i]=(VkMappedMemoryRange){
      .sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=filtered[i].image.memory,.size=VK_WHOLE_SIZE};
   for (unsigned i=0;i<2;i++) depth_ranges[7+i]=(VkMappedMemoryRange){
      .sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=cubes[i].image.memory,.size=VK_WHOLE_SIZE};
   SUCCESS(d->FlushMappedMemoryRanges(device,9,depth_ranges));
   VkCommandBuffer depth_init; SUCCESS(d->AllocateCommandBuffers(device,&ca,&depth_init));
   SUCCESS(d->BeginCommandBuffer(depth_init,&begin));
   VkImageMemoryBarrier depth_initial[9];
   for (unsigned i=0;i<3;i++) depth_initial[i]=(VkImageMemoryBarrier){
      .sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,
      .dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT,
      .oldLayout=VK_IMAGE_LAYOUT_PREINITIALIZED,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
      .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
      .image=images[i+3].image,.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1}};
   for (unsigned i=0;i<2;i++) {
      depth_initial[3+i]=depth_initial[0];
      depth_initial[3+i].image=arrays[i].image.image;
      depth_initial[3+i].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
      depth_initial[3+i].subresourceRange=(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,0,2,0,2};
   }
   for (unsigned i=0;i<2;i++) { depth_initial[5+i]=depth_initial[3+i]; depth_initial[5+i].image=filtered[i].image.image; }
   for (unsigned i=0;i<2;i++) { depth_initial[7+i]=depth_initial[3+i];
      depth_initial[7+i].image=cubes[i].image.image; depth_initial[7+i].subresourceRange.layerCount=12; }
   d->CmdPipelineBarrier(depth_init,VK_PIPELINE_STAGE_HOST_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,NULL,0,NULL,9,depth_initial);
   VkImageCopy depth_copy={.srcSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},
      .dstSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1},.extent={4,4,1}};
   d->CmdCopyImage(depth_init,images[4].image,VK_IMAGE_LAYOUT_GENERAL,
      images[5].image,VK_IMAGE_LAYOUT_GENERAL,1,&depth_copy);
   VkImageMemoryBarrier depth_ready=depth_initial[2];
   depth_ready.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; depth_ready.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
   depth_ready.oldLayout=VK_IMAGE_LAYOUT_GENERAL;
   d->CmdPipelineBarrier(depth_init,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      0,0,NULL,0,NULL,1,&depth_ready);
   SUCCESS(d->EndCommandBuffer(depth_init));
   VkSubmitInfo depth_submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&depth_init};
   SUCCESS(d->QueueSubmit(queue,1,&depth_submit,fence));
   SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
   SUCCESS(d->InvalidateMappedMemoryRanges(device,9,depth_ranges));
   struct rd_pipeline *array_variant=NULL;
   for (unsigned round=0;round<2;round++) {
      for (unsigned s=0;s<BUFFER_COUNT;s++) for (size_t i=0;i<stores[s].words;i++) stores[s].map[i]=poison(i,s,round);
      for (unsigned s=0;s<3;s++) for (size_t i=0;i<images[s].words;i++) images[s].map[i]=poison(i,BUFFER_COUNT+s,round);
      for (unsigned i=0;i<16;i++) stores[UNIFORMS].map[stores[UNIFORMS].binding_words+i]=0x10000000u+(i/4)*0x100u+i%4;
      for (unsigned i=0;i<N;i++) {
         stores[SEEDS].map[stores[SEEDS].binding_words+8+i]=seed_value(i);
         stores[SOURCE_TEXELS].map[stores[SOURCE_TEXELS].binding_words+GUARD+i]=source_texel(i);
         stores[TARGET_TEXELS].map[stores[TARGET_TEXELS].binding_words+GUARD+i]=target_texel(i);
         stores[IMAGE_UPLOAD].map[stores[IMAGE_UPLOAD].binding_words+GUARD+i]=image_value(i);
      }
      for (unsigned i=0;i<16;i++) stores[SAMPLE_UPLOAD].map[stores[SAMPLE_UPLOAD].binding_words+GUARD+i]=float_word(sample_value(i%4,i/4));
      for (unsigned s=0;s<BUFFER_COUNT;s++) memcpy(stores[s].saved,stores[s].map,stores[s].words*4);
      SUCCESS(d->FlushMappedMemoryRanges(device,BUFFER_COUNT+3,ranges));
      SUCCESS(d->ResetCommandPool(device,command_pool,0));
      descriptor_buffers(d,device,set,stores,round,false);
      VkDescriptorImageInfo array_info={.sampler=samplers[0],.imageView=arrays[round].image.view,
         .imageLayout=VK_IMAGE_LAYOUT_GENERAL};
      VkWriteDescriptorSet array_write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,
         .dstBinding=41,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
         .pImageInfo=&array_info};
      d->UpdateDescriptorSets(device,1,&array_write,0,NULL);
      VkDescriptorImageInfo filtered_info[4]; VkWriteDescriptorSet filtered_writes[4];
      uint32_t filtered_bindings[4]={43,47,53,59};
      for (unsigned i=0;i<4;i++) {
         filtered_info[i]=(VkDescriptorImageInfo){.sampler=samplers[3+i],
            .imageView=filtered[round].image.view,.imageLayout=VK_IMAGE_LAYOUT_GENERAL};
         filtered_writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet=set,.dstBinding=filtered_bindings[i],.descriptorCount=1,
            .descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&filtered_info[i]};
      }
      d->UpdateDescriptorSets(device,4,filtered_writes,0,NULL);
      VkDescriptorImageInfo cube_info[4]; VkWriteDescriptorSet cube_writes[4];
      uint32_t cube_bindings[4]={61,67,71,73};
      for (unsigned i=0;i<4;i++) {
         cube_info[i]=(VkDescriptorImageInfo){.sampler=samplers[7+i],
            .imageView=cubes[round].image.view,.imageLayout=VK_IMAGE_LAYOUT_GENERAL};
         cube_writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet=set,.dstBinding=cube_bindings[i],.descriptorCount=1,
            .descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&cube_info[i]};
      }
      d->UpdateDescriptorSets(device,4,cube_writes,0,NULL);
      if (!layouts[1]) {
         VkDescriptorSetLayout compatible_sets[2]={sl[0],sl[1]};
         VkPipelineLayoutCreateInfo compatible_layout={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount=2,.pSetLayouts=compatible_sets};
         SUCCESS(d->CreatePipelineLayout(device,&compatible_layout,NULL,&layouts[1]));
      }
      SUCCESS(d->BeginCommandBuffer(command,&begin));
      VkImageMemoryBarrier initial[3];
      for (unsigned i=0;i<3;i++) initial[i]=(VkImageMemoryBarrier){.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
         .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
         .image=images[i].image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
      d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,3,initial);
      VkClearColorValue clear={.uint32={0x013579bdu,0,0,0}};
      VkImageSubresourceRange clear_range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
      d->CmdClearColorImage(command,images[2].image,VK_IMAGE_LAYOUT_GENERAL,&clear,1,&clear_range);
      for (unsigned i=0;i<2;i++) {
         VkBufferImageCopy copy={.bufferOffset=GUARD*4,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},
            .imageExtent={images[i].width,images[i].height,1}};
         d->CmdCopyBufferToImage(command,stores[i?SAMPLE_UPLOAD:IMAGE_UPLOAD].buffer,images[i].image,VK_IMAGE_LAYOUT_GENERAL,1,&copy);
      }
      barriers(d,command,images,true);
      d->CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
      uint32_t dynamic[2]={16,16};
      d->CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layouts[1],1,1,&set,2,dynamic);
      d->CmdDispatch(command,NX,NY,1);
      barriers(d,command,images,false);
      VkBufferImageCopy copy={.bufferOffset=GUARD*4,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={NX,NY,1}};
      d->CmdCopyImageToBuffer(command,images[0].image,VK_IMAGE_LAYOUT_GENERAL,stores[IMAGE_READBACK].buffer,1,&copy);
      VkMemoryBarrier transfer={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT};
      d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
         0,1,&transfer,0,NULL,0,NULL);
      VkImageCopy image_copy={.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.srcOffset={2,1,0},
         .dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.dstOffset={3,1,0},.extent={11,3,1}};
      d->CmdCopyImage(command,images[0].image,VK_IMAGE_LAYOUT_GENERAL,images[2].image,VK_IMAGE_LAYOUT_GENERAL,1,&image_copy);
      d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
         0,1,&transfer,0,NULL,0,NULL);
      d->CmdCopyImageToBuffer(command,images[2].image,VK_IMAGE_LAYOUT_GENERAL,stores[COPY_READBACK].buffer,1,&copy);
      VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
      d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
         VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
      SUCCESS(d->EndCommandBuffer(command));
      d->DestroyPipelineLayout(device,layouts[1],NULL); layouts[1]=VK_NULL_HANDLE;
      if (sl[2]) { d->DestroyDescriptorSetLayout(device,sl[2],NULL); sl[2]=VK_NULL_HANDLE; }
      SUCCESS(d->ResetFences(device,1,&fence));
      VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
      uint64_t before=inside->submit_count;
      printf("RESOURCE_ROUND_BEGIN round=%u grid=%u,%u updates_before_bind=1 rerecord=1 dynamic=16,16\n",round,NX,NY); fflush(stdout);
      SUCCESS(d->QueueSubmit(queue,1,&submit,fence));
      SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
      SUCCESS(d->InvalidateMappedMemoryRanges(device,BUFFER_COUNT+3,ranges));
      REQUIRE(verify(stores,images,arrays,filtered,cubes,round)==0);
      REQUIRE(p->sampling_variants && p->sampling_variants->pipeline->sampling_mask);
      struct rd_pipeline *selected=p->sampling_variants->pipeline;
      REQUIRE(selected->buffer_count==p->buffer_count+1);
      if (round) REQUIRE(selected==array_variant); else array_variant=selected;
      printf("ARRAY_FETCH_PATH round=%u cached=%u raw_mask=%x between_recordings_BO_swap=1\n",
         round,round,selected->sampling_mask);
      REQUIRE(inside->submit_count-before>=2);
      printf("RESOURCE_ROUND_PASS round=%u records=%u oracle_words=%u input_and_allocation_guards=exact submits=%" PRIu64 "\n",
         round,N,N*FIELDS,inside->submit_count-before); fflush(stdout);
   }
   d->DestroyFence(device,fence,NULL); d->DestroyCommandPool(device,command_pool,NULL);
   d->DestroyDescriptorPool(device,pool,NULL);
   for (unsigned i=0;i<2;i++) d->DestroyBufferView(device,views[i],NULL);
   for (unsigned i=0;i<6;i++) {
      d->DestroyImageView(device,images[i].view,NULL); d->UnmapMemory(device,images[i].memory);
      d->DestroyImage(device,images[i].image,NULL); d->FreeMemory(device,images[i].memory,NULL);
   }
   for (unsigned i=0;i<2;i++) {
      free(arrays[i].saved);
      d->DestroyImageView(device,arrays[i].image.view,NULL); d->UnmapMemory(device,arrays[i].image.memory);
      d->DestroyImage(device,arrays[i].image.image,NULL); d->FreeMemory(device,arrays[i].image.memory,NULL);
   }
   for (unsigned i=0;i<2;i++) {
      free(filtered[i].saved);
      d->DestroyImageView(device,filtered[i].image.view,NULL); d->UnmapMemory(device,filtered[i].image.memory);
      d->DestroyImage(device,filtered[i].image.image,NULL); d->FreeMemory(device,filtered[i].image.memory,NULL);
   }
   for (unsigned i=0;i<2;i++) {
      free(cubes[i].saved);
      d->DestroyImageView(device,cubes[i].image.view,NULL); d->UnmapMemory(device,cubes[i].image.memory);
      d->DestroyImage(device,cubes[i].image.image,NULL); d->FreeMemory(device,cubes[i].image.memory,NULL);
   }
   for (unsigned i=0;i<BUFFER_COUNT;i++) {
      free(stores[i].saved); d->UnmapMemory(device,stores[i].memory);
      d->DestroyBuffer(device,stores[i].buffer,NULL); d->FreeMemory(device,stores[i].memory,NULL);
   }
   d->DestroyPipeline(device,pipeline,NULL); d->DestroyPipelineLayout(device,layouts[0],NULL);
   for (unsigned i=0;i<2;i++) d->DestroyDescriptorSetLayout(device,sl[i],NULL);
   for (unsigned i=0;i<11;i++) d->DestroySampler(device,samplers[i],NULL);
   d->DestroyDevice(device,NULL); DestroyInstance(instance,NULL); dlclose(lib);
   puts("PASS CUBE: six faces/base layer6, nearest, seamless edge/corner linear+mip, explicit gradients and gather, current BO/cache reuse exact/full guards");
   puts("PASS FILTER_ARRAY: repeat/edge/mirror/border, mag/min, spatial+mip linear, explicit+gradient LOD/bias, RNE layers and gather exact/guards");
   puts("PASS ARRAY_FETCH: two mips/layers, BO swap between recordings with same specialization key, exact raw format/default alpha and full guards");
   puts("PASS DEPTH: D16/D32 sampling/comparison, host PREINITIALIZED sources and D32 depth-image copy, all allocation guards");
   puts("PASS IMAGE_GROUP: full singleton split copy/clear/readback and mutable R32_SFLOAT-to-UINT storage view");
   puts("PASS RESOURCE: UBO/dynamic offsets/updates before recording/compatible layouts/typed texels/images/nearest+linear/image copy+clear exact/guards");
   return 0;
}
