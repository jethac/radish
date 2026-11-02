int resource_baseline_main(int argc,char **argv);
#define main resource_baseline_main
#include "core_resources.c"
#undef main

enum { MS_WIDTH=5, MS_HEIGHT=3, MS_LAYERS=2, MS_SAMPLES=4,
       MS_COUNT=MS_WIDTH*MS_HEIGHT*MS_LAYERS*MS_SAMPLES, MS_FIELDS=18, MS_IMAGES=7 };
struct ms_image {
   VkImage image;
   VkImageView view;
   VkDeviceMemory memory;
   uint32_t *map;
   size_t words, first_word, end_word;
   VkImageAspectFlags aspect;
};

static int ms_image_create(const struct vk_device_dispatch_table *d,VkDevice device,
   const VkPhysicalDeviceMemoryProperties *properties,VkFormat format,
   VkImageAspectFlags aspect,struct ms_image *image)
{
   VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
      .format=format,.extent={MS_WIDTH,MS_HEIGHT,1},.mipLevels=1,.arrayLayers=MS_LAYERS,
      .samples=VK_SAMPLE_COUNT_4_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
      .usage=VK_IMAGE_USAGE_SAMPLED_BIT,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
   if (aspect==VK_IMAGE_ASPECT_COLOR_BIT) ci.usage|=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
   SUCCESS(d->CreateImage(device,&ci,NULL,&image->image));
   VkMemoryRequirements r; d->GetImageMemoryRequirements(device,image->image,&r);
   REQUIRE(r.size && r.size<1024*1024 && r.alignment && !(r.size%4) && !(r.alignment%4));
   unsigned type=memory_type(&r,properties); REQUIRE(type!=UINT32_MAX);
   VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize=r.alignment+r.size+64,.memoryTypeIndex=type};
   SUCCESS(d->AllocateMemory(device,&ai,NULL,&image->memory));
   SUCCESS(d->BindImageMemory(device,image->image,image->memory,r.alignment));
   SUCCESS(d->MapMemory(device,image->memory,0,VK_WHOLE_SIZE,0,(void **)&image->map));
   image->words=ai.allocationSize/4; image->first_word=r.alignment/4;
   image->end_word=(r.alignment+r.size)/4; image->aspect=aspect;
   VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image->image,
      .viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY,.format=format,
      .subresourceRange={aspect,0,1,0,MS_LAYERS}};
   SUCCESS(d->CreateImageView(device,&vi,NULL,&image->view));
   return 0;
}

static uint32_t ms_uint(unsigned image,unsigned layer,unsigned round)
{
   return (image?0x65430000u:0x12340000u)+layer*17u+round*257u;
}

static int ms_verify(struct storage *output,struct ms_image images[MS_IMAGES],unsigned round)
{
   for (unsigned i=0;i<MS_COUNT;i++) {
      unsigned pixel=i/MS_SAMPLES,x=pixel%MS_WIDTH,y=(pixel/MS_WIDTH)%MS_HEIGHT;
      unsigned layer=pixel/(MS_WIDTH*MS_HEIGHT);
      bool copied=x>=2 && y>=1;
      unsigned source_float=layer^round;
      unsigned target_float=copied?source_float:source_float^1;
      uint32_t expected[MS_FIELDS]={ms_uint(0,layer,round),ms_uint(copied?0:1,layer,round),
         float_word((float)source_float),float_word((float)target_float),4,4,5,3,2,5,3,2,4,2,4,2,4,2};
      memcpy(output->saved+output->binding_words+GUARD+i*MS_FIELDS,expected,sizeof(expected));
   }
   for (size_t i=0;i<output->words;i++) if (output->map[i]!=output->saved[i]) {
      fprintf(stderr,"FAIL MS round=%u word=%zu actual=%08x expected=%08x\n",
         round,i,output->map[i],output->saved[i]); return 1;
   }
   for (unsigned j=0;j<MS_IMAGES;j++) for (size_t i=0;i<images[j].words;i++) {
      if (i>=images[j].first_word && i<images[j].end_word) continue;
      uint32_t expected=poison(i,20+j,round);
      if (images[j].map[i]!=expected) {
         fprintf(stderr,"FAIL MS image guard round=%u image=%u word=%zu actual=%08x expected=%08x\n",
            round,j,i,images[j].map[i],expected); return 1;
      }
   }
   return 0;
}

int main(int argc,char **argv)
{
   if (argc!=3 && !(argc==4 && !strcmp(argv[3],"--compile-only"))) {
      fprintf(stderr,"Usage: %s ICD-library multisample.spv [--compile-only]\n",argv[0]); return 1;
   }
   void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); if (!lib) { fprintf(stderr,"%s\n",dlerror()); return 1; }
   PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr"); REQUIRE(gipa);
   VkInstance instance=VK_NULL_HANDLE; INSTANCE_FN(CreateInstance);
   VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_0};
   VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
   SUCCESS(CreateInstance(&ici,NULL,&instance));
   INSTANCE_FN(EnumeratePhysicalDevices); INSTANCE_FN(GetPhysicalDeviceMemoryProperties);
   INSTANCE_FN(GetPhysicalDeviceProperties); INSTANCE_FN(GetPhysicalDeviceImageFormatProperties);
   INSTANCE_FN(CreateDevice); INSTANCE_FN(DestroyInstance);
   VkPhysicalDevice physical; uint32_t count=1;
   SUCCESS(EnumeratePhysicalDevices(instance,&count,&physical)); REQUIRE(count==1);
   VkPhysicalDeviceMemoryProperties memory_properties; GetPhysicalDeviceMemoryProperties(physical,&memory_properties);
   VkPhysicalDeviceProperties properties; GetPhysicalDeviceProperties(physical,&properties);
   REQUIRE((properties.limits.sampledImageColorSampleCounts&5)==5 &&
      (properties.limits.sampledImageIntegerSampleCounts&5)==5 &&
      (properties.limits.sampledImageDepthSampleCounts&5)==5 &&
      (properties.limits.sampledImageStencilSampleCounts&5)==5);
   VkFormat formats[MS_IMAGES]={VK_FORMAT_R32_UINT,VK_FORMAT_R32_UINT,VK_FORMAT_R16_UNORM,VK_FORMAT_R16_UNORM,
      VK_FORMAT_D16_UNORM,VK_FORMAT_D32_SFLOAT,VK_FORMAT_S8_UINT};
   for (unsigned i=0;i<MS_IMAGES;i++) {
      VkImageFormatProperties fp;
      SUCCESS(GetPhysicalDeviceImageFormatProperties(physical,formats[i],VK_IMAGE_TYPE_2D,
         VK_IMAGE_TILING_OPTIMAL,VK_IMAGE_USAGE_SAMPLED_BIT,0,&fp));
      REQUIRE((fp.sampleCounts&5)==5 && fp.maxArrayLayers>=MS_LAYERS);
   }
   float priority=1; VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueCount=1,.pQueuePriorities=&priority};
   VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qci};
   VkDevice device; SUCCESS(CreateDevice(physical,&dci,NULL,&device));
   struct rd_device *inside=rd_device_from_handle(device);
   const struct vk_device_dispatch_table *d=&inside->vk.dispatch_table;
   VkDescriptorSetLayoutBinding bindings[8];
   for (unsigned i=0;i<8;i++) bindings[i]=(VkDescriptorSetLayoutBinding){.binding=i,
      .descriptorType=i?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
   VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=8,.pBindings=bindings};
   VkDescriptorSetLayout set_layout; SUCCESS(d->CreateDescriptorSetLayout(device,&li,NULL,&set_layout));
   VkPipelineLayoutCreateInfo pi={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&set_layout};
   VkPipelineLayout layout; SUCCESS(d->CreatePipelineLayout(device,&pi,NULL,&layout));
   size_t bytes; uint32_t *code=read_spirv(argv[2],&bytes); REQUIRE(code);
   VkShaderModuleCreateInfo mi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=bytes,.pCode=code};
   VkShaderModule module; SUCCESS(d->CreateShaderModule(device,&mi,NULL,&module)); free(code);
   VkComputePipelineCreateInfo pipeline_info={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,
              .module=module,.pName="main"},.layout=layout};
   VkPipeline pipeline; SUCCESS(d->CreateComputePipelines(device,VK_NULL_HANDLE,1,&pipeline_info,NULL,&pipeline));
   d->DestroyShaderModule(device,module,NULL);
   struct rd_pipeline *compiled=rd_pipeline_from_handle(pipeline);
   printf("MS_COMPILED active=%u textures=%u native=%u fullregs=%d\n",compiled->descriptor_count,
      compiled->resource_usage.texture_count,compiled->variant->instrlen,compiled->variant->info.max_reg+1); fflush(stdout);
   if (argc==4) {
      d->DestroyPipeline(device,pipeline,NULL); d->DestroyPipelineLayout(device,layout,NULL);
      d->DestroyDescriptorSetLayout(device,set_layout,NULL); d->DestroyDevice(device,NULL);
      DestroyInstance(instance,NULL); dlclose(lib);
      puts("PASS MS compile-only: native fetch/query pipeline before images/buffers/commands"); return 0;
   }
   struct storage output={0};
   REQUIRE(storage_create(d,device,&memory_properties,(GUARD+MS_COUNT*MS_FIELDS+16)*4,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&output)==0);
   struct ms_image images[MS_IMAGES]={0};
   for (unsigned i=0;i<MS_IMAGES;i++) REQUIRE(ms_image_create(d,device,&memory_properties,formats[i],
      i<4?VK_IMAGE_ASPECT_COLOR_BIT:i<6?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_STENCIL_BIT,&images[i])==0);
   VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_NEAREST,
      .minFilter=VK_FILTER_NEAREST,.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
   VkSampler sampler; SUCCESS(d->CreateSampler(device,&si,NULL,&sampler));
   VkDescriptorPoolSize pool_sizes[2]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,7}};
   VkDescriptorPoolCreateInfo pool_info={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=2,.pPoolSizes=pool_sizes};
   VkDescriptorPool pool; SUCCESS(d->CreateDescriptorPool(device,&pool_info,NULL,&pool));
   VkDescriptorSetAllocateInfo allocation={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,
      .descriptorSetCount=1,.pSetLayouts=&set_layout};
   VkDescriptorSet set; SUCCESS(d->AllocateDescriptorSets(device,&allocation,&set));
   VkDescriptorBufferInfo bi={output.buffer,GUARD*4,MS_COUNT*MS_FIELDS*4};
   VkDescriptorImageInfo ii[MS_IMAGES]; VkWriteDescriptorSet writes[8];
   for (unsigned i=0;i<MS_IMAGES;i++) ii[i]=(VkDescriptorImageInfo){sampler,images[i].view,VK_IMAGE_LAYOUT_GENERAL};
   for (unsigned i=0;i<8;i++) writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet=set,.dstBinding=i,.descriptorCount=1,.descriptorType=bindings[i].descriptorType,
      .pBufferInfo=i?NULL:&bi,.pImageInfo=i?&ii[i-1]:NULL};
   d->UpdateDescriptorSets(device,8,writes,0,NULL);
   VkCommandPoolCreateInfo cp={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
   VkCommandPool command_pool; SUCCESS(d->CreateCommandPool(device,&cp,NULL,&command_pool));
   VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=command_pool,
      .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
   VkCommandBuffer command; SUCCESS(d->AllocateCommandBuffers(device,&ca,&command));
   VkQueue queue; d->GetDeviceQueue(device,0,0,&queue);
   VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence; SUCCESS(d->CreateFence(device,&fi,NULL,&fence));
   for (unsigned round=0;round<2;round++) {
      if (round) { SUCCESS(d->ResetFences(device,1,&fence)); SUCCESS(d->ResetCommandPool(device,command_pool,0)); }
      for (size_t i=0;i<output.words;i++) output.map[i]=output.saved[i]=poison(i,0,round);
      for (unsigned j=0;j<MS_IMAGES;j++) for (size_t i=0;i<images[j].words;i++) images[j].map[i]=poison(i,20+j,round);
      VkMappedMemoryRange mapped[8];
      for (unsigned j=0;j<8;j++) mapped[j]=(VkMappedMemoryRange){.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
         .memory=j?images[j-1].memory:output.memory,.size=VK_WHOLE_SIZE};
      SUCCESS(d->FlushMappedMemoryRanges(device,8,mapped));
      VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      SUCCESS(d->BeginCommandBuffer(command,&begin));
      VkImageMemoryBarrier barriers_ms[MS_IMAGES];
      for (unsigned j=0;j<MS_IMAGES;j++) barriers_ms[j]=(VkImageMemoryBarrier){.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .dstAccessMask=j<4?VK_ACCESS_TRANSFER_WRITE_BIT:VK_ACCESS_SHADER_READ_BIT,
         .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
         .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
         .image=images[j].image,.subresourceRange={images[j].aspect,0,1,0,MS_LAYERS}};
      d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
         VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,NULL,0,NULL,MS_IMAGES,barriers_ms);
      for (unsigned j=0;j<4;j++) for (unsigned layer=0;layer<MS_LAYERS;layer++) {
         VkClearColorValue color={0};
         if (j<2) color.uint32[0]=ms_uint(j,layer,round);
         else color.float32[0]=(float)(layer^round^(j==3));
         VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,layer,1};
         d->CmdClearColorImage(command,images[j].image,VK_IMAGE_LAYOUT_GENERAL,&color,1,&range);
      }
      VkMemoryBarrier transfer_barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT};
      d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&transfer_barrier,0,NULL,0,NULL);
      VkImageCopy copy={.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,MS_LAYERS},.srcOffset={1,0,0},
         .dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,MS_LAYERS},.dstOffset={2,1,0},.extent={3,2,1}};
      for (unsigned j=0;j<4;j+=2) d->CmdCopyImage(command,images[j].image,VK_IMAGE_LAYOUT_GENERAL,
         images[j+1].image,VK_IMAGE_LAYOUT_GENERAL,1,&copy);
      VkMemoryBarrier shader_barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT};
      d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&shader_barrier,0,NULL,0,NULL);
      d->CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
      d->CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);
      d->CmdDispatch(command,MS_COUNT/4,1,1);
      VkMemoryBarrier host_barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
      d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host_barrier,0,NULL,0,NULL);
      SUCCESS(d->EndCommandBuffer(command));
      VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
      SUCCESS(d->QueueSubmit(queue,1,&submit,fence));
      SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,60000000000ull));
      SUCCESS(d->InvalidateMappedMemoryRanges(device,8,mapped));
      REQUIRE(ms_verify(&output,images,round)==0);
      printf("MS_ROUND_PASS round=%u records=%u fields=%u samples=4 layers=2 color_clear_copy_fetch=1 depth_stencil_query_only=1 guards=exact\n",round,MS_COUNT,MS_FIELDS); fflush(stdout);
   }
   d->DestroyFence(device,fence,NULL); d->DestroyCommandPool(device,command_pool,NULL);
   d->DestroyDescriptorPool(device,pool,NULL); d->DestroySampler(device,sampler,NULL);
   for (unsigned j=0;j<MS_IMAGES;j++) {
      d->DestroyImageView(device,images[j].view,NULL); d->UnmapMemory(device,images[j].memory);
      d->DestroyImage(device,images[j].image,NULL); d->FreeMemory(device,images[j].memory,NULL);
   }
   d->UnmapMemory(device,output.memory); d->DestroyBuffer(device,output.buffer,NULL);
   d->FreeMemory(device,output.memory,NULL); free(output.saved);
   d->DestroyPipeline(device,pipeline,NULL); d->DestroyPipelineLayout(device,layout,NULL);
   d->DestroyDescriptorSetLayout(device,set_layout,NULL); d->DestroyDevice(device,NULL);
   DestroyInstance(instance,NULL); dlclose(lib);
   puts("PASS MS: two rounds full color sample fetch/size/count/copy/clear and depth-stencil query-only with exact output/allocation guards; equal per-sample clears do not discriminate sample-address selection");
   return 0;
}
