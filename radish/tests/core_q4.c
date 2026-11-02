int resource_baseline_main(int argc,char **argv);
#define main resource_baseline_main
#include "core_resources.c"
#undef main

enum { Q4_ROWS=2, Q4_K=256, Q4_BLOCK_BYTES=144, Q4_STORES=6 };
enum { Q4_A, Q4_B, Q4_D, Q4_FUSE0, Q4_FUSE1, Q4_DEQUANT };
static float q4_vector(unsigned i) { return ((int)(i%11)-5)*0.0625f; }
static float q4_fuse0(unsigned row) { return row?-0.75f:1.25f; }
static float q4_fuse1(unsigned row) { return row?2.f:-0.5f; }
static void q4_block(uint8_t *out,unsigned row)
{
   uint16_t d=row?0x2c00:0x3000,minimum=row?0x2800:0x2c00;
   memcpy(out,&d,2); memcpy(out+2,&minimum,2);
   for (unsigned j=0;j<12;j++) out[4+j]=(uint8_t)(0xa3u+row*47u+j*29u);
   for (unsigned j=0;j<128;j++) out[16+j]=(uint8_t)(((j*7u+row*3u)&15u)|(((j*11u+row*5u+1u)&15u)<<4));
}
static float q4_weight(const uint8_t *block,unsigned row,unsigned column)
{
   unsigned group=column/32; const uint8_t *scales=block+4;
   unsigned scale,minimum;
   if (group<4) { scale=scales[group]&63u; minimum=scales[group+4]&63u; }
   else { scale=(scales[group+4]&15u)|((scales[group-4]>>6)<<4);
      minimum=(scales[group+4]>>4)|((scales[group]>>6)<<4); }
   unsigned byte=block[16+(group/2)*32+(column%32)];
   unsigned q=(group&1u)?byte>>4:byte&15u;
   int numerator=(int)(2*scale*q)-(int)minimum;
   return numerator*(row?0.03125f:0.0625f);
}
static uint16_t q4_half(float value)
{
   uint32_t bits=float_word(value); unsigned magnitude=bits&0x7fffffffu;
   if (!magnitude) return (uint16_t)(bits>>16);
   int exponent=(int)((bits>>23)&255u)-127+15;
   if (exponent<=0 || exponent>=31 || (bits&8191u)) abort();
   return (uint16_t)(((bits>>16)&0x8000u)|((unsigned)exponent<<10)|((bits>>13)&1023u));
}
static int q4_pipeline(const struct vk_device_dispatch_table *d,VkDevice device,const char *path,
   unsigned kind,VkDescriptorSetLayout *set_layout,VkPipelineLayout *layout,VkPipeline *pipeline)
{
   unsigned count=kind?5:2;
   VkDescriptorSetLayoutBinding bindings[5];
   for (unsigned i=0;i<count;i++) bindings[i]=(VkDescriptorSetLayoutBinding){.binding=i,
      .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
   VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=count,.pBindings=bindings};
   SUCCESS(d->CreateDescriptorSetLayout(device,&li,NULL,set_layout));
   VkPushConstantRange push={.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,.size=kind?52:20};
   VkPipelineLayoutCreateInfo pi={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount=1,.pSetLayouts=set_layout,.pushConstantRangeCount=1,.pPushConstantRanges=&push};
   SUCCESS(d->CreatePipelineLayout(device,&pi,NULL,layout));
   size_t bytes; uint32_t *code=read_spirv(path,&bytes); REQUIRE(code && bytes>=20 && code[1]==0x00010000u);
   VkShaderModuleCreateInfo mi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=bytes,.pCode=code};
   VkShaderModule module; SUCCESS(d->CreateShaderModule(device,&mi,NULL,&module)); free(code);
   uint32_t values[3]={32,2,1};
   VkSpecializationMapEntry entries[3]={{0,0,4},{1,4,4},{2,8,4}};
   VkSpecializationInfo specialization={.mapEntryCount=3,.pMapEntries=entries,.dataSize=sizeof(values),.pData=values};
   VkComputePipelineCreateInfo info={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,
              .module=module,.pName="main",.pSpecializationInfo=kind?&specialization:NULL},.layout=*layout};
   SUCCESS(d->CreateComputePipelines(device,VK_NULL_HANDLE,1,&info,NULL,pipeline));
   d->DestroyShaderModule(device,module,NULL);
   struct rd_pipeline *compiled=rd_pipeline_from_handle(*pipeline);
   unsigned local=compiled->variant->local_size[0]*compiled->variant->local_size[1]*compiled->variant->local_size[2];
   REQUIRE(compiled->descriptor_count==count && compiled->buffer_count==count && local==32);
   printf("Q4_COMPILED kind=%s descriptors=%u native=%u full=%d half=%d private=%u shared=%u physical=%u,%u,%u\n",
      kind?"matvec":"dequant",compiled->descriptor_count,compiled->variant->instrlen,
      compiled->variant->info.max_reg+1,compiled->variant->info.max_half_reg+1,
      compiled->variant->pvtmem_size,compiled->variant->shared_size,
      compiled->variant->local_size[0],compiled->variant->local_size[1],compiled->variant->local_size[2]); fflush(stdout);
   return 0;
}

int main(int argc,char **argv)
{
   if (argc!=4 && !(argc==5 && !strcmp(argv[4],"--compile-only"))) {
      fprintf(stderr,"Usage: %s ICD-library dequant_q4_k.spv mul_mat_vec_q4_k.spv [--compile-only]\n",argv[0]); return 1;
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
   VkPhysicalDeviceShaderFloat16Int8FeaturesKHR queried={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR};
   VkPhysicalDevice16BitStorageFeaturesKHR queried16={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES_KHR};
   VkPhysicalDevice8BitStorageFeaturesKHR queried8={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES_KHR};
   queried.pNext=&queried16; queried16.pNext=&queried8;
   VkPhysicalDeviceFeatures2KHR queried_core={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR,.pNext=&queried};
   GetPhysicalDeviceFeatures2KHR(physical,&queried_core);
   REQUIRE(queried.shaderInt8 && !queried.shaderFloat16 && queried_core.features.robustBufferAccess);
   REQUIRE(queried16.storageBuffer16BitAccess && queried8.storageBuffer8BitAccess);
   uint32_t extension_count=0; SUCCESS(EnumerateDeviceExtensionProperties(physical,NULL,&extension_count,NULL));
   VkExtensionProperties *extensions=calloc(extension_count,sizeof(*extensions)); REQUIRE(extensions);
   SUCCESS(EnumerateDeviceExtensionProperties(physical,NULL,&extension_count,extensions));
   const char *device_extensions[]={VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME,
      VK_KHR_16BIT_STORAGE_EXTENSION_NAME,VK_KHR_8BIT_STORAGE_EXTENSION_NAME,
      VK_KHR_STORAGE_BUFFER_STORAGE_CLASS_EXTENSION_NAME};
   for (unsigned e=0;e<4;e++) {
      bool found=false;
      for (uint32_t i=0;i<extension_count;i++) found|=!strcmp(extensions[i].extensionName,device_extensions[e]);
      REQUIRE(found);
   }
   free(extensions);
   VkPhysicalDeviceMemoryProperties memory_properties; GetPhysicalDeviceMemoryProperties(physical,&memory_properties);
   VkPhysicalDeviceProperties properties; GetPhysicalDeviceProperties(physical,&properties);
   REQUIRE(properties.apiVersion==VK_API_VERSION_1_0 && properties.limits.minStorageBufferOffsetAlignment<=64);
   VkPhysicalDeviceShaderFloat16Int8FeaturesKHR requested_int8={
      .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR,.shaderInt8=VK_TRUE};
   VkPhysicalDevice16BitStorageFeaturesKHR requested16={
      .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES_KHR,.storageBuffer16BitAccess=VK_TRUE};
   VkPhysicalDevice8BitStorageFeaturesKHR requested8={
      .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES_KHR,.storageBuffer8BitAccess=VK_TRUE};
   requested_int8.pNext=&requested16; requested16.pNext=&requested8;
   VkPhysicalDeviceFeatures requested={.robustBufferAccess=VK_TRUE};
   float priority=1; VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueCount=1,.pQueuePriorities=&priority};
   VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&requested_int8,
      .queueCreateInfoCount=1,.pQueueCreateInfos=&qci,.pEnabledFeatures=&requested,
      .enabledExtensionCount=4,.ppEnabledExtensionNames=device_extensions};
   VkDevice device; SUCCESS(CreateDevice(physical,&dci,NULL,&device));
   struct rd_device *inside=rd_device_from_handle(device);
   REQUIRE(inside->vk.enabled_features.robustBufferAccess && inside->vk.enabled_features.shaderInt8 &&
      !inside->vk.enabled_features.shaderFloat16 && !inside->vk.enabled_features.shaderInt16 &&
      inside->vk.enabled_features.storageBuffer8BitAccess && inside->vk.enabled_features.storageBuffer16BitAccess);
   puts("Q4_PUBLIC_FEATURE int8=1 storage8=1 storage16=1 robust=1 float16=0 int16=0"); fflush(stdout);
   const struct vk_device_dispatch_table *d=&inside->vk.dispatch_table;
   REQUIRE(properties.limits.maxPerStageDescriptorStorageBuffers>=5 && properties.limits.maxDescriptorSetStorageBuffers>=5);
   REQUIRE(properties.limits.maxPushConstantsSize>=52 && properties.limits.maxComputeWorkGroupInvocations>=32);
   VkDescriptorSetLayout set_layouts[2]; VkPipelineLayout layouts[2]; VkPipeline pipelines[2];
   for (unsigned kind=0;kind<2;kind++) REQUIRE(q4_pipeline(d,device,argv[2+kind],kind,&set_layouts[kind],&layouts[kind],&pipelines[kind])==0);
   if (argc==5) {
      for (unsigned kind=0;kind<2;kind++) {
         d->DestroyPipeline(device,pipelines[kind],NULL); d->DestroyPipelineLayout(device,layouts[kind],NULL);
         d->DestroyDescriptorSetLayout(device,set_layouts[kind],NULL);
      }
      d->DestroyDevice(device,NULL); DestroyInstance(instance,NULL); dlclose(lib);
      puts("PASS Q4 compile-only: original public five-buffer interface, storage8/16 and arithmetic8, no buffers or dispatch"); return 0;
   }
   struct storage stores[Q4_STORES]={0};
   const uint32_t sizes[Q4_STORES]={Q4_ROWS*Q4_BLOCK_BYTES,Q4_K*4,Q4_ROWS*4,Q4_ROWS*4,Q4_ROWS*4,Q4_ROWS*Q4_K*2};
   for (unsigned s=0;s<Q4_STORES;s++) {
      REQUIRE(storage_create(d,device,&memory_properties,sizes[s]+128,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&stores[s])==0);
      for (size_t i=0;i<stores[s].words;i++) stores[s].map[i]=poison(i,50+s,0);
   }
   uint8_t *weights=(uint8_t *)(stores[Q4_A].map+stores[Q4_A].binding_words+GUARD);
   for (unsigned row=0;row<Q4_ROWS;row++) q4_block(weights+row*Q4_BLOCK_BYTES,row);
   for (unsigned i=0;i<Q4_K;i++) stores[Q4_B].map[stores[Q4_B].binding_words+GUARD+i]=float_word(q4_vector(i));
   for (unsigned row=0;row<Q4_ROWS;row++) {
      stores[Q4_FUSE0].map[stores[Q4_FUSE0].binding_words+GUARD+row]=float_word(q4_fuse0(row));
      stores[Q4_FUSE1].map[stores[Q4_FUSE1].binding_words+GUARD+row]=float_word(q4_fuse1(row));
   }
   for (unsigned s=0;s<Q4_STORES;s++) memcpy(stores[s].saved,stores[s].map,stores[s].words*4);
   uint16_t expected_dequant[Q4_ROWS*Q4_K]; uint32_t expected_dot[Q4_ROWS];
   for (unsigned row=0;row<Q4_ROWS;row++) {
      double sum=0;
      for (unsigned column=0;column<Q4_K;column++) {
         float value=q4_weight(weights+row*Q4_BLOCK_BYTES,row,column);
         expected_dequant[row*Q4_K+column]=q4_half(value);
         sum+=(double)value*q4_vector(column);
      }
      sum+=q4_fuse0(row); sum+=q4_fuse1(row);
      expected_dot[row]=float_word((float)sum);
      printf("Q4_ORACLE row=%u fused_result_bits=%08x fusion_flags=3\n",row,expected_dot[row]);
   }
   memcpy(stores[Q4_DEQUANT].saved+stores[Q4_DEQUANT].binding_words+GUARD,expected_dequant,sizeof(expected_dequant));
   memcpy(stores[Q4_D].saved+stores[Q4_D].binding_words+GUARD,expected_dot,sizeof(expected_dot));
   VkDescriptorPoolSize pool_size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,7};
   VkDescriptorPoolCreateInfo pool_info={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=2,.poolSizeCount=1,.pPoolSizes=&pool_size};
   VkDescriptorPool pool; SUCCESS(d->CreateDescriptorPool(device,&pool_info,NULL,&pool));
   VkDescriptorSetAllocateInfo allocation={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=2,.pSetLayouts=set_layouts};
   VkDescriptorSet sets[2]; SUCCESS(d->AllocateDescriptorSets(device,&allocation,sets));
   VkDescriptorBufferInfo descriptors[7]; VkWriteDescriptorSet writes[7];
   const unsigned mapping[7]={Q4_A,Q4_DEQUANT,Q4_A,Q4_B,Q4_D,Q4_FUSE0,Q4_FUSE1};
   for (unsigned i=0;i<7;i++) {
      unsigned s=mapping[i]; descriptors[i]=(VkDescriptorBufferInfo){stores[s].buffer,64,sizes[s]};
      writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=sets[i<2?0:1],
         .dstBinding=i<2?i:i-2,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&descriptors[i]};
   }
   d->UpdateDescriptorSets(device,7,writes,0,NULL);
   VkMappedMemoryRange ranges[Q4_STORES];
   for (unsigned s=0;s<Q4_STORES;s++) ranges[s]=(VkMappedMemoryRange){.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=stores[s].memory,.size=VK_WHOLE_SIZE};
   SUCCESS(d->FlushMappedMemoryRanges(device,Q4_STORES,ranges));
   VkCommandPoolCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; VkCommandPool command_pool;
   SUCCESS(d->CreateCommandPool(device,&ci,NULL,&command_pool));
   VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=command_pool,
      .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1}; VkCommandBuffer command;
   SUCCESS(d->AllocateCommandBuffers(device,&ca,&command));
   VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; SUCCESS(d->BeginCommandBuffer(command,&begin));
   VkMemoryBarrier before={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,
      .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
   d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&before,0,NULL,0,NULL);
   const uint32_t dequant_push[5]={2,256,256,256,512};
   const uint32_t matvec_push[13]={256,256,256,2,512,256,2,3,0,1,1,1,1};
   for (unsigned kind=0;kind<2;kind++) {
      d->CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipelines[kind]);
      d->CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layouts[kind],0,1,&sets[kind],0,NULL);
      d->CmdPushConstants(command,layouts[kind],VK_SHADER_STAGE_COMPUTE_BIT,0,kind?52:20,kind?matvec_push:dequant_push);
      d->CmdDispatch(command,1,1,1);
   }
   VkMemoryBarrier after={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
   d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&after,0,NULL,0,NULL);
   SUCCESS(d->EndCommandBuffer(command));
   VkQueue queue; d->GetDeviceQueue(device,0,0,&queue);
   VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence; SUCCESS(d->CreateFence(device,&fi,NULL,&fence));
   VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
   puts("Q4_CASE_BEGIN rows=2 columns=256 fusion_flags=3 dequant_halves=512 dot_floats=2"); fflush(stdout);
   SUCCESS(d->QueueSubmit(queue,1,&submit,fence)); SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
   SUCCESS(d->InvalidateMappedMemoryRanges(device,Q4_STORES,ranges));
   for (unsigned s=0;s<Q4_STORES;s++) for (size_t i=0;i<stores[s].words;i++) {
      if (stores[s].map[i]!=stores[s].saved[i]) {
         fprintf(stderr,"FAIL Q4 buffer=%u word=%zu actual=%08x expected=%08x\n",s,i,stores[s].map[i],stores[s].saved[i]); return 1;
      }
   }
   puts("Q4_CASE_PASS dequant_halves=512 fused_dot_floats=2 complete_allocations=6 guards=exact"); fflush(stdout);
   d->DestroyFence(device,fence,NULL); d->DestroyCommandPool(device,command_pool,NULL); d->DestroyDescriptorPool(device,pool,NULL);
   for (unsigned s=0;s<Q4_STORES;s++) {
      free(stores[s].saved); d->UnmapMemory(device,stores[s].memory); d->DestroyBuffer(device,stores[s].buffer,NULL); d->FreeMemory(device,stores[s].memory,NULL);
   }
   for (unsigned kind=0;kind<2;kind++) {
      d->DestroyPipeline(device,pipelines[kind],NULL); d->DestroyPipelineLayout(device,layouts[kind],NULL);
      d->DestroyDescriptorSetLayout(device,set_layouts[kind],NULL);
   }
   d->DestroyDevice(device,NULL); DestroyInstance(instance,NULL); dlclose(lib);
   puts("PASS Q4: real backend dequant and five-buffer fused matvec, independent exact CPU decode/dot oracle and full allocation guards");
   return 0;
}
