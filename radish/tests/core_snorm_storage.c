int resource_baseline_main(int argc,char **argv);
#define main resource_baseline_main
#include "core_resources.c"
#undef main

enum { TYPES=4, LANES=8, RESULT_WORDS=LANES*16, OOB_WORDS=8, TOTAL_WORDS=RESULT_WORDS+OOB_WORDS };
static const VkFormat formats[TYPES]={VK_FORMAT_A8B8G8R8_UNORM_PACK32,
 VK_FORMAT_R8G8B8A8_SNORM,VK_FORMAT_A8B8G8R8_UINT_PACK32,VK_FORMAT_A8B8G8R8_SINT_PACK32};
static uint32_t packed(unsigned j,unsigned lane,unsigned round,bool store) {
 int c[4];
 if(store) {
  const int fixed[2][4]={{0,255,0,255},{-127,0,127,-127}};
  for(unsigned i=0;i<4;i++) c[i]=j<2?fixed[j][i]:j==2?(int[]){(int)lane,63+(int)lane,127+(int)lane,247+(int)lane}[i]:
    (int[]){-120+(int)lane,-1,0,120-(int)lane}[i];
 } else for(unsigned i=0;i<4;i++) c[i]=j==0?(int)((lane*17+i*53+round*31)&255):
   j==1?(int)((lane*19+i*37+round*11)%255)-127:j==2?(int)((lane*23+i*61+round*7)&255):
   (int)((lane*13+i*47+round*17)%241)-120;
 if(!store && j==1 && lane==0) { const int endpoints[4]={-128,-127,0,127};for(unsigned i=0;i<4;i++)c[i]=endpoints[i]; }
 uint32_t w=0; for(unsigned i=0;i<4;i++) w|=(uint32_t)(c[i]&255)<<(8*i); return w;
}
int main(int argc,char **argv) {
 REQUIRE(argc==3 || (argc==4 && !strcmp(argv[3],"--compile-only")));
 void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); REQUIRE(lib);
 PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr"); REQUIRE(gipa);
 VkInstance instance=VK_NULL_HANDLE; INSTANCE_FN(CreateInstance);
 VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_0};
 VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
 SUCCESS(CreateInstance(&ici,NULL,&instance));
 INSTANCE_FN(EnumeratePhysicalDevices); INSTANCE_FN(GetPhysicalDeviceMemoryProperties);
 INSTANCE_FN(GetPhysicalDeviceProperties); INSTANCE_FN(GetPhysicalDeviceFormatProperties);
 INSTANCE_FN(GetPhysicalDeviceFeatures); INSTANCE_FN(CreateDevice); INSTANCE_FN(DestroyInstance);
 VkPhysicalDevice physical; uint32_t count=1; SUCCESS(EnumeratePhysicalDevices(instance,&count,&physical)); REQUIRE(count==1);
 VkPhysicalDeviceMemoryProperties memory; GetPhysicalDeviceMemoryProperties(physical,&memory);
 VkPhysicalDeviceProperties properties; GetPhysicalDeviceProperties(physical,&properties);
 REQUIRE(properties.limits.maxPerStageDescriptorStorageImages>=4 && properties.limits.maxPerStageDescriptorStorageBuffers>=1);
 for(unsigned j=0;j<TYPES;j++) { VkFormatProperties fp; GetPhysicalDeviceFormatProperties(physical,formats[j],&fp);
  REQUIRE(fp.bufferFeatures&VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT); }
 VkPhysicalDeviceFeatures supported; GetPhysicalDeviceFeatures(physical,&supported); REQUIRE(supported.robustBufferAccess);
 VkPhysicalDeviceFeatures enabled={.robustBufferAccess=VK_TRUE};
 float priority=1; VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueCount=1,.pQueuePriorities=&priority};
 VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi,.pEnabledFeatures=&enabled};
 VkDevice device; SUCCESS(CreateDevice(physical,&di,NULL,&device));
 const struct vk_device_dispatch_table *d=&rd_device_from_handle(device)->vk.dispatch_table;
 VkDescriptorSetLayoutBinding bindings[5]; for(unsigned i=0;i<5;i++) bindings[i]=(VkDescriptorSetLayoutBinding){.binding=i,
  .descriptorType=i<4?VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
 VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=5,.pBindings=bindings};
 VkDescriptorSetLayout sl; SUCCESS(d->CreateDescriptorSetLayout(device,&li,NULL,&sl));
 VkPipelineLayoutCreateInfo pi={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&sl};
 VkPipelineLayout layout; SUCCESS(d->CreatePipelineLayout(device,&pi,NULL,&layout));
 size_t bytes; uint32_t *code=read_spirv(argv[2],&bytes); REQUIRE(code);
 VkShaderModuleCreateInfo mi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=bytes,.pCode=code};
 VkShaderModule module; SUCCESS(d->CreateShaderModule(device,&mi,NULL,&module)); free(code);
 VkComputePipelineCreateInfo pci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
  .stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"},.layout=layout};
 VkPipeline pipeline; SUCCESS(d->CreateComputePipelines(device,VK_NULL_HANDLE,1,&pci,NULL,&pipeline)); d->DestroyShaderModule(device,module,NULL);
 struct rd_pipeline *p=rd_pipeline_from_handle(pipeline); printf("PACKED_COMPILED active=%u images=%u buffers=%u native=%u\n",p->descriptor_count,p->resource_usage.image_count,p->buffer_count,p->variant->instrlen); fflush(stdout);
 if(argc==4) { d->DestroyPipeline(device,pipeline,NULL);d->DestroyPipelineLayout(device,layout,NULL);d->DestroyDescriptorSetLayout(device,sl,NULL);
  d->DestroyDevice(device,NULL);DestroyInstance(instance,NULL);dlclose(lib);puts("PASS SNORM_CORE compile-only");return 0; }
 struct storage stores[5]={0}; VkBufferView views[4];
 for(unsigned j=0;j<5;j++) REQUIRE(!storage_create(d,device,&memory,GUARD*4+(j<4?64:TOTAL_WORDS*4)+64,
   j<4?VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT:VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&stores[j]));
 for(unsigned j=0;j<4;j++) { VkBufferViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,.buffer=stores[j].buffer,.format=formats[j],.offset=GUARD*4,.range=64};
  SUCCESS(d->CreateBufferView(device,&vi,NULL,&views[j])); }
 VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,4},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
 VkDescriptorPoolCreateInfo dpi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=2,.pPoolSizes=sizes};
 VkDescriptorPool pool; SUCCESS(d->CreateDescriptorPool(device,&dpi,NULL,&pool));
 VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&sl};
 VkDescriptorSet set; SUCCESS(d->AllocateDescriptorSets(device,&ai,&set));
 VkDescriptorBufferInfo bi={stores[4].buffer,GUARD*4,TOTAL_WORDS*4}; VkWriteDescriptorSet writes[5];
 for(unsigned j=0;j<5;j++) writes[j]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=j,.descriptorCount=1,
  .descriptorType=bindings[j].descriptorType,.pTexelBufferView=j<4?&views[j]:NULL,.pBufferInfo=j==4?&bi:NULL};
 VkCommandPoolCreateInfo cp={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; VkCommandPool command_pool;SUCCESS(d->CreateCommandPool(device,&cp,NULL,&command_pool));
 VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=command_pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
 VkCommandBuffer command; SUCCESS(d->AllocateCommandBuffers(device,&ca,&command));
 VkQueue queue; d->GetDeviceQueue(device,0,0,&queue); VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence;SUCCESS(d->CreateFence(device,&fi,NULL,&fence));
 VkMappedMemoryRange ranges[5];for(unsigned j=0;j<5;j++) ranges[j]=(VkMappedMemoryRange){.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=stores[j].memory,.size=VK_WHOLE_SIZE};
 for(unsigned round=0;round<2;round++) {
  for(unsigned j=0;j<5;j++) {for(size_t i=0;i<stores[j].words;i++)stores[j].map[i]=poison(i,40+j,round);
   if(j<4)for(unsigned i=0;i<8;i++)stores[j].map[stores[j].binding_words+GUARD+i]=packed(j,i,round,false);
   memcpy(stores[j].saved,stores[j].map,stores[j].words*4); }
  for(unsigned j=0;j<4;j++)for(unsigned lane=0;lane<8;lane++) {
   uint32_t w=packed(j,lane,round,false);for(unsigned c=0;c<4;c++) {uint32_t x=(w>>(8*c))&255;
    if((j==1||j==3)&&(x&128))x|=0xffffff00u;
    if(j==1 && x==0xffffff80u)x=0xffffff81u;
    stores[4].saved[stores[4].binding_words+GUARD+lane*16+j*4+c]=x; }
   stores[j].saved[stores[j].binding_words+GUARD+lane+8]=packed(j,lane,round,true);
  }
  for(unsigned i=0;i<OOB_WORDS;i++)stores[4].saved[stores[4].binding_words+GUARD+RESULT_WORDS+i]=0;
  SUCCESS(d->FlushMappedMemoryRanges(device,5,ranges));SUCCESS(d->ResetCommandPool(device,command_pool,0));d->UpdateDescriptorSets(device,5,writes,0,NULL);
  VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};SUCCESS(d->BeginCommandBuffer(command,&begin));
  VkMemoryBarrier before={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
  d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&before,0,NULL,0,NULL);
  d->CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);d->CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);d->CmdDispatch(command,LANES,1,1);
  VkMemoryBarrier after={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
  d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&after,0,NULL,0,NULL);
  SUCCESS(d->EndCommandBuffer(command));SUCCESS(d->ResetFences(device,1,&fence));
  VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};SUCCESS(d->QueueSubmit(queue,1,&submit,fence));
  SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));SUCCESS(d->InvalidateMappedMemoryRanges(device,5,ranges));
  for(unsigned j=0;j<5;j++)for(size_t i=0;i<stores[j].words;i++) if(stores[j].map[i]!=stores[j].saved[i]) {
   fprintf(stderr,"FAIL PACKED_STORAGE round=%u buffer=%u word=%zu actual=%08x expected=%08x\n",round,j,i,stores[j].map[i],stores[j].saved[i]);return 1; }
  printf("PACKED_ROUND_PASS round=%u outputs=%u typed_stores=32 full5allocations=exact\n",round,RESULT_WORDS);fflush(stdout);
 }
 d->DestroyFence(device,fence,NULL);d->DestroyCommandPool(device,command_pool,NULL);d->DestroyDescriptorPool(device,pool,NULL);
 for(unsigned j=0;j<5;j++) {if(j<4)d->DestroyBufferView(device,views[j],NULL);free(stores[j].saved);d->UnmapMemory(device,stores[j].memory);d->DestroyBuffer(device,stores[j].buffer,NULL);d->FreeMemory(device,stores[j].memory,NULL);}
 d->DestroyPipeline(device,pipeline,NULL);d->DestroyPipelineLayout(device,layout,NULL);d->DestroyDescriptorSetLayout(device,sl,NULL);d->DestroyDevice(device,NULL);DestroyInstance(instance,NULL);dlclose(lib);
 puts("PASS SNORM_CORE: underlyingRGBA8_SNORM−128/−127/0/127 endpoints and OOB,272 exact outputs+64 typed stores/full5allocation guards");return 0;
}
