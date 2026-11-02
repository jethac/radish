int resource_baseline_main(int argc,char **argv);
#define main resource_baseline_main
#include "core_resources.c"
#undef main

enum { LANE_W=7, LANE_H=3, LANE_LAYERS=2, LANE_SAMPLES=4, LANE_RECORDS=21, LANE_FIELDS=15 };
static uint32_t lane_tag(unsigned round,unsigned layer,unsigned x,unsigned y,unsigned sample)
{ return 0x62000000u+round*0x01000000u+layer*0x00100000u+y*0x1000u+x*0x10u+sample; }
static int lane_verify(struct storage *output,uint32_t *map,const uint32_t *saved,size_t words,unsigned round)
{
   for (unsigned i=0;i<LANE_RECORDS;i++) {
      uint32_t *r=output->saved+output->binding_words+GUARD+i*LANE_FIELDS;
      for (unsigned s=0;s<LANE_SAMPLES;s++) r[s]=r[4+s]=lane_tag(round,1,i%LANE_W,i/LANE_W,s);
      r[8]=r[10]=LANE_W; r[9]=r[11]=LANE_H; r[12]=LANE_LAYERS; r[13]=r[14]=LANE_SAMPLES;
   }
   for (size_t i=0;i<output->words;i++) if (output->map[i]!=output->saved[i]) {
      fprintf(stderr,"FAIL MS_LANES round=%u output_word=%zu actual=%08x expected=%08x\n",round,i,output->map[i],output->saved[i]); return 1;
   }
   for (size_t i=0;i<words;i++) if (map[i]!=saved[i]) {
      fprintf(stderr,"FAIL MS_LANES round=%u image_word=%zu actual=%08x expected=%08x\n",round,i,map[i],saved[i]); return 1;
   }
   return 0;
}
int main(int argc,char **argv)
{
   if (argc!=3 && !(argc==4 && !strcmp(argv[3],"--compile-only"))) {
      fprintf(stderr,"Usage: %s ICD-library ms_lanes.spv [--compile-only]\n",argv[0]); return 1;
   }
   REQUIRE(getenv("RADISH_SW_TEXEL_FETCH") && !strcmp(getenv("RADISH_SW_TEXEL_FETCH"),"1"));
   REQUIRE(!getenv("RADISH_BATCH_CHUNKS") || !strcmp(getenv("RADISH_BATCH_CHUNKS"),"1"));
   REQUIRE(!getenv("RADISH_TILED_GROUPS_X") || !strcmp(getenv("RADISH_TILED_GROUPS_X"),"1"));
   puts("MS_LANES_SCOPE internal_optimal_memory_layout_evidence=1 public_pixel_producer_claim=0 same_image_BO=1 oversized_MS_guard_unchanged=1");
   void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); if (!lib) { fprintf(stderr,"%s\n",dlerror()); return 1; }
   PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr"); REQUIRE(gipa);
   VkInstance instance=VK_NULL_HANDLE; INSTANCE_FN(CreateInstance);
   VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_0};
   VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
   SUCCESS(CreateInstance(&ici,NULL,&instance));
   INSTANCE_FN(EnumeratePhysicalDevices); INSTANCE_FN(GetPhysicalDeviceMemoryProperties);
   INSTANCE_FN(GetPhysicalDeviceImageFormatProperties); INSTANCE_FN(CreateDevice); INSTANCE_FN(DestroyInstance);
   VkPhysicalDevice physical; uint32_t n=1; SUCCESS(EnumeratePhysicalDevices(instance,&n,&physical)); REQUIRE(n==1);
   VkPhysicalDeviceMemoryProperties memory; GetPhysicalDeviceMemoryProperties(physical,&memory);
   VkImageFormatProperties fp;
   SUCCESS(GetPhysicalDeviceImageFormatProperties(physical,VK_FORMAT_R32_UINT,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,0,&fp));
   REQUIRE((fp.sampleCounts&VK_SAMPLE_COUNT_4_BIT) && fp.maxArrayLayers>=LANE_LAYERS);
   float priority=1; VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueCount=1,.pQueuePriorities=&priority};
   VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qci};
   VkDevice device; SUCCESS(CreateDevice(physical,&dci,NULL,&device));
   const struct vk_device_dispatch_table *d=&rd_device_from_handle(device)->vk.dispatch_table;
   VkDescriptorSetLayoutBinding bindings[3];
   for (unsigned i=0;i<3;i++) bindings[i]=(VkDescriptorSetLayoutBinding){.binding=i,.descriptorCount=1,
      .descriptorType=i<2?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
   VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=3,.pBindings=bindings};
   VkDescriptorSetLayout sl; SUCCESS(d->CreateDescriptorSetLayout(device,&li,NULL,&sl));
   VkPipelineLayoutCreateInfo pi={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&sl};
   VkPipelineLayout layout; SUCCESS(d->CreatePipelineLayout(device,&pi,NULL,&layout));
   size_t bytes; uint32_t *code=read_spirv(argv[2],&bytes); REQUIRE(code);
   VkShaderModuleCreateInfo mi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=bytes,.pCode=code};
   VkShaderModule module; SUCCESS(d->CreateShaderModule(device,&mi,NULL,&module)); free(code);
   VkComputePipelineCreateInfo pci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"},.layout=layout};
   VkPipeline pipeline; SUCCESS(d->CreateComputePipelines(device,VK_NULL_HANDLE,1,&pci,NULL,&pipeline));
   d->DestroyShaderModule(device,module,NULL);
   struct rd_pipeline *p=rd_pipeline_from_handle(pipeline); REQUIRE(p->sampling_nir);
   printf("MS_LANES_COMPILED native=%u textures=%u before_allocations=1\n",p->variant->instrlen,p->resource_usage.texture_count); fflush(stdout);
   if (argc==4) {
      d->DestroyPipeline(device,pipeline,NULL); d->DestroyPipelineLayout(device,layout,NULL); d->DestroyDescriptorSetLayout(device,sl,NULL);
      d->DestroyDevice(device,NULL); DestroyInstance(instance,NULL); dlclose(lib);
      puts("PASS MS_LANES compile-only: original pipeline before image/buffer allocation; lazy raw child remains later"); return 0;
   }
   struct storage output={0};
   REQUIRE(storage_create(d,device,&memory,(GUARD+LANE_RECORDS*LANE_FIELDS+16)*4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&output)==0);
   VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R32_UINT,
      .extent={LANE_W,LANE_H,1},.mipLevels=1,.arrayLayers=LANE_LAYERS,.samples=VK_SAMPLE_COUNT_4_BIT,
      .tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
   VkImage image; SUCCESS(d->CreateImage(device,&ci,NULL,&image));
   VkMemoryRequirements req; d->GetImageMemoryRequirements(device,image,&req);
   REQUIRE(req.size && req.size<1024*1024 && req.alignment && !(req.size%4) && !(req.alignment%4));
   VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.alignment+req.size+64,.memoryTypeIndex=memory_type(&req,&memory)};
   REQUIRE(ai.memoryTypeIndex!=UINT32_MAX && !(ai.allocationSize%4));
   VkDeviceMemory image_memory; SUCCESS(d->AllocateMemory(device,&ai,NULL,&image_memory));
   SUCCESS(d->BindImageMemory(device,image,image_memory,req.alignment));
   uint32_t *map; SUCCESS(d->MapMemory(device,image_memory,0,VK_WHOLE_SIZE,0,(void **)&map));
   size_t words=ai.allocationSize/4; uint32_t *saved=malloc(ai.allocationSize); REQUIRE(saved);
   struct rd_image *inside=rd_image_from_handle(image);
   REQUIRE(inside->texel_size==4 && inside->vk.samples==4 && inside->levels[0].row_pitch>=LANE_W*LANE_SAMPLES*4 &&
      !(inside->levels[0].offset%4) && !(inside->levels[0].row_pitch%4) && !(inside->levels[0].array_pitch%4));
   VkImageView views[2];
   for (unsigned i=0;i<2;i++) {
      VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image,.format=ci.format,
         .viewType=i?VK_IMAGE_VIEW_TYPE_2D_ARRAY:VK_IMAGE_VIEW_TYPE_2D,
         .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,i?0:1,i?LANE_LAYERS:1}};
      SUCCESS(d->CreateImageView(device,&vi,NULL,&views[i]));
   }
   VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,
      .mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
   VkSampler sampler; SUCCESS(d->CreateSampler(device,&si,NULL,&sampler));
   VkDescriptorPoolSize ps[2]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
   VkDescriptorPoolCreateInfo dp={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=2,.pPoolSizes=ps};
   VkDescriptorPool pool; SUCCESS(d->CreateDescriptorPool(device,&dp,NULL,&pool));
   VkDescriptorSetAllocateInfo da={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&sl};
   VkDescriptorSet set; SUCCESS(d->AllocateDescriptorSets(device,&da,&set));
   VkDescriptorImageInfo ii[2]={{sampler,views[0],VK_IMAGE_LAYOUT_GENERAL},{sampler,views[1],VK_IMAGE_LAYOUT_GENERAL}};
   VkDescriptorBufferInfo bi={output.buffer,GUARD*4,LANE_RECORDS*LANE_FIELDS*4};
   VkWriteDescriptorSet writes[3];
   for (unsigned i=0;i<3;i++) writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=i,
      .descriptorCount=1,.descriptorType=bindings[i].descriptorType,.pImageInfo=i<2?&ii[i]:NULL,.pBufferInfo=i==2?&bi:NULL};
   VkCommandPoolCreateInfo cp={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
   VkCommandPool command_pool; SUCCESS(d->CreateCommandPool(device,&cp,NULL,&command_pool));
   VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=command_pool,
      .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
   VkCommandBuffer command; SUCCESS(d->AllocateCommandBuffers(device,&ca,&command));
   VkQueue queue; d->GetDeviceQueue(device,0,0,&queue);
   VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence; SUCCESS(d->CreateFence(device,&fi,NULL,&fence));
   VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   SUCCESS(d->BeginCommandBuffer(command,&begin));
   VkImageMemoryBarrier init={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
      .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_GENERAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,LANE_LAYERS}};
   d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&init);
   VkClearColorValue zero={0}; d->CmdClearColorImage(command,image,VK_IMAGE_LAYOUT_GENERAL,&zero,1,&init.subresourceRange);
   SUCCESS(d->EndCommandBuffer(command));
   VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
   SUCCESS(d->QueueSubmit(queue,1,&submit,fence)); SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
   printf("MS_LANES_LAYOUT image_bytes=%llu allocation_bytes=%llu image_BO_bytes=%u offset=%llu row=%llu array=%llu initial_clear_fence_complete=1\n",
      (unsigned long long)req.size,(unsigned long long)ai.allocationSize,inside->memory->bo->size,
      (unsigned long long)inside->levels[0].offset,(unsigned long long)inside->levels[0].row_pitch,(unsigned long long)inside->levels[0].array_pitch); fflush(stdout);
   VkMappedMemoryRange ranges[2]={{.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=output.memory,.size=VK_WHOLE_SIZE},
      {.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=image_memory,.size=VK_WHOLE_SIZE}};
   struct rd_pipeline *child=NULL;
   for (unsigned round=0;round<2;round++) {
      for (size_t i=0;i<output.words;i++) output.map[i]=output.saved[i]=poison(i,90,round);
      for (size_t i=0;i<words;i++) map[i]=saved[i]=poison(i,91,round);
      for (unsigned layer=0;layer<LANE_LAYERS;layer++) for (unsigned y=0;y<LANE_H;y++)
         for (unsigned x=0;x<LANE_W;x++) for (unsigned s=0;s<LANE_SAMPLES;s++) {
            VkDeviceSize offset=req.alignment+inside->levels[0].offset+layer*inside->levels[0].array_pitch+
               y*inside->levels[0].row_pitch+x*LANE_SAMPLES*4+s*4;
            REQUIRE(offset>=req.alignment && offset+4<=req.alignment+req.size && offset/4<words);
            map[offset/4]=saved[offset/4]=lane_tag(round,layer,x,y,s);
         }
      SUCCESS(d->FlushMappedMemoryRanges(device,2,ranges));
      SUCCESS(d->ResetCommandPool(device,command_pool,0));
      d->UpdateDescriptorSets(device,3,writes,0,NULL);
      SUCCESS(d->BeginCommandBuffer(command,&begin));
      VkMemoryBarrier before={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,
         .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
      d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&before,0,NULL,0,NULL);
      d->CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
      d->CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);
      d->CmdDispatch(command,LANE_RECORDS,1,1);
      VkMemoryBarrier after={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
      d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&after,0,NULL,0,NULL);
      SUCCESS(d->EndCommandBuffer(command)); SUCCESS(d->ResetFences(device,1,&fence));
      SUCCESS(d->QueueSubmit(queue,1,&submit,fence)); SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
      SUCCESS(d->InvalidateMappedMemoryRanges(device,2,ranges));
      REQUIRE(lane_verify(&output,map,saved,words,round)==0);
      REQUIRE(p->sampling_variants && !p->sampling_variants->next && p->sampling_variants->pipeline->sampling_mask==2);
      struct rd_pipeline *selected=p->sampling_variants->pipeline;
      if (round) REQUIRE(child==selected); else child=selected;
      printf("MS_LANES_ROUND_PASS round=%u exact_words=%u full_image_words=%zu full_output_guards=1 raw_mask=%x native=%u cached=%u same_image_BO=1\n",
         round,LANE_RECORDS*LANE_FIELDS,words,selected->sampling_mask,selected->variant->instrlen,round); fflush(stdout);
   }
   d->DestroyFence(device,fence,NULL); d->DestroyCommandPool(device,command_pool,NULL); d->DestroyDescriptorPool(device,pool,NULL);
   d->DestroySampler(device,sampler,NULL); for (unsigned i=0;i<2;i++) d->DestroyImageView(device,views[i],NULL);
   d->UnmapMemory(device,image_memory); d->DestroyImage(device,image,NULL); d->FreeMemory(device,image_memory,NULL); free(saved);
   free(output.saved); d->UnmapMemory(device,output.memory); d->DestroyBuffer(device,output.buffer,NULL); d->FreeMemory(device,output.memory,NULL);
   d->DestroyPipeline(device,pipeline,NULL); d->DestroyPipelineLayout(device,layout,NULL); d->DestroyDescriptorSetLayout(device,sl,NULL);
   d->DestroyDevice(device,NULL); DestroyInstance(instance,NULL); dlclose(lib);
   puts("PASS MS_LANES: same-BO native nonarray versus raw array,630 exact words/full allocation guards; internal distinct-sample layout evidence only, no oversized admission");
   return 0;
}
