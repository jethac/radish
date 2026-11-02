int resource_baseline_main(int argc,char **argv);
#define main resource_baseline_main
#include "core_resources.c"
#undef main

enum { CUBE_SIDE=4, CUBE_FACES=6, CUBE_RECORDS=96, CUBE_FIELDS=8 };
static uint32_t cube_value(unsigned face,unsigned x,unsigned y,unsigned round)
{ return 0x00100000u+round*0x10000u+face*0x1000u+y*0x100u+x; }

static int cube_verify(struct storage *output,struct image *image,uint32_t *saved,
   const VkSubresourceLayout layouts[CUBE_FACES],unsigned round)
{
   for (unsigned face=0;face<CUBE_FACES;face++) for (unsigned y=0;y<CUBE_SIDE;y++)
      for (unsigned x=0;x<CUBE_SIDE;x++) {
         unsigned index=(face*CUBE_SIDE+y)*CUBE_SIDE+x;
         uint32_t value=cube_value(face,x,y,round);
         uint32_t *record=output->saved+output->binding_words+GUARD+index*CUBE_FIELDS;
         record[0]=value; record[1]=value+64; record[2]=value+69; record[3]=value+128;
         record[4]=CUBE_SIDE; record[5]=CUBE_SIDE; record[6]=face; record[7]=index;
         size_t pixel=image->binding_words+layouts[face].offset/4+y*(layouts[face].rowPitch/4)+x;
         REQUIRE(pixel<image->words);
         saved[pixel]=value+128;
      }
   for (size_t i=0;i<output->words;i++) if (output->map[i]!=output->saved[i]) {
      fprintf(stderr,"FAIL STORAGE_CUBE round=%u output_word=%zu actual=%08x expected=%08x\n",
         round,i,output->map[i],output->saved[i]); return 1;
   }
   for (size_t i=0;i<image->words;i++) if (image->map[i]!=saved[i]) {
      fprintf(stderr,"FAIL STORAGE_CUBE round=%u image_word=%zu actual=%08x expected=%08x\n",
         round,i,image->map[i],saved[i]); return 1;
   }
   return 0;
}

int main(int argc,char **argv)
{
   if (argc!=3 && !(argc==4 && !strcmp(argv[3],"--compile-only"))) {
      fprintf(stderr,"Usage: %s ICD-library storage_cube.spv [--compile-only]\n",argv[0]); return 1;
   }
   REQUIRE(!getenv("RADISH_SW_TEXEL_FETCH"));
   void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); if (!lib) { fprintf(stderr,"%s\n",dlerror()); return 1; }
   PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr"); REQUIRE(gipa);
   VkInstance instance=VK_NULL_HANDLE; INSTANCE_FN(CreateInstance);
   VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_0};
   VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
   SUCCESS(CreateInstance(&ici,NULL,&instance));
   INSTANCE_FN(EnumeratePhysicalDevices); INSTANCE_FN(GetPhysicalDeviceMemoryProperties);
   INSTANCE_FN(GetPhysicalDeviceFormatProperties); INSTANCE_FN(CreateDevice); INSTANCE_FN(DestroyInstance);
   VkPhysicalDevice physical; uint32_t count=1;
   SUCCESS(EnumeratePhysicalDevices(instance,&count,&physical)); REQUIRE(count==1);
   VkPhysicalDeviceMemoryProperties memory; GetPhysicalDeviceMemoryProperties(physical,&memory);
   VkFormatProperties fp; GetPhysicalDeviceFormatProperties(physical,VK_FORMAT_R32_UINT,&fp);
   REQUIRE((fp.linearTilingFeatures&(VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT))==
      (VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT));
   float priority=1; VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueCount=1,.pQueuePriorities=&priority};
   VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qci};
   VkDevice device; SUCCESS(CreateDevice(physical,&dci,NULL,&device));
   const struct vk_device_dispatch_table *d=&rd_device_from_handle(device)->vk.dispatch_table;
   VkDescriptorSetLayoutBinding bindings[2]={
      {.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT},
      {.binding=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT}};
   VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=2,.pBindings=bindings};
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
   const struct ir3_shader_variant *v=p->variant;
   REQUIRE(!p->sampling_nir && !p->sampling_mask && p->resource_usage.image_count==1);
   printf("STORAGE_CUBE_COMPILED native=%u full=%d half=%d private=%u shared=%u images=%u aliases=%u local=%u,%u,%u\n",
      v->instrlen,v->info.max_reg+1,v->info.max_half_reg+1,v->pvtmem_size,v->shared_size,
      p->resource_usage.image_count,v->image_mapping.num_tex,v->local_size[0],v->local_size[1],v->local_size[2]); fflush(stdout);
   if (argc==4) {
      d->DestroyPipeline(device,pipeline,NULL); d->DestroyPipelineLayout(device,layout,NULL);
      d->DestroyDescriptorSetLayout(device,set_layout,NULL); d->DestroyDevice(device,NULL);
      DestroyInstance(instance,NULL); dlclose(lib);
      puts("PASS STORAGE_CUBE compile-only: no data or commands allocated"); return 0;
   }
   struct storage output={0};
   REQUIRE(storage_create(d,device,&memory,GUARD*4+CUBE_RECORDS*CUBE_FIELDS*4+64,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&output)==0);
   struct image image={0};
   VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.flags=VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
      .imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R32_UINT,.extent={8,8,1},.mipLevels=2,.arrayLayers=12,
      .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_LINEAR,.usage=VK_IMAGE_USAGE_STORAGE_BIT,
      .initialLayout=VK_IMAGE_LAYOUT_PREINITIALIZED};
   SUCCESS(d->CreateImage(device,&ci,NULL,&image.image));
   VkMemoryRequirements req; d->GetImageMemoryRequirements(device,image.image,&req);
   REQUIRE(req.alignment && !(req.alignment%4) && req.size<1024*1024 && !(req.size%4));
   VkMemoryAllocateInfo mai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize=req.alignment+req.size+64,.memoryTypeIndex=memory_type(&req,&memory)};
   REQUIRE(mai.memoryTypeIndex!=UINT32_MAX);
   SUCCESS(d->AllocateMemory(device,&mai,NULL,&image.memory));
   SUCCESS(d->BindImageMemory(device,image.image,image.memory,req.alignment));
   SUCCESS(d->MapMemory(device,image.memory,0,VK_WHOLE_SIZE,0,(void **)&image.map));
   image.words=mai.allocationSize/4; image.binding_words=req.alignment/4;
   uint32_t *saved=malloc(mai.allocationSize); REQUIRE(saved);
   VkSubresourceLayout layouts[CUBE_FACES];
   for (unsigned face=0;face<CUBE_FACES;face++) {
      VkImageSubresource sub={.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT,.mipLevel=1,.arrayLayer=6+face};
      d->GetImageSubresourceLayout(device,image.image,&sub,&layouts[face]);
      REQUIRE(!(layouts[face].offset%4) && !(layouts[face].rowPitch%4) &&
         layouts[face].rowPitch>=CUBE_SIDE*4 && layouts[face].offset+(CUBE_SIDE-1)*layouts[face].rowPitch+CUBE_SIDE*4<=req.size);
   }
   VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=image.image,
      .viewType=VK_IMAGE_VIEW_TYPE_CUBE,.format=VK_FORMAT_R32_UINT,
      .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,1,1,6,CUBE_FACES}};
   SUCCESS(d->CreateImageView(device,&vi,NULL,&image.view));
   VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
   VkDescriptorPoolCreateInfo dpi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=2,.pPoolSizes=sizes};
   VkDescriptorPool pool; SUCCESS(d->CreateDescriptorPool(device,&dpi,NULL,&pool));
   VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&set_layout};
   VkDescriptorSet set; SUCCESS(d->AllocateDescriptorSets(device,&ai,&set));
   VkDescriptorImageInfo ii={.imageView=image.view,.imageLayout=VK_IMAGE_LAYOUT_GENERAL};
   VkDescriptorBufferInfo bi={output.buffer,GUARD*4,CUBE_RECORDS*CUBE_FIELDS*4};
   VkWriteDescriptorSet writes[2]={
      {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=0,.descriptorCount=1,
       .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,.pImageInfo=&ii},
      {.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=1,.descriptorCount=1,
       .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&bi}};
   VkCommandPoolCreateInfo cpi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; VkCommandPool command_pool;
   SUCCESS(d->CreateCommandPool(device,&cpi,NULL,&command_pool));
   VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=command_pool,
      .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1}; VkCommandBuffer command;
   SUCCESS(d->AllocateCommandBuffers(device,&ca,&command));
   VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence; SUCCESS(d->CreateFence(device,&fi,NULL,&fence));
   VkQueue queue; d->GetDeviceQueue(device,0,0,&queue);
   VkMappedMemoryRange ranges[2]={{.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=output.memory,.size=VK_WHOLE_SIZE},
      {.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=image.memory,.size=VK_WHOLE_SIZE}};
   for (unsigned round=0;round<2;round++) {
      for (size_t i=0;i<output.words;i++) output.map[i]=poison(i,91,round);
      memcpy(output.saved,output.map,output.words*4);
      for (size_t i=0;i<image.words;i++) image.map[i]=poison(i,92,round);
      for (unsigned face=0;face<CUBE_FACES;face++) for (unsigned y=0;y<CUBE_SIDE;y++)
         for (unsigned x=0;x<CUBE_SIDE;x++) {
            size_t pixel=image.binding_words+layouts[face].offset/4+y*(layouts[face].rowPitch/4)+x;
            REQUIRE(pixel<image.words); image.map[pixel]=cube_value(face,x,y,round);
         }
      memcpy(saved,image.map,image.words*4);
      SUCCESS(d->FlushMappedMemoryRanges(device,2,ranges));
      SUCCESS(d->ResetCommandPool(device,command_pool,0));
      d->UpdateDescriptorSets(device,2,writes,0,NULL);
      VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; SUCCESS(d->BeginCommandBuffer(command,&begin));
      VkImageMemoryBarrier ib={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,
         .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT,
         .oldLayout=round?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_PREINITIALIZED,.newLayout=VK_IMAGE_LAYOUT_GENERAL,
         .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=image.image,
         .subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,2,0,12}};
      d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,NULL,0,NULL,1,&ib);
      d->CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
      d->CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);
      d->CmdDispatch(command,CUBE_SIDE,CUBE_SIDE,CUBE_FACES);
      VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
      d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
      SUCCESS(d->EndCommandBuffer(command)); SUCCESS(d->ResetFences(device,1,&fence));
      VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
      printf("STORAGE_CUBE_ROUND_BEGIN round=%u faces=6 view_mip=1 view_base_layer=6 updates_before_record=1\n",round); fflush(stdout);
      SUCCESS(d->QueueSubmit(queue,1,&submit,fence)); SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
      SUCCESS(d->InvalidateMappedMemoryRanges(device,2,ranges));
      REQUIRE(cube_verify(&output,&image,saved,layouts,round)==0);
      printf("STORAGE_CUBE_ROUND_PASS round=%u exact_output_words=%u full_image_output_guards=1\n",round,CUBE_RECORDS*CUBE_FIELDS); fflush(stdout);
   }
   d->DestroyFence(device,fence,NULL); d->DestroyCommandPool(device,command_pool,NULL); d->DestroyDescriptorPool(device,pool,NULL);
   free(saved); d->DestroyImageView(device,image.view,NULL); d->UnmapMemory(device,image.memory);
   d->DestroyImage(device,image.image,NULL); d->FreeMemory(device,image.memory,NULL);
   free(output.saved); d->UnmapMemory(device,output.memory); d->DestroyBuffer(device,output.buffer,NULL); d->FreeMemory(device,output.memory,NULL);
   d->DestroyPipeline(device,pipeline,NULL); d->DestroyPipelineLayout(device,layout,NULL); d->DestroyDescriptorSetLayout(device,set_layout,NULL);
   d->DestroyDevice(device,NULL); DestroyInstance(instance,NULL); dlclose(lib);
   puts("PASS STORAGE_CUBE: six faces, nonzero mip/base layer, read/store/add/CAS/query,1536exactwords/fullallocations,validrerecord");
   return 0;
}
