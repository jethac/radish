int resource_baseline_main(int argc,char **argv);
#define main resource_baseline_main
#include "core_resources.c"
#undef main
#include "etc_oracle.h"
#include "vendor/detex.h"
#include <math.h>

enum { ETC_TYPES=10,ETC_MIPS=4,ETC_LAYERS=3,ETC_SIDE=16,ETC_RECORDS=64,ETC_FIELDS=35 };
static const VkFormat etc_formats[ETC_TYPES]={VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK,VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK,
 VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK,VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK,
 VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK,VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK,
 VK_FORMAT_EAC_R11_UNORM_BLOCK,VK_FORMAT_EAC_R11_SNORM_BLOCK,VK_FORMAT_EAC_R11G11_UNORM_BLOCK,VK_FORMAT_EAC_R11G11_SNORM_BLOCK};
struct etc_numeric_observation { unsigned mismatch[2], nonfinite[2]; double max_error[2]; };
static struct etc_numeric_observation etc_numeric;
struct etc_image {VkImage image;VkImageView view,block_view;VkDeviceMemory memory;uint32_t *map,*saved;size_t words,binding_words,image_words;float decoded[ETC_MIPS][ETC_LAYERS][ETC_SIDE*ETC_SIDE][4];};
static unsigned etc_bytes(unsigned f){return f==4||f==5||f>=8?16:8;}
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
static VkDeviceSize etc_regions(unsigned f,VkBufferImageCopy regions[ETC_MIPS]) {
 VkDeviceSize cursor=GUARD*4;unsigned bs=etc_bytes(f);
 for(unsigned m=0;m<ETC_MIPS;m++) {
  unsigned width=ETC_SIDE>>m,row=(width+7)&~3u;
  regions[m]=(VkBufferImageCopy){.bufferOffset=cursor,.bufferRowLength=row,.bufferImageHeight=row,
   .imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,m,0,ETC_LAYERS},.imageExtent={width,width,1}};
  VkDeviceSize bytes=(VkDeviceSize)(row/4)*(row/4)*bs*ETC_LAYERS;cursor=(cursor+bytes+63)&~63ull;
 }
 return cursor+64;
}
static int etc_create(const struct vk_device_dispatch_table *d,VkDevice device,const VkPhysicalDeviceMemoryProperties *memory,unsigned f,struct etc_image *out) {
 VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.flags=VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,
  .imageType=VK_IMAGE_TYPE_2D,.format=etc_formats[f],.extent={ETC_SIDE,ETC_SIDE,1},.mipLevels=ETC_MIPS,.arrayLayers=ETC_LAYERS,
  .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
 SUCCESS(d->CreateImage(device,&ci,NULL,&out->image));VkMemoryRequirements r;d->GetImageMemoryRequirements(device,out->image,&r);
 REQUIRE(r.alignment&&!(r.alignment%4)&&r.size<1024*1024&&!(r.size%4));unsigned type=memory_type(&r,memory);REQUIRE(type!=UINT32_MAX);
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=r.alignment+r.size+64,.memoryTypeIndex=type};
 SUCCESS(d->AllocateMemory(device,&ai,NULL,&out->memory));SUCCESS(d->BindImageMemory(device,out->image,out->memory,r.alignment));
 SUCCESS(d->MapMemory(device,out->memory,0,VK_WHOLE_SIZE,0,(void **)&out->map));out->words=ai.allocationSize/4;out->binding_words=r.alignment/4;out->image_words=r.size/4;
 out->saved=malloc(ai.allocationSize);REQUIRE(out->saved);
 VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=out->image,.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY,.format=etc_formats[f],.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,1,3,1,2}};
 SUCCESS(d->CreateImageView(device,&vi,NULL,&out->view));return 0;
}
static int etc_create_block(const struct vk_device_dispatch_table *d,VkDevice device,const VkPhysicalDeviceMemoryProperties *memory,unsigned f,bool compressed,struct etc_image *out) {
 VkFormat raw=etc_bytes(f)==8?VK_FORMAT_R32G32_UINT:VK_FORMAT_R32G32B32A32_UINT;
 VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.flags=compressed?VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT|VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT:0,
  .imageType=VK_IMAGE_TYPE_2D,.format=compressed?etc_formats[f]:raw,.extent={compressed?4:1,compressed?4:1,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
  .usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
 SUCCESS(d->CreateImage(device,&ci,NULL,&out->image));VkMemoryRequirements r;d->GetImageMemoryRequirements(device,out->image,&r);REQUIRE(r.alignment&&!(r.alignment%4)&&r.size<1024*1024&&!(r.size%4));unsigned type=memory_type(&r,memory);REQUIRE(type!=UINT32_MAX);
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=r.alignment+r.size+64,.memoryTypeIndex=type};SUCCESS(d->AllocateMemory(device,&ai,NULL,&out->memory));SUCCESS(d->BindImageMemory(device,out->image,out->memory,r.alignment));SUCCESS(d->MapMemory(device,out->memory,0,VK_WHOLE_SIZE,0,(void **)&out->map));out->words=ai.allocationSize/4;out->binding_words=r.alignment/4;out->image_words=r.size/4;out->saved=malloc(ai.allocationSize);REQUIRE(out->saved);
 VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=out->image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=raw,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};SUCCESS(d->CreateImageView(device,&vi,NULL,&out->view));return 0;
}
static int etc_prepare(struct etc_image *image,struct storage *upload,struct storage *readback,struct storage *output,unsigned f,unsigned round,const VkBufferImageCopy regions[ETC_MIPS]) {
 for(size_t i=0;i<upload->words;i++)upload->map[i]=poison(i,10,round);
 for(size_t i=0;i<readback->words;i++)readback->map[i]=poison(i,11,round);
 for(size_t i=0;i<output->words;i++)output->map[i]=poison(i,12,round);
 for(size_t i=0;i<image->words;i++)image->map[i]=poison(i,13,round);
 memcpy(readback->saved,readback->map,readback->words*4);memcpy(output->saved,output->map,output->words*4);
 unsigned bs=etc_bytes(f);
 for(unsigned m=0;m<ETC_MIPS;m++) {
  unsigned width=ETC_SIDE>>m,nb=(width+3)/4,row=regions[m].bufferRowLength/4*bs,slice=row*(regions[m].bufferImageHeight/4);
  for(unsigned layer=0;layer<ETC_LAYERS;layer++)for(unsigned by=0;by<nb;by++)for(unsigned bx=0;bx<nb;bx++) {
   uint8_t block[16];float rgba[16][4];REQUIRE(!etc_block(f,m,layer,bx,by,round,block));REQUIRE(etc_oracle_decode(f,block,rgba));
   size_t off=regions[m].bufferOffset+layer*slice+by*row+bx*bs;
   REQUIRE(off+bs<=upload->words*4-upload->binding_words*4);
   memcpy((uint8_t *)(upload->map+upload->binding_words)+off,block,bs);
   memcpy((uint8_t *)(readback->saved+readback->binding_words)+off,block,bs);
   for(unsigned y=0;y<4&&by*4+y<width;y++)for(unsigned x=0;x<4&&bx*4+x<width;x++)
    memcpy(image->decoded[m][layer][(by*4+y)*width+bx*4+x],rgba[y*4+x],16);
  }
 }
 memcpy(upload->saved,upload->map,upload->words*4);return 0;
}
static double etc_encoded(double x){return x<=0.0031308?x*12.92:1.055*pow(x,1.0/2.4)-0.055;}
static int etc_check(struct etc_image *image,struct storage *upload,struct storage *readback,struct storage *output,unsigned f,unsigned round,const VkBufferImageCopy regions[ETC_MIPS]) {
 const unsigned sizes[9]={8,8,2,4,4,2,2,2,2};unsigned bs=etc_bytes(f);uint32_t raw[4]={0,0,0,1};
 size_t row=regions[2].bufferRowLength/4*bs,slice=row*(regions[2].bufferImageHeight/4);
 memcpy(raw,(uint8_t *)(upload->saved+upload->binding_words)+regions[2].bufferOffset+slice,bs);
 for(unsigned lane=0;lane<ETC_RECORDS;lane++) {
  unsigned x=lane&7,y=lane>>3,layer=1+(lane&1),xx=MIN2(x+1,7),yy=MIN2(y+1,7);float expected[16];
  for(unsigned c=0;c<4;c++) {
   expected[c]=image->decoded[1][layer][y*8+x][c];
   expected[4+c]=(image->decoded[1][layer][y*8+x][c]+image->decoded[1][layer][y*8+xx][c]+image->decoded[1][layer][yy*8+x][c]+image->decoded[1][layer][yy*8+xx][c])*0.25f;
   expected[8+c]=image->decoded[2][layer][(y/2)*4+x/2][c];expected[12+c]=image->decoded[3][layer][(y/4)*2+x/4][c];
  }
  size_t first=output->binding_words+GUARD+lane*ETC_FIELDS;
  for(unsigned k=0;k<16;k++) {
   if(output->map[first+k]==output->saved[first+k]){fprintf(stderr,"FAIL ETC unwritten format=%u round=%u lane=%u field=%u\n",f,round,lane,k);return 1;}
   float value;memcpy(&value,&output->map[first+k],4);double a=value,e=expected[k];bool linear=k>=4&&k<8;
   double tolerance=linear?(f<6?0.51/255.0:0.51/1023.0):1.5/32767.0;
   if(f<6&&(f&1)&&k%4<3&&!linear){a=etc_encoded(a);e=etc_encoded(e);tolerance=0.51/255.0;}
   unsigned group=linear?1:0;
   if(isfinite(value)&&fabs(a-e)>etc_numeric.max_error[group])etc_numeric.max_error[group]=fabs(a-e);
   if(!isfinite(value)||fabs(a-e)>tolerance){
    unsigned total=etc_numeric.mismatch[0]+etc_numeric.mismatch[1];
    etc_numeric.mismatch[group]++;
    if(!isfinite(value))etc_numeric.nonfinite[group]++;
    if(total<16)fprintf(stderr,"ETC_NUMERIC_MISMATCH format=%u round=%u lane=%u field=%u actual=%.9g expected=%.9g tolerance=%.9g\n",f,round,lane,k,value,expected[k],tolerance);
   }
   output->saved[first+k]=output->map[first+k];
  }
  for(unsigned k=0;k<9;k++)output->saved[first+16+k]=sizes[k];for(unsigned k=0;k<4;k++){output->saved[first+25+k]=raw[k];output->saved[first+29+k]=raw[k];}output->saved[first+33]=output->saved[first+34]=1;
 }
 for(unsigned j=0;j<3;j++){struct storage *s=j==0?upload:j==1?readback:output;for(size_t i=0;i<s->words;i++)if(s->map[i]!=s->saved[i]){fprintf(stderr,"FAIL ETC format=%u round=%u buffer=%u word=%zu actual=%08x expected=%08x\n",f,round,j,i,s->map[i],s->saved[i]);return 1;}}
 for(size_t i=0;i<image->words;i++)if(image->map[i]!=image->saved[i]){fprintf(stderr,"FAIL ETC format=%u round=%u image_word=%zu actual=%08x expected=%08x\n",f,round,i,image->map[i],image->saved[i]);return 1;}return 0;
}
int main(int argc,char **argv) {
 bool compile_only=argc==4&&!strcmp(argv[3],"--compile-only");
 REQUIRE(compile_only||(argc==6&&!strcmp(argv[3],"--observe-case")));REQUIRE(!getenv("RADISH_SW_TEXEL_FETCH"));
 unsigned selected_format=0,selected_round=0;
 if(!compile_only) {char *end;unsigned long value=strtoul(argv[4],&end,10);REQUIRE(*argv[4]&&!*end&&value<ETC_TYPES);selected_format=value;
  value=strtoul(argv[5],&end,10);REQUIRE(*argv[5]&&!*end&&value<2);selected_round=value;}

 void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);REQUIRE(lib);PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr");REQUIRE(gipa);
 VkInstance instance=VK_NULL_HANDLE;INSTANCE_FN(CreateInstance);VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_1};
 VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};SUCCESS(CreateInstance(&ici,NULL,&instance));
 INSTANCE_FN(EnumeratePhysicalDevices);INSTANCE_FN(GetPhysicalDeviceProperties);INSTANCE_FN(GetPhysicalDeviceFormatProperties);INSTANCE_FN(GetPhysicalDeviceImageFormatProperties);
 INSTANCE_FN(GetPhysicalDeviceMemoryProperties);INSTANCE_FN(CreateDevice);INSTANCE_FN(DestroyInstance);
 uint32_t n=1;VkPhysicalDevice physical;SUCCESS(EnumeratePhysicalDevices(instance,&n,&physical));REQUIRE(n==1);VkPhysicalDeviceProperties properties;GetPhysicalDeviceProperties(physical,&properties);REQUIRE(properties.apiVersion>=VK_API_VERSION_1_1);
 VkPhysicalDeviceMemoryProperties memory;GetPhysicalDeviceMemoryProperties(physical,&memory);
 for(unsigned f=0;f<ETC_TYPES;f++){VkFormatProperties fp;GetPhysicalDeviceFormatProperties(physical,etc_formats[f],&fp);VkFormatFeatureFlags need=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;REQUIRE((fp.optimalTilingFeatures&need)==need);
  VkImageFormatProperties ip;SUCCESS(GetPhysicalDeviceImageFormatProperties(physical,etc_formats[f],VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,&ip));REQUIRE(ip.maxExtent.width>=ETC_SIDE&&ip.maxMipLevels>=ETC_MIPS&&ip.maxArrayLayers>=ETC_LAYERS&&(ip.sampleCounts&1));}
 float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueCount=1,.pQueuePriorities=&priority};VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};VkDevice device;SUCCESS(CreateDevice(physical,&di,NULL,&device));
 const struct vk_device_dispatch_table *d=&rd_device_from_handle(device)->vk.dispatch_table;
 VkDescriptorSetLayoutBinding bindings[5];for(unsigned i=0;i<5;i++)bindings[i]=(VkDescriptorSetLayoutBinding){.binding=i,.descriptorType=i<4?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
 VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=5,.pBindings=bindings};VkDescriptorSetLayout sl;SUCCESS(d->CreateDescriptorSetLayout(device,&li,NULL,&sl));
 VkPipelineLayoutCreateInfo pi={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&sl};VkPipelineLayout layout;SUCCESS(d->CreatePipelineLayout(device,&pi,NULL,&layout));
 size_t bytes;uint32_t *code=read_spirv(argv[2],&bytes);REQUIRE(code);VkShaderModuleCreateInfo mi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=bytes,.pCode=code};VkShaderModule module;SUCCESS(d->CreateShaderModule(device,&mi,NULL,&module));free(code);
 VkComputePipelineCreateInfo pci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"},.layout=layout};VkPipeline pipeline;SUCCESS(d->CreateComputePipelines(device,VK_NULL_HANDLE,1,&pci,NULL,&pipeline));d->DestroyShaderModule(device,module,NULL);
 struct rd_pipeline *p=rd_pipeline_from_handle(pipeline);printf("ETC_COMPILED native=%u textures=%u samplers=%u buffers=%u before_allocations=1\n",p->variant->instrlen,p->resource_usage.texture_count,p->resource_usage.sampler_count,p->buffer_count);fflush(stdout);
 if(compile_only){d->DestroyPipeline(device,pipeline,NULL);d->DestroyPipelineLayout(device,layout,NULL);d->DestroyDescriptorSetLayout(device,sl,NULL);d->DestroyDevice(device,NULL);DestroyInstance(instance,NULL);dlclose(lib);puts("PASS ETC_OBSERVE compile-only; no data or commands");return 0;}
 VkSampler samplers[2];for(unsigned i=0;i<2;i++){VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=i?VK_FILTER_LINEAR:VK_FILTER_NEAREST,.minFilter=i?VK_FILTER_LINEAR:VK_FILTER_NEAREST,.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.maxLod=2};SUCCESS(d->CreateSampler(device,&si,NULL,&samplers[i]));}
 VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,4},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};VkDescriptorPoolCreateInfo dpi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=2,.pPoolSizes=sizes};VkDescriptorPool pool;SUCCESS(d->CreateDescriptorPool(device,&dpi,NULL,&pool));
 VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&sl};VkDescriptorSet set;SUCCESS(d->AllocateDescriptorSets(device,&ai,&set));
 VkCommandPoolCreateInfo cp={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};VkCommandPool command_pool;SUCCESS(d->CreateCommandPool(device,&cp,NULL,&command_pool));VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=command_pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer command;SUCCESS(d->AllocateCommandBuffers(device,&ca,&command));
 VkQueue queue;d->GetDeviceQueue(device,0,0,&queue);VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;SUCCESS(d->CreateFence(device,&fi,NULL,&fence));
 for(unsigned f=selected_format;f<selected_format+1;f++) {
  struct etc_image image={0},block_image={0},copy_image={0};REQUIRE(!etc_create(d,device,&memory,f,&image));REQUIRE(!etc_create_block(d,device,&memory,f,true,&block_image));REQUIRE(!etc_create_block(d,device,&memory,f,false,&copy_image));VkBufferImageCopy regions[ETC_MIPS];VkDeviceSize transfer_bytes=etc_regions(f,regions);struct storage upload={0},readback={0},output={0};
  REQUIRE(!storage_create(d,device,&memory,transfer_bytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,&upload));REQUIRE(!storage_create(d,device,&memory,transfer_bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT,&readback));REQUIRE(!storage_create(d,device,&memory,GUARD*4+ETC_RECORDS*ETC_FIELDS*4+64,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&output));
  VkMappedMemoryRange ranges[6]={{.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=image.memory,.size=VK_WHOLE_SIZE},{.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=upload.memory,.size=VK_WHOLE_SIZE},{.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=readback.memory,.size=VK_WHOLE_SIZE},{.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=output.memory,.size=VK_WHOLE_SIZE},{.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=block_image.memory,.size=VK_WHOLE_SIZE},{.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=copy_image.memory,.size=VK_WHOLE_SIZE}};
  VkDescriptorImageInfo ii[4]={{samplers[0],image.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{samplers[1],image.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{samplers[0],block_image.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{samplers[0],copy_image.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};VkDescriptorBufferInfo bi={output.buffer,GUARD*4,ETC_RECORDS*ETC_FIELDS*4};VkWriteDescriptorSet writes[5];for(unsigned i=0;i<5;i++)writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=i,.descriptorCount=1,.descriptorType=bindings[i].descriptorType,.pImageInfo=i<4?&ii[i]:NULL,.pBufferInfo=i==4?&bi:NULL};
  for(unsigned round=selected_round;round<selected_round+1;round++) {
   REQUIRE(!etc_prepare(&image,&upload,&readback,&output,f,round,regions));for(size_t i=0;i<block_image.words;i++)block_image.map[i]=poison(i,14,round);for(size_t i=0;i<copy_image.words;i++)copy_image.map[i]=poison(i,15,round);SUCCESS(d->FlushMappedMemoryRanges(device,6,ranges));SUCCESS(d->ResetCommandPool(device,command_pool,0));
   VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};SUCCESS(d->BeginCommandBuffer(command,&begin));
   VkImageMemoryBarrier ib={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image.image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,ETC_MIPS,0,ETC_LAYERS}};
   VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT};d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&host,0,NULL,1,&ib);
   d->CmdCopyBufferToImage(command,upload.buffer,image.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,ETC_MIPS,regions);
   ib.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ib.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;ib.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;ib.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&ib);
   VkImageMemoryBarrier extras[2]={ib,ib};extras[0].image=copy_image.image;extras[1].image=block_image.image;
   for(unsigned j=0;j<2;j++){extras[j].srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;extras[j].dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;extras[j].oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;extras[j].newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;extras[j].subresourceRange=(VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};}
   d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,2,extras);
   VkImageCopy to_raw={.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,2,1,1},.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.extent={4,4,1}};
   d->CmdCopyImage(command,image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,copy_image.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&to_raw);
   extras[0].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;extras[0].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;extras[0].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;extras[0].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
   d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&extras[0]);
   VkImageCopy to_compressed={.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.extent={1,1,1}};
   d->CmdCopyImage(command,copy_image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,block_image.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&to_compressed);
   d->CmdCopyImageToBuffer(command,image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.buffer,ETC_MIPS,regions);
   VkMemoryBarrier after={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&after,0,NULL,0,NULL);SUCCESS(d->EndCommandBuffer(command));SUCCESS(d->ResetFences(device,1,&fence));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};SUCCESS(d->QueueSubmit(queue,1,&submit,fence));SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));SUCCESS(d->InvalidateMappedMemoryRanges(device,6,ranges));
   for(size_t i=0;i<readback.words;i++)REQUIRE(readback.map[i]==readback.saved[i]);for(size_t i=0;i<image.words;i++)if(i<image.binding_words||i>=image.binding_words+image.image_words)REQUIRE(image.map[i]==poison(i,13,round));memcpy(image.saved,image.map,image.words*4);
   struct etc_image *extra_images[2]={&block_image,&copy_image};for(unsigned j=0;j<2;j++){struct etc_image *e=extra_images[j];for(size_t i=0;i<e->words;i++)if(i<e->binding_words||i>=e->binding_words+e->image_words)REQUIRE(e->map[i]==poison(i,14+j,round));memcpy(e->saved,e->map,e->words*4);}
   SUCCESS(d->ResetCommandPool(device,command_pool,0));d->UpdateDescriptorSets(device,5,writes,0,NULL);SUCCESS(d->BeginCommandBuffer(command,&begin));
   ib.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;ib.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;ib.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;ib.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   host.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;host.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;VkImageMemoryBarrier sampled[3]={ib,extras[1],extras[0]};for(unsigned j=1;j<3;j++){sampled[j].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT;sampled[j].oldLayout=j==1?VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;sampled[j].newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;sampled[j].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;}
   d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&host,0,NULL,3,sampled);
   d->CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);d->CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);d->CmdDispatch(command,ETC_RECORDS,1,1);
   after.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;after.dstAccessMask=VK_ACCESS_HOST_READ_BIT;d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&after,0,NULL,0,NULL);SUCCESS(d->EndCommandBuffer(command));SUCCESS(d->ResetFences(device,1,&fence));SUCCESS(d->QueueSubmit(queue,1,&submit,fence));SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));SUCCESS(d->InvalidateMappedMemoryRanges(device,6,ranges));
   REQUIRE(!p->sampling_variants);REQUIRE(!etc_check(&image,&upload,&readback,&output,f,round,regions));for(unsigned j=0;j<2;j++)for(size_t i=0;i<extra_images[j]->words;i++)REQUIRE(extra_images[j]->map[i]==extra_images[j]->saved[i]);printf("ETC_CASE_SUMMARY format=%u round=%u mismatches_nearest=%u mismatches_linear=%u nonfinite_nearest=%u nonfinite_linear=%u max_nearest_error=%.12g max_linear_error=%.12g guards=exact api=success records=%u fields=%u\n",
      f,round,etc_numeric.mismatch[0],etc_numeric.mismatch[1],etc_numeric.nonfinite[0],etc_numeric.nonfinite[1],etc_numeric.max_error[0],etc_numeric.max_error[1],ETC_RECORDS,ETC_FIELDS);fflush(stdout);printf("ETC_CASE_GUARDS format=%u round=%u output_words=%u full6allocations=1 array_base_layer=1 mip_base=1 block_view_single_level_layer=1 crossformat_copy_both_directions=1 native_path=1 no_force=1\n",f,round,ETC_RECORDS*ETC_FIELDS);fflush(stdout);
  }
  struct storage *stores[3]={&upload,&readback,&output};for(unsigned j=0;j<3;j++){free(stores[j]->saved);d->UnmapMemory(device,stores[j]->memory);d->DestroyBuffer(device,stores[j]->buffer,NULL);d->FreeMemory(device,stores[j]->memory,NULL);}
  struct etc_image *all_images[3]={&image,&block_image,&copy_image};for(unsigned j=0;j<3;j++){struct etc_image *e=all_images[j];d->DestroyImageView(device,e->view,NULL);free(e->saved);d->UnmapMemory(device,e->memory);d->DestroyImage(device,e->image,NULL);d->FreeMemory(device,e->memory,NULL);}
 }
 d->DestroyFence(device,fence,NULL);d->DestroyCommandPool(device,command_pool,NULL);d->DestroyDescriptorPool(device,pool,NULL);for(unsigned i=0;i<2;i++)d->DestroySampler(device,samplers[i],NULL);d->DestroyPipeline(device,pipeline,NULL);d->DestroyPipelineLayout(device,layout,NULL);d->DestroyDescriptorSetLayout(device,sl,NULL);d->DestroyDevice(device,NULL);DestroyInstance(instance,NULL);dlclose(lib);
 if(etc_numeric.mismatch[0]+etc_numeric.mismatch[1]){puts("ETC_OBSERVE_NUMERICAL_FAIL: original oracle retained, full allocation guards passed");return 2;}
 puts("PASS ETC_OBSERVE_CASE: original exact/tolerance checks and full guards");return 0;
}
