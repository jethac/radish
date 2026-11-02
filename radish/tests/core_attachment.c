int resource_baseline_main(int argc,char **argv);
#define main resource_baseline_main
#include "core_resources.c"
#undef main

struct attachment_image { VkImage image; VkImageView view; VkDeviceMemory memory; uint32_t *map; size_t words,first,count; };
static int attachment_create(const struct vk_device_dispatch_table *d,VkDevice device,const VkPhysicalDeviceMemoryProperties *memory,unsigned index,struct attachment_image *im)
{
 VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,
  .extent={4,4,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
  .usage=VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|
    (index?0:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT),.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
 SUCCESS(d->CreateImage(device,&ci,NULL,&im->image));VkMemoryRequirements r;d->GetImageMemoryRequirements(device,im->image,&r);
 REQUIRE(r.alignment&&!(r.alignment%4)&&r.size<1024*1024&&!(r.size%4));uint32_t type=memory_type(&r,memory);REQUIRE(type!=UINT32_MAX);
 VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=r.alignment+r.size+64,.memoryTypeIndex=type};
 SUCCESS(d->AllocateMemory(device,&ai,NULL,&im->memory));SUCCESS(d->BindImageMemory(device,im->image,im->memory,r.alignment));
 SUCCESS(d->MapMemory(device,im->memory,0,VK_WHOLE_SIZE,0,(void **)&im->map));im->words=ai.allocationSize/4;im->first=r.alignment/4;im->count=r.size/4;
 VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=im->image,.viewType=VK_IMAGE_VIEW_TYPE_2D,
  .format=ci.format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
 SUCCESS(d->CreateImageView(device,&vi,NULL,&im->view));return 0;
}
int main(int argc,char **argv)
{
 REQUIRE(argc==3||(argc==4&&!strcmp(argv[3],"--compile-only")));
 void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);REQUIRE(lib);PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr");REQUIRE(gipa);
 VkInstance instance=VK_NULL_HANDLE;INSTANCE_FN(CreateInstance);VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_1};
 VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};SUCCESS(CreateInstance(&ici,NULL,&instance));
 INSTANCE_FN(EnumeratePhysicalDevices);INSTANCE_FN(GetPhysicalDeviceMemoryProperties);INSTANCE_FN(GetPhysicalDeviceProperties);
 INSTANCE_FN(GetPhysicalDeviceFormatProperties);INSTANCE_FN(GetPhysicalDeviceImageFormatProperties);INSTANCE_FN(CreateDevice);INSTANCE_FN(DestroyInstance);
 VkPhysicalDevice physical;uint32_t count=1;SUCCESS(EnumeratePhysicalDevices(instance,&count,&physical));REQUIRE(count==1);
 VkPhysicalDeviceMemoryProperties memory;GetPhysicalDeviceMemoryProperties(physical,&memory);VkPhysicalDeviceProperties p;GetPhysicalDeviceProperties(physical,&p);
 REQUIRE(p.apiVersion>=VK_API_VERSION_1_1&&p.limits.maxFramebufferWidth>=4096&&p.limits.maxFramebufferHeight>=4096&&p.limits.maxFramebufferLayers>=256);
 REQUIRE(p.limits.maxViewportDimensions[0]>=p.limits.maxFramebufferWidth&&p.limits.maxViewportDimensions[1]>=p.limits.maxFramebufferHeight);
 REQUIRE(p.limits.viewportBoundsRange[0]<=-8192.0f&&p.limits.viewportBoundsRange[1]>=8191.0f);
 REQUIRE(p.limits.maxPerStageDescriptorInputAttachments>=4&&p.limits.maxDescriptorSetInputAttachments>=4);
 VkFormatProperties fp;GetPhysicalDeviceFormatProperties(physical,VK_FORMAT_R8G8B8A8_UNORM,&fp);
 REQUIRE(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
 VkImageFormatProperties ip;SUCCESS(GetPhysicalDeviceImageFormatProperties(physical,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,
  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,0,&ip));
 REQUIRE(ip.maxExtent.width>=4096&&ip.maxExtent.height>=4096&&ip.maxArrayLayers>=256&&(ip.sampleCounts&1));
 float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueCount=1,.pQueuePriorities=&priority};
 VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};VkDevice device;SUCCESS(CreateDevice(physical,&di,NULL,&device));
 const struct vk_device_dispatch_table *d=&rd_device_from_handle(device)->vk.dispatch_table;
 VkDescriptorSetLayoutBinding bindings[3]={{.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT},
  {.binding=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT},
  {.binding=2,.descriptorType=VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,.descriptorCount=4,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT}};
 VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=3,.pBindings=bindings};VkDescriptorSetLayout sl;SUCCESS(d->CreateDescriptorSetLayout(device,&li,NULL,&sl));
 VkPipelineLayoutCreateInfo pi={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&sl};VkPipelineLayout layout;SUCCESS(d->CreatePipelineLayout(device,&pi,NULL,&layout));
 size_t code_size;uint32_t *code=read_spirv(argv[2],&code_size);REQUIRE(code);VkShaderModuleCreateInfo mi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=code_size,.pCode=code};VkShaderModule module;SUCCESS(d->CreateShaderModule(device,&mi,NULL,&module));free(code);
 VkComputePipelineCreateInfo pci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"},.layout=layout};VkPipeline pipeline;SUCCESS(d->CreateComputePipelines(device,VK_NULL_HANDLE,1,&pci,NULL,&pipeline));d->DestroyShaderModule(device,module,NULL);
 struct rd_pipeline *compiled=rd_pipeline_from_handle(pipeline);REQUIRE(compiled->descriptor_count==2&&compiled->resource_usage.texture_count==1&&compiled->buffer_count==1);
 printf("ATTACHMENT_COMPILED active=2 textures=1 buffers=1 native=%u host_input_descriptors=4 before_allocations=1\n",compiled->variant->instrlen);fflush(stdout);
 if(argc==4){d->DestroyPipeline(device,pipeline,NULL);d->DestroyPipelineLayout(device,layout,NULL);d->DestroyDescriptorSetLayout(device,sl,NULL);d->DestroyDevice(device,NULL);DestroyInstance(instance,NULL);dlclose(lib);puts("PASS ATTACHMENT_SHARED compile-only");return 0;}
 struct attachment_image images[2]={0};for(unsigned j=0;j<2;j++)REQUIRE(!attachment_create(d,device,&memory,j,&images[j]));
 struct storage data[3]={0};for(unsigned j=0;j<3;j++)REQUIRE(!storage_create(d,device,&memory,GUARD*4+(j==2?64:32)*4+64,j==0?VK_BUFFER_USAGE_TRANSFER_SRC_BIT:j==1?VK_BUFFER_USAGE_TRANSFER_DST_BIT:VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&data[j]));
 VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};VkSampler sampler;SUCCESS(d->CreateSampler(device,&si,NULL,&sampler));
 VkDescriptorPoolSize sizes[3]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2},{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,8}};VkDescriptorPoolCreateInfo dpi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=2,.poolSizeCount=3,.pPoolSizes=sizes};VkDescriptorPool pool;SUCCESS(d->CreateDescriptorPool(device,&dpi,NULL,&pool));
 VkDescriptorSetLayout layouts[2]={sl,sl};VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=2,.pSetLayouts=layouts};VkDescriptorSet sets[2];SUCCESS(d->AllocateDescriptorSets(device,&ai,sets));
 VkDescriptorUpdateTemplateEntry entry={.dstBinding=2,.descriptorCount=4,.descriptorType=VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,.stride=sizeof(VkDescriptorImageInfo)};VkDescriptorUpdateTemplateCreateInfo ti={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,.descriptorUpdateEntryCount=1,.pDescriptorUpdateEntries=&entry,.templateType=VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET,.descriptorSetLayout=sl};VkDescriptorUpdateTemplate update;SUCCESS(d->CreateDescriptorUpdateTemplate(device,&ti,NULL,&update));
 VkCommandPoolCreateInfo cp={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};VkCommandPool command_pool;SUCCESS(d->CreateCommandPool(device,&cp,NULL,&command_pool));VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=command_pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer command;SUCCESS(d->AllocateCommandBuffers(device,&ca,&command));
 VkQueue queue;d->GetDeviceQueue(device,0,0,&queue);VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;SUCCESS(d->CreateFence(device,&fi,NULL,&fence));
 VkMappedMemoryRange ranges[5];for(unsigned j=0;j<5;j++)ranges[j]=(VkMappedMemoryRange){.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=j<3?data[j].memory:images[j-3].memory,.size=VK_WHOLE_SIZE};
 for(unsigned round=0;round<2;round++) {
  for(unsigned j=0;j<3;j++){for(size_t i=0;i<data[j].words;i++)data[j].map[i]=poison(i,50+j,round);memcpy(data[j].saved,data[j].map,data[j].words*4);}
  for(unsigned j=0;j<2;j++)for(size_t i=0;i<images[j].words;i++)if(i<images[j].first||i>=images[j].first+images[j].count)images[j].map[i]=poison(i,60+j,round);
  for(unsigned j=0;j<2;j++)for(unsigned i=0;i<16;i++){uint32_t value=0;for(unsigned c=0;c<4;c++)value|=((i*17+c*43+j*29+round*31)&255u)<<(8*c);data[0].map[data[0].binding_words+GUARD+j*16+i]=value;data[1].saved[data[1].binding_words+GUARD+j*16+i]=value;if(!j)for(unsigned c=0;c<4;c++)data[2].saved[data[2].binding_words+GUARD+4*i+c]=(value>>(8*c))&255u;}
  memcpy(data[0].saved,data[0].map,data[0].words*4);SUCCESS(d->FlushMappedMemoryRanges(device,5,ranges));SUCCESS(d->ResetCommandPool(device,command_pool,0));
  VkDescriptorImageInfo sampled={sampler,images[0].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkDescriptorImageInfo input[4];for(unsigned j=0;j<4;j++)input[j]=(VkDescriptorImageInfo){VK_NULL_HANDLE,images[(j+round)&1].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkDescriptorBufferInfo output={data[2].buffer,GUARD*4,64*4};
  VkWriteDescriptorSet writes[3]={{.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=sets[0],.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&sampled},
   {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=sets[0],.dstBinding=1,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&output},
   {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=sets[0],.dstBinding=2,.descriptorCount=4,.descriptorType=VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,.pImageInfo=input}};
  d->UpdateDescriptorSets(device,3,writes,0,NULL);VkCopyDescriptorSet copy={.sType=VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET,.srcSet=sets[0],.srcBinding=2,.dstSet=sets[1],.dstBinding=2,.descriptorCount=4};d->UpdateDescriptorSets(device,0,NULL,1,&copy);d->UpdateDescriptorSetWithTemplate(device,sets[0],update,input);
  for(unsigned j=0;j<2;j++){struct rd_descriptor_set *set=rd_descriptor_set_from_handle(sets[j]);for(unsigned k=0;k<4;k++)REQUIRE(set->descriptors[2+k].type==VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT&&set->descriptors[2+k].image_view==rd_image_view_from_handle(input[k].imageView));}
  VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};SUCCESS(d->BeginCommandBuffer(command,&begin));
  VkImageMemoryBarrier ib[2];for(unsigned j=0;j<2;j++)ib[j]=(VkImageMemoryBarrier){.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.oldLayout=round?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=images[j].image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
  VkMemoryBarrier mb={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&mb,0,NULL,2,ib);
  for(unsigned j=0;j<2;j++){VkBufferImageCopy region={.bufferOffset=GUARD*4+j*16*4,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={4,4,1}};d->CmdCopyBufferToImage(command,data[0].buffer,images[j].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);ib[j].srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ib[j].dstAccessMask=0;ib[j].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;ib[j].newLayout=j?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;}
  for(unsigned j=0;j<2;j++)ib[j].dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
  d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,NULL,0,NULL,2,ib);
  ib[0].srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;ib[0].oldLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;ib[0].newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;ib[0].dstAccessMask=VK_ACCESS_SHADER_READ_BIT;d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,NULL,0,NULL,1,ib);
  d->CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);d->CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,sets,0,NULL);d->CmdDispatch(command,16,1,1);
  for(unsigned j=0;j<2;j++){ib[j].srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT;ib[j].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;ib[j].oldLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;ib[j].newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;}
  d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,2,ib);
  for(unsigned j=0;j<2;j++){VkBufferImageCopy region={.bufferOffset=GUARD*4+j*16*4,.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={4,4,1}};d->CmdCopyImageToBuffer(command,images[j].image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,data[1].buffer,1,&region);ib[j].srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;ib[j].dstAccessMask=0;ib[j].oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;ib[j].newLayout=VK_IMAGE_LAYOUT_GENERAL;}
  mb.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;mb.dstAccessMask=VK_ACCESS_HOST_READ_BIT;d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&mb,0,NULL,2,ib);
  SUCCESS(d->EndCommandBuffer(command));SUCCESS(d->ResetFences(device,1,&fence));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};SUCCESS(d->QueueSubmit(queue,1,&submit,fence));SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));SUCCESS(d->InvalidateMappedMemoryRanges(device,5,ranges));
  for(unsigned j=0;j<3;j++)for(size_t i=0;i<data[j].words;i++)if(data[j].map[i]!=data[j].saved[i]){fprintf(stderr,"FAIL ATTACHMENT round=%u buffer=%u word=%zu actual=%08x expected=%08x\n",round,j,i,data[j].map[i],data[j].saved[i]);return 1;}
  for(unsigned j=0;j<2;j++)for(size_t i=0;i<images[j].words;i++)if(i<images[j].first||i>=images[j].first+images[j].count)REQUIRE(images[j].map[i]==poison(i,60+j,round));
  printf("ATTACHMENT_ROUND_PASS round=%u sampled_words=64 copied_texels=32 full3buffers=1 image_outer_guards=2 input_descriptor_write_copy_template=4\n",round);fflush(stdout);
 }
 d->DestroyFence(device,fence,NULL);d->DestroyCommandPool(device,command_pool,NULL);d->DestroyDescriptorUpdateTemplate(device,update,NULL);d->DestroyDescriptorPool(device,pool,NULL);d->DestroySampler(device,sampler,NULL);
 for(unsigned j=0;j<3;j++){free(data[j].saved);d->UnmapMemory(device,data[j].memory);d->DestroyBuffer(device,data[j].buffer,NULL);d->FreeMemory(device,data[j].memory,NULL);}
 for(unsigned j=0;j<2;j++){d->DestroyImageView(device,images[j].view,NULL);d->UnmapMemory(device,images[j].memory);d->DestroyImage(device,images[j].image,NULL);d->FreeMemory(device,images[j].memory,NULL);}
 d->DestroyPipeline(device,pipeline,NULL);d->DestroyPipelineLayout(device,layout,NULL);d->DestroyDescriptorSetLayout(device,sl,NULL);d->DestroyDevice(device,NULL);DestroyInstance(instance,NULL);dlclose(lib);
 puts("PASS ATTACHMENT_SHARED: RGBA8 usage/view/layout/transfer/sampled oracle and host input-descriptor updates; no rendering");return 0;
}
