int resource_baseline_main(int argc,char **argv);
#define main resource_baseline_main
#include "core_resources.c"
#undef main

enum { TIMELINE_WORDS=32 };
static VkResult wait_value(PFN_vkWaitSemaphoresKHR wait_fn,VkDevice device,
                          VkSemaphore semaphore,uint64_t value,uint64_t timeout)
{
   VkSemaphoreWaitInfoKHR info={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR,
      .semaphoreCount=1,.pSemaphores=&semaphore,.pValues=&value};
   return wait_fn(device,&info,timeout);
}
static VkResult signal_value(PFN_vkSignalSemaphoreKHR signal_fn,VkDevice device,
                            VkSemaphore semaphore,uint64_t value)
{
   VkSemaphoreSignalInfoKHR info={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO_KHR,
      .semaphore=semaphore,.value=value};
   return signal_fn(device,&info);
}

int main(int argc,char **argv)
{
   if(argc!=3 && !(argc==4 && !strcmp(argv[3],"--compile-only"))) {
      fprintf(stderr,"Usage: %s ICD-library timeline.spv [--compile-only]\n",argv[0]);return 1;
   }
   void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!lib){fprintf(stderr,"%s\n",dlerror());return 1;}
   PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr");REQUIRE(gipa);
   VkInstance instance=VK_NULL_HANDLE;INSTANCE_FN(CreateInstance);
   const char *instance_exts[]={VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME};
   VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_0};
   VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app,
      .enabledExtensionCount=1,.ppEnabledExtensionNames=instance_exts};
   SUCCESS(CreateInstance(&ici,NULL,&instance));
   INSTANCE_FN(EnumeratePhysicalDevices);INSTANCE_FN(GetPhysicalDeviceFeatures2KHR);
   INSTANCE_FN(GetPhysicalDeviceProperties2KHR);INSTANCE_FN(GetPhysicalDeviceMemoryProperties);
   INSTANCE_FN(EnumerateDeviceExtensionProperties);INSTANCE_FN(CreateDevice);INSTANCE_FN(DestroyInstance);
   INSTANCE_FN(GetDeviceProcAddr);
   VkPhysicalDevice physical;uint32_t count=1;
   SUCCESS(EnumeratePhysicalDevices(instance,&count,&physical));REQUIRE(count==1);
   VkPhysicalDeviceTimelineSemaphoreFeaturesKHR queried={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR};
   VkPhysicalDeviceFeatures2KHR qfeatures={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR,.pNext=&queried};
   GetPhysicalDeviceFeatures2KHR(physical,&qfeatures);REQUIRE(queried.timelineSemaphore);
   VkPhysicalDeviceTimelineSemaphorePropertiesKHR tl_props={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES_KHR};
   VkPhysicalDeviceProperties2KHR props={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR,.pNext=&tl_props};
   GetPhysicalDeviceProperties2KHR(physical,&props);
   REQUIRE(props.properties.apiVersion==VK_API_VERSION_1_0 && tl_props.maxTimelineSemaphoreValueDifference>=UINT64_C(0x100000020));
   count=0;SUCCESS(EnumerateDeviceExtensionProperties(physical,NULL,&count,NULL));
   VkExtensionProperties *extensions=calloc(count,sizeof(*extensions));REQUIRE(extensions);
   SUCCESS(EnumerateDeviceExtensionProperties(physical,NULL,&count,extensions));bool found=false;
   for(uint32_t i=0;i<count;i++)found|=!strcmp(extensions[i].extensionName,VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
   free(extensions);REQUIRE(found);
   const char *device_exts[]={VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME};
   VkPhysicalDeviceTimelineSemaphoreFeaturesKHR requested={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR,.timelineSemaphore=VK_TRUE};
   VkPhysicalDeviceFeatures core_features={0};float priority=1;
   VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueCount=1,.pQueuePriorities=&priority};
   VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&requested,
      .queueCreateInfoCount=1,.pQueueCreateInfos=&qci,.enabledExtensionCount=1,
      .ppEnabledExtensionNames=device_exts,.pEnabledFeatures=&core_features};
   VkDevice device;SUCCESS(CreateDevice(physical,&dci,NULL,&device));
   struct rd_device *inside=rd_device_from_handle(device);REQUIRE(inside->vk.enabled_features.timelineSemaphore);
   const struct vk_device_dispatch_table *d=&inside->vk.dispatch_table;
   PFN_vkGetSemaphoreCounterValueKHR get_counter=(PFN_vkGetSemaphoreCounterValueKHR)GetDeviceProcAddr(device,"vkGetSemaphoreCounterValueKHR");
   PFN_vkWaitSemaphoresKHR wait_fn=(PFN_vkWaitSemaphoresKHR)GetDeviceProcAddr(device,"vkWaitSemaphoresKHR");
   PFN_vkSignalSemaphoreKHR signal_fn=(PFN_vkSignalSemaphoreKHR)GetDeviceProcAddr(device,"vkSignalSemaphoreKHR");
   REQUIRE(get_counter && wait_fn && signal_fn);
   printf("TIMELINE_PUBLIC_FEATURE api=1.0 extension=1 supported=1 enabled=1 aliases=KHR max_difference=%"PRIu64"\n",tl_props.maxTimelineSemaphoreValueDifference);fflush(stdout);
   VkDescriptorSetLayoutBinding binding={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
   VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};
   VkDescriptorSetLayout set_layout;SUCCESS(d->CreateDescriptorSetLayout(device,&li,NULL,&set_layout));
   VkPushConstantRange push_range={.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,.size=4};
   VkPipelineLayoutCreateInfo pi={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&set_layout,.pushConstantRangeCount=1,.pPushConstantRanges=&push_range};
   VkPipelineLayout layout;SUCCESS(d->CreatePipelineLayout(device,&pi,NULL,&layout));
   size_t bytes;uint32_t *code=read_spirv(argv[2],&bytes);REQUIRE(code);
   VkShaderModuleCreateInfo mi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=bytes,.pCode=code};
   VkShaderModule module;SUCCESS(d->CreateShaderModule(device,&mi,NULL,&module));free(code);
   VkComputePipelineCreateInfo pipeline_info={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"},.layout=layout};
   VkPipeline pipeline;SUCCESS(d->CreateComputePipelines(device,VK_NULL_HANDLE,1,&pipeline_info,NULL,&pipeline));d->DestroyShaderModule(device,module,NULL);
   struct rd_pipeline *compiled=rd_pipeline_from_handle(pipeline);
   printf("TIMELINE_COMPILED native=%u full=%d half=%d private=%u physical=%u,%u,%u\n",compiled->variant->instrlen,compiled->variant->info.max_reg+1,compiled->variant->info.max_half_reg+1,compiled->variant->pvtmem_size,compiled->variant->local_size[0],compiled->variant->local_size[1],compiled->variant->local_size[2]);fflush(stdout);
   if(argc==4){d->DestroyPipeline(device,pipeline,NULL);d->DestroyPipelineLayout(device,layout,NULL);d->DestroyDescriptorSetLayout(device,set_layout,NULL);d->DestroyDevice(device,NULL);DestroyInstance(instance,NULL);dlclose(lib);puts("PASS TIMELINE compile-only: public feature/KHR aliases and tiny shader, no buffer or dispatch");return 0;}
   VkSemaphoreTypeCreateInfoKHR type={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR,.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE_KHR,.initialValue=7};
   VkSemaphoreCreateInfo sci={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,.pNext=&type};
   VkSemaphore semaphores[2];SUCCESS(d->CreateSemaphore(device,&sci,NULL,&semaphores[0]));type.initialValue=3;SUCCESS(d->CreateSemaphore(device,&sci,NULL,&semaphores[1]));
   uint64_t value=0;SUCCESS(get_counter(device,semaphores[0],&value));REQUIRE(value==7);
   REQUIRE(wait_value(wait_fn,device,semaphores[0],8,UINT64_C(2000000))==VK_TIMEOUT);
   SUCCESS(signal_value(signal_fn,device,semaphores[0],9));SUCCESS(wait_value(wait_fn,device,semaphores[0],9,UINT64_C(5000000000)));
   SUCCESS(get_counter(device,semaphores[0],&value));REQUIRE(value==9);
   uint64_t any_values[2]={10,3};VkSemaphoreWaitInfoKHR any={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR,.flags=VK_SEMAPHORE_WAIT_ANY_BIT_KHR,.semaphoreCount=2,.pSemaphores=semaphores,.pValues=any_values};
   SUCCESS(wait_fn(device,&any,UINT64_C(2000000)));any.flags=0;REQUIRE(wait_fn(device,&any,UINT64_C(2000000))==VK_TIMEOUT);
   SUCCESS(signal_value(signal_fn,device,semaphores[0],10));SUCCESS(wait_fn(device,&any,UINT64_C(5000000000)));
   puts("TIMELINE_HOST_PASS initial=7 timeout=8 host_signal=9 wait_any=1 wait_all=1 final=10");fflush(stdout);
   VkPhysicalDeviceMemoryProperties memory;GetPhysicalDeviceMemoryProperties(physical,&memory);
   struct storage output={0};REQUIRE(storage_create(d,device,&memory,512,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&output)==0);
   for(size_t i=0;i<output.words;i++)output.map[i]=poison(i,81,0);memcpy(output.saved,output.map,output.words*4);
   const uint32_t seed=UINT32_C(0x51a3b7d9);for(unsigned i=0;i<TIMELINE_WORDS;i++)output.saved[output.binding_words+GUARD+i]=(i*17u)^seed;
   VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=output.memory,.size=VK_WHOLE_SIZE};SUCCESS(d->FlushMappedMemoryRanges(device,1,&range));
   VkDescriptorPoolSize pool_size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};VkDescriptorPoolCreateInfo pool_info={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&pool_size};VkDescriptorPool pool;SUCCESS(d->CreateDescriptorPool(device,&pool_info,NULL,&pool));
   VkDescriptorSetAllocateInfo allocate={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&set_layout};VkDescriptorSet set;SUCCESS(d->AllocateDescriptorSets(device,&allocate,&set));
   VkDescriptorBufferInfo buffer_info={output.buffer,GUARD*4,TIMELINE_WORDS*4};VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&buffer_info};d->UpdateDescriptorSets(device,1,&write,0,NULL);
   VkCommandPoolCreateInfo cp={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};VkCommandPool command_pool;SUCCESS(d->CreateCommandPool(device,&cp,NULL,&command_pool));
   VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=command_pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};VkCommandBuffer command;SUCCESS(d->AllocateCommandBuffers(device,&ca,&command));
   VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};SUCCESS(d->BeginCommandBuffer(command,&begin));
   VkMemoryBarrier before={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT};d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&before,0,NULL,0,NULL);
   d->CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);d->CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);d->CmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&seed);d->CmdDispatch(command,1,1,1);
   VkMemoryBarrier after={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&after,0,NULL,0,NULL);SUCCESS(d->EndCommandBuffer(command));
   VkQueue queue;d->GetDeviceQueue(device,0,0,&queue);VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence;SUCCESS(d->CreateFence(device,&fi,NULL,&fence));
   uint64_t release=UINT64_C(0x100000011),completed=release+1;
   VkTimelineSemaphoreSubmitInfoKHR timeline={.sType=VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR,.waitSemaphoreValueCount=1,.pWaitSemaphoreValues=&release,.signalSemaphoreValueCount=1,.pSignalSemaphoreValues=&completed};
   VkPipelineStageFlags wait_stage=VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
   VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.pNext=&timeline,.waitSemaphoreCount=1,.pWaitSemaphores=semaphores,.pWaitDstStageMask=&wait_stage,.commandBufferCount=1,.pCommandBuffers=&command,.signalSemaphoreCount=1,.pSignalSemaphores=semaphores};
   SUCCESS(d->QueueSubmit(queue,1,&submit,fence));
   REQUIRE(d->GetFenceStatus(device,fence)==VK_NOT_READY);REQUIRE(wait_value(wait_fn,device,semaphores[0],completed,UINT64_C(2000000))==VK_TIMEOUT);
   SUCCESS(get_counter(device,semaphores[0],&value));REQUIRE(value==10);
   printf("TIMELINE_QUEUED_WAIT_PENDING release=%"PRIu64" completed=%"PRIu64" current=%"PRIu64"\n",release,completed,value);fflush(stdout);
   SUCCESS(signal_value(signal_fn,device,semaphores[0],release));
   SUCCESS(wait_value(wait_fn,device,semaphores[0],completed,UINT64_C(5000000000)));SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,UINT64_C(5000000000)));SUCCESS(get_counter(device,semaphores[0],&value));REQUIRE(value==completed);
   SUCCESS(d->InvalidateMappedMemoryRanges(device,1,&range));
   for(size_t i=0;i<output.words;i++)if(output.map[i]!=output.saved[i]){fprintf(stderr,"FAIL TIMELINE word=%zu actual=%08x expected=%08x\n",i,output.map[i],output.saved[i]);return 1;}
   printf("TIMELINE_GPU_PASS outputs=32 complete_allocation_words=%zu guards=exact final=%"PRIu64"\n",output.words,value);fflush(stdout);
   d->DestroyFence(device,fence,NULL);d->DestroyCommandPool(device,command_pool,NULL);d->DestroyDescriptorPool(device,pool,NULL);free(output.saved);d->UnmapMemory(device,output.memory);d->DestroyBuffer(device,output.buffer,NULL);d->FreeMemory(device,output.memory,NULL);
   d->DestroySemaphore(device,semaphores[1],NULL);d->DestroySemaphore(device,semaphores[0],NULL);d->DestroyPipeline(device,pipeline,NULL);d->DestroyPipelineLayout(device,layout,NULL);d->DestroyDescriptorSetLayout(device,set_layout,NULL);d->DestroyDevice(device,NULL);DestroyInstance(instance,NULL);dlclose(lib);
   puts("PASS TIMELINE: public KHR initial/counter/host signal/wait/finite timeout/queued wait plus real compute and guarded readback");return 0;
}
