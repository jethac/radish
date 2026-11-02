int resource_baseline_main(int argc,char **argv);
#define main resource_baseline_main
#include "core_resources.c"
#undef main

enum { I8_LANES=32, I8_FIELDS=40, I8_RESULTS=I8_LANES*I8_FIELDS, I8_STORES=2 };
static uint32_t i8_a(unsigned i)
{
   static const unsigned values[8]={0,1,127,128,255,129,254,64};
   return values[i&7u]^((i>>3)*13u);
}
static uint32_t i8_b(unsigned i) { return (i*13u+3u)%127u+1u; }
static int32_t i8_signed(uint32_t x) { return (x&128u)?(int32_t)(x&255u)-256:(int32_t)(x&255u); }
static uint32_t i8_signbits(uint32_t x) { return (uint32_t)i8_signed(x); }
static uint32_t i8_reverse(uint32_t x)
{
   uint32_t y=0; for (unsigned bit=0;bit<8;bit++) y|=((x>>bit)&1u)<<(7u-bit); return y;
}
static uint32_t i8_popcount(uint32_t x)
{
   uint32_t n=0; for (unsigned bit=0;bit<8;bit++) n+=(x>>bit)&1u; return n;
}
static uint32_t i8_lsb(uint32_t x)
{
   if (!x) return UINT32_MAX;
   for (unsigned bit=0;bit<8;bit++) if (x&(1u<<bit)) return bit;
   return UINT32_MAX;
}
static uint32_t i8_msb(int32_t x)
{
   uint32_t bits=x<0?(uint32_t)~x:(uint32_t)x;
   if (!bits) return UINT32_MAX;
   for (int bit=31;bit>=0;bit--) if (bits&(1u<<bit)) return (uint32_t)bit;
   return UINT32_MAX;
}
static void i8_expected(unsigned i,uint32_t out[I8_FIELDS])
{
   uint32_t a=i8_a(i),b=i8_b(i),shift=i&7u;
   int32_t sa=i8_signed(a),sb=(int32_t)b;
   uint32_t raw_a=0xabc00000u|a,raw_b=0xdef00000u|b;
   out[0]=(a+b)&255u; out[1]=(a-b)&255u; out[2]=(a*b)&255u;
   out[3]=a/b; out[4]=a%b;
   out[5]=i8_signbits((uint32_t)(sa+sb)); out[6]=i8_signbits((uint32_t)(sa-sb));
   out[7]=i8_signbits((uint32_t)(sa*sb)); out[8]=(uint32_t)(sa/sb); out[9]=(a&127u)%b;
   out[10]=i8_signbits((uint32_t)-sa);
   int32_t abs_input=sa==-128?-127:sa;
   out[11]=(uint32_t)(abs_input<0?-abs_input:abs_input);
   out[12]=a<b?a:b; out[13]=a>b?a:b;
   out[14]=(uint32_t)(sa<sb?sa:sb); out[15]=(uint32_t)(sa>sb?sa:sb);
   out[16]=a&b; out[17]=a|b; out[18]=a^b; out[19]=(~a)&255u;
   out[20]=(a<<shift)&255u; out[21]=a>>shift;
   out[22]=(uint32_t)(sa>=0?sa/(int32_t)(1u<<shift):-((-sa+(int32_t)(1u<<shift)-1)/(int32_t)(1u<<shift)));
   out[23]=(a==b)|((a!=b)<<1)|((a<b)<<2)|((a>=b)<<3)|((sa<sb)<<4)|((sa>=sb)<<5);
   out[24]=(i&1u)?a:b; out[25]=float_word((float)a); out[26]=float_word((float)sa);
   out[27]=b; out[28]=(uint32_t)sa;
   unsigned index=(raw_a*3u+i*7u)&511u;
   out[29]=(a+index*17u)&255u;
   unsigned peer=(i*7u+3u)&31u;
   out[30]=i8_a(peer); out[31]=i8_signbits(i8_a(peer));
   uint32_t packed=raw_a^(raw_b<<8u);
   for (unsigned j=0;j<4;j++) out[32+j]=float_word((float)((packed>>(j*8))&255u));
   out[36]=i8_popcount(a); out[37]=i8_reverse(a); out[38]=i8_lsb(a); out[39]=i8_msb(sa);
}
static int i8_verify(struct storage stores[I8_STORES])
{
   uint32_t expected[I8_RESULTS];
   for (unsigned i=0;i<I8_LANES;i++) i8_expected(i,expected+i*I8_FIELDS);
   memcpy(stores[0].saved+stores[0].binding_words+GUARD,expected,sizeof(expected));
   for (unsigned s=0;s<I8_STORES;s++) for (size_t i=0;i<stores[s].words;i++) {
      if (stores[s].map[i]!=stores[s].saved[i]) {
         size_t start=stores[s].binding_words+GUARD;
         size_t relative=i>=start?i-start:SIZE_MAX;
         fprintf(stderr,"FAIL INT8 buffer=%u word=%zu lane=%zu field=%zu actual=%08x expected=%08x\n",
            s,i,relative/I8_FIELDS,relative%I8_FIELDS,stores[s].map[i],stores[s].saved[i]); return 1;
      }
   }
   return 0;
}

int main(int argc,char **argv)
{
   if (argc!=3 && !(argc==4 && !strcmp(argv[3],"--compile-only"))) {
      fprintf(stderr,"Usage: %s ICD-library int8.spv [--compile-only]\n",argv[0]); return 1;
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
   VkPhysicalDeviceFeatures2KHR queried_core={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR,.pNext=&queried};
   GetPhysicalDeviceFeatures2KHR(physical,&queried_core);
   REQUIRE(queried.shaderInt8 && !queried.shaderFloat16 && queried_core.features.robustBufferAccess);
   uint32_t extension_count=0; SUCCESS(EnumerateDeviceExtensionProperties(physical,NULL,&extension_count,NULL));
   VkExtensionProperties *extensions=calloc(extension_count,sizeof(*extensions)); REQUIRE(extensions);
   SUCCESS(EnumerateDeviceExtensionProperties(physical,NULL,&extension_count,extensions));
   bool found=false;
   for (uint32_t i=0;i<extension_count;i++) found|=!strcmp(extensions[i].extensionName,VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);
   free(extensions); REQUIRE(found);
   VkPhysicalDeviceMemoryProperties memory_properties; GetPhysicalDeviceMemoryProperties(physical,&memory_properties);
   VkPhysicalDeviceProperties properties; GetPhysicalDeviceProperties(physical,&properties);
   REQUIRE(properties.apiVersion==VK_API_VERSION_1_0 && properties.limits.minStorageBufferOffsetAlignment<=64);
   const char *device_extensions[]={VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME};
   VkPhysicalDeviceShaderFloat16Int8FeaturesKHR requested_int8={
      .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR,.shaderInt8=VK_TRUE};
   VkPhysicalDeviceFeatures requested={.robustBufferAccess=VK_TRUE};
   float priority=1; VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueCount=1,.pQueuePriorities=&priority};
   VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&requested_int8,
      .queueCreateInfoCount=1,.pQueueCreateInfos=&qci,.pEnabledFeatures=&requested,
      .enabledExtensionCount=1,.ppEnabledExtensionNames=device_extensions};
   VkDevice device; SUCCESS(CreateDevice(physical,&dci,NULL,&device));
   struct rd_device *inside=rd_device_from_handle(device);
   REQUIRE(inside->vk.enabled_features.robustBufferAccess && inside->vk.enabled_features.shaderInt8 &&
      !inside->vk.enabled_features.shaderFloat16 && !inside->vk.enabled_features.shaderInt16 &&
      !inside->vk.enabled_features.storageBuffer8BitAccess && !inside->vk.enabled_features.storageBuffer16BitAccess);
   puts("INT8_PUBLIC_FEATURE extension=1 supported=1 enabled=1 robust=1 float16=0 int16=0 storage8=0 storage16=0"); fflush(stdout);
   const struct vk_device_dispatch_table *d=&inside->vk.dispatch_table;
   VkDescriptorSetLayoutBinding bindings[I8_STORES];
   for (unsigned i=0;i<I8_STORES;i++) bindings[i]=(VkDescriptorSetLayoutBinding){.binding=i,
      .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
   VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=I8_STORES,.pBindings=bindings};
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
   printf("INT8_COMPILED descriptors=%u buffers=%u native=%u full=%d half=%d private=%u shared=%u physical=%u,%u,%u\n",
      compiled->descriptor_count,compiled->buffer_count,compiled->variant->instrlen,
      compiled->variant->info.max_reg+1,compiled->variant->info.max_half_reg+1,
      compiled->variant->pvtmem_size,compiled->variant->shared_size,
      compiled->variant->local_size[0],compiled->variant->local_size[1],compiled->variant->local_size[2]); fflush(stdout);
   REQUIRE(compiled->variant->pvtmem_size && compiled->variant->shared_size>=64);
   if (argc==4) {
      d->DestroyPipeline(device,pipeline,NULL); d->DestroyPipelineLayout(device,layout,NULL);
      d->DestroyDescriptorSetLayout(device,set_layout,NULL); d->DestroyDevice(device,NULL);
      DestroyInstance(instance,NULL); dlclose(lib);
      puts("PASS INT8 compile-only: public arithmetic feature, shared/private storage retained, no buffers or dispatch"); return 0;
   }
   struct storage stores[I8_STORES]={0};
   REQUIRE(storage_create(d,device,&memory_properties,I8_RESULTS*4+128,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&stores[0])==0);
   REQUIRE(storage_create(d,device,&memory_properties,I8_LANES*2*4+128,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&stores[1])==0);
   for (unsigned s=0;s<I8_STORES;s++) for (size_t i=0;i<stores[s].words;i++) stores[s].map[i]=poison(i,40+s,0);
   uint32_t *input=stores[1].map+stores[1].binding_words+GUARD;
   for (unsigned i=0;i<I8_LANES;i++) { input[2*i]=0xabc00000u|i8_a(i); input[2*i+1]=0xdef00000u|i8_b(i); }
   for (unsigned s=0;s<I8_STORES;s++) memcpy(stores[s].saved,stores[s].map,stores[s].words*4);
   VkDescriptorPoolSize pool_size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,I8_STORES};
   VkDescriptorPoolCreateInfo pool_info={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,
      .poolSizeCount=1,.pPoolSizes=&pool_size}; VkDescriptorPool pool;
   SUCCESS(d->CreateDescriptorPool(device,&pool_info,NULL,&pool));
   VkDescriptorSetAllocateInfo allocation={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,
      .descriptorSetCount=1,.pSetLayouts=&set_layout}; VkDescriptorSet set;
   SUCCESS(d->AllocateDescriptorSets(device,&allocation,&set));
   VkDescriptorBufferInfo descriptors[I8_STORES]={{stores[0].buffer,64,I8_RESULTS*4},{stores[1].buffer,64,I8_LANES*2*4}};
   VkWriteDescriptorSet writes[I8_STORES];
   for (unsigned i=0;i<I8_STORES;i++) writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet=set,.dstBinding=i,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&descriptors[i]};
   d->UpdateDescriptorSets(device,I8_STORES,writes,0,NULL);
   VkMappedMemoryRange ranges[I8_STORES];
   for (unsigned i=0;i<I8_STORES;i++) ranges[i]=(VkMappedMemoryRange){.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
      .memory=stores[i].memory,.size=VK_WHOLE_SIZE};
   SUCCESS(d->FlushMappedMemoryRanges(device,I8_STORES,ranges));
   VkCommandPoolCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; VkCommandPool command_pool;
   SUCCESS(d->CreateCommandPool(device,&ci,NULL,&command_pool));
   VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=command_pool,
      .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1}; VkCommandBuffer command;
   SUCCESS(d->AllocateCommandBuffers(device,&ca,&command));
   VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   SUCCESS(d->BeginCommandBuffer(command,&begin));
   VkMemoryBarrier before={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT,
      .dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
   d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&before,0,NULL,0,NULL);
   d->CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
   d->CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);
   d->CmdDispatch(command,1,1,1);
   VkMemoryBarrier after={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,
      .dstAccessMask=VK_ACCESS_HOST_READ_BIT};
   d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&after,0,NULL,0,NULL);
   SUCCESS(d->EndCommandBuffer(command));
   VkQueue queue; d->GetDeviceQueue(device,0,0,&queue);
   VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence;
   SUCCESS(d->CreateFence(device,&fi,NULL,&fence));
   VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
   puts("INT8_CASE_BEGIN lanes=32 fields=40 private_elements=512 shared_arrays=2"); fflush(stdout);
   SUCCESS(d->QueueSubmit(queue,1,&submit,fence));
   SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
   SUCCESS(d->InvalidateMappedMemoryRanges(device,I8_STORES,ranges));
   REQUIRE(i8_verify(stores)==0);
   puts("INT8_CASE_PASS outputs=1280 complete_allocations=2 guards=exact"); fflush(stdout);
   d->DestroyFence(device,fence,NULL); d->DestroyCommandPool(device,command_pool,NULL);
   d->DestroyDescriptorPool(device,pool,NULL);
   for (unsigned i=0;i<I8_STORES;i++) {
      free(stores[i].saved); d->UnmapMemory(device,stores[i].memory);
      d->DestroyBuffer(device,stores[i].buffer,NULL); d->FreeMemory(device,stores[i].memory,NULL);
   }
   d->DestroyPipeline(device,pipeline,NULL); d->DestroyPipelineLayout(device,layout,NULL);
   d->DestroyDescriptorSetLayout(device,set_layout,NULL); d->DestroyDevice(device,NULL);
   DestroyInstance(instance,NULL); dlclose(lib);
   puts("PASS INT8: public arithmetic feature, exact integer/conversion/shared/private results and full allocation guards");
   return 0;
}
