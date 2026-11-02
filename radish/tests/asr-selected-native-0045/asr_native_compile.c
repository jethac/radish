#include <errno.h>
int resource_baseline_main(int argc,char **argv);
#define main resource_baseline_main
#include "core_resources.c"
#undef main

int main(int argc,char **argv)
{
   if (argc!=4) {
      fprintf(stderr,"Usage: %s ICD-library flat-SPIRV-directory selected-plan.txt\n",argv[0]); return 1;
   }
   void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); if (!lib) { fprintf(stderr,"%s\n",dlerror()); return 1; }
   PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr"); REQUIRE(gipa);
   VkInstance instance=VK_NULL_HANDLE; INSTANCE_FN(CreateInstance);
   VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_0};
   const char *instance_extensions[]={VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME};
   VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app,
      .enabledExtensionCount=2,.ppEnabledExtensionNames=instance_extensions};
   SUCCESS(CreateInstance(&ici,NULL,&instance));
   INSTANCE_FN(EnumeratePhysicalDevices); INSTANCE_FN(GetPhysicalDeviceMemoryProperties);
   INSTANCE_FN(GetPhysicalDeviceProperties); INSTANCE_FN(GetPhysicalDeviceFeatures2KHR);
   INSTANCE_FN(EnumerateDeviceExtensionProperties); INSTANCE_FN(CreateDevice); INSTANCE_FN(DestroyInstance);
   VkPhysicalDevice physical; uint32_t count=1;
   SUCCESS(EnumeratePhysicalDevices(instance,&count,&physical)); REQUIRE(count==1);
   VkPhysicalDeviceShaderFloat16Int8FeaturesKHR queried={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR};
   VkPhysicalDevice16BitStorageFeaturesKHR queried16={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES_KHR};
   VkPhysicalDevice8BitStorageFeaturesKHR queried8={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES_KHR};
   VkPhysicalDeviceTimelineSemaphoreFeaturesKHR queried_timeline={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR};
   queried.pNext=&queried16; queried16.pNext=&queried8; queried8.pNext=&queried_timeline;
   VkPhysicalDeviceFeatures2KHR queried_core={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2_KHR,.pNext=&queried};
   GetPhysicalDeviceFeatures2KHR(physical,&queried_core);
   REQUIRE(queried.shaderInt8 && !queried.shaderFloat16 && queried_core.features.robustBufferAccess);
   REQUIRE(queried16.storageBuffer16BitAccess && queried8.storageBuffer8BitAccess && queried_timeline.timelineSemaphore);
   uint32_t extension_count=0; SUCCESS(EnumerateDeviceExtensionProperties(physical,NULL,&extension_count,NULL));
   VkExtensionProperties *extensions=calloc(extension_count,sizeof(*extensions)); REQUIRE(extensions);
   SUCCESS(EnumerateDeviceExtensionProperties(physical,NULL,&extension_count,extensions));
   const char *device_extensions[]={VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME,
      VK_KHR_16BIT_STORAGE_EXTENSION_NAME,VK_KHR_8BIT_STORAGE_EXTENSION_NAME,
      VK_KHR_STORAGE_BUFFER_STORAGE_CLASS_EXTENSION_NAME,VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,VK_KHR_MAINTENANCE_3_EXTENSION_NAME};
   for (unsigned e=0;e<6;e++) {
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
   VkPhysicalDeviceTimelineSemaphoreFeaturesKHR requested_timeline={
      .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR,.timelineSemaphore=VK_TRUE};
   requested_int8.pNext=&requested16; requested16.pNext=&requested8; requested8.pNext=&requested_timeline;
   VkPhysicalDeviceFeatures requested=queried_core.features;
   float priority=1; VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueCount=1,.pQueuePriorities=&priority};
   VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&requested_int8,
      .queueCreateInfoCount=1,.pQueueCreateInfos=&qci,.pEnabledFeatures=&requested,
      .enabledExtensionCount=6,.ppEnabledExtensionNames=device_extensions};
   VkDevice device; SUCCESS(CreateDevice(physical,&dci,NULL,&device));
   struct rd_device *inside=rd_device_from_handle(device);
   REQUIRE(inside->vk.enabled_features.robustBufferAccess && inside->vk.enabled_features.shaderInt8 &&
      !inside->vk.enabled_features.shaderFloat16 && !inside->vk.enabled_features.shaderInt16 &&
      inside->vk.enabled_features.storageBuffer8BitAccess && inside->vk.enabled_features.storageBuffer16BitAccess && inside->vk.enabled_features.timelineSemaphore);
   puts("ASR_NATIVE_PUBLIC_FEATURE int8=1 storage8=1 storage16=1 timeline=1 robust=1 float16=0 int16=0"); fflush(stdout);
   const struct vk_device_dispatch_table *d=&inside->vk.dispatch_table;
   REQUIRE(properties.limits.maxPerStageDescriptorStorageBuffers>=5 && properties.limits.maxDescriptorSetStorageBuffers>=5 && properties.limits.maxPushConstantsSize>=120);
   REQUIRE(properties.limits.maxComputeWorkGroupInvocations>=128);
   FILE *plan=fopen(argv[3],"r"); REQUIRE(plan);
   char line[4096]; unsigned cases=0;
   while (fgets(line,sizeof(line),plan)) {
      REQUIRE(strchr(line,'\n') || feof(plan));
      char *save=NULL,*token=strtok_r(line," \t\r\n",&save); if (!token || token[0]=='#') continue;
      char *fields[32]; unsigned count=0;
      do { REQUIRE(count<32); fields[count++]=token; } while ((token=strtok_r(NULL," \t\r\n",&save)));
      REQUIRE(count>=8 && strlen(fields[0])<64 && strlen(fields[1])<96 && cases<64);
      for (const char *p=fields[1];*p;p++) REQUIRE((*p>='a' && *p<='z') || (*p>='0' && *p<='9') || *p=='_');
      uint32_t numbers[18];
      for (unsigned i=2;i<count;i++) {
         char *end=NULL; errno=0; unsigned long value=strtoul(fields[i],&end,10);
         REQUIRE(!errno && end!=fields[i] && !*end && value<=UINT32_MAX && i-2<18);
         numbers[i-2]=(uint32_t)value;
      }
      unsigned parameters=numbers[0],push_bytes=numbers[1],spec_count=numbers[5];
      REQUIRE(parameters>=1 && parameters<=5 && push_bytes && !(push_bytes&3) && push_bytes<=128 && push_bytes<=properties.limits.maxPushConstantsSize);
      REQUIRE(spec_count<=12 && count==8+spec_count);
      uint64_t logical_threads=1;
      for (unsigned i=0;i<3;i++) {
         REQUIRE(numbers[i+2] && numbers[i+2]<=properties.limits.maxComputeWorkGroupSize[i]);
         logical_threads*=numbers[i+2];
      }
      REQUIRE(logical_threads<=128 && logical_threads<=properties.limits.maxComputeWorkGroupInvocations);
      VkDescriptorSetLayoutBinding bindings[5];
      for (unsigned i=0;i<5;i++) bindings[i]=(VkDescriptorSetLayoutBinding){.binding=i,
         .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
      VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=5,.pBindings=bindings};
      VkDescriptorSetLayout set_layout; SUCCESS(d->CreateDescriptorSetLayout(device,&li,NULL,&set_layout));
      VkPushConstantRange push={.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,.size=push_bytes};
      VkPipelineLayoutCreateInfo pi={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
         .setLayoutCount=1,.pSetLayouts=&set_layout,.pushConstantRangeCount=1,.pPushConstantRanges=&push};
      VkPipelineLayout layout; SUCCESS(d->CreatePipelineLayout(device,&pi,NULL,&layout));
      char path[512]; int length=snprintf(path,sizeof(path),"%s/%s.spv",argv[2],fields[1]);
      REQUIRE(length>0 && (size_t)length<sizeof(path));
      size_t bytes; uint32_t *code=read_spirv(path,&bytes);
      REQUIRE(code && bytes>=20 && code[0]==0x07230203u && code[1]==0x00010000u);
      VkShaderModuleCreateInfo mi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=bytes,.pCode=code};
      VkShaderModule module; SUCCESS(d->CreateShaderModule(device,&mi,NULL,&module)); free(code);
      VkSpecializationMapEntry entries[12];
      for (unsigned i=0;i<spec_count;i++) entries[i]=(VkSpecializationMapEntry){.constantID=i,.offset=i*4,.size=4};
      VkSpecializationInfo specialization={.mapEntryCount=spec_count,.pMapEntries=entries,.dataSize=spec_count*4,.pData=&numbers[6]};
      VkComputePipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
         .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,
                 .module=module,.pName="main",.pSpecializationInfo=spec_count?&specialization:NULL},.layout=layout};
      printf("ASR_NATIVE_BEGIN index=%u case=%s module=%s parameters=%u layout=5 push=%u logical=%u,%u,%u specs=%u",
         cases,fields[0],fields[1],parameters,push_bytes,numbers[2],numbers[3],numbers[4],spec_count);
      for (unsigned i=0;i<spec_count;i++) printf(" spec%u=%u",i,numbers[i+6]); puts(""); fflush(stdout);
      VkPipeline pipeline; SUCCESS(d->CreateComputePipelines(device,VK_NULL_HANDLE,1,&ci,NULL,&pipeline));
      struct rd_pipeline *compiled=rd_pipeline_from_handle(pipeline);
      REQUIRE(compiled->bo && compiled->descriptor_count<=parameters && compiled->buffer_count==compiled->descriptor_count);
      uint64_t physical_threads=1;
      for (unsigned i=0;i<3;i++) {
         REQUIRE(compiled->logical_local_size[i]==numbers[i+2]);
         physical_threads*=compiled->variant->local_size[i];
      }
      REQUIRE(physical_threads==logical_threads);
      REQUIRE(!compiled->variant->pvtmem_size || compiled->private_bo);
      printf("ASR_NATIVE_COMPILED index=%u case=%s descriptors=%u buffers=%u native=%u full=%d half=%d private=%u shared=%u logical=%u,%u,%u physical=%u,%u,%u\n",
         cases,fields[0],compiled->descriptor_count,compiled->buffer_count,compiled->variant->instrlen,
         compiled->variant->info.max_reg+1,compiled->variant->info.max_half_reg+1,
         compiled->variant->pvtmem_size,compiled->variant->shared_size,
         compiled->logical_local_size[0],compiled->logical_local_size[1],compiled->logical_local_size[2],
         compiled->variant->local_size[0],compiled->variant->local_size[1],compiled->variant->local_size[2]); fflush(stdout);
      d->DestroyPipeline(device,pipeline,NULL); d->DestroyShaderModule(device,module,NULL);
      d->DestroyPipelineLayout(device,layout,NULL); d->DestroyDescriptorSetLayout(device,set_layout,NULL);
      cases++;
   }
   REQUIRE(!ferror(plan) && cases); fclose(plan);
   d->DestroyDevice(device,NULL); DestroyInstance(instance,NULL); dlclose(lib);
   printf("PASS ASR native compile-only: cases=%u real public API1.0+KHR/storage8/16/Int8/timeline/RBA, no tensors or command/dispatch/submission\n",cases);
   return 0;
}
