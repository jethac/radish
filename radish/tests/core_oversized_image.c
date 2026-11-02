int resource_baseline_main(int argc,char **argv);
#define main resource_baseline_main
#include "core_resources.c"
#undef main

enum { BIG_SIDE=2048, BIG_LAYERS=2, BIG_RECORDS=8, BIG_FIELDS=11, TRACKED=1024 };
struct checked_word { size_t index; uint32_t expected; };
static const unsigned big_x[BIG_RECORDS]={0,1,1023,1024,2046,2047,17,2030};
static const unsigned big_y[BIG_RECORDS]={0,2047,1023,1024,1,2046,31,2015};
static float big_value(unsigned layer,unsigned x,unsigned y,unsigned c,unsigned round)
{ return layer*16.0f+c*4.0f+(x&7)*0.25f+(y&7)*0.5f+round*32.0f; }
static int remember(struct checked_word tracked[TRACKED],unsigned *count,
   uint32_t *map,size_t words,size_t index,uint32_t expected)
{
   REQUIRE(index<words);
   for (unsigned i=0;i<*count;i++) if (tracked[i].index==index) {
      tracked[i].expected=expected; map[index]=expected; return 0;
   }
   REQUIRE(*count<TRACKED);
   tracked[(*count)++]=(struct checked_word){index,expected}; map[index]=expected;
   return 0;
}
static int big_prepare(struct image *image,VkSubresourceLayout layouts[BIG_LAYERS],
   struct checked_word tracked[TRACKED],unsigned *count,unsigned round)
{
   *count=0;
   for (unsigned i=0;i<GUARD;i++) {
      REQUIRE(remember(tracked,count,image->map,image->words,i,poison(i,80,round))==0);
      size_t tail=image->words-GUARD+i;
      REQUIRE(remember(tracked,count,image->map,image->words,tail,poison(tail,81,round))==0);
   }
   for (unsigned layer=0;layer<BIG_LAYERS;layer++) {
      size_t start=image->binding_words+layouts[layer].offset/4;
      size_t end=start+layouts[layer].rowPitch*BIG_SIDE/4;
      for (unsigned i=0;i<GUARD;i++) {
         REQUIRE(remember(tracked,count,image->map,image->words,start+i,poison(start+i,82+layer,round))==0);
         REQUIRE(remember(tracked,count,image->map,image->words,end-GUARD+i,poison(end-GUARD+i,84+layer,round))==0);
      }
   }
   for (unsigned i=0;i<BIG_RECORDS;i++) for (unsigned dy=0;dy<2;dy++) for (unsigned dx=0;dx<2;dx++) {
      unsigned x=MIN2(big_x[i]+dx,BIG_SIDE-1),y=MIN2(big_y[i]+dy,BIG_SIDE-1),layer=i&1;
      size_t pixel=image->binding_words+layouts[layer].offset/4+y*(layouts[layer].rowPitch/4)+x*4;
      for (unsigned c=0;c<4;c++) REQUIRE(remember(tracked,count,image->map,image->words,pixel+c,
         float_word(big_value(layer,x,y,c,round)))==0);
   }
   return 0;
}
static int big_verify(struct storage *output,struct image *image,
   const struct checked_word tracked[TRACKED],unsigned count,unsigned round)
{
   for (unsigned i=0;i<BIG_RECORDS;i++) {
      uint32_t *record=output->saved+output->binding_words+GUARD+i*BIG_FIELDS;
      unsigned x=big_x[i],y=big_y[i],xx=MIN2(x+1,BIG_SIDE-1),yy=MIN2(y+1,BIG_SIDE-1),layer=i&1;
      for (unsigned c=0;c<4;c++) {
         record[c]=float_word(big_value(layer,x,y,c,round));
         record[4+c]=float_word((big_value(layer,x,y,c,round)+big_value(layer,xx,y,c,round)+
            big_value(layer,x,yy,c,round)+big_value(layer,xx,yy,c,round))*0.25f);
      }
      record[8]=record[9]=BIG_SIDE; record[10]=BIG_LAYERS;
   }
   for (size_t i=0;i<output->words;i++) if (output->map[i]!=output->saved[i]) {
      fprintf(stderr,"FAIL OVERSIZED round=%u output_word=%zu actual=%08x expected=%08x\n",
         round,i,output->map[i],output->saved[i]); return 1;
   }
   for (unsigned i=0;i<count;i++) if (image->map[tracked[i].index]!=tracked[i].expected) {
      fprintf(stderr,"FAIL OVERSIZED round=%u tracked_image_word=%zu actual=%08x expected=%08x\n",
         round,tracked[i].index,image->map[tracked[i].index],tracked[i].expected); return 1;
   }
   return 0;
}
int main(int argc,char **argv)
{
   if (argc!=3 && !(argc==4 && !strcmp(argv[3],"--compile-only"))) {
      fprintf(stderr,"Usage: %s ICD-library oversized.spv [--compile-only]\n",argv[0]); return 1;
   }
   REQUIRE(!getenv("RADISH_SW_TEXEL_FETCH"));
   REQUIRE(!getenv("RADISH_BATCH_CHUNKS") || !strcmp(getenv("RADISH_BATCH_CHUNKS"),"1"));
   REQUIRE(!getenv("RADISH_TILED_GROUPS_X") || !strcmp(getenv("RADISH_TILED_GROUPS_X"),"1"));
   puts("OVERSIZED_PUBLIC_PATH private_force_option_absent=1");
   void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); if (!lib) { fprintf(stderr,"%s\n",dlerror()); return 1; }
   PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr"); REQUIRE(gipa);
   VkInstance instance=VK_NULL_HANDLE; INSTANCE_FN(CreateInstance);
   VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_0};
   VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
   SUCCESS(CreateInstance(&ici,NULL,&instance));
   INSTANCE_FN(EnumeratePhysicalDevices); INSTANCE_FN(GetPhysicalDeviceMemoryProperties);
   INSTANCE_FN(GetPhysicalDeviceFormatProperties); INSTANCE_FN(CreateDevice); INSTANCE_FN(DestroyInstance);
   VkPhysicalDevice physical; uint32_t n=1; SUCCESS(EnumeratePhysicalDevices(instance,&n,&physical)); REQUIRE(n==1);
   VkPhysicalDeviceMemoryProperties memory; GetPhysicalDeviceMemoryProperties(physical,&memory);
   VkFormatProperties fp; GetPhysicalDeviceFormatProperties(physical,VK_FORMAT_R32G32B32A32_SFLOAT,&fp);
   REQUIRE((fp.linearTilingFeatures&(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))==
      (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT));
   float priority=1; VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueCount=1,.pQueuePriorities=&priority};
   VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qci};
   VkDevice device; SUCCESS(CreateDevice(physical,&dci,NULL,&device));
   const struct vk_device_dispatch_table *d=&rd_device_from_handle(device)->vk.dispatch_table;
   VkDescriptorSetLayoutBinding bindings[3];
   for (unsigned i=0;i<3;i++) bindings[i]=(VkDescriptorSetLayoutBinding){.binding=i,.descriptorCount=1,
      .descriptorType=i<2?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
   VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=3,.pBindings=bindings};
   VkDescriptorSetLayout set_layout; SUCCESS(d->CreateDescriptorSetLayout(device,&li,NULL,&set_layout));
   VkPipelineLayoutCreateInfo pi={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&set_layout};
   VkPipelineLayout layout; SUCCESS(d->CreatePipelineLayout(device,&pi,NULL,&layout));
   size_t bytes; uint32_t *code=read_spirv(argv[2],&bytes); REQUIRE(code);
   VkShaderModuleCreateInfo mi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=bytes,.pCode=code};
   VkShaderModule module; SUCCESS(d->CreateShaderModule(device,&mi,NULL,&module)); free(code);
   VkComputePipelineCreateInfo pci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"},.layout=layout};
   VkPipeline pipeline; SUCCESS(d->CreateComputePipelines(device,VK_NULL_HANDLE,1,&pci,NULL,&pipeline));
   d->DestroyShaderModule(device,module,NULL);
   struct rd_pipeline *p=rd_pipeline_from_handle(pipeline);
   REQUIRE(p->sampling_nir);
   printf("OVERSIZED_COMPILED native=%u textures=%u samplers=%u before_allocations=1\n",
      p->variant->instrlen,p->resource_usage.texture_count,p->resource_usage.sampler_count); fflush(stdout);
   if (argc==4) {
      d->DestroyPipeline(device,pipeline,NULL); d->DestroyPipelineLayout(device,layout,NULL);
      d->DestroyDescriptorSetLayout(device,set_layout,NULL); d->DestroyDevice(device,NULL);
      DestroyInstance(instance,NULL); dlclose(lib);
      puts("PASS OVERSIZED compile-only: original pipeline before image/buffer allocation; lazy child remains later"); return 0;
   }
   struct storage output={0};
   REQUIRE(storage_create(d,device,&memory,GUARD*4+BIG_RECORDS*BIG_FIELDS*4+64,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&output)==0);
   struct image image={0};
   VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
      .format=VK_FORMAT_R32G32B32A32_SFLOAT,.extent={BIG_SIDE,BIG_SIDE,1},.mipLevels=1,.arrayLayers=BIG_LAYERS,
      .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_LINEAR,.usage=VK_IMAGE_USAGE_SAMPLED_BIT,
      .initialLayout=VK_IMAGE_LAYOUT_PREINITIALIZED};
   SUCCESS(d->CreateImage(device,&ci,NULL,&image.image));
   VkMemoryRequirements req; d->GetImageMemoryRequirements(device,image.image,&req);
   REQUIRE(req.size==(VkDeviceSize)128*1024*1024 && req.alignment==64);
   VkMemoryAllocateInfo mai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.alignment+req.size+64,
      .memoryTypeIndex=memory_type(&req,&memory)};
   REQUIRE(mai.memoryTypeIndex!=UINT32_MAX);
   SUCCESS(d->AllocateMemory(device,&mai,NULL,&image.memory));
   SUCCESS(d->BindImageMemory(device,image.image,image.memory,req.alignment));
   SUCCESS(d->MapMemory(device,image.memory,0,VK_WHOLE_SIZE,0,(void **)&image.map));
   image.words=mai.allocationSize/4; image.binding_words=req.alignment/4;
   VkSubresourceLayout layouts[BIG_LAYERS];
   for (unsigned layer=0;layer<BIG_LAYERS;layer++) {
      VkImageSubresource sub={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.arrayLayer=layer};
      d->GetImageSubresourceLayout(device,image.image,&sub,&layouts[layer]);
      REQUIRE(layouts[layer].arrayPitch==(VkDeviceSize)64*1024*1024 && layouts[layer].rowPitch==32768 &&
         layouts[layer].offset==layer*layouts[layer].arrayPitch && layouts[layer].arrayPitch>(0x3fffull<<12));
   }
   struct rd_image *internal=rd_image_from_handle(image.image);
   printf("OVERSIZED_ALLOC image_bytes=%llu vk_allocation=%llu bo_bytes=%u layer_pitch=%llu native_limit=%llu one_image=1 sparse_init=1 no_huge_host_copy=1\n",
      (unsigned long long)req.size,(unsigned long long)mai.allocationSize,internal->memory->bo->size,
      (unsigned long long)layouts[0].arrayPitch,(unsigned long long)(0x3fffull<<12)); fflush(stdout);
   VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image.image,
      .viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY,.format=ci.format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,BIG_LAYERS}};
   SUCCESS(d->CreateImageView(device,&vi,NULL,&image.view));
   VkSampler samplers[2];
   for (unsigned i=0;i<2;i++) {
      VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
         .magFilter=i?VK_FILTER_LINEAR:VK_FILTER_NEAREST,.minFilter=i?VK_FILTER_LINEAR:VK_FILTER_NEAREST,
         .mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
         .addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
      SUCCESS(d->CreateSampler(device,&si,NULL,&samplers[i]));
   }
   VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
   VkDescriptorPoolCreateInfo dpi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=2,.pPoolSizes=sizes};
   VkDescriptorPool pool; SUCCESS(d->CreateDescriptorPool(device,&dpi,NULL,&pool));
   VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&set_layout};
   VkDescriptorSet set; SUCCESS(d->AllocateDescriptorSets(device,&ai,&set));
   VkDescriptorImageInfo ii[2]; VkWriteDescriptorSet writes[3];
   VkDescriptorBufferInfo bi={output.buffer,GUARD*4,BIG_RECORDS*BIG_FIELDS*4};
   for (unsigned i=0;i<3;i++) {
      if (i<2) ii[i]=(VkDescriptorImageInfo){.sampler=samplers[i],.imageView=image.view,.imageLayout=VK_IMAGE_LAYOUT_GENERAL};
      writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=i,
         .descriptorCount=1,.descriptorType=bindings[i].descriptorType,.pImageInfo=i<2?&ii[i]:NULL,.pBufferInfo=i==2?&bi:NULL};
   }
   VkCommandPoolCreateInfo cpi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; VkCommandPool command_pool;
   SUCCESS(d->CreateCommandPool(device,&cpi,NULL,&command_pool));
   VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=command_pool,
      .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1}; VkCommandBuffer command;
   SUCCESS(d->AllocateCommandBuffers(device,&ca,&command));
   VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence; SUCCESS(d->CreateFence(device,&fi,NULL,&fence));
   VkQueue queue; d->GetDeviceQueue(device,0,0,&queue);
   VkMappedMemoryRange ranges[2]={{.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=output.memory,.size=VK_WHOLE_SIZE},
      {.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=image.memory,.size=VK_WHOLE_SIZE}};
   struct checked_word tracked[TRACKED]; unsigned tracked_count=0;
   struct rd_pipeline *child=NULL;
   for (unsigned round=0;round<2;round++) {
      for (size_t i=0;i<output.words;i++) output.map[i]=poison(i,79,round);
      memcpy(output.saved,output.map,output.words*4);
      REQUIRE(big_prepare(&image,layouts,tracked,&tracked_count,round)==0);
      SUCCESS(d->FlushMappedMemoryRanges(device,2,ranges));
      SUCCESS(d->ResetCommandPool(device,command_pool,0));
      d->UpdateDescriptorSets(device,3,writes,0,NULL);
      VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; SUCCESS(d->BeginCommandBuffer(command,&begin));
      VkImageMemoryBarrier ib={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,
         .dstAccessMask=VK_ACCESS_SHADER_READ_BIT,.oldLayout=round?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_PREINITIALIZED,
         .newLayout=VK_IMAGE_LAYOUT_GENERAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
         .image=image.image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,BIG_LAYERS}};
      d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,NULL,0,NULL,1,&ib);
      d->CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
      d->CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);
      d->CmdDispatch(command,BIG_RECORDS,1,1);
      VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
      d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
      SUCCESS(d->EndCommandBuffer(command)); SUCCESS(d->ResetFences(device,1,&fence));
      VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
      printf("OVERSIZED_ROUND_BEGIN round=%u tracked_image_words=%u same_image_BO=1 updates_before_record=1\n",round,tracked_count); fflush(stdout);
      SUCCESS(d->QueueSubmit(queue,1,&submit,fence)); SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
      SUCCESS(d->InvalidateMappedMemoryRanges(device,2,ranges));
      REQUIRE(big_verify(&output,&image,tracked,tracked_count,round)==0);
      REQUIRE(p->sampling_variants && p->sampling_variants->pipeline->sampling_mask==3);
      struct rd_pipeline *selected=p->sampling_variants->pipeline;
      if (round) REQUIRE(child==selected); else child=selected;
      printf("OVERSIZED_ROUND_PASS round=%u exact_output_words=%u full_output_guards=1 sparse_image_words=%u cached=%u public_auto_mask=%x native=%u textures=%u\n",
         round,BIG_RECORDS*BIG_FIELDS,tracked_count,round,selected->sampling_mask,selected->variant->instrlen,selected->resource_usage.texture_count); fflush(stdout);
   }
   d->DestroyFence(device,fence,NULL); d->DestroyCommandPool(device,command_pool,NULL); d->DestroyDescriptorPool(device,pool,NULL);
   for (unsigned i=0;i<2;i++) d->DestroySampler(device,samplers[i],NULL);
   d->DestroyImageView(device,image.view,NULL); d->UnmapMemory(device,image.memory); d->DestroyImage(device,image.image,NULL); d->FreeMemory(device,image.memory,NULL);
   free(output.saved); d->UnmapMemory(device,output.memory); d->DestroyBuffer(device,output.buffer,NULL); d->FreeMemory(device,output.memory,NULL);
   d->DestroyPipeline(device,pipeline,NULL); d->DestroyPipelineLayout(device,layout,NULL); d->DestroyDescriptorSetLayout(device,set_layout,NULL);
   d->DestroyDevice(device,NULL); DestroyInstance(instance,NULL); dlclose(lib);
   puts("PASS OVERSIZED_PUBLIC: real64MiB layer stride, automatic TXF+linearRGBA+size,176exactwords/fulloutput+sparseimageguards,sameBOcontentupdates,cachedchild");
   return 0;
}
