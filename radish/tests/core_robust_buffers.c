int resource_baseline_main(int argc,char **argv);
#define main resource_baseline_main
#include "core_resources.c"
#undef main

enum { RBA_COUNT=40001, RBA_RESULTS=40, RBA_STORES=5 };
enum { RBA_OUTPUT, RBA_UNIFORM, RBA_DATA, RBA_SOURCE_TEXEL, RBA_TARGET_TEXEL };
static uint32_t rba_source(unsigned i) { return 0x32000000u+i*19u; }
static uint32_t rba_target(unsigned i) { return 0x41000000u+i*23u; }

static int rba_verify(struct storage stores[RBA_STORES],unsigned test,unsigned range)
{
   uint32_t expected[RBA_RESULTS]={0};
   for (unsigned i=0;i<4;i++) {
      bool valid=range>=(i+1)*4;
      expected[i]=valid?0x12340000u+test*0x100u+i:0;
      expected[4+i]=valid?0x20001000u+test*0x100u+i:0;
   }
   expected[8]=range>=16?0x20001003u+test*0x100u:0;
   expected[9]=range>=4?0x50100001u:0;
   expected[15]=RBA_COUNT;
   expected[16]=rba_source(0); expected[17]=rba_source(32752); expected[18]=rba_source(40000);
   expected[21]=RBA_COUNT; expected[22]=rba_target(32752); expected[23]=rba_target(40000);
   expected[26]=rba_target(32752); expected[27]=0x77700001u;
   expected[30]=range>=4?0x70100001u:0; expected[31]=range>=8?0x50200002u:0;
   expected[32]=range>=12?0x50300003u:0;
   expected[33]=range>=16?0x20001008u+test*0x100u:0;
   expected[34]=rba_target(32752)+7; expected[35]=0x77700003u;
   expected[36]=range/4; expected[37]=0x507033ceu; expected[39]=0x0044abcdu;
   memcpy(stores[RBA_OUTPUT].saved+stores[RBA_OUTPUT].binding_words+GUARD,expected,sizeof(expected));
   uint32_t final_data[4]={0x70100001u,0x50200002u,0x50300003u,0x20001008u+test*0x100u};
   for (unsigned i=0;i<4;i++) if (range>=(i+1)*4)
      stores[RBA_DATA].saved[stores[RBA_DATA].binding_words+8+i]=final_data[i];
   stores[RBA_TARGET_TEXEL].saved[stores[RBA_TARGET_TEXEL].binding_words+GUARD+32752]=rba_target(32752)+7;
   stores[RBA_TARGET_TEXEL].saved[stores[RBA_TARGET_TEXEL].binding_words+GUARD+40000]=0x77700003u;
   for (unsigned s=0;s<RBA_STORES;s++) for (size_t i=0;i<stores[s].words;i++) {
      if (stores[s].map[i]!=stores[s].saved[i]) {
         fprintf(stderr,"FAIL RBA case=%u range=%u buffer=%u word=%zu actual=%08x expected=%08x\n",
            test,range,s,i,stores[s].map[i],stores[s].saved[i]); return 1;
      }
   }
   return 0;
}

int main(int argc,char **argv)
{
   if (argc!=3 && !(argc==4 && !strcmp(argv[3],"--compile-only"))) {
      fprintf(stderr,"Usage: %s ICD-library robust.spv [--compile-only]\n",argv[0]); return 1;
   }
   void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); if (!lib) { fprintf(stderr,"%s\n",dlerror()); return 1; }
   PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr"); REQUIRE(gipa);
   VkInstance instance=VK_NULL_HANDLE; INSTANCE_FN(CreateInstance);
   VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_0};
   VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
   SUCCESS(CreateInstance(&ici,NULL,&instance));
   INSTANCE_FN(EnumeratePhysicalDevices); INSTANCE_FN(GetPhysicalDeviceMemoryProperties);
   INSTANCE_FN(GetPhysicalDeviceProperties); INSTANCE_FN(CreateDevice); INSTANCE_FN(DestroyInstance);
   VkPhysicalDevice physical; uint32_t count=1;
   SUCCESS(EnumeratePhysicalDevices(instance,&count,&physical)); REQUIRE(count==1);
   struct rd_physical_device *inside_physical=rd_physical_device_from_handle(physical);
   REQUIRE(inside_physical->vk.supported_features.robustBufferAccess);
   puts("RBA_PUBLIC_FEATURE supported=1; ordinary CreateDevice feature enablement"); fflush(stdout);
   VkPhysicalDeviceMemoryProperties memory_properties; GetPhysicalDeviceMemoryProperties(physical,&memory_properties);
   VkPhysicalDeviceProperties properties; GetPhysicalDeviceProperties(physical,&properties);
   REQUIRE(properties.apiVersion==VK_API_VERSION_1_0);
   VkPhysicalDeviceFeatures requested={.robustBufferAccess=VK_TRUE};
   float priority=1; VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueCount=1,.pQueuePriorities=&priority};
   VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qci,
      .pEnabledFeatures=&requested};
   VkDevice device; SUCCESS(CreateDevice(physical,&dci,NULL,&device));
   struct rd_device *inside=rd_device_from_handle(device);
   REQUIRE(inside->vk.enabled_features.robustBufferAccess);
   REQUIRE(!inside_physical->vk.supported_features.shaderInt16 && !inside_physical->vk.supported_features.shaderInt64 &&
      !inside_physical->vk.supported_features.shaderFloat64 && !inside_physical->vk.supported_features.shaderFloat16 &&
      !inside_physical->vk.supported_features.storageBuffer8BitAccess && !inside_physical->vk.supported_features.storageBuffer16BitAccess);
   const struct vk_device_dispatch_table *d=&inside->vk.dispatch_table;
   VkDescriptorType types[6]={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
      VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER};
   VkDescriptorSetLayoutBinding bindings[6];
   for (unsigned i=0;i<6;i++) bindings[i]=(VkDescriptorSetLayoutBinding){.binding=i,.descriptorType=types[i],
      .descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
   VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=6,.pBindings=bindings};
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
   printf("RBA_COMPILED enabled=1 active=%u buffers=%u native=%u fullregs=%d\n",
      compiled->descriptor_count,compiled->buffer_count,compiled->variant->instrlen,
      compiled->variant->info.max_reg+1); fflush(stdout);
   if (argc==4) {
      d->DestroyPipeline(device,pipeline,NULL); d->DestroyPipelineLayout(device,layout,NULL);
      d->DestroyDescriptorSetLayout(device,set_layout,NULL); d->DestroyDevice(device,NULL);
      DestroyInstance(instance,NULL); dlclose(lib);
      puts("PASS RBA compile-only: public corefeature requested through CreateDevice, pipeline before buffers/views/dispatch"); return 0;
   }
   struct storage stores[RBA_STORES]={0};
   VkDeviceSize sizes[RBA_STORES]={GUARD*4+RBA_RESULTS*4+64,128,128,GUARD*4+RBA_COUNT*4+64,GUARD*4+RBA_COUNT*4+64};
   VkBufferUsageFlags usages[RBA_STORES]={VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT};
   for (unsigned i=0;i<RBA_STORES;i++) REQUIRE(storage_create(d,device,&memory_properties,sizes[i],usages[i],&stores[i])==0);
   VkBufferView views[2];
   for (unsigned i=0;i<2;i++) {
      VkBufferViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,.buffer=stores[RBA_SOURCE_TEXEL+i].buffer,
         .format=VK_FORMAT_R32_UINT,.offset=GUARD*4,.range=RBA_COUNT*4};
      SUCCESS(d->CreateBufferView(device,&vi,NULL,&views[i]));
      REQUIRE(rd_buffer_view_from_handle(views[i])->vk.elements==RBA_COUNT);
   }
   VkDescriptorPoolSize pool_sizes[5]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,1},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,2},{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,1},{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,1}};
   VkDescriptorPoolCreateInfo pool_info={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,
      .poolSizeCount=5,.pPoolSizes=pool_sizes};
   VkDescriptorPool pool; SUCCESS(d->CreateDescriptorPool(device,&pool_info,NULL,&pool));
   VkDescriptorSetAllocateInfo allocation={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,
      .descriptorSetCount=1,.pSetLayouts=&set_layout};
   VkDescriptorSet set; SUCCESS(d->AllocateDescriptorSets(device,&allocation,&set));
   VkDescriptorBufferInfo initial[4]={{stores[RBA_OUTPUT].buffer,GUARD*4,RBA_RESULTS*4},
      {stores[RBA_UNIFORM].buffer,16,16},{stores[RBA_DATA].buffer,16,16},{stores[RBA_DATA].buffer,16,16}};
   VkWriteDescriptorSet writes[6];
   for (unsigned i=0;i<6;i++) writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,
      .dstBinding=i,.descriptorCount=1,.descriptorType=types[i],.pBufferInfo=i<4?&initial[i]:NULL,
      .pTexelBufferView=i>=4?&views[i-4]:NULL};
   d->UpdateDescriptorSets(device,6,writes,0,NULL);
   VkCommandPoolCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; VkCommandPool command_pool;
   SUCCESS(d->CreateCommandPool(device,&ci,NULL,&command_pool));
   VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=command_pool,
      .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1}; VkCommandBuffer command;
   SUCCESS(d->AllocateCommandBuffers(device,&ca,&command));
   VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   SUCCESS(d->BeginCommandBuffer(command,&begin));
   d->CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
   uint32_t dynamic[3]={16,16,16};
   d->CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,3,dynamic);
   d->CmdDispatch(command,1,1,1);
   VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
      .dstAccessMask=VK_ACCESS_HOST_READ_BIT};
   d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
   SUCCESS(d->EndCommandBuffer(command));
   VkQueue queue; d->GetDeviceQueue(device,0,0,&queue);
   VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence;
   SUCCESS(d->CreateFence(device,&fi,NULL,&fence));
   VkMappedMemoryRange ranges[RBA_STORES];
   for (unsigned i=0;i<RBA_STORES;i++) ranges[i]=(VkMappedMemoryRange){.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
      .memory=stores[i].memory,.size=VK_WHOLE_SIZE};
   const unsigned spans[4]={1,3,12,16};
   for (unsigned test=0;test<4;test++) {
      for (unsigned s=0;s<RBA_STORES;s++) for (size_t i=0;i<stores[s].words;i++) stores[s].map[i]=poison(i,20+s,test);
      for (unsigned i=0;i<4;i++) {
         stores[RBA_UNIFORM].map[stores[RBA_UNIFORM].binding_words+8+i]=0x12340000u+test*0x100u+i;
         stores[RBA_DATA].map[stores[RBA_DATA].binding_words+8+i]=0x20001000u+test*0x100u+i;
      }
      for (unsigned i=0;i<RBA_COUNT;i++) {
         stores[RBA_SOURCE_TEXEL].map[stores[RBA_SOURCE_TEXEL].binding_words+GUARD+i]=rba_source(i);
         stores[RBA_TARGET_TEXEL].map[stores[RBA_TARGET_TEXEL].binding_words+GUARD+i]=rba_target(i);
      }
      for (unsigned s=0;s<RBA_STORES;s++) memcpy(stores[s].saved,stores[s].map,stores[s].words*4);
      SUCCESS(d->FlushMappedMemoryRanges(device,RBA_STORES,ranges));
      for (unsigned i=1;i<4;i++) initial[i].range=spans[test];
      d->UpdateDescriptorSets(device,3,&writes[1],0,NULL);
      SUCCESS(d->ResetFences(device,1,&fence));
      VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
      printf("RBA_CASE_BEGIN case=%u descriptor_span=%u dynamic_base=32 texels=%u\n",test,spans[test],RBA_COUNT); fflush(stdout);
      SUCCESS(d->QueueSubmit(queue,1,&submit,fence));
      SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
      SUCCESS(d->InvalidateMappedMemoryRanges(device,RBA_STORES,ranges));
      REQUIRE(rba_verify(stores,test,spans[test])==0);
      printf("RBA_CASE_PASS case=%u span=%u output_words=%u all_source_backing_guards=exact core_RBA_only=1\n",
         test,spans[test],RBA_RESULTS); fflush(stdout);
   }
   d->DestroyFence(device,fence,NULL); d->DestroyCommandPool(device,command_pool,NULL);
   d->DestroyDescriptorPool(device,pool,NULL);
   for (unsigned i=0;i<2;i++) d->DestroyBufferView(device,views[i],NULL);
   for (unsigned i=0;i<RBA_STORES;i++) {
      free(stores[i].saved); d->UnmapMemory(device,stores[i].memory);
      d->DestroyBuffer(device,stores[i].buffer,NULL); d->FreeMemory(device,stores[i].memory,NULL);
   }
   d->DestroyPipeline(device,pipeline,NULL); d->DestroyPipelineLayout(device,layout,NULL);
   d->DestroyDescriptorSetLayout(device,set_layout,NULL); d->DestroyDevice(device,NULL);
   DestroyInstance(instance,NULL); dlclose(lib);
   puts("PASS RBA: short/dynamic ranges, vector tails, stores, add/CAS, large texel ranges, exact guards; public feature enablement");
   return 0;
}
