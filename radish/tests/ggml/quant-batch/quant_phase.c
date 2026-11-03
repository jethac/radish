int resource_baseline_main(int argc,char **argv);
#define main resource_baseline_main
#include "core_resources.c"
#undef main

enum { ROWS=33, K=512, A_BATCHES=2, B_BATCHES=4 };
static unsigned block_bytes(unsigned type) { return type==0?144:type==1?210:34; }
static unsigned block_k(unsigned type) { return type==2?32:256; }
static void make_block(uint8_t *out,unsigned type,unsigned seed)
{
 if(type==0) {
  const uint16_t d=0x1c00,minimum=0x1800; memcpy(out,&d,2);memcpy(out+2,&minimum,2);
  for(unsigned j=0;j<12;j++)out[4+j]=(uint8_t)(0xa3u+seed*47u+j*29u);
  for(unsigned j=0;j<128;j++)out[16+j]=(uint8_t)(seed*31u+j*53u);
 } else if(type==1) {
  for(unsigned j=0;j<128;j++)out[j]=(uint8_t)(seed*19u+j*37u);
  for(unsigned j=0;j<64;j++)out[128+j]=(uint8_t)(seed*43u+j*11u);
  for(unsigned j=0;j<16;j++){int v=(int)((seed+j*5u)%15)-7;out[192+j]=(uint8_t)v;}
  const uint16_t d=0x1400;memcpy(out+208,&d,2);
 } else {
  const uint16_t d=0x1c00;memcpy(out,&d,2);
  for(unsigned j=0;j<32;j++)out[2+j]=(uint8_t)(seed*23u+j*41u);
 }
}
static int signed_byte(uint8_t v) { return v<128?v:(int)v-256; }
static float weight(const uint8_t *p,unsigned type,unsigned i)
{
 if(type==0){unsigned g=i/32,scale,minimum;const uint8_t *s=p+4;
  if(g<4){scale=s[g]&63u;minimum=s[g+4]&63u;}
  else{scale=(s[g+4]&15u)|((s[g-4]>>6)<<4);minimum=(s[g+4]>>4)|((s[g]>>6)<<4);}
  unsigned q=p[16+(g/2)*32+i%32];q=g&1?q>>4:q&15u;
  return ((int)(2*scale*q)-(int)minimum)*(1.f/512.f);
 }
 if(type==1){unsigned n=i/128,j=i%128,l=j%32,segment=j/32;
  unsigned lo=p[n*64+(segment&1)*32+l];lo=segment>=2?lo>>4:lo&15u;
  unsigned hi=(p[128+n*32+l]>>(2*segment))&3u;
  return ((int)(lo|(hi<<4))-32)*signed_byte(p[192+n*8+segment*2+l/16])*(1.f/1024.f);
 }
 return signed_byte(p[2+i])*(1.f/256.f);
}
static float vector_value(unsigned batch,unsigned column,unsigned k,unsigned round)
{return ((int)((batch*3+column*5+k*11+round*13)%7)-3)*(1.f/256.f);}

int main(int argc,char **argv)
{
 REQUIRE(argc==5||(argc==6&&!strcmp(argv[5],"--compile-only")));
 REQUIRE(strlen(argv[3])==1&&argv[3][0]>='0'&&argv[3][0]<='2');unsigned type=(unsigned)(argv[3][0]-'0');
 REQUIRE(!strcmp(argv[4],"mm")||!strcmp(argv[4],"mv"));bool mv=!strcmp(argv[4],"mv");unsigned cols=mv?3:19;
   void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); if (!lib) { fprintf(stderr,"%s\n",dlerror()); return 1; }
   PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr"); REQUIRE(gipa);
   VkInstance instance=VK_NULL_HANDLE; INSTANCE_FN(CreateInstance);
   VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_1};
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
   REQUIRE(properties.apiVersion>=VK_API_VERSION_1_1 && properties.limits.minStorageBufferOffsetAlignment<=64);
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
   puts("ASR_QUANT_PUBLIC_FEATURE int8=1 storage8=1 storage16=1 robust=1 float16=0 int16=0"); fflush(stdout);
   const struct vk_device_dispatch_table *d=&inside->vk.dispatch_table;
   REQUIRE(properties.limits.maxPerStageDescriptorStorageBuffers>=3 && properties.limits.maxDescriptorSetStorageBuffers>=3);
   REQUIRE(properties.limits.maxPushConstantsSize>=128 && properties.limits.maxComputeWorkGroupInvocations>=128);

 REQUIRE(properties.limits.maxComputeWorkGroupSize[0]>=(mv?128u:64u)&&properties.limits.maxComputeSharedMemorySize>=4096);
 REQUIRE(properties.limits.minStorageBufferOffsetAlignment&&GUARD*4%properties.limits.minStorageBufferOffsetAlignment==0);
 VkDescriptorSetLayoutBinding bindings[3];for(unsigned i=0;i<3;i++)bindings[i]=(VkDescriptorSetLayoutBinding){.binding=i,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
 VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=3,.pBindings=bindings};VkDescriptorSetLayout sl;SUCCESS(d->CreateDescriptorSetLayout(device,&li,NULL,&sl));
 VkPushConstantRange pr={.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,.size=68};VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&sl,.pushConstantRangeCount=1,.pPushConstantRanges=&pr};VkPipelineLayout layout;SUCCESS(d->CreatePipelineLayout(device,&pli,NULL,&layout));
 size_t bytes;uint32_t *code=read_spirv(argv[2],&bytes);REQUIRE(code&&bytes>=20&&code[0]==0x07230203u&&code[1]==0x00010000u);VkShaderModuleCreateInfo mi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=bytes,.pCode=code};VkShaderModule module;SUCCESS(d->CreateShaderModule(device,&mi,NULL,&module));free(code);
 VkComputePipelineCreateInfo pci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"},.layout=layout};VkPipeline pipeline;SUCCESS(d->CreateComputePipelines(device,VK_NULL_HANDLE,1,&pci,NULL,&pipeline));d->DestroyShaderModule(device,module,NULL);
 struct rd_pipeline *p=rd_pipeline_from_handle(pipeline);struct ir3_shader_variant *v=p->variant;
 printf("ASR_QUANT_NATIVE type=%u mode=%s descriptors=%u buffers=%u images=%u logical=%u,%u,%u physical=%u,%u,%u packed1d=%u shared=%u native=%u full=%d half=%d private=%u\n",type,argv[4],p->descriptor_count,p->buffer_count,p->resource_usage.image_count,p->logical_local_size[0],p->logical_local_size[1],p->logical_local_size[2],v->local_size[0],v->local_size[1],v->local_size[2],p->packed_1d_barrier,v->shared_size,v->instrlen,v->info.max_reg+1,v->info.max_half_reg+1,v->pvtmem_size);fflush(stdout);
 REQUIRE(p->descriptor_count==3&&p->buffer_count==3&&!p->resource_usage.image_count&&p->logical_local_size[0]==(mv?128u:64u)&&p->logical_local_size[1]==1&&p->logical_local_size[2]==1&&p->packed_1d_barrier&&v->local_size[0]==(mv?64u:32u)&&v->local_size[1]==2&&v->local_size[2]==1&&v->shared_size==(mv?512u:4096u));
 printf("ASR_QUANT_COMPILED type=%u mode=%s descriptors=3 push=68 local=%u,1,1 shared=%u native=%u full=%d half=%d private=%u before_data=1\n",type,argv[4],p->logical_local_size[0],v->shared_size,v->instrlen,v->info.max_reg+1,v->info.max_half_reg+1,v->pvtmem_size);fflush(stdout);
 if(argc==6){d->DestroyPipeline(device,pipeline,NULL);d->DestroyPipelineLayout(device,layout,NULL);d->DestroyDescriptorSetLayout(device,sl,NULL);d->DestroyDevice(device,NULL);DestroyInstance(instance,NULL);dlclose(lib);puts("PASS ASR_QUANT compile-only: actual selected module/68-byte ABI; no data or dispatch");return 0;}
 const unsigned qk=block_k(type),qb=block_bytes(type),a_stride=(ROWS*K/qk+1)*qb,b_stride=cols*K+32,d_stride=cols*ROWS+13;
 const uint32_t sizes[3]={A_BATCHES*a_stride,B_BATCHES*b_stride*4,B_BATCHES*d_stride*4};struct storage stores[3]={0};
 for(unsigned j=0;j<3;j++){REQUIRE(sizes[j]<=properties.limits.maxStorageBufferRange);REQUIRE(!storage_create(d,device,&memory_properties,sizes[j]+128,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&stores[j]));}
 VkDescriptorPoolSize ps={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,3};VkDescriptorPoolCreateInfo dpi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&ps};VkDescriptorPool pool;SUCCESS(d->CreateDescriptorPool(device,&dpi,NULL,&pool));VkDescriptorSetAllocateInfo da={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&sl};VkDescriptorSet set;SUCCESS(d->AllocateDescriptorSets(device,&da,&set));
 VkDescriptorBufferInfo infos[3];VkWriteDescriptorSet writes[3];VkMappedMemoryRange ranges[3];for(unsigned j=0;j<3;j++){infos[j]=(VkDescriptorBufferInfo){stores[j].buffer,GUARD*4,sizes[j]};writes[j]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&infos[j]};ranges[j]=(VkMappedMemoryRange){.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=stores[j].memory,.size=VK_WHOLE_SIZE};}d->UpdateDescriptorSets(device,3,writes,0,NULL);
 VkQueue queue;d->GetDeviceQueue(device,0,0,&queue);
 for(unsigned round=0;round<2;round++){
  for(unsigned j=0;j<3;j++)for(size_t i=0;i<stores[j].words;i++)stores[j].map[i]=poison(i,160+j,round);
  uint8_t *a=(uint8_t *)(stores[0].map+stores[0].binding_words+GUARD);
  for(unsigned batch=0;batch<A_BATCHES;batch++)for(unsigned row=0;row<ROWS;row++)for(unsigned block=0;block<K/qk;block++)make_block(a+batch*a_stride+(row*K/qk+block)*qb,type,round*97+batch*61+row*7+block*13);
  for(unsigned batch=0;batch<B_BATCHES;batch++)for(unsigned col=0;col<cols;col++)for(unsigned k=0;k<K;k++)stores[1].map[stores[1].binding_words+GUARD+batch*b_stride+col*K+k]=float_word(vector_value(batch,col,k,round));
  for(unsigned j=0;j<3;j++)memcpy(stores[j].saved,stores[j].map,stores[j].words*4);
  for(unsigned batch=0;batch<B_BATCHES;batch++)for(unsigned col=0;col<cols;col++)for(unsigned row=0;row<ROWS;row++){
   double sum=0;for(unsigned k=0;k<K;k++)sum+=(double)weight(a+(batch/2)*a_stride+(row*K/qk+k/qk)*qb,type,k%qk)*vector_value(batch,col,k,round);
   stores[2].saved[stores[2].binding_words+GUARD+batch*d_stride+col*ROWS+row]=float_word((float)sum);
  }
  SUCCESS(d->FlushMappedMemoryRanges(device,3,ranges));
  VkCommandPoolCreateInfo cpi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};VkCommandPool cp;SUCCESS(d->CreateCommandPool(device,&cpi,NULL,&cp));VkCommandBufferAllocateInfo cai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=cp,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer command;SUCCESS(d->AllocateCommandBuffers(device,&cai,&command));VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};SUCCESS(d->BeginCommandBuffer(command,&begin));
  VkMemoryBarrier before={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&before,0,NULL,0,NULL);d->CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);d->CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);
  uint32_t push[17]={ROWS,cols,K,K,K,ROWS,a_stride/qb*qk,b_stride,d_stride,0,B_BATCHES,1,A_BATCHES,B_BATCHES,2,1,cols};
  for(unsigned part=0;part<(round?2u:1u);part++){push[9]=round?part*2:0;d->CmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),push);d->CmdDispatch(command,mv?(ROWS+3)/4:(ROWS+15)/16,mv?cols:(cols+15)/16,round?2:B_BATCHES);}
  VkMemoryBarrier after={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&after,0,NULL,0,NULL);SUCCESS(d->EndCommandBuffer(command));VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;SUCCESS(d->CreateFence(device,&fi,NULL,&fence));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};struct rd_device *inside=rd_device_from_handle(device);const uint64_t submits_before=inside->submit_count;const uint32_t flushes_before=inside->seqno;SUCCESS(d->QueueSubmit(queue,1,&submit,fence));SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,UINT64_C(5000000000)));const uint64_t actual_submits=inside->submit_count-submits_before;const uint32_t actual_flushes=(uint32_t)(inside->seqno-flushes_before);const unsigned expected_submits=mv?14u:(round?4u:3u);REQUIRE(actual_submits==expected_submits&&actual_flushes==expected_submits);printf("ASR_QUANT_BATCH_PASS type=%u mode=%s round=%u submits=%llu flushes=%u expected=%u one_workgroup_per_chunk=1 max_chunks=8\n",type,argv[4],round,(unsigned long long)actual_submits,actual_flushes,expected_submits);fflush(stdout);SUCCESS(d->InvalidateMappedMemoryRanges(device,3,ranges));
  for(unsigned j=0;j<3;j++)for(size_t i=0;i<stores[j].words;i++)if(stores[j].map[i]!=stores[j].saved[i]){fprintf(stderr,"FAIL ASR_QUANT type=%u mode=%s round=%u buffer=%u word=%zu actual=%08x expected=%08x\n",type,argv[4],round,j,i,stores[j].map[i],stores[j].saved[i]);return 1;}
  printf("ASR_QUANT_ROUND_PASS type=%u mode=%s round=%u M=33 N=%u K=512 batches=4 gqa=2 outputs=%u full3allocations=1 guards=exact split_z=%u\n",type,argv[4],round,cols,B_BATCHES*cols*ROWS,round?2:1);fflush(stdout);d->DestroyFence(device,fence,NULL);d->DestroyCommandPool(device,cp,NULL);
 }
 d->DestroyDescriptorPool(device,pool,NULL);for(unsigned j=0;j<3;j++){free(stores[j].saved);d->UnmapMemory(device,stores[j].memory);d->DestroyBuffer(device,stores[j].buffer,NULL);d->FreeMemory(device,stores[j].memory,NULL);}d->DestroyPipeline(device,pipeline,NULL);d->DestroyPipelineLayout(device,layout,NULL);d->DestroyDescriptorSetLayout(device,sl,NULL);d->DestroyDevice(device,NULL);DestroyInstance(instance,NULL);dlclose(lib);puts("PASS ASR_QUANT: actual quantized phase module; exact dyadic CPU decode/dot/full3allocation oracle; no timing");return 0;
}
