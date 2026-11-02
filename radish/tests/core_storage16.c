int resource_baseline_main(int argc,char **argv);
#define main resource_baseline_main
#include "core_resources.c"
#undef main

enum { S16_LANES=32, S16_RESULTS=100, S16_STORES=4 };
static const unsigned s16_spans[5]={1,2,3,6,S16_LANES*6};
static const uint16_t s16_float_bits[8]={0x3e00,0x8000,0x0001,0x7e35,0xfc00,0x3555,0x83ff,0x7bff};
static uint16_t s16_unsigned(unsigned i) { return (uint16_t)(0x8100u+i*29u); }
static uint16_t s16_signed(unsigned i) { return (uint16_t)(i&1u?0x7fffu-i:0x8000u+i); }
static void s16_put(uint8_t *data,size_t offset,uint16_t value) { memcpy(data+offset,&value,2); }

static int s16_verify(struct storage stores[S16_STORES],unsigned test,unsigned base)
{
   unsigned span=s16_spans[test];
   uint32_t expected[S16_RESULTS]={0};
   uint8_t *destination=(uint8_t *)stores[2].saved+stores[2].binding_words*4+base;
   for (unsigned i=0;i<S16_LANES;i++) {
      unsigned offset=i*6;
      if (span>=offset+2) { expected[i]=s16_unsigned(i); s16_put(destination,offset,s16_unsigned(i)); }
      if (span>=offset+4) {
         uint16_t bits=s16_signed(i);
         expected[32+i]=(bits&0x8000u)?0xffff0000u|bits:bits;
         s16_put(destination,offset+2,bits);
      }
      if (span>=offset+6) s16_put(destination,offset+4,s16_float_bits[i%8]);
      unsigned writer=(i>>1)+(i&1u)*16u;
      uint16_t value=(uint16_t)(0xa500u+writer);
      s16_put((uint8_t *)stores[3].saved+stores[3].binding_words*4+64,i*2,value);
      expected[68+i]=value;
   }
   expected[64]=span>=6?0x3fc00000u:0;
   expected[65]=expected[66]=span/6; expected[67]=S16_LANES;
   memcpy(stores[0].saved+stores[0].binding_words+GUARD,expected,sizeof(expected));
   for (unsigned s=0;s<S16_STORES;s++) for (size_t i=0;i<stores[s].words;i++) {
      if (stores[s].map[i]!=stores[s].saved[i]) {
         fprintf(stderr,"FAIL STORAGE16 case=%u span=%u buffer=%u word=%zu actual=%08x expected=%08x\n",
            test,span,s,i,stores[s].map[i],stores[s].saved[i]); return 1;
      }
   }
   return 0;
}

int main(int argc,char **argv)
{
   if (argc!=3 && !(argc==4 && !strcmp(argv[3],"--compile-only"))) {
      fprintf(stderr,"Usage: %s ICD-library storage16.spv [--compile-only]\n",argv[0]); return 1;
   }
   void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); if (!lib) { fprintf(stderr,"%s\n",dlerror()); return 1; }
   PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr"); REQUIRE(gipa);
   VkInstance instance=VK_NULL_HANDLE; INSTANCE_FN(CreateInstance);
   VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_0};
   const char *instance_extensions[]={VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME};
   VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app,
      .enabledExtensionCount=1,.ppEnabledExtensionNames=instance_extensions};
   SUCCESS(CreateInstance(&ici,NULL,&instance));
   INSTANCE_FN(EnumeratePhysicalDevices); INSTANCE_FN(GetPhysicalDeviceMemoryProperties);
   INSTANCE_FN(GetPhysicalDeviceProperties); INSTANCE_FN(GetPhysicalDeviceFeatures2KHR);
   INSTANCE_FN(EnumerateDeviceExtensionProperties); INSTANCE_FN(CreateDevice); INSTANCE_FN(DestroyInstance);
   VkPhysicalDevice physical; uint32_t count=1;
   SUCCESS(EnumeratePhysicalDevices(instance,&count,&physical)); REQUIRE(count==1);
   VkPhysicalDevice16BitStorageFeaturesKHR queried={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES_KHR};
   VkPhysicalDeviceFeatures2KHR queried_core={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR,.pNext=&queried};
   GetPhysicalDeviceFeatures2KHR(physical,&queried_core);
   REQUIRE(queried.storageBuffer16BitAccess && !queried.uniformAndStorageBuffer16BitAccess &&
      !queried.storagePushConstant16 && !queried.storageInputOutput16);
   REQUIRE(queried_core.features.robustBufferAccess && !queried_core.features.shaderInt16);
   uint32_t extension_count=0; SUCCESS(EnumerateDeviceExtensionProperties(physical,NULL,&extension_count,NULL));
   VkExtensionProperties *extensions=calloc(extension_count,sizeof(*extensions)); REQUIRE(extensions);
   SUCCESS(EnumerateDeviceExtensionProperties(physical,NULL,&extension_count,extensions));
   bool found_storage=false,found_class=false;
   for (uint32_t i=0;i<extension_count;i++) {
      found_storage|=!strcmp(extensions[i].extensionName,VK_KHR_16BIT_STORAGE_EXTENSION_NAME);
      found_class|=!strcmp(extensions[i].extensionName,VK_KHR_STORAGE_BUFFER_STORAGE_CLASS_EXTENSION_NAME);
   }
   free(extensions); REQUIRE(found_storage && found_class);
   VkPhysicalDeviceMemoryProperties memory_properties; GetPhysicalDeviceMemoryProperties(physical,&memory_properties);
   VkPhysicalDeviceProperties properties; GetPhysicalDeviceProperties(physical,&properties);
   REQUIRE(properties.apiVersion==VK_API_VERSION_1_0);
   REQUIRE(properties.limits.minStorageBufferOffsetAlignment<=16);
   const char *device_extensions[]={VK_KHR_16BIT_STORAGE_EXTENSION_NAME,VK_KHR_STORAGE_BUFFER_STORAGE_CLASS_EXTENSION_NAME};
   VkPhysicalDevice16BitStorageFeaturesKHR requested_storage={
      .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES_KHR,.storageBuffer16BitAccess=VK_TRUE};
   VkPhysicalDeviceFeatures requested={.robustBufferAccess=VK_TRUE};
   float priority=1; VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueCount=1,.pQueuePriorities=&priority};
   VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&requested_storage,
      .queueCreateInfoCount=1,.pQueueCreateInfos=&qci,.pEnabledFeatures=&requested,
      .enabledExtensionCount=2,.ppEnabledExtensionNames=device_extensions};
   VkDevice device; SUCCESS(CreateDevice(physical,&dci,NULL,&device));
   struct rd_device *inside=rd_device_from_handle(device);
   REQUIRE(inside->vk.enabled_features.robustBufferAccess && inside->vk.enabled_features.storageBuffer16BitAccess &&
      !inside->vk.enabled_features.uniformAndStorageBuffer16BitAccess && !inside->vk.enabled_features.storagePushConstant16 &&
      !inside->vk.enabled_features.storageInputOutput16 && !inside->vk.enabled_features.shaderInt16 &&
      !inside->vk.enabled_features.shaderFloat16);
   puts("STORAGE16_PUBLIC_FEATURE extension=1 supported=1 enabled=1 robust=1 other_storage16=0 arithmetic16=0"); fflush(stdout);
   const struct vk_device_dispatch_table *d=&inside->vk.dispatch_table;
   VkDescriptorType types[4]={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
   VkDescriptorSetLayoutBinding bindings[4];
   for (unsigned i=0;i<4;i++) bindings[i]=(VkDescriptorSetLayoutBinding){.binding=i,.descriptorType=types[i],
      .descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
   VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=4,.pBindings=bindings};
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
   printf("STORAGE16_COMPILED descriptors=%u buffers=%u native=%u full=%d half=%d private=%u shared=%u physical=%u,%u,%u\n",
      compiled->descriptor_count,compiled->buffer_count,compiled->variant->instrlen,
      compiled->variant->info.max_reg+1,compiled->variant->info.max_half_reg+1,
      compiled->variant->pvtmem_size,compiled->variant->shared_size,
      compiled->variant->local_size[0],compiled->variant->local_size[1],compiled->variant->local_size[2]); fflush(stdout);
   if (argc==4) {
      d->DestroyPipeline(device,pipeline,NULL); d->DestroyPipelineLayout(device,layout,NULL);
      d->DestroyDescriptorSetLayout(device,set_layout,NULL); d->DestroyDevice(device,NULL);
      DestroyInstance(instance,NULL); dlclose(lib);
      puts("PASS STORAGE16 compile-only: queried and enabled public storage-only feature, no buffers or dispatch"); return 0;
   }
   struct storage stores[S16_STORES]={0};
   for (unsigned i=0;i<S16_STORES;i++) REQUIRE(storage_create(d,device,&memory_properties,512,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&stores[i])==0);
   VkDescriptorPoolSize pool_sizes[2]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,2}};
   VkDescriptorPoolCreateInfo pool_info={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,
      .poolSizeCount=2,.pPoolSizes=pool_sizes}; VkDescriptorPool pool;
   SUCCESS(d->CreateDescriptorPool(device,&pool_info,NULL,&pool));
   VkDescriptorSetAllocateInfo allocation={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,
      .descriptorSetCount=1,.pSetLayouts=&set_layout}; VkDescriptorSet set;
   SUCCESS(d->AllocateDescriptorSets(device,&allocation,&set));
   VkDescriptorBufferInfo descriptors[4]={{stores[0].buffer,64,S16_RESULTS*4},
      {stores[1].buffer,16,1},{stores[2].buffer,16,1},{stores[3].buffer,64,S16_LANES*2}};
   VkWriteDescriptorSet writes[4];
   for (unsigned i=0;i<4;i++) writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet=set,.dstBinding=i,.descriptorCount=1,.descriptorType=types[i],.pBufferInfo=&descriptors[i]};
   VkCommandPoolCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; VkCommandPool command_pool;
   SUCCESS(d->CreateCommandPool(device,&ci,NULL,&command_pool));
   VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=command_pool,
      .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1}; VkCommandBuffer command;
   SUCCESS(d->AllocateCommandBuffers(device,&ca,&command));
   VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   VkQueue queue; d->GetDeviceQueue(device,0,0,&queue);
   VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence;
   SUCCESS(d->CreateFence(device,&fi,NULL,&fence));
   VkMappedMemoryRange ranges[S16_STORES];
   for (unsigned i=0;i<S16_STORES;i++) ranges[i]=(VkMappedMemoryRange){.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
      .memory=stores[i].memory,.size=VK_WHOLE_SIZE};
   for (unsigned test=0;test<5;test++) {
      unsigned dynamic=16*(test+1),base=16+dynamic;
      for (unsigned s=0;s<S16_STORES;s++) for (size_t i=0;i<stores[s].words;i++) stores[s].map[i]=poison(i,30+s,test);
      uint8_t *source=(uint8_t *)stores[1].map+stores[1].binding_words*4+base;
      for (unsigned i=0;i<S16_LANES;i++) {
         s16_put(source,i*6,s16_unsigned(i)); s16_put(source,i*6+2,s16_signed(i));
         s16_put(source,i*6+4,s16_float_bits[i%8]);
      }
      for (unsigned s=0;s<S16_STORES;s++) memcpy(stores[s].saved,stores[s].map,stores[s].words*4);
      SUCCESS(d->FlushMappedMemoryRanges(device,S16_STORES,ranges));
      SUCCESS(d->ResetCommandPool(device,command_pool,0));
      descriptors[1].range=descriptors[2].range=s16_spans[test];
      d->UpdateDescriptorSets(device,4,writes,0,NULL);
      SUCCESS(d->BeginCommandBuffer(command,&begin));
      VkMemoryBarrier before={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,
         .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
      d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&before,0,NULL,0,NULL);
      d->CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
      uint32_t dynamic_offsets[2]={dynamic,dynamic};
      d->CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,2,dynamic_offsets);
      d->CmdDispatch(command,1,1,1);
      VkMemoryBarrier after={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
         .dstAccessMask=VK_ACCESS_HOST_READ_BIT};
      d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&after,0,NULL,0,NULL);
      SUCCESS(d->EndCommandBuffer(command)); SUCCESS(d->ResetFences(device,1,&fence));
      VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
      printf("STORAGE16_CASE_BEGIN case=%u span=%u dynamic=%u base=%u lanes=32\n",test,s16_spans[test],dynamic,base); fflush(stdout);
      SUCCESS(d->QueueSubmit(queue,1,&submit,fence));
      SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
      SUCCESS(d->InvalidateMappedMemoryRanges(device,S16_STORES,ranges));
      REQUIRE(s16_verify(stores,test,base)==0);
      printf("STORAGE16_CASE_PASS case=%u span=%u outputs=100 complete_allocations=4 guards=exact\n",test,s16_spans[test]); fflush(stdout);
   }
   d->DestroyFence(device,fence,NULL); d->DestroyCommandPool(device,command_pool,NULL);
   d->DestroyDescriptorPool(device,pool,NULL);
   for (unsigned i=0;i<S16_STORES;i++) {
      free(stores[i].saved); d->UnmapMemory(device,stores[i].memory);
      d->DestroyBuffer(device,stores[i].buffer,NULL); d->FreeMemory(device,stores[i].memory,NULL);
   }
   d->DestroyPipeline(device,pipeline,NULL); d->DestroyPipelineLayout(device,layout,NULL);
   d->DestroyDescriptorSetLayout(device,set_layout,NULL); d->DestroyDevice(device,NULL);
   DestroyInstance(instance,NULL); dlclose(lib);
   puts("PASS STORAGE16: public storage-only feature, five short/dynamic spans, signed/float bits, adjacent writers, complete allocation guards");
   return 0;
}
