#include "test_common.h"
#include "rd_private.h"
#include <inttypes.h>

enum { NX=17, NY=5, N=NX*NY, GUARD=16, FIELDS=16, BUFFER_COUNT=9 };
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
   unsigned height,VkImageUsageFlags usage,struct image *image)
{
   VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
      .format=format,.extent={width,height,1},.mipLevels=1,.arrayLayers=1,
      .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_LINEAR,
      .usage=usage|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
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
   VkBindImageMemoryInfo bind={.sType=VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,.image=image->image,.memory=image->memory,.memoryOffset=r.alignment};
   SUCCESS(d->BindImageMemory2(device,1,&bind));
   SUCCESS(d->MapMemory(device,image->memory,0,VK_WHOLE_SIZE,0,(void **)&image->map));
   image->words=ai.allocationSize/4; image->binding_words=r.alignment/4;
   VkImageSubresource sub={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT}; VkSubresourceLayout layout;
   d->GetImageSubresourceLayout(device,image->image,&sub,&layout);
   REQUIRE(layout.offset==0 && !(layout.rowPitch%4) && layout.rowPitch>=width*4);
   image->width=width; image->height=height; image->row_words=layout.rowPitch/4;
   VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image->image,
      .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=format,
      .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
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
static int verify(struct storage stores[BUFFER_COUNT],struct image images[3],unsigned round)
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
         float_word(sample_value(x0,ty)),float_word(linear),0,0,1,value,NX,NY,N,N};
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
   return 0;
}
int main(int argc,char **argv)
{
   if (argc!=3 && !(argc==4 && !strcmp(argv[3],"--compile-only"))) {
      fprintf(stderr,"Usage: %s ICD-library resources.spv [--compile-only]\n",argv[0]); return 1;
   }
   REQUIRE(!getenv("RADISH_BATCH_CHUNKS") || !strcmp(getenv("RADISH_BATCH_CHUNKS"),"1"));
   REQUIRE(!getenv("RADISH_TILED_GROUPS_X") || !strcmp(getenv("RADISH_TILED_GROUPS_X"),"1"));
   void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); if (!lib) { fprintf(stderr,"%s\n",dlerror()); return 1; }
   PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr"); REQUIRE(gipa);
   VkInstance instance=VK_NULL_HANDLE; INSTANCE_FN(CreateInstance);
   VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_0};
   const char *iex[]={VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME};
   VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app,.enabledExtensionCount=1,.ppEnabledExtensionNames=iex};
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
   float priority=1; VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueCount=1,.pQueuePriorities=&priority};
   const char *dex[]={VK_KHR_BIND_MEMORY_2_EXTENSION_NAME,VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME};
   VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qci,.enabledExtensionCount=2,.ppEnabledExtensionNames=dex};
   VkDevice device; SUCCESS(CreateDevice(physical,&dci,NULL,&device));
   struct rd_device *inside=rd_device_from_handle(device);
   PFN_vkGetDeviceProcAddr gdpa=(PFN_vkGetDeviceProcAddr)gipa(instance,"vkGetDeviceProcAddr"); REQUIRE(gdpa);
   struct vk_device_dispatch_table aliases=inside->vk.dispatch_table;
   aliases.BindImageMemory2=(PFN_vkBindImageMemory2)gdpa(device,"vkBindImageMemory2KHR");
   aliases.GetImageMemoryRequirements2=(PFN_vkGetImageMemoryRequirements2)gdpa(device,"vkGetImageMemoryRequirements2KHR");
   aliases.GetImageSparseMemoryRequirements2=(PFN_vkGetImageSparseMemoryRequirements2)gdpa(device,"vkGetImageSparseMemoryRequirements2KHR");
   REQUIRE(aliases.BindImageMemory2 && aliases.GetImageMemoryRequirements2 && aliases.GetImageSparseMemoryRequirements2);
   const struct vk_device_dispatch_table *d=&aliases;
   VkSampler samplers[2];
   for (unsigned i=0;i<2;i++) {
      VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
         .magFilter=i?VK_FILTER_LINEAR:VK_FILTER_NEAREST,.minFilter=i?VK_FILTER_LINEAR:VK_FILTER_NEAREST,
         .mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         .addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
      SUCCESS(d->CreateSampler(device,&si,NULL,&samplers[i]));
   }
   uint32_t numbers[9]={19,7,0,13,2,17,9,5,11};
   VkDescriptorType types[9]={VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER};
   VkDescriptorSetLayoutBinding bindings[9];
   for (unsigned i=0;i<9;i++) bindings[i]=(VkDescriptorSetLayoutBinding){.binding=numbers[i],.descriptorType=types[i],
      .descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,.pImmutableSamplers=numbers[i]==17?&samplers[0]:NULL};
   VkDescriptorSetLayout sl[3]; VkDescriptorSetLayoutCreateInfo sci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
   SUCCESS(d->CreateDescriptorSetLayout(device,&sci,NULL,&sl[0])); sci.bindingCount=9; sci.pBindings=bindings;
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
      for (unsigned i=0;i<2;i++) d->DestroySampler(device,samplers[i],NULL);
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
   struct image images[3]={0};
   REQUIRE(image_create(d,device,&memory_properties,VK_FORMAT_R32_UINT,NX,NY,VK_IMAGE_USAGE_STORAGE_BIT,&images[0])==0);
   REQUIRE(image_create(d,device,&memory_properties,VK_FORMAT_R32_SFLOAT,4,4,VK_IMAGE_USAGE_SAMPLED_BIT,&images[1])==0);
   REQUIRE(image_create(d,device,&memory_properties,VK_FORMAT_R32_UINT,NX,NY,VK_IMAGE_USAGE_STORAGE_BIT,&images[2])==0);
   VkBufferView views[2]; REQUIRE(buffer_view(d,device,stores[SOURCE_TEXELS].buffer,&views[0])==0);
   REQUIRE(buffer_view(d,device,stores[TARGET_TEXELS].buffer,&views[1])==0);
   VkDescriptorPoolSize pools[8]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,1},
      {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,1},{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,1},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2}};
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
   VkCommandPoolCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; VkCommandPool command_pool;
   SUCCESS(d->CreateCommandPool(device,&ci,NULL,&command_pool));
   VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=command_pool,
      .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1}; VkCommandBuffer command;
   SUCCESS(d->AllocateCommandBuffers(device,&ca,&command));
   VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
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
   d->DestroyDescriptorSetLayout(device,sl[2],NULL); sl[2]=VK_NULL_HANDLE;
   VkQueue queue; d->GetDeviceQueue(device,0,0,&queue);
   VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence;
   SUCCESS(d->CreateFence(device,&fi,NULL,&fence));
   VkMappedMemoryRange ranges[BUFFER_COUNT+3];
   for (unsigned i=0;i<BUFFER_COUNT+3;i++) ranges[i]=(VkMappedMemoryRange){.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
      .memory=i<BUFFER_COUNT?stores[i].memory:images[i-BUFFER_COUNT].memory,.size=VK_WHOLE_SIZE};
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
      descriptor_buffers(d,device,set,stores,round,false);
      SUCCESS(d->ResetFences(device,1,&fence));
      VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
      uint64_t before=inside->submit_count;
      printf("RESOURCE_ROUND_BEGIN round=%u grid=%u,%u late_updates=1 dynamic=16,16\n",round,NX,NY); fflush(stdout);
      SUCCESS(d->QueueSubmit(queue,1,&submit,fence));
      SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
      SUCCESS(d->InvalidateMappedMemoryRanges(device,BUFFER_COUNT+3,ranges));
      REQUIRE(verify(stores,images,round)==0);
      REQUIRE(inside->submit_count-before>=2);
      printf("RESOURCE_ROUND_PASS round=%u records=%u oracle_words=%u input_and_allocation_guards=exact submits=%" PRIu64 "\n",
         round,N,N*FIELDS,inside->submit_count-before); fflush(stdout);
   }
   d->DestroyFence(device,fence,NULL); d->DestroyCommandPool(device,command_pool,NULL);
   d->DestroyDescriptorPool(device,pool,NULL);
   for (unsigned i=0;i<2;i++) d->DestroyBufferView(device,views[i],NULL);
   for (unsigned i=0;i<3;i++) {
      d->DestroyImageView(device,images[i].view,NULL); d->UnmapMemory(device,images[i].memory);
      d->DestroyImage(device,images[i].image,NULL); d->FreeMemory(device,images[i].memory,NULL);
   }
   for (unsigned i=0;i<BUFFER_COUNT;i++) {
      free(stores[i].saved); d->UnmapMemory(device,stores[i].memory);
      d->DestroyBuffer(device,stores[i].buffer,NULL); d->FreeMemory(device,stores[i].memory,NULL);
   }
   d->DestroyPipeline(device,pipeline,NULL); d->DestroyPipelineLayout(device,layouts[0],NULL);
   for (unsigned i=0;i<2;i++) d->DestroyDescriptorSetLayout(device,sl[i],NULL);
   for (unsigned i=0;i<2;i++) d->DestroySampler(device,samplers[i],NULL);
   d->DestroyDevice(device,NULL); DestroyInstance(instance,NULL); dlclose(lib);
   puts("PASS RESOURCE: UBO/dynamic offsets/late updates/compatible layouts/typed texels/images/nearest+linear/image copy+clear exact/guards");
   return 0;
}
