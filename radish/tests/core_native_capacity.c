int resource_baseline_main(int argc,char **argv);
#define main resource_baseline_main
#include "core_resources.c"
#undef main

enum { CAP_SAMPLED=16, CAP_IMAGES=18, CAP_FIELDS=22, CAP_RECORDS=4, CAP_TYPED=2 };
struct cap_image { struct image image; uint32_t *saved; };

static uint32_t cap_value(unsigned descriptor,unsigned pixel,unsigned round)
{ return 0x12000000u+descriptor*0x100u+pixel*17u+round*0x10000u; }

static int cap_image_create(const struct vk_device_dispatch_table *d,VkDevice device,
   const VkPhysicalDeviceMemoryProperties *properties,unsigned index,struct cap_image *out)
{
   struct image *image=&out->image;
   unsigned width=index<CAP_SAMPLED?2:3+2*(index-CAP_SAMPLED);
   unsigned height=index<CAP_SAMPLED?2:5+2*(index-CAP_SAMPLED);
   VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,
      .format=VK_FORMAT_R32_UINT,.extent={width,height,1},.mipLevels=1,.arrayLayers=1,
      .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_LINEAR,
      .usage=index<CAP_SAMPLED?VK_IMAGE_USAGE_SAMPLED_BIT:VK_IMAGE_USAGE_STORAGE_BIT,
      .initialLayout=VK_IMAGE_LAYOUT_PREINITIALIZED};
   SUCCESS(d->CreateImage(device,&ci,NULL,&image->image));
   VkMemoryRequirements r; d->GetImageMemoryRequirements(device,image->image,&r);
   REQUIRE(r.alignment && !(r.alignment%4) && r.size<1024*1024 && !(r.size%4));
   unsigned type=memory_type(&r,properties); REQUIRE(type!=UINT32_MAX);
   VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize=r.alignment+r.size+64,.memoryTypeIndex=type};
   SUCCESS(d->AllocateMemory(device,&ai,NULL,&image->memory));
   SUCCESS(d->BindImageMemory(device,image->image,image->memory,r.alignment));
   SUCCESS(d->MapMemory(device,image->memory,0,VK_WHOLE_SIZE,0,(void **)&image->map));
   image->words=ai.allocationSize/4; image->binding_words=r.alignment/4;
   image->width=width; image->height=height;
   VkImageSubresource sub={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT}; VkSubresourceLayout layout;
   d->GetImageSubresourceLayout(device,image->image,&sub,&layout);
   REQUIRE(!layout.offset && !(layout.rowPitch%4) && layout.rowPitch>=width*4);
   REQUIRE(layout.rowPitch*(height-1)+width*4<=r.size);
   image->row_words=layout.rowPitch/4;
   out->saved=malloc(ai.allocationSize); REQUIRE(out->saved);
   VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image->image,
      .viewType=VK_IMAGE_VIEW_TYPE_2D,.format=VK_FORMAT_R32_UINT,
      .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
   SUCCESS(d->CreateImageView(device,&vi,NULL,&image->view));
   return 0;
}

static int cap_verify(struct storage *output,struct cap_image images[CAP_IMAGES],
   struct storage typed[CAP_TYPED],unsigned round)
{
   for (unsigned pixel=0;pixel<CAP_RECORDS;pixel++) {
      uint32_t *record=output->saved+output->binding_words+GUARD+pixel*CAP_FIELDS;
      for (unsigned i=0;i<CAP_SAMPLED;i++) record[i]=cap_value(i,pixel,round);
      for (unsigned i=0;i<2;i++) { record[16+2*i]=3+2*i; record[17+2*i]=5+2*i; }
      record[20]=35; record[21]=67;
   }
   for (size_t i=0;i<output->words;i++) if (output->map[i]!=output->saved[i]) {
      fprintf(stderr,"FAIL CAPACITY round=%u output_word=%zu actual=%08x expected=%08x\n",
         round,i,output->map[i],output->saved[i]); return 1;
   }
   for (unsigned j=0;j<CAP_IMAGES;j++) for (size_t i=0;i<images[j].image.words;i++)
      if (images[j].image.map[i]!=images[j].saved[i]) {
         fprintf(stderr,"FAIL CAPACITY round=%u image=%u word=%zu actual=%08x expected=%08x\n",
            round,j,i,images[j].image.map[i],images[j].saved[i]); return 1;
      }
   for (unsigned j=0;j<CAP_TYPED;j++) for (size_t i=0;i<typed[j].words;i++)
      if (typed[j].map[i]!=typed[j].saved[i]) {
         fprintf(stderr,"FAIL CAPACITY round=%u typed_buffer=%u word=%zu actual=%08x expected=%08x\n",
            round,j,i,typed[j].map[i],typed[j].saved[i]); return 1;
      }
   return 0;
}

int main(int argc,char **argv)
{
   if (argc!=3 && !(argc==4 && !strcmp(argv[3],"--compile-only"))) {
      fprintf(stderr,"Usage: %s ICD-library capacity.spv [--compile-only]\n",argv[0]); return 1;
   }
   REQUIRE(!getenv("RADISH_BATCH_CHUNKS") || !strcmp(getenv("RADISH_BATCH_CHUNKS"),"1"));
   REQUIRE(!getenv("RADISH_TILED_GROUPS_X") || !strcmp(getenv("RADISH_TILED_GROUPS_X"),"1"));
   void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); if (!lib) { fprintf(stderr,"%s\n",dlerror()); return 1; }
   PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr"); REQUIRE(gipa);
   VkInstance instance=VK_NULL_HANDLE; INSTANCE_FN(CreateInstance);
   VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_0};
   VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
   SUCCESS(CreateInstance(&ici,NULL,&instance));
   INSTANCE_FN(EnumeratePhysicalDevices); INSTANCE_FN(GetPhysicalDeviceMemoryProperties);
   INSTANCE_FN(GetPhysicalDeviceProperties); INSTANCE_FN(GetPhysicalDeviceFormatProperties);
   INSTANCE_FN(CreateDevice); INSTANCE_FN(DestroyInstance);
   VkPhysicalDevice physical; uint32_t count=1;
   SUCCESS(EnumeratePhysicalDevices(instance,&count,&physical)); REQUIRE(count==1);
   VkPhysicalDeviceProperties properties; GetPhysicalDeviceProperties(physical,&properties);
   REQUIRE(properties.limits.maxPerStageDescriptorSampledImages>=16 &&
      properties.limits.maxPerStageDescriptorSamplers>=16 &&
      properties.limits.maxPerStageDescriptorStorageImages>=4 &&
      properties.limits.maxPerStageDescriptorStorageBuffers>=1 &&
      properties.limits.maxPerStageResources>=21);
   VkPhysicalDeviceMemoryProperties memory; GetPhysicalDeviceMemoryProperties(physical,&memory);
   VkFormatProperties fp; GetPhysicalDeviceFormatProperties(physical,VK_FORMAT_R32_UINT,&fp);
   REQUIRE((fp.linearTilingFeatures&(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))==
      (VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT));
   REQUIRE(fp.bufferFeatures&VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT);
   float priority=1; VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueCount=1,.pQueuePriorities=&priority};
   VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount=1,.pQueueCreateInfos=&qci};
   VkDevice device; SUCCESS(CreateDevice(physical,&dci,NULL,&device));
   const struct vk_device_dispatch_table *d=&rd_device_from_handle(device)->vk.dispatch_table;
   VkDescriptorSetLayoutBinding bindings[21];
   for (unsigned i=0;i<21;i++) bindings[i]=(VkDescriptorSetLayoutBinding){.binding=i,
      .descriptorType=i<16?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:i<18?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                      i<20?VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
   VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount=21,.pBindings=bindings};
   VkDescriptorSetLayout set_layout; SUCCESS(d->CreateDescriptorSetLayout(device,&li,NULL,&set_layout));
   VkPipelineLayoutCreateInfo pi={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount=1,.pSetLayouts=&set_layout};
   VkPipelineLayout layout; SUCCESS(d->CreatePipelineLayout(device,&pi,NULL,&layout));
   size_t bytes; uint32_t *code=read_spirv(argv[2],&bytes); REQUIRE(code);
   VkShaderModuleCreateInfo mi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=bytes,.pCode=code};
   VkShaderModule module; SUCCESS(d->CreateShaderModule(device,&mi,NULL,&module)); free(code);
   VkComputePipelineCreateInfo pci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,
              .module=module,.pName="main"},.layout=layout};
   VkPipeline pipeline; SUCCESS(d->CreateComputePipelines(device,VK_NULL_HANDLE,1,&pci,NULL,&pipeline));
   d->DestroyShaderModule(device,module,NULL);
   struct rd_pipeline *p=rd_pipeline_from_handle(pipeline);
   const struct ir3_shader_variant *v=p->variant;
   REQUIRE(!p->sampling_nir && !p->sampling_mask);
   printf("CAPACITY_COMPILED active=%u buffers=%u texture_mask=%x textures=%u sampler_mask=%x samplers=%u image_mask=%x images=%u alias_base=%u aliases=%u native=%u full=%d half=%d private=%u shared=%u local=%u,%u,%u\n",
      p->descriptor_count,p->buffer_count,p->resource_usage.textures,p->resource_usage.texture_count,
      p->resource_usage.samplers,p->resource_usage.sampler_count,p->resource_usage.images,p->resource_usage.image_count,
      v->image_mapping.tex_base,v->image_mapping.num_tex,v->instrlen,v->info.max_reg+1,v->info.max_half_reg+1,
      v->pvtmem_size,v->shared_size,v->local_size[0],v->local_size[1],v->local_size[2]);
   for (unsigned i=0;i<v->image_mapping.num_tex;i++)
      printf("CAPACITY_ALIAS native_texture=%u image_or_ssbo=%u\n",v->image_mapping.tex_base+i,v->image_mapping.tex_to_image[i]);
   fflush(stdout);
   if (argc==4) {
      d->DestroyPipeline(device,pipeline,NULL); d->DestroyPipelineLayout(device,layout,NULL);
      d->DestroyDescriptorSetLayout(device,set_layout,NULL); d->DestroyDevice(device,NULL);
      DestroyInstance(instance,NULL); dlclose(lib);
      puts("PASS CAPACITY compile-only:16 sampled values+2image dimensions+2texel-buffer lengths remain live; no data/commands allocated"); return 0;
   }
   struct storage output={0};
   REQUIRE(storage_create(d,device,&memory,GUARD*4+CAP_RECORDS*CAP_FIELDS*4+64,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&output)==0);
   struct cap_image images[CAP_IMAGES]={0};
   for (unsigned i=0;i<CAP_IMAGES;i++) REQUIRE(cap_image_create(d,device,&memory,i,&images[i])==0);
   struct storage typed[CAP_TYPED]={0}; VkBufferView typed_views[CAP_TYPED];
   const unsigned typed_lengths[CAP_TYPED]={35,67};
   for (unsigned i=0;i<CAP_TYPED;i++) {
      REQUIRE(storage_create(d,device,&memory,GUARD*4+typed_lengths[i]*4+64,
         VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,&typed[i])==0);
      VkBufferViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
         .buffer=typed[i].buffer,.format=VK_FORMAT_R32_UINT,.offset=GUARD*4,.range=typed_lengths[i]*4};
      SUCCESS(d->CreateBufferView(device,&vi,NULL,&typed_views[i]));
   }
   VkSamplerCreateInfo si={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_NEAREST,
      .minFilter=VK_FILTER_NEAREST,.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
   VkSampler sampler; SUCCESS(d->CreateSampler(device,&si,NULL,&sampler));
   VkDescriptorPoolSize sizes[4]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,16},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,2},{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,2},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
   VkDescriptorPoolCreateInfo dpi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets=1,.poolSizeCount=4,.pPoolSizes=sizes};
   VkDescriptorPool pool; SUCCESS(d->CreateDescriptorPool(device,&dpi,NULL,&pool));
   VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&set_layout};
   VkDescriptorSet set; SUCCESS(d->AllocateDescriptorSets(device,&ai,&set));
   VkDescriptorImageInfo image_info[CAP_IMAGES]; VkWriteDescriptorSet writes[21];
   for (unsigned i=0;i<CAP_IMAGES;i++) image_info[i]=(VkDescriptorImageInfo){
      .sampler=i<16?sampler:VK_NULL_HANDLE,.imageView=images[i].image.view,.imageLayout=VK_IMAGE_LAYOUT_GENERAL};
   VkDescriptorBufferInfo bi={output.buffer,GUARD*4,CAP_RECORDS*CAP_FIELDS*4};
   for (unsigned i=0;i<21;i++) writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet=set,.dstBinding=i,.descriptorCount=1,.descriptorType=bindings[i].descriptorType,
      .pImageInfo=i<18?&image_info[i]:NULL,.pTexelBufferView=i>=18&&i<20?&typed_views[i-18]:NULL,
      .pBufferInfo=i==20?&bi:NULL};
   VkCommandPoolCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; VkCommandPool command_pool;
   SUCCESS(d->CreateCommandPool(device,&ci,NULL,&command_pool));
   VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool=command_pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
   VkCommandBuffer command; SUCCESS(d->AllocateCommandBuffers(device,&ca,&command));
   VkQueue queue; d->GetDeviceQueue(device,0,0,&queue);
   VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence;
   SUCCESS(d->CreateFence(device,&fi,NULL,&fence));
   VkMappedMemoryRange ranges[21]; ranges[0]=(VkMappedMemoryRange){.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
      .memory=output.memory,.size=VK_WHOLE_SIZE};
   for (unsigned i=0;i<CAP_IMAGES;i++) ranges[i+1]=(VkMappedMemoryRange){.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
      .memory=images[i].image.memory,.size=VK_WHOLE_SIZE};
   for (unsigned i=0;i<CAP_TYPED;i++) ranges[19+i]=(VkMappedMemoryRange){.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
      .memory=typed[i].memory,.size=VK_WHOLE_SIZE};
   for (unsigned round=0;round<2;round++) {
      for (size_t i=0;i<output.words;i++) output.map[i]=poison(i,30,round);
      memcpy(output.saved,output.map,output.words*4);
      for (unsigned j=0;j<CAP_IMAGES;j++) {
         struct image *image=&images[j].image;
         for (size_t i=0;i<image->words;i++) image->map[i]=poison(i,j,round);
         if (j<CAP_SAMPLED) for (unsigned y=0;y<2;y++) for (unsigned x=0;x<2;x++)
            image->map[image->binding_words+y*image->row_words+x]=cap_value(j,2*y+x,round);
         memcpy(images[j].saved,image->map,image->words*4);
      }
      for (unsigned j=0;j<CAP_TYPED;j++) {
         for (size_t i=0;i<typed[j].words;i++) typed[j].map[i]=poison(i,40+j,round);
         memcpy(typed[j].saved,typed[j].map,typed[j].words*4);
      }
      SUCCESS(d->FlushMappedMemoryRanges(device,21,ranges));
      SUCCESS(d->ResetCommandPool(device,command_pool,0));
      d->UpdateDescriptorSets(device,21,writes,0,NULL);
      VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      SUCCESS(d->BeginCommandBuffer(command,&begin));
      VkImageMemoryBarrier barriers[CAP_IMAGES];
      for (unsigned i=0;i<CAP_IMAGES;i++) barriers[i]=(VkImageMemoryBarrier){.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT,
         .oldLayout=round?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_PREINITIALIZED,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
         .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
         .image=images[i].image.image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
      d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
         0,0,NULL,0,NULL,CAP_IMAGES,barriers);
      d->CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
      d->CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);
      d->CmdDispatch(command,CAP_RECORDS,1,1);
      VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
         .dstAccessMask=VK_ACCESS_HOST_READ_BIT};
      d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
         0,1,&host,0,NULL,0,NULL);
      SUCCESS(d->EndCommandBuffer(command));
      SUCCESS(d->ResetFences(device,1,&fence));
      VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
      printf("CAPACITY_ROUND_BEGIN round=%u sampled=16 image_sizes=2 texel_sizes=2 grid=4,1,1 updates_before_record=1\n",round); fflush(stdout);
      SUCCESS(d->QueueSubmit(queue,1,&submit,fence));
      SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
      SUCCESS(d->InvalidateMappedMemoryRanges(device,21,ranges));
      REQUIRE(cap_verify(&output,images,typed,round)==0);
      printf("CAPACITY_ROUND_PASS round=%u output_words=%u full21allocation_guards=exact\n",round,CAP_RECORDS*CAP_FIELDS); fflush(stdout);
   }
   d->DestroyFence(device,fence,NULL); d->DestroyCommandPool(device,command_pool,NULL);
   d->DestroyDescriptorPool(device,pool,NULL); d->DestroySampler(device,sampler,NULL);
   for (unsigned i=0;i<CAP_IMAGES;i++) {
      free(images[i].saved); d->DestroyImageView(device,images[i].image.view,NULL);
      d->UnmapMemory(device,images[i].image.memory); d->DestroyImage(device,images[i].image.image,NULL);
      d->FreeMemory(device,images[i].image.memory,NULL);
   }
   for (unsigned i=0;i<CAP_TYPED;i++) {
      d->DestroyBufferView(device,typed_views[i],NULL); free(typed[i].saved);
      d->UnmapMemory(device,typed[i].memory); d->DestroyBuffer(device,typed[i].buffer,NULL);
      d->FreeMemory(device,typed[i].memory,NULL);
   }
   free(output.saved); d->UnmapMemory(device,output.memory); d->DestroyBuffer(device,output.buffer,NULL);
   d->FreeMemory(device,output.memory,NULL); d->DestroyPipeline(device,pipeline,NULL);
   d->DestroyPipelineLayout(device,layout,NULL); d->DestroyDescriptorSetLayout(device,set_layout,NULL);
   d->DestroyDevice(device,NULL); DestroyInstance(instance,NULL); dlclose(lib);
   puts("PASS CAPACITY:16 static sampled descriptors+2 image-size+2 texel-buffer-size queries+1SSBO,176 exact output words and all guards");
   return 0;
}
