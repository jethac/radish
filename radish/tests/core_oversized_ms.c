int resource_baseline_main(int argc,char **argv);
#define main resource_baseline_main
#include "core_resources.c"
#undef main

enum { LANE_W=2048, LANE_H=2048, LANE_LAYERS=2, LANE_SAMPLES=4, LANE_RECORDS=8, LANE_FIELDS=15 };
static const unsigned selected_x[LANE_RECORDS]={0,2047,1023,1024,1,2046,17,2030};
static const unsigned selected_y[LANE_RECORDS]={2047,0,1023,1024,2046,1,31,2015};
static uint32_t layer_value(unsigned round,unsigned layer)
{ return 0x62000000u+round*0x01000000u+layer*0x00100000u; }
static int lane_verify(struct storage *output,uint32_t *map,size_t words,
   const struct rd_image *image,unsigned round)
{
   for (unsigned i=0;i<LANE_RECORDS;i++) {
      unsigned layer=i&1;
      uint32_t *r=output->saved+output->binding_words+GUARD+i*LANE_FIELDS;
      for (unsigned sample=0;sample<LANE_SAMPLES;sample++) r[sample]=r[4+sample]=layer_value(round,layer);
      r[8]=r[10]=LANE_W; r[9]=r[11]=LANE_H; r[12]=LANE_LAYERS; r[13]=r[14]=LANE_SAMPLES;
   }
   for (size_t i=0;i<output->words;i++) if (output->map[i]!=output->saved[i]) {
      fprintf(stderr,"FAIL OVERSIZED_MS round=%u output_word=%zu actual=%08x expected=%08x\n",round,i,output->map[i],output->saved[i]); return 1;
   }
   for (unsigned i=0;i<GUARD;i++) {
      REQUIRE(map[i]==poison(i,91,round));
      size_t tail=words-GUARD+i; REQUIRE(map[tail]==poison(tail,91,round));
   }
   for (unsigned i=0;i<LANE_RECORDS;i++) for (unsigned sample=0;sample<LANE_SAMPLES;sample++) {
      unsigned layer=i&1;
      VkDeviceSize offset=image->memory_offset+image->levels[0].offset+layer*image->levels[0].array_pitch+
         selected_y[i]*image->levels[0].row_pitch+(VkDeviceSize)selected_x[i]*LANE_SAMPLES*4+sample*4;
      REQUIRE(offset/4<words);
      if (map[offset/4]!=layer_value(round,layer)) {
         fprintf(stderr,"FAIL OVERSIZED_MS round=%u sparse_image_word=%llu actual=%08x expected=%08x\n",
            round,(unsigned long long)(offset/4),map[offset/4],layer_value(round,layer)); return 1;
      }
   }
   return 0;
}
int main(int argc,char **argv)
{
   if (argc!=3 && !(argc==4 && !strcmp(argv[3],"--compile-only"))) {
      fprintf(stderr,"Usage: %s ICD-library oversized_ms.spv [--compile-only]\n",argv[0]); return 1;
   }
   REQUIRE(!getenv("RADISH_SW_TEXEL_FETCH"));
   REQUIRE(!getenv("RADISH_BATCH_CHUNKS") || !strcmp(getenv("RADISH_BATCH_CHUNKS"),"1"));
   REQUIRE(!getenv("RADISH_TILED_GROUPS_X") || !strcmp(getenv("RADISH_TILED_GROUPS_X"),"1"));
   puts("OVERSIZED_MS_SCOPE private_force_absent=1 public_clear_producer=1 same_image_BO=1 one_128MiB_image=1 sparse_host_checks=1");
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
   VkDescriptorSetLayoutBinding bindings[4];
   for (unsigned i=0;i<4;i++) bindings[i]=(VkDescriptorSetLayoutBinding){.binding=i,.descriptorCount=1,
      .descriptorType=i<3?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
   VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=4,.pBindings=bindings};
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
   printf("OVERSIZED_MS_COMPILED native=%u textures=%u before_allocations=1\n",p->variant->instrlen,p->resource_usage.texture_count); fflush(stdout);
   if (argc==4) {
      d->DestroyPipeline(device,pipeline,NULL); d->DestroyPipelineLayout(device,layout,NULL); d->DestroyDescriptorSetLayout(device,sl,NULL);
      d->DestroyDevice(device,NULL); DestroyInstance(instance,NULL); dlclose(lib);
      puts("PASS OVERSIZED_MS compile-only: original pipeline before image/buffer allocation; lazy raw child remains later"); return 0;
   }
   struct storage output={0};
   REQUIRE(storage_create(d,device,&memory,(GUARD+LANE_RECORDS*LANE_FIELDS+16)*4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&output)==0);
   VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R32_UINT,
      .extent={LANE_W,LANE_H,1},.mipLevels=1,.arrayLayers=LANE_LAYERS,.samples=VK_SAMPLE_COUNT_4_BIT,
      .tiling=VK_IMAGE_TILING_OPTIMAL,.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
   VkImage image; SUCCESS(d->CreateImage(device,&ci,NULL,&image));
   VkMemoryRequirements req; d->GetImageMemoryRequirements(device,image,&req);
   REQUIRE(req.size==(VkDeviceSize)128*1024*1024 && req.alignment==64);
   VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.alignment+req.size+64,.memoryTypeIndex=memory_type(&req,&memory)};
   REQUIRE(ai.memoryTypeIndex!=UINT32_MAX && !(ai.allocationSize%4));
   VkDeviceMemory image_memory; SUCCESS(d->AllocateMemory(device,&ai,NULL,&image_memory));
   SUCCESS(d->BindImageMemory(device,image,image_memory,req.alignment));
   uint32_t *map; SUCCESS(d->MapMemory(device,image_memory,0,VK_WHOLE_SIZE,0,(void **)&map));
   size_t words=ai.allocationSize/4;
   struct rd_image *inside=rd_image_from_handle(image);
   REQUIRE(inside->texel_size==4 && inside->vk.samples==4 && inside->levels[0].row_pitch>=LANE_W*LANE_SAMPLES*4 &&
      !(inside->levels[0].offset%4) && !(inside->levels[0].row_pitch%4) && !(inside->levels[0].array_pitch%4));
   REQUIRE(inside->levels[0].row_pitch==32768 && inside->levels[0].array_pitch==67108864 && inside->levels[0].array_pitch>(0x3fffull<<12));
   VkImageView views[3];
   for (unsigned i=0;i<3;i++) {
      VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image,.format=ci.format,
         .viewType=i==2?VK_IMAGE_VIEW_TYPE_2D_ARRAY:VK_IMAGE_VIEW_TYPE_2D,
         .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,i==2?0:i,i==2?LANE_LAYERS:1}};
      SUCCESS(d->CreateImageView(device,&vi,NULL,&views[i]));
   }
   VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_NEAREST,.minFilter=VK_FILTER_NEAREST,
      .mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
   VkSampler sampler; SUCCESS(d->CreateSampler(device,&si,NULL,&sampler));
   VkDescriptorPoolSize ps[2]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,3},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
   VkDescriptorPoolCreateInfo dp={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=2,.pPoolSizes=ps};
   VkDescriptorPool pool; SUCCESS(d->CreateDescriptorPool(device,&dp,NULL,&pool));
   VkDescriptorSetAllocateInfo da={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&sl};
   VkDescriptorSet set; SUCCESS(d->AllocateDescriptorSets(device,&da,&set));
   VkDescriptorImageInfo ii[3]={{sampler,views[0],VK_IMAGE_LAYOUT_GENERAL},{sampler,views[1],VK_IMAGE_LAYOUT_GENERAL},{sampler,views[2],VK_IMAGE_LAYOUT_GENERAL}};
   VkDescriptorBufferInfo bi={output.buffer,GUARD*4,LANE_RECORDS*LANE_FIELDS*4};
   VkWriteDescriptorSet writes[4];
   for (unsigned i=0;i<4;i++) writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=i,
      .descriptorCount=1,.descriptorType=bindings[i].descriptorType,.pImageInfo=i<3?&ii[i]:NULL,.pBufferInfo=i==3?&bi:NULL};
   VkCommandPoolCreateInfo cp={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
   VkCommandPool command_pool; SUCCESS(d->CreateCommandPool(device,&cp,NULL,&command_pool));
   VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=command_pool,
      .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
   VkCommandBuffer command; SUCCESS(d->AllocateCommandBuffers(device,&ca,&command));
   VkQueue queue; d->GetDeviceQueue(device,0,0,&queue);
   VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence; SUCCESS(d->CreateFence(device,&fi,NULL,&fence));
   VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
   printf("OVERSIZED_MS_LAYOUT image_bytes=%llu allocation_bytes=%llu image_BO_bytes=%u offset=%llu row=%llu array=%llu no_huge_host_copy=1\n",
      (unsigned long long)req.size,(unsigned long long)ai.allocationSize,inside->memory->bo->size,
      (unsigned long long)inside->levels[0].offset,(unsigned long long)inside->levels[0].row_pitch,(unsigned long long)inside->levels[0].array_pitch); fflush(stdout);
   VkMappedMemoryRange ranges[2]={{.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=output.memory,.size=VK_WHOLE_SIZE},
      {.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=image_memory,.size=VK_WHOLE_SIZE}};
   struct rd_pipeline *child=NULL;
   for (unsigned round=0;round<2;round++) {
      for (size_t i=0;i<output.words;i++) output.map[i]=output.saved[i]=poison(i,90,round);
      for (unsigned i=0;i<GUARD;i++) {
         map[i]=poison(i,91,round); size_t tail=words-GUARD+i; map[tail]=poison(tail,91,round);
      }
      SUCCESS(d->FlushMappedMemoryRanges(device,2,ranges));
      SUCCESS(d->ResetCommandPool(device,command_pool,0));
      d->UpdateDescriptorSets(device,4,writes,0,NULL);
      SUCCESS(d->BeginCommandBuffer(command,&begin));
      VkImageMemoryBarrier clear_barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .srcAccessMask=round?VK_ACCESS_SHADER_READ_BIT:0,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
         .oldLayout=round?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
         .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
         .image=image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,LANE_LAYERS}};
      d->CmdPipelineBarrier(command,round?VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
         VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&clear_barrier);
      for (unsigned layer=0;layer<LANE_LAYERS;layer++) {
         VkClearColorValue color={.uint32={layer_value(round,layer),0,0,0}};
         VkImageSubresourceRange range={VK_IMAGE_ASPECT_COLOR_BIT,0,1,layer,1};
         d->CmdClearColorImage(command,image,VK_IMAGE_LAYOUT_GENERAL,&color,1,&range);
      }
      VkMemoryBarrier before={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask=VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,
         .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
      d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&before,0,NULL,0,NULL);
      d->CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
      d->CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);
      d->CmdDispatch(command,LANE_RECORDS,1,1);
      VkMemoryBarrier after={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
      d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&after,0,NULL,0,NULL);
      SUCCESS(d->EndCommandBuffer(command)); SUCCESS(d->ResetFences(device,1,&fence));
      SUCCESS(d->QueueSubmit(queue,1,&submit,fence)); SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
      SUCCESS(d->InvalidateMappedMemoryRanges(device,2,ranges));
      REQUIRE(lane_verify(&output,map,words,inside,round)==0);
      REQUIRE(p->sampling_variants && !p->sampling_variants->next && p->sampling_variants->pipeline->sampling_mask==4);
      struct rd_pipeline *selected=p->sampling_variants->pipeline;
      if (round) REQUIRE(child==selected); else child=selected;
      printf("OVERSIZED_MS_ROUND_PASS round=%u exact_words=%u sparse_image_words=%zu full_output_guards=1 raw_mask=%x native=%u cached=%u same_image_BO=1\n",
         round,LANE_RECORDS*LANE_FIELDS,(size_t)(GUARD*2+LANE_RECORDS*LANE_SAMPLES),selected->sampling_mask,selected->variant->instrlen,round); fflush(stdout);
   }
   d->DestroyFence(device,fence,NULL); d->DestroyCommandPool(device,command_pool,NULL); d->DestroyDescriptorPool(device,pool,NULL);
   d->DestroySampler(device,sampler,NULL); for (unsigned i=0;i<3;i++) d->DestroyImageView(device,views[i],NULL);
   d->UnmapMemory(device,image_memory); d->DestroyImage(device,image,NULL); d->FreeMemory(device,image_memory,NULL);
   free(output.saved); d->UnmapMemory(device,output.memory); d->DestroyBuffer(device,output.buffer,NULL); d->FreeMemory(device,output.memory,NULL);
   d->DestroyPipeline(device,pipeline,NULL); d->DestroyPipelineLayout(device,layout,NULL); d->DestroyDescriptorSetLayout(device,sl,NULL);
   d->DestroyDevice(device,NULL); DestroyInstance(instance,NULL); dlclose(lib);
   puts("PASS OVERSIZED_MS_PUBLIC:128MiB public cleared image,240 exact words/full output+sparse image guards, native nonarray versus automatic raw array, private force absent");
   return 0;
}
