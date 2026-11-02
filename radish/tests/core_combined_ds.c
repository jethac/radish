int resource_baseline_main(int argc,char **argv);
#define main resource_baseline_main
#include "core_resources.c"
#undef main
#include <math.h>

enum { DS_ASPECTS=VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT, DS_WORDS=128 };
struct ds_image {
 VkImage image; VkImageView depth,stencil; VkDeviceMemory memory;
 uint32_t *map,*saved; size_t words,first,count; VkSubresourceLayout sub;
 unsigned width,height; bool linear;
};
static int ds_create(const struct vk_device_dispatch_table *d,VkDevice device,const VkPhysicalDeviceMemoryProperties *memory,unsigned index,struct ds_image *im)
{
 im->linear=index!=1;im->width=index==0?8:4;im->height=4;
 VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_D24_UNORM_S8_UINT,
  .extent={im->width,im->height,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,
  .tiling=im->linear?VK_IMAGE_TILING_LINEAR:VK_IMAGE_TILING_OPTIMAL,
  .usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|(im->linear?0:VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT),
  .initialLayout=im->linear?VK_IMAGE_LAYOUT_PREINITIALIZED:VK_IMAGE_LAYOUT_UNDEFINED};
 SUCCESS(d->CreateImage(device,&ci,NULL,&im->image));VkMemoryRequirements r;d->GetImageMemoryRequirements(device,im->image,&r);
 REQUIRE(r.alignment&&!(r.alignment%4)&&r.size<1024*1024&&!(r.size%4));unsigned type=memory_type(&r,memory);REQUIRE(type!=UINT32_MAX);
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=r.alignment+r.size+64,.memoryTypeIndex=type};
 SUCCESS(d->AllocateMemory(device,&ai,NULL,&im->memory));SUCCESS(d->BindImageMemory(device,im->image,im->memory,r.alignment));
 SUCCESS(d->MapMemory(device,im->memory,0,VK_WHOLE_SIZE,0,(void **)&im->map));im->words=ai.allocationSize/4;im->first=r.alignment/4;im->count=r.size/4;
 im->saved=malloc(ai.allocationSize);REQUIRE(im->saved);
 if(im->linear) {
  VkImageSubresource sub={VK_IMAGE_ASPECT_DEPTH_BIT,0,0};d->GetImageSubresourceLayout(device,im->image,&sub,&im->sub);
  VkSubresourceLayout stencil;sub.aspectMask=VK_IMAGE_ASPECT_STENCIL_BIT;d->GetImageSubresourceLayout(device,im->image,&sub,&stencil);
  REQUIRE(im->sub.offset==stencil.offset&&im->sub.size==stencil.size&&im->sub.rowPitch==stencil.rowPitch);
  REQUIRE(!(im->sub.offset%4)&&!(im->sub.rowPitch%4)&&im->sub.rowPitch>=im->width*4&&im->sub.size<=r.size);
  REQUIRE(im->sub.offset+(im->height-1)*im->sub.rowPitch+im->width*4<=r.size);
 } else {
  VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=im->image,.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=ci.format,.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1}};
  SUCCESS(d->CreateImageView(device,&vi,NULL,&im->depth));vi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_STENCIL_BIT;SUCCESS(d->CreateImageView(device,&vi,NULL,&im->stencil));
 }
 return 0;
}
static size_t ds_word(const struct ds_image *im,unsigned x,unsigned y)
{ return im->first+im->sub.offset/4+y*im->sub.rowPitch/4+x; }
static uint32_t ds_value(unsigned x,unsigned y,unsigned round)
{
 static const uint32_t depth[8]={0,0xffffff,0x555555,0xaaaaaa,0x800000,0x7fffff,1,0xfffffe};
 uint32_t z=depth[(x+2*y+round)%8];unsigned s=(x*37+y*53+round*71)&255;
 return z|(s<<24);
}
static VkImageMemoryBarrier ds_barrier(VkImage image,VkImageLayout old,VkImageLayout next,VkAccessFlags src,VkAccessFlags dst)
{
 return (VkImageMemoryBarrier){.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=src,.dstAccessMask=dst,.oldLayout=old,.newLayout=next,
  .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image,.subresourceRange={DS_ASPECTS,0,1,0,1}};
}
static void ds_copy(const struct vk_device_dispatch_table *d,VkCommandBuffer command,VkImage src,VkImage dst,VkImageAspectFlags aspects,unsigned sx,unsigned sy,unsigned dx,unsigned dy,unsigned w,unsigned h)
{
 VkImageCopy c={.srcSubresource={aspects,0,0,1},.srcOffset={(int)sx,(int)sy,0},.dstSubresource={aspects,0,0,1},.dstOffset={(int)dx,(int)dy,0},.extent={w,h,1}};
 d->CmdCopyImage(command,src,VK_IMAGE_LAYOUT_GENERAL,dst,VK_IMAGE_LAYOUT_GENERAL,1,&c);
}
int main(int argc,char **argv)
{
 REQUIRE(argc==3||(argc==4&&!strcmp(argv[3],"--compile-only")));
 void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);REQUIRE(lib);PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr");REQUIRE(gipa);
 VkInstance instance=VK_NULL_HANDLE;INSTANCE_FN(CreateInstance);VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_1};
 VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};SUCCESS(CreateInstance(&ici,NULL,&instance));
 INSTANCE_FN(EnumeratePhysicalDevices);INSTANCE_FN(GetPhysicalDeviceMemoryProperties);INSTANCE_FN(GetPhysicalDeviceProperties);INSTANCE_FN(GetPhysicalDeviceFeatures);INSTANCE_FN(GetPhysicalDeviceFormatProperties);INSTANCE_FN(GetPhysicalDeviceImageFormatProperties);INSTANCE_FN(CreateDevice);INSTANCE_FN(DestroyInstance);
 VkPhysicalDevice physical;uint32_t count=1;SUCCESS(EnumeratePhysicalDevices(instance,&count,&physical));REQUIRE(count==1);
 VkPhysicalDeviceMemoryProperties memory;GetPhysicalDeviceMemoryProperties(physical,&memory);VkPhysicalDeviceProperties props;GetPhysicalDeviceProperties(physical,&props);REQUIRE(props.apiVersion>=VK_API_VERSION_1_1);
 VkPhysicalDeviceFeatures features;GetPhysicalDeviceFeatures(physical,&features);REQUIRE(!features.textureCompressionETC2);
 const VkFormat compressed[10]={VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK,VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK,VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK,VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK,VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK,VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK,VK_FORMAT_EAC_R11_UNORM_BLOCK,VK_FORMAT_EAC_R11_SNORM_BLOCK,VK_FORMAT_EAC_R11G11_UNORM_BLOCK,VK_FORMAT_EAC_R11G11_SNORM_BLOCK};
 for(unsigned j=0;j<10;j++){VkFormatProperties q;GetPhysicalDeviceFormatProperties(physical,compressed[j],&q);VkFormatFeatureFlags required=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT|VK_FORMAT_FEATURE_BLIT_SRC_BIT;REQUIRE((q.optimalTilingFeatures&required)==required&&q.linearTilingFeatures==0&&q.bufferFeatures==0);}
 puts("D24S8_QUERY compressed_blit_src=10 compression_feature=0 single_sample_initializer=linear_preinitialized");
 VkFormatProperties fp;GetPhysicalDeviceFormatProperties(physical,VK_FORMAT_D24_UNORM_S8_UINT,&fp);
 REQUIRE((fp.linearTilingFeatures&(VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT))==(VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT));
 REQUIRE((fp.optimalTilingFeatures&(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))==(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT));
 VkImageFormatProperties ip;for(unsigned j=0;j<2;j++){SUCCESS(GetPhysicalDeviceImageFormatProperties(physical,VK_FORMAT_D24_UNORM_S8_UINT,VK_IMAGE_TYPE_2D,j?VK_IMAGE_TILING_OPTIMAL:VK_IMAGE_TILING_LINEAR,
  VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|(j?VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:0),0,&ip));REQUIRE(ip.maxExtent.width>=8&&ip.maxExtent.height>=4&&(ip.sampleCounts&VK_SAMPLE_COUNT_1_BIT));}
 float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueCount=1,.pQueuePriorities=&priority};
 VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};VkDevice device;SUCCESS(CreateDevice(physical,&di,NULL,&device));
 const struct vk_device_dispatch_table *d=&rd_device_from_handle(device)->vk.dispatch_table;
 VkDescriptorSetLayoutBinding bindings[3];for(unsigned j=0;j<3;j++)bindings[j]=(VkDescriptorSetLayoutBinding){.binding=j,.descriptorType=j<2?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
 VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=3,.pBindings=bindings};VkDescriptorSetLayout sl;SUCCESS(d->CreateDescriptorSetLayout(device,&li,NULL,&sl));
 VkPipelineLayoutCreateInfo pi={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&sl};VkPipelineLayout layout;SUCCESS(d->CreatePipelineLayout(device,&pi,NULL,&layout));
 size_t code_size;uint32_t *code=read_spirv(argv[2],&code_size);REQUIRE(code);VkShaderModuleCreateInfo mi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=code_size,.pCode=code};VkShaderModule module;SUCCESS(d->CreateShaderModule(device,&mi,NULL,&module));free(code);
 VkComputePipelineCreateInfo pci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"},.layout=layout};VkPipeline pipeline;SUCCESS(d->CreateComputePipelines(device,VK_NULL_HANDLE,1,&pci,NULL,&pipeline));d->DestroyShaderModule(device,module,NULL);
 struct rd_pipeline *compiled=rd_pipeline_from_handle(pipeline);REQUIRE(compiled->descriptor_count==3&&compiled->resource_usage.texture_count==2&&compiled->buffer_count==1);
 printf("D24S8_COMPILED active=3 textures=2 buffers=1 native=%u before_allocations=1\n",compiled->variant->instrlen);fflush(stdout);
 if(argc==4){d->DestroyPipeline(device,pipeline,NULL);d->DestroyPipelineLayout(device,layout,NULL);d->DestroyDescriptorSetLayout(device,sl,NULL);d->DestroyDevice(device,NULL);DestroyInstance(instance,NULL);dlclose(lib);puts("PASS D24S8_PUBLIC compile-only");return 0;}
 struct ds_image images[3]={0};for(unsigned j=0;j<3;j++)REQUIRE(!ds_create(d,device,&memory,j,&images[j]));
 struct storage output={0};REQUIRE(!storage_create(d,device,&memory,GUARD*4+DS_WORDS*4+64,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&output));
 VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};VkSampler sampler;SUCCESS(d->CreateSampler(device,&si,NULL,&sampler));
 VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};VkDescriptorPoolCreateInfo dpi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=2,.pPoolSizes=sizes};VkDescriptorPool pool;SUCCESS(d->CreateDescriptorPool(device,&dpi,NULL,&pool));
 VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&sl};VkDescriptorSet set;SUCCESS(d->AllocateDescriptorSets(device,&ai,&set));
 VkDescriptorImageInfo sampled[2]={{sampler,images[1].depth,VK_IMAGE_LAYOUT_GENERAL},{sampler,images[1].stencil,VK_IMAGE_LAYOUT_GENERAL}};VkDescriptorBufferInfo out={output.buffer,GUARD*4,DS_WORDS*4};VkWriteDescriptorSet writes[3];
 for(unsigned j=0;j<3;j++)writes[j]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=j,.descriptorCount=1,.descriptorType=bindings[j].descriptorType,.pImageInfo=j<2?&sampled[j]:NULL,.pBufferInfo=j==2?&out:NULL};d->UpdateDescriptorSets(device,3,writes,0,NULL);
 VkCommandPoolCreateInfo cp={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};VkCommandPool command_pool;SUCCESS(d->CreateCommandPool(device,&cp,NULL,&command_pool));VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=command_pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer command;SUCCESS(d->AllocateCommandBuffers(device,&ca,&command));
 VkQueue queue;d->GetDeviceQueue(device,0,0,&queue);VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;SUCCESS(d->CreateFence(device,&fi,NULL,&fence));
 VkMappedMemoryRange ranges[4];for(unsigned j=0;j<4;j++)ranges[j]=(VkMappedMemoryRange){.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=j<3?images[j].memory:output.memory,.size=VK_WHOLE_SIZE};
 for(unsigned round=0;round<2;round++) {
  for(unsigned j=0;j<3;j++){struct ds_image *im=&images[j];for(size_t i=0;i<im->words;i++)if(im->linear||i<im->first||i>=im->first+im->count)im->map[i]=poison(i,70+j,round);memcpy(im->saved,im->map,im->words*4);}
  for(size_t i=0;i<output.words;i++)output.map[i]=poison(i,73,round);memcpy(output.saved,output.map,output.words*4);
  for(unsigned y=0;y<4;y++)for(unsigned x=0;x<8;x++)images[0].map[ds_word(&images[0],x,y)]=ds_value(x,y,round);memcpy(images[0].saved,images[0].map,images[0].words*4);
  uint32_t expected[16];for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++)expected[y*4+x]=ds_value(x,y,round);
  for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++){unsigned q=(y+1)*4+x+1;expected[q]=(expected[q]&0xff000000u)|(ds_value(x+4,y,round)&0xffffffu);}
  for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++){unsigned q=y*4+x;expected[q]=(expected[q]&0xffffffu)|(ds_value(x+6,y+2,round)&0xff000000u);}
  for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++)images[2].saved[ds_word(&images[2],x,y)]=expected[y*4+x];
  SUCCESS(d->FlushMappedMemoryRanges(device,4,ranges));SUCCESS(d->ResetCommandPool(device,command_pool,0));VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};SUCCESS(d->BeginCommandBuffer(command,&begin));
  VkImageMemoryBarrier ib[3];for(unsigned j=0;j<3;j++)ib[j]=ds_barrier(images[j].image,round?VK_IMAGE_LAYOUT_GENERAL:images[j].linear?VK_IMAGE_LAYOUT_PREINITIALIZED:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT);
  VkMemoryBarrier mb={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT};d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&mb,0,NULL,3,ib);
  ds_copy(d,command,images[0].image,images[1].image,DS_ASPECTS,0,0,0,0,4,4);
  ib[1]=ds_barrier(images[1].image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT);d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&ib[1]);
  ds_copy(d,command,images[0].image,images[1].image,VK_IMAGE_ASPECT_DEPTH_BIT,4,0,1,1,2,2);d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&ib[1]);
  ds_copy(d,command,images[0].image,images[1].image,VK_IMAGE_ASPECT_STENCIL_BIT,6,2,0,0,2,2);
  ib[1].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,NULL,0,NULL,1,&ib[1]);
  d->CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);d->CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);d->CmdDispatch(command,16,1,1);
  ib[1].srcAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;ib[1].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&ib[1]);
  ds_copy(d,command,images[1].image,images[2].image,DS_ASPECTS,0,0,0,0,4,4);
  mb.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_HOST_READ_BIT;d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&mb,0,NULL,0,NULL);
  SUCCESS(d->EndCommandBuffer(command));SUCCESS(d->ResetFences(device,1,&fence));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};SUCCESS(d->QueueSubmit(queue,1,&submit,fence));SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));SUCCESS(d->InvalidateMappedMemoryRanges(device,4,ranges));
  for(unsigned i=0;i<16;i++)for(unsigned c=0;c<8;c++){size_t q=output.binding_words+GUARD+8*i+c;uint32_t want=c==0?float_word((expected[i]&0xffffffu)/16777215.0f):c==3?float_word(1.0f):c==4?expected[i]>>24:c==7?1:0;
   if(c==0){float actual;memcpy(&actual,&output.map[q],4);float ref=(expected[i]&0xffffffu)/16777215.0f;if(!isfinite(actual)||fabsf(actual-ref)>0.0000002384185791015625f){fprintf(stderr,"FAIL D24S8 round=%u lane=%u depth actual=%.9g expected=%.9g\n",round,i,actual,ref);return 1;}output.saved[q]=output.map[q];}
   else output.saved[q]=want;
  }
  for(size_t i=0;i<output.words;i++)if(output.map[i]!=output.saved[i]){fprintf(stderr,"FAIL D24S8 round=%u output_word=%zu actual=%08x expected=%08x\n",round,i,output.map[i],output.saved[i]);return 1;}
  for(unsigned j=0;j<3;j++)for(size_t i=0;i<images[j].words;i++)if(images[j].linear||i<images[j].first||i>=images[j].first+images[j].count)if(images[j].map[i]!=images[j].saved[i]){fprintf(stderr,"FAIL D24S8 round=%u image=%u word=%zu actual=%08x expected=%08x\n",round,j,i,images[j].map[i],images[j].saved[i]);return 1;}
  printf("D24S8_ROUND_PASS round=%u sampled_words=128 exact_copy_texels=16 aspect_rectangles=2 full_linear_allocations=2 full_output=1 optimal_outer_guards=1\n",round);fflush(stdout);
 }
 d->DestroyFence(device,fence,NULL);d->DestroyCommandPool(device,command_pool,NULL);d->DestroyDescriptorPool(device,pool,NULL);d->DestroySampler(device,sampler,NULL);free(output.saved);d->UnmapMemory(device,output.memory);d->DestroyBuffer(device,output.buffer,NULL);d->FreeMemory(device,output.memory,NULL);
 for(unsigned j=0;j<3;j++){free(images[j].saved);if(images[j].depth)d->DestroyImageView(device,images[j].depth,NULL);if(images[j].stencil)d->DestroyImageView(device,images[j].stencil,NULL);d->UnmapMemory(device,images[j].memory);d->DestroyImage(device,images[j].image,NULL);d->FreeMemory(device,images[j].memory,NULL);}
 d->DestroyPipeline(device,pipeline,NULL);d->DestroyPipelineLayout(device,layout,NULL);d->DestroyDescriptorSetLayout(device,sl,NULL);d->DestroyDevice(device,NULL);DestroyInstance(instance,NULL);dlclose(lib);
 puts("PASS D24S8_PUBLIC: 256 sampled words, 32 exact packed readback texels, aspect preservation and all allocation guards; single-sample only");return 0;
}
