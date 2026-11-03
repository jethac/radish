int resource_baseline_main(int argc,char **argv);
#define main resource_baseline_main
#include "core_resources.c"
#undef main
#include <math.h>
static float word_float(uint32_t word) { float value; memcpy(&value,&word,4); return value; }
static float mask_value(unsigned row,unsigned col) { return col%7==6?-INFINITY:-(float)(row%5)/8.0f; }
static uint16_t mask_half(unsigned row,unsigned col) {
 static const uint16_t values[5]={0,0xb000,0xb400,0xb600,0xb800};
 return col%7==6?0xfc00:values[row%5];
}
static float input_value(unsigned row,unsigned col,unsigned config) {
 return (float)((int)((col*17+row*31+config*7)%65)-32)/8.0f;
}
static void cpu_reference(float *out,unsigned width,unsigned config) {
 const bool masked=config==1||config==3,sinks=config>=2,alibi=config==3;
 for(unsigned row=0;row<12;row++){
  const unsigned head=row/2,position=row%2;
  const float slope=alibi?powf(head<4?0.25f:0.5f,head<4?(float)(head+1):(float)(2*(head-4)+1)):1.0f;
  const float sink=((float)head-3.0f)/4.0f;float values[1024],exps[1024],maximum=sinks?sink:-INFINITY;
  for(unsigned col=0;col<width;col++){
   values[col]=input_value(row,col,config)*0.5f+(masked?slope*mask_value(position+2*(head%3),col):0.0f);
   maximum=fmaxf(maximum,values[col]);
  }
  double sum=0;for(unsigned col=0;col<width;col++){exps[col]=expf(values[col]-maximum);sum+=(double)exps[col];}
  if(sinks)sum+=(double)expf(sink-maximum);
  const float inverse=(float)(1.0/sum);
  for(unsigned col=0;col<width;col++)out[row*width+col]=exps[col]*inverse;
 }
}
static int make_pipeline(const struct vk_device_dispatch_table *d,VkDevice device,VkPipelineLayout layout,const char *path,unsigned index,VkPipeline *result) {
 size_t size;uint32_t *code=read_spirv(path,&size);REQUIRE(code&&size>=20&&code[1]==0x00010000u);
 VkShaderModuleCreateInfo mi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=size,.pCode=code};VkShaderModule module;SUCCESS(d->CreateShaderModule(device,&mi,NULL,&module));free(code);
 const uint32_t block=32;VkSpecializationMapEntry entry={.constantID=0,.size=4};VkSpecializationInfo spec={.mapEntryCount=1,.pMapEntries=&entry,.dataSize=4,.pData=&block};
 VkComputePipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main",.pSpecializationInfo=&spec},.layout=layout};VkPipeline pipeline;SUCCESS(d->CreateComputePipelines(device,VK_NULL_HANDLE,1,&pi,NULL,&pipeline));d->DestroyShaderModule(device,module,NULL);
 struct rd_pipeline *p=rd_pipeline_from_handle(pipeline);struct ir3_shader_variant *v=p->variant;
 REQUIRE(p->descriptor_count==4&&p->buffer_count==4&&p->logical_local_size[0]==32&&p->logical_local_size[1]==1&&p->logical_local_size[2]==1&&p->packed_1d_barrier&&v->local_size[0]==16&&v->local_size[1]==2&&v->local_size[2]==1&&v->shared_size==128);
 printf("SOFTMAX_NATIVE index=%u native=%u full=%d half=%d private=%u private_sp=%u shared=%u descriptors=4 push=68 spec=32 before_data=1\n",index,v->instrlen,v->info.max_reg+1,v->info.max_half_reg+1,v->pvtmem_size,p->private_per_sp,v->shared_size);fflush(stdout);*result=pipeline;return 0;
}
int main(int argc,char **argv) {
 REQUIRE(argc==5||(argc==6&&!strcmp(argv[5],"--compile-only")));
 REQUIRE(!strcmp(argv[4],"f32")||!strcmp(argv[4],"f16"));bool half_mask=!strcmp(argv[4],"f16");
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
   puts("ASR_SOFTMAX_PUBLIC_FEATURE int8=1 storage8=1 storage16=1 robust=1 float16=0 int16=0"); fflush(stdout);
   const struct vk_device_dispatch_table *d=&inside->vk.dispatch_table;
   REQUIRE(properties.limits.maxPerStageDescriptorStorageBuffers>=4 && properties.limits.maxDescriptorSetStorageBuffers>=4);
   REQUIRE(properties.limits.maxPushConstantsSize>=128 && properties.limits.maxComputeWorkGroupInvocations>=128);

 VkDescriptorSetLayoutBinding bindings[4];for(unsigned i=0;i<4;i++)bindings[i]=(VkDescriptorSetLayoutBinding){.binding=i,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
 VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=4,.pBindings=bindings};VkDescriptorSetLayout sl;SUCCESS(d->CreateDescriptorSetLayout(device,&li,NULL,&sl));
 VkPushConstantRange pr={.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,.size=68};VkPipelineLayoutCreateInfo pli={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&sl,.pushConstantRangeCount=1,.pPushConstantRanges=&pr};VkPipelineLayout layout;SUCCESS(d->CreatePipelineLayout(device,&pli,NULL,&layout));
 VkPipeline pipelines[2];for(unsigned j=0;j<2;j++)REQUIRE(!make_pipeline(d,device,layout,argv[2+j],j,&pipelines[j]));
 if(argc==6){for(unsigned j=0;j<2;j++)d->DestroyPipeline(device,pipelines[j],NULL);d->DestroyPipelineLayout(device,layout,NULL);d->DestroyDescriptorSetLayout(device,sl,NULL);d->DestroyDevice(device,NULL);DestroyInstance(instance,NULL);dlclose(lib);puts("PASS SOFTMAX compile-only: old/new actual modules/spec32 before_data=1");return 0;}
 const unsigned widths[]={1,9,17,33,64,67,76,128,256,320,336,512,1024};unsigned failures=0;
 VkQueue queue;d->GetDeviceQueue(device,0,0,&queue);
 for(unsigned wi=0;wi<sizeof(widths)/sizeof(widths[0]);wi++){
  const unsigned width=widths[wi],outputs=12*width;
  const uint32_t sizes[5]={outputs*4,6*width*(half_mask?2:4),6*4,(outputs+width)*4,(outputs+width)*4};struct storage stores[5]={0};
  for(unsigned j=0;j<5;j++){REQUIRE(sizes[j]<=properties.limits.maxStorageBufferRange);REQUIRE(!storage_create(d,device,&memory_properties,sizes[j]+128,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&stores[j]));}
  VkDescriptorPoolSize ps={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,8};VkDescriptorPoolCreateInfo dpi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=2,.poolSizeCount=1,.pPoolSizes=&ps};VkDescriptorPool pool;SUCCESS(d->CreateDescriptorPool(device,&dpi,NULL,&pool));VkDescriptorSetLayout layouts[2]={sl,sl};VkDescriptorSetAllocateInfo da={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=2,.pSetLayouts=layouts};VkDescriptorSet sets[2];SUCCESS(d->AllocateDescriptorSets(device,&da,sets));
  VkDescriptorBufferInfo infos[8];VkWriteDescriptorSet writes[8];for(unsigned k=0;k<2;k++)for(unsigned j=0;j<4;j++){unsigned i=k*4+j,slot=j<3?j:3+k;infos[i]=(VkDescriptorBufferInfo){stores[slot].buffer,GUARD*4,sizes[slot]};writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=sets[k],.dstBinding=j,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&infos[i]};}d->UpdateDescriptorSets(device,8,writes,0,NULL);
  VkMappedMemoryRange ranges[5];for(unsigned j=0;j<5;j++)ranges[j]=(VkMappedMemoryRange){.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=stores[j].memory,.size=VK_WHOLE_SIZE};
  for(unsigned config=0;config<4;config++){
   for(unsigned j=0;j<5;j++)for(size_t i=0;i<stores[j].words;i++)stores[j].map[i]=poison(i,210+j,config);
   for(unsigned row=0;row<12;row++)for(unsigned col=0;col<width;col++)stores[0].map[stores[0].binding_words+GUARD+row*width+col]=float_word(input_value(row,col,config));
   uint8_t *mask=(uint8_t *)(stores[1].map+stores[1].binding_words+GUARD);
   for(unsigned row=0;row<6;row++)for(unsigned col=0;col<width;col++){
    if(half_mask){uint16_t value=mask_half(row,col);memcpy(mask+(row*width+col)*2,&value,2);}else{uint32_t value=float_word(mask_value(row,col));memcpy(mask+(row*width+col)*4,&value,4);}
   }
   for(unsigned head=0;head<6;head++)stores[2].map[stores[2].binding_words+GUARD+head]=float_word(((float)head-3.0f)/4.0f);
   for(unsigned j=0;j<5;j++)memcpy(stores[j].saved,stores[j].map,stores[j].words*4);
   SUCCESS(d->FlushMappedMemoryRanges(device,5,ranges));
   VkCommandPoolCreateInfo cpi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};VkCommandPool cp;SUCCESS(d->CreateCommandPool(device,&cpi,NULL,&cp));VkCommandBufferAllocateInfo cai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=cp,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer command;SUCCESS(d->AllocateCommandBuffers(device,&cai,&command));VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};SUCCESS(d->BeginCommandBuffer(command,&begin));
   VkMemoryBarrier before={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&before,0,NULL,0,NULL);
   const bool masked=config==1||config==3,sinks=config>=2;uint32_t push[17]={width,masked?6*width:0,width,2,6,3,1,width,2*width,6*width,float_word(0.5f),float_word(config==3?8.0f:0.0f),float_word(0.25f),float_word(0.5f),4,12,sinks};
   for(unsigned j=0;j<2;j++){d->CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipelines[j]);d->CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&sets[j],0,NULL);d->CmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),push);d->CmdDispatch(command,13,1,1);}
   VkMemoryBarrier after={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&after,0,NULL,0,NULL);SUCCESS(d->EndCommandBuffer(command));VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;SUCCESS(d->CreateFence(device,&fi,NULL,&fence));VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};SUCCESS(d->QueueSubmit(queue,1,&submit,fence));SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,UINT64_C(5000000000)));SUCCESS(d->InvalidateMappedMemoryRanges(device,5,ranges));
   for(unsigned j=0;j<5;j++)for(size_t i=0;i<stores[j].words;i++){
    const size_t start=stores[j].binding_words+GUARD;bool output=j>=3&&i>=start&&i<start+outputs;
    if(!output&&stores[j].map[i]!=stores[j].saved[i]){fprintf(stderr,"FAIL SOFTMAX guard/input width=%u config=%u buffer=%u word=%zu\n",width,config,j,i);return 1;}
   }
   float reference[12*1024];cpu_reference(reference,width,config);unsigned diffs=0,nonfinite=0;double errors[2]={0},norm=0,maxdiff=0;
   for(unsigned i=0;i<outputs;i++){
    uint32_t words[2]={stores[3].map[stores[3].binding_words+GUARD+i],stores[4].map[stores[4].binding_words+GUARD+i]};float values[2]={word_float(words[0]),word_float(words[1])};
    if(words[0]!=words[1]){if(diffs<8)fprintf(stderr,"SOFTMAX_BIT_DIFF width=%u config=%u index=%u old=%08x new=%08x cpu=%.9g\n",width,config,i,words[0],words[1],reference[i]);diffs++;}
    norm+=(double)reference[i]*reference[i];for(unsigned j=0;j<2;j++){if(!isfinite(values[j]))nonfinite++;double e=(double)values[j]-reference[i];errors[j]+=e*e;}double e=fabs((double)values[0]-values[1]);if(e>maxdiff)maxdiff=e;
   }
   REQUIRE(norm>0);double old_nmse=errors[0]/norm,new_nmse=errors[1]/norm;bool pass=!diffs&&!nonfinite&&old_nmse<=1e-6&&new_nmse<=1e-6;if(!pass)failures++;
   printf("SOFTMAX_CASE width=%u mask=%s config=%u outputs=%u bit_diffs=%u nonfinite=%u max_old_new_abs=%.12g old_cpu_nmse=%.12g new_cpu_nmse=%.12g cpu_nmse_limit=1e-6 full5allocations=1 guards=exact result=%s\n",width,argv[4],config,outputs,diffs,nonfinite,maxdiff,old_nmse,new_nmse,pass?"PASS":"FAIL");fflush(stdout);
   d->DestroyFence(device,fence,NULL);d->DestroyCommandPool(device,cp,NULL);
  }
  d->DestroyDescriptorPool(device,pool,NULL);for(unsigned j=0;j<5;j++){free(stores[j].saved);d->UnmapMemory(device,stores[j].memory);d->DestroyBuffer(device,stores[j].buffer,NULL);d->FreeMemory(device,stores[j].memory,NULL);}
 }
 for(unsigned j=0;j<2;j++)d->DestroyPipeline(device,pipelines[j],NULL);d->DestroyPipelineLayout(device,layout,NULL);d->DestroyDescriptorSetLayout(device,sl,NULL);d->DestroyDevice(device,NULL);DestroyInstance(instance,NULL);dlclose(lib);
 printf("SOFTMAX_COMPARE_DONE mask=%s cases=52 failures=%u timing_collected=false\n",argv[4],failures);return failures?1:0;
}
