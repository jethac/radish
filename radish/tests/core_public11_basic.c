#include "test_common.h"
#include "rd_private.h"
#include <inttypes.h>

enum { GUARD=16, MAX_LOCAL=128, OUTPUT_WORDS=2*GUARD+16*MAX_LOCAL, EXCHANGE_WORDS=40 };
struct storage { VkBuffer buffer; VkDeviceMemory memory; uint32_t *map; size_t words; };
static const uint32_t locals[][3]={{32,1,1},{33,1,1},{8,8,1},{128,1,1},{3,5,3}};

static uint32_t poison(size_t i, unsigned test)
{
   return 0xd7000000u ^ (uint32_t)i*181u ^ test*0x10101u;
}

static uint32_t *read_spirv(const char *path, size_t *size)
{
   FILE *file=fopen(path,"rb");
   if (!file) return NULL;
   if (fseek(file,0,SEEK_END)) { fclose(file); return NULL; }
   long length=ftell(file);
   if (length<=0 || length%4 || length>1024*1024) { fclose(file); return NULL; }
   rewind(file);
   uint32_t *code=malloc((size_t)length);
   if (!code || fread(code,1,(size_t)length,file)!=(size_t)length) {
      free(code); fclose(file); return NULL;
   }
   fclose(file); *size=(size_t)length; return code;
}

static int storage_create(const struct vk_device_dispatch_table *d, VkDevice device,
                          const VkPhysicalDeviceMemoryProperties *properties,
                          VkDeviceSize bytes, struct storage *s)
{
   VkBufferCreateInfo info={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size=bytes,.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
   SUCCESS(d->CreateBuffer(device,&info,NULL,&s->buffer));
   VkMemoryRequirements requirements;
   d->GetBufferMemoryRequirements(device,s->buffer,&requirements);
   REQUIRE(requirements.size>=bytes && requirements.size<=65536 && !(requirements.size%4));
   uint32_t type=UINT32_MAX;
   for (uint32_t i=0;i<properties->memoryTypeCount;i++) {
      VkMemoryPropertyFlags flags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      if ((requirements.memoryTypeBits&(1u<<i)) &&
          (properties->memoryTypes[i].propertyFlags&flags)==flags) { type=i; break; }
   }
   REQUIRE(type!=UINT32_MAX);
   VkMemoryAllocateInfo allocation={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize=requirements.size,.memoryTypeIndex=type};
   SUCCESS(d->AllocateMemory(device,&allocation,NULL,&s->memory));
   SUCCESS(d->BindBufferMemory(device,s->buffer,s->memory,0));
   SUCCESS(d->MapMemory(device,s->memory,0,VK_WHOLE_SIZE,0,(void **)&s->map));
   s->words=requirements.size/4;
   return 0;
}

static int check_output(const struct storage *out, const struct storage *exchange, unsigned test)
{
   const uint32_t *local=locals[test];
   const uint32_t n=local[0]*local[1]*local[2], groups=(n+15)/16, salt=0x23450000u+test*997u;
   uint32_t members[8]={0}, elected[8]={0}, odd_members[8]={0}, odd_elected[8]={0}, first_index[8]={0};
   uint32_t seen[8]={0};
   for (uint32_t i=0;i<n;i++) {
      const uint32_t *r=out->map+GUARD+16*i;
      REQUIRE(r[0]==i && r[1]==16 && r[2]==groups && r[3]<groups && r[4]<16);
      uint32_t g=r[3], lane=r[4];
      REQUIRE(!(seen[g] & (1u<<lane)));
      seen[g]|=1u<<lane; members[g]++;
      REQUIRE(r[5]<=1 && r[6]<=1 && ((lane&1u) || !r[6]));
      if (r[5]) { elected[g]++; first_index[g]=i; }
      odd_members[g]+=lane&1u;
      odd_elected[g]+=r[6];
   }
   for (uint32_t g=0;g<groups;g++)
      REQUIRE(members[g]>0 && members[g]<=16 && elected[g]==1 && odd_elected[g]==(odd_members[g]>0));
   for (uint32_t i=0;i<n;i++) {
      const uint32_t *r=out->map+GUARD+16*i;
      uint32_t g=r[3], value=first_index[g]^salt;
      uint32_t buffer_value=value^((g&1u)?0u:0x01020304u);
      REQUIRE(r[7]==value && r[8]==members[g] && r[9]==buffer_value && r[10]==members[g]);
      REQUIRE(r[11]==salt && r[12]==i%local[0] && r[13]==i/local[0]%local[1] &&
              r[14]==i/(local[0]*local[1]) && r[15]==(0x5a000000u^i));
   }
   for (size_t i=0;i<out->words;i++)
      if (i<GUARD || i>=GUARD+16*n) REQUIRE(out->map[i]==poison(i,test));
   for (size_t i=0;i<exchange->words;i++) {
      uint32_t expected=poison(i,test);
      if (i>=GUARD && i<GUARD+2*groups) {
         uint32_t g=(i-GUARD)/2;
         expected=(i&1u)?members[g]:(first_index[g]^salt^((g&1u)?0u:0x01020304u));
      }
      REQUIRE(exchange->map[i]==expected);
   }
   printf("BASIC CASE_PASS case=%u local=%u,%u,%u invocations=%u subgroups=%u full_records=1 all_guards=1\n",
          test,local[0],local[1],local[2],n,groups);
   return 0;
}

static int check_instance_contract(PFN_vkGetInstanceProcAddr gipa)
{
   VkInstance instance=VK_NULL_HANDLE;
   INSTANCE_FN(CreateInstance);
   const uint32_t versions[]={0,VK_API_VERSION_1_0,VK_API_VERSION_1_1,VK_API_VERSION_1_2};
   for (unsigned i=0;i<sizeof(versions)/sizeof(versions[0]);i++) {
      VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=versions[i]};
      VkInstanceCreateInfo info={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
      SUCCESS(CreateInstance(&info,NULL,&instance));
      INSTANCE_FN(DestroyInstance);
      DestroyInstance(instance,NULL); instance=VK_NULL_HANDLE;
   }
   const char *unsupported="VK_RADISH_intentionally_unsupported";
   VkInstanceCreateInfo bad={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .enabledExtensionCount=1,.ppEnabledExtensionNames=&unsupported};
   REQUIRE(CreateInstance(&bad,NULL,&instance)==VK_ERROR_EXTENSION_NOT_PRESENT);
   const char *extensions[]={VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
      VK_KHR_DEVICE_GROUP_CREATION_EXTENSION_NAME,VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
      VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME,VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME};
   VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_0};
   VkInstanceCreateInfo info={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app,
      .enabledExtensionCount=sizeof(extensions)/sizeof(extensions[0]),.ppEnabledExtensionNames=extensions};
   SUCCESS(CreateInstance(&info,NULL,&instance));
   const char *instance_aliases[]={"vkGetPhysicalDeviceFeatures2KHR","vkGetPhysicalDeviceProperties2KHR",
      "vkGetPhysicalDeviceFormatProperties2KHR","vkGetPhysicalDeviceImageFormatProperties2KHR",
      "vkGetPhysicalDeviceQueueFamilyProperties2KHR","vkGetPhysicalDeviceMemoryProperties2KHR",
      "vkGetPhysicalDeviceSparseImageFormatProperties2KHR","vkEnumeratePhysicalDeviceGroupsKHR",
      "vkGetPhysicalDeviceExternalBufferPropertiesKHR","vkGetPhysicalDeviceExternalFencePropertiesKHR",
      "vkGetPhysicalDeviceExternalSemaphorePropertiesKHR"};
   for (unsigned i=0;i<sizeof(instance_aliases)/sizeof(instance_aliases[0]);i++) REQUIRE(gipa(instance,instance_aliases[i]));
   INSTANCE_FN(EnumeratePhysicalDevices); INSTANCE_FN(CreateDevice); INSTANCE_FN(GetDeviceProcAddr); INSTANCE_FN(DestroyInstance);
   uint32_t count=1; VkPhysicalDevice physical;
   SUCCESS(EnumeratePhysicalDevices(instance,&count,&physical)); REQUIRE(count==1);
   PFN_vkGetPhysicalDeviceProperties2KHR properties2=(PFN_vkGetPhysicalDeviceProperties2KHR)gipa(instance,"vkGetPhysicalDeviceProperties2KHR");
   VkPhysicalDeviceProperties2 properties={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
   properties2(physical,&properties); REQUIRE(properties.properties.apiVersion>=VK_API_VERSION_1_1);
   const char *device_extensions[]={VK_KHR_DEVICE_GROUP_EXTENSION_NAME,VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME,
      VK_KHR_MAINTENANCE_3_EXTENSION_NAME,VK_KHR_BIND_MEMORY_2_EXTENSION_NAME,VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME};
   float priority=1.0f;
   VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueCount=1,.pQueuePriorities=&priority};
   VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qci,
      .enabledExtensionCount=sizeof(device_extensions)/sizeof(device_extensions[0]),.ppEnabledExtensionNames=device_extensions};
   VkDevice device; SUCCESS(CreateDevice(physical,&dci,NULL,&device));
   const char *device_aliases[]={"vkBindBufferMemory2KHR","vkBindImageMemory2KHR","vkGetDeviceGroupPeerMemoryFeaturesKHR",
      "vkCmdSetDeviceMaskKHR","vkCmdDispatchBaseKHR","vkGetImageMemoryRequirements2KHR","vkGetBufferMemoryRequirements2KHR",
      "vkGetImageSparseMemoryRequirements2KHR","vkCreateDescriptorUpdateTemplateKHR","vkDestroyDescriptorUpdateTemplateKHR",
      "vkUpdateDescriptorSetWithTemplateKHR","vkGetDescriptorSetLayoutSupportKHR"};
   for (unsigned i=0;i<sizeof(device_aliases)/sizeof(device_aliases[0]);i++) REQUIRE(GetDeviceProcAddr(device,device_aliases[i]));
   PFN_vkDestroyDevice destroy=(PFN_vkDestroyDevice)GetDeviceProcAddr(device,"vkDestroyDevice"); REQUIRE(destroy);
   destroy(device,NULL); DestroyInstance(instance,NULL);
   puts("CONTRACT PASS negotiation=0,1.0,1.1,1.2 instance_extension_error=1 khr_instance_aliases=11 khr_device_aliases=12");
   return 0;
}

int main(int argc,char **argv)
{
   if (argc!=3 && !(argc==4 && !strcmp(argv[3],"--compile-only"))) {
      fprintf(stderr,"Usage: %s ICD-library basic.spv [--compile-only]\n",argv[0]); return 1;
   }
   void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
   if (!lib) { fprintf(stderr,"%s\n",dlerror()); return 1; }
   PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr");
   REQUIRE(gipa);
   REQUIRE(check_instance_contract(gipa)==0);
   VkInstance instance=VK_NULL_HANDLE;
   INSTANCE_FN(CreateInstance);
   INSTANCE_FN(EnumerateInstanceVersion);
   uint32_t instance_version=0;
   SUCCESS(EnumerateInstanceVersion(&instance_version));
   REQUIRE(instance_version>=VK_API_VERSION_1_1);
   VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_1};
   VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
   SUCCESS(CreateInstance(&ici,NULL,&instance));
   INSTANCE_FN(EnumeratePhysicalDevices); INSTANCE_FN(GetPhysicalDeviceMemoryProperties);
   INSTANCE_FN(CreateDevice); INSTANCE_FN(DestroyInstance);
   uint32_t count=1; VkPhysicalDevice physical;
   SUCCESS(EnumeratePhysicalDevices(instance,&count,&physical)); REQUIRE(count==1);
   INSTANCE_FN(GetPhysicalDeviceProperties2); INSTANCE_FN(GetPhysicalDeviceFeatures2);
   INSTANCE_FN(GetPhysicalDeviceExternalFenceProperties);
   INSTANCE_FN(GetPhysicalDeviceExternalSemaphoreProperties);
   VkPhysicalDeviceSubgroupProperties subgroup={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
   VkPhysicalDeviceIDProperties ids={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES,.pNext=&subgroup};
   VkPhysicalDeviceMaintenance3Properties maintenance={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES,.pNext=&ids};
   VkPhysicalDeviceProperties2 properties={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&maintenance};
   GetPhysicalDeviceProperties2(physical,&properties);
   REQUIRE(properties.properties.apiVersion>=VK_API_VERSION_1_1 && subgroup.subgroupSize==16);
   REQUIRE(subgroup.supportedStages&VK_SHADER_STAGE_COMPUTE_BIT);
   REQUIRE(subgroup.supportedOperations&VK_SUBGROUP_FEATURE_BASIC_BIT);
   VkPhysicalDeviceVariablePointersFeatures variable={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VARIABLE_POINTERS_FEATURES};
   VkPhysicalDeviceSamplerYcbcrConversionFeatures ycbcr={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES,.pNext=&variable};
   VkPhysicalDeviceProtectedMemoryFeatures protected_memory={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES,.pNext=&ycbcr};
   VkPhysicalDevice16BitStorageFeatures storage16={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES,.pNext=&protected_memory};
   VkPhysicalDeviceFeatures2 features={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,.pNext=&storage16};
   GetPhysicalDeviceFeatures2(physical,&features);
   REQUIRE(storage16.storageBuffer16BitAccess && !storage16.uniformAndStorageBuffer16BitAccess &&
           !storage16.storagePushConstant16 && !storage16.storageInputOutput16);
   REQUIRE(!protected_memory.protectedMemory && !ycbcr.samplerYcbcrConversion &&
           !variable.variablePointers && !variable.variablePointersStorageBuffer);
   VkPhysicalDeviceExternalFenceInfo fence_info={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_FENCE_INFO,
      .handleType=VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT};
   VkExternalFenceProperties fence_properties={.sType=VK_STRUCTURE_TYPE_EXTERNAL_FENCE_PROPERTIES,
      .exportFromImportedHandleTypes=UINT32_MAX,.compatibleHandleTypes=UINT32_MAX,.externalFenceFeatures=UINT32_MAX};
   GetPhysicalDeviceExternalFenceProperties(physical,&fence_info,&fence_properties);
   REQUIRE(!fence_properties.exportFromImportedHandleTypes && !fence_properties.compatibleHandleTypes && !fence_properties.externalFenceFeatures);
   VkPhysicalDeviceExternalSemaphoreInfo semaphore_info={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO,
      .handleType=VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT};
   VkExternalSemaphoreProperties semaphore_properties={.sType=VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES,
      .exportFromImportedHandleTypes=UINT32_MAX,.compatibleHandleTypes=UINT32_MAX,.externalSemaphoreFeatures=UINT32_MAX};
   GetPhysicalDeviceExternalSemaphoreProperties(physical,&semaphore_info,&semaphore_properties);
   REQUIRE(!semaphore_properties.exportFromImportedHandleTypes && !semaphore_properties.compatibleHandleTypes && !semaphore_properties.externalSemaphoreFeatures);
   VkPhysicalDeviceMemoryProperties memory_properties;
   GetPhysicalDeviceMemoryProperties(physical,&memory_properties);
   printf("BASIC PUBLIC api=%u.%u subgroup=%u stages=%x operations=%x storage16=%u protected=%u variable=%u ycbcr=%u external_fence=0 external_semaphore=0\n",
      VK_API_VERSION_MAJOR(properties.properties.apiVersion),VK_API_VERSION_MINOR(properties.properties.apiVersion),
      subgroup.subgroupSize,subgroup.supportedStages,subgroup.supportedOperations,storage16.storageBuffer16BitAccess,
      protected_memory.protectedMemory,variable.variablePointersStorageBuffer,ycbcr.samplerYcbcrConversion);
   float priority=1.0f;
   VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueCount=1,.pQueuePriorities=&priority};
   VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount=1,.pQueueCreateInfos=&qci};
   VkDevice device;
   VkPhysicalDeviceSamplerYcbcrConversionFeatures unsupported={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES,.samplerYcbcrConversion=VK_TRUE};
   dci.pNext=&unsupported;
   REQUIRE(CreateDevice(physical,&dci,NULL,&device)==VK_ERROR_FEATURE_NOT_PRESENT);
   dci.pNext=NULL;
   const char *unsupported_extension="VK_RADISH_intentionally_unsupported";
   dci.enabledExtensionCount=1; dci.ppEnabledExtensionNames=&unsupported_extension;
   REQUIRE(CreateDevice(physical,&dci,NULL,&device)==VK_ERROR_EXTENSION_NOT_PRESENT);
   dci.enabledExtensionCount=0; dci.ppEnabledExtensionNames=NULL;
   SUCCESS(CreateDevice(physical,&dci,NULL,&device));
   struct rd_device *inside_device=rd_device_from_handle(device);
   INSTANCE_FN(GetDeviceProcAddr);
   const char *core11_names[]={"vkEnumerateInstanceVersion","vkBindBufferMemory2","vkBindImageMemory2","vkGetDeviceGroupPeerMemoryFeatures","vkCmdSetDeviceMask","vkEnumeratePhysicalDeviceGroups","vkGetImageMemoryRequirements2","vkGetBufferMemoryRequirements2","vkGetImageSparseMemoryRequirements2","vkGetPhysicalDeviceFeatures2","vkGetPhysicalDeviceProperties2","vkGetPhysicalDeviceFormatProperties2","vkGetPhysicalDeviceImageFormatProperties2","vkGetPhysicalDeviceQueueFamilyProperties2","vkGetPhysicalDeviceMemoryProperties2","vkGetPhysicalDeviceSparseImageFormatProperties2","vkTrimCommandPool","vkGetDeviceQueue2","vkGetPhysicalDeviceExternalBufferProperties","vkGetPhysicalDeviceExternalFenceProperties","vkGetPhysicalDeviceExternalSemaphoreProperties","vkCmdDispatchBase","vkCreateDescriptorUpdateTemplate","vkDestroyDescriptorUpdateTemplate","vkUpdateDescriptorSetWithTemplate","vkGetDescriptorSetLayoutSupport","vkCreateSamplerYcbcrConversion","vkDestroySamplerYcbcrConversion"};
   for (unsigned i=0;i<sizeof(core11_names)/sizeof(core11_names[0]);i++) REQUIRE(gipa(instance,core11_names[i]));
   const char *device11_names[]={"vkBindBufferMemory2","vkBindImageMemory2","vkGetDeviceGroupPeerMemoryFeatures","vkCmdSetDeviceMask","vkGetImageMemoryRequirements2","vkGetBufferMemoryRequirements2","vkGetImageSparseMemoryRequirements2","vkTrimCommandPool","vkGetDeviceQueue2","vkCmdDispatchBase","vkCreateDescriptorUpdateTemplate","vkDestroyDescriptorUpdateTemplate","vkUpdateDescriptorSetWithTemplate","vkGetDescriptorSetLayoutSupport","vkCreateSamplerYcbcrConversion","vkDestroySamplerYcbcrConversion"};
   for (unsigned i=0;i<sizeof(device11_names)/sizeof(device11_names[0]);i++) REQUIRE(GetDeviceProcAddr(device,device11_names[i]));
   puts("CONTRACT PASS core11_names=28 individual_features=1 unsupported_feature_error=1 device_extension_error=1");
   struct vk_device_dispatch_table public_dispatch={0};
   const struct vk_device_dispatch_table *d=&public_dispatch;
   public_dispatch.AllocateCommandBuffers=(PFN_vkAllocateCommandBuffers)GetDeviceProcAddr(device,"vkAllocateCommandBuffers"); REQUIRE(public_dispatch.AllocateCommandBuffers);
   public_dispatch.AllocateDescriptorSets=(PFN_vkAllocateDescriptorSets)GetDeviceProcAddr(device,"vkAllocateDescriptorSets"); REQUIRE(public_dispatch.AllocateDescriptorSets);
   public_dispatch.AllocateMemory=(PFN_vkAllocateMemory)GetDeviceProcAddr(device,"vkAllocateMemory"); REQUIRE(public_dispatch.AllocateMemory);
   public_dispatch.BeginCommandBuffer=(PFN_vkBeginCommandBuffer)GetDeviceProcAddr(device,"vkBeginCommandBuffer"); REQUIRE(public_dispatch.BeginCommandBuffer);
   public_dispatch.BindBufferMemory=(PFN_vkBindBufferMemory)GetDeviceProcAddr(device,"vkBindBufferMemory"); REQUIRE(public_dispatch.BindBufferMemory);
   public_dispatch.CmdBindDescriptorSets=(PFN_vkCmdBindDescriptorSets)GetDeviceProcAddr(device,"vkCmdBindDescriptorSets"); REQUIRE(public_dispatch.CmdBindDescriptorSets);
   public_dispatch.CmdBindPipeline=(PFN_vkCmdBindPipeline)GetDeviceProcAddr(device,"vkCmdBindPipeline"); REQUIRE(public_dispatch.CmdBindPipeline);
   public_dispatch.CmdDispatch=(PFN_vkCmdDispatch)GetDeviceProcAddr(device,"vkCmdDispatch"); REQUIRE(public_dispatch.CmdDispatch);
   public_dispatch.CmdPipelineBarrier=(PFN_vkCmdPipelineBarrier)GetDeviceProcAddr(device,"vkCmdPipelineBarrier"); REQUIRE(public_dispatch.CmdPipelineBarrier);
   public_dispatch.CmdPushConstants=(PFN_vkCmdPushConstants)GetDeviceProcAddr(device,"vkCmdPushConstants"); REQUIRE(public_dispatch.CmdPushConstants);
   public_dispatch.CreateBuffer=(PFN_vkCreateBuffer)GetDeviceProcAddr(device,"vkCreateBuffer"); REQUIRE(public_dispatch.CreateBuffer);
   public_dispatch.CreateCommandPool=(PFN_vkCreateCommandPool)GetDeviceProcAddr(device,"vkCreateCommandPool"); REQUIRE(public_dispatch.CreateCommandPool);
   public_dispatch.CreateComputePipelines=(PFN_vkCreateComputePipelines)GetDeviceProcAddr(device,"vkCreateComputePipelines"); REQUIRE(public_dispatch.CreateComputePipelines);
   public_dispatch.CreateDescriptorPool=(PFN_vkCreateDescriptorPool)GetDeviceProcAddr(device,"vkCreateDescriptorPool"); REQUIRE(public_dispatch.CreateDescriptorPool);
   public_dispatch.CreateDescriptorSetLayout=(PFN_vkCreateDescriptorSetLayout)GetDeviceProcAddr(device,"vkCreateDescriptorSetLayout"); REQUIRE(public_dispatch.CreateDescriptorSetLayout);
   public_dispatch.CreateFence=(PFN_vkCreateFence)GetDeviceProcAddr(device,"vkCreateFence"); REQUIRE(public_dispatch.CreateFence);
   public_dispatch.CreatePipelineLayout=(PFN_vkCreatePipelineLayout)GetDeviceProcAddr(device,"vkCreatePipelineLayout"); REQUIRE(public_dispatch.CreatePipelineLayout);
   public_dispatch.CreateShaderModule=(PFN_vkCreateShaderModule)GetDeviceProcAddr(device,"vkCreateShaderModule"); REQUIRE(public_dispatch.CreateShaderModule);
   public_dispatch.DestroyBuffer=(PFN_vkDestroyBuffer)GetDeviceProcAddr(device,"vkDestroyBuffer"); REQUIRE(public_dispatch.DestroyBuffer);
   public_dispatch.DestroyCommandPool=(PFN_vkDestroyCommandPool)GetDeviceProcAddr(device,"vkDestroyCommandPool"); REQUIRE(public_dispatch.DestroyCommandPool);
   public_dispatch.DestroyDescriptorPool=(PFN_vkDestroyDescriptorPool)GetDeviceProcAddr(device,"vkDestroyDescriptorPool"); REQUIRE(public_dispatch.DestroyDescriptorPool);
   public_dispatch.DestroyDescriptorSetLayout=(PFN_vkDestroyDescriptorSetLayout)GetDeviceProcAddr(device,"vkDestroyDescriptorSetLayout"); REQUIRE(public_dispatch.DestroyDescriptorSetLayout);
   public_dispatch.DestroyDevice=(PFN_vkDestroyDevice)GetDeviceProcAddr(device,"vkDestroyDevice"); REQUIRE(public_dispatch.DestroyDevice);
   public_dispatch.DestroyFence=(PFN_vkDestroyFence)GetDeviceProcAddr(device,"vkDestroyFence"); REQUIRE(public_dispatch.DestroyFence);
   public_dispatch.DestroyPipeline=(PFN_vkDestroyPipeline)GetDeviceProcAddr(device,"vkDestroyPipeline"); REQUIRE(public_dispatch.DestroyPipeline);
   public_dispatch.DestroyPipelineLayout=(PFN_vkDestroyPipelineLayout)GetDeviceProcAddr(device,"vkDestroyPipelineLayout"); REQUIRE(public_dispatch.DestroyPipelineLayout);
   public_dispatch.DestroyShaderModule=(PFN_vkDestroyShaderModule)GetDeviceProcAddr(device,"vkDestroyShaderModule"); REQUIRE(public_dispatch.DestroyShaderModule);
   public_dispatch.EndCommandBuffer=(PFN_vkEndCommandBuffer)GetDeviceProcAddr(device,"vkEndCommandBuffer"); REQUIRE(public_dispatch.EndCommandBuffer);
   public_dispatch.FlushMappedMemoryRanges=(PFN_vkFlushMappedMemoryRanges)GetDeviceProcAddr(device,"vkFlushMappedMemoryRanges"); REQUIRE(public_dispatch.FlushMappedMemoryRanges);
   public_dispatch.FreeMemory=(PFN_vkFreeMemory)GetDeviceProcAddr(device,"vkFreeMemory"); REQUIRE(public_dispatch.FreeMemory);
   public_dispatch.GetBufferMemoryRequirements=(PFN_vkGetBufferMemoryRequirements)GetDeviceProcAddr(device,"vkGetBufferMemoryRequirements"); REQUIRE(public_dispatch.GetBufferMemoryRequirements);
   public_dispatch.GetDeviceQueue=(PFN_vkGetDeviceQueue)GetDeviceProcAddr(device,"vkGetDeviceQueue"); REQUIRE(public_dispatch.GetDeviceQueue);
   public_dispatch.InvalidateMappedMemoryRanges=(PFN_vkInvalidateMappedMemoryRanges)GetDeviceProcAddr(device,"vkInvalidateMappedMemoryRanges"); REQUIRE(public_dispatch.InvalidateMappedMemoryRanges);
   public_dispatch.MapMemory=(PFN_vkMapMemory)GetDeviceProcAddr(device,"vkMapMemory"); REQUIRE(public_dispatch.MapMemory);
   public_dispatch.QueueSubmit=(PFN_vkQueueSubmit)GetDeviceProcAddr(device,"vkQueueSubmit"); REQUIRE(public_dispatch.QueueSubmit);
   public_dispatch.ResetCommandBuffer=(PFN_vkResetCommandBuffer)GetDeviceProcAddr(device,"vkResetCommandBuffer"); REQUIRE(public_dispatch.ResetCommandBuffer);
   public_dispatch.ResetFences=(PFN_vkResetFences)GetDeviceProcAddr(device,"vkResetFences"); REQUIRE(public_dispatch.ResetFences);
   public_dispatch.UnmapMemory=(PFN_vkUnmapMemory)GetDeviceProcAddr(device,"vkUnmapMemory"); REQUIRE(public_dispatch.UnmapMemory);
   public_dispatch.UpdateDescriptorSets=(PFN_vkUpdateDescriptorSets)GetDeviceProcAddr(device,"vkUpdateDescriptorSets"); REQUIRE(public_dispatch.UpdateDescriptorSets);
   public_dispatch.WaitForFences=(PFN_vkWaitForFences)GetDeviceProcAddr(device,"vkWaitForFences"); REQUIRE(public_dispatch.WaitForFences);

   VkDescriptorSetLayoutBinding bindings[2];
   for (unsigned i=0;i<2;i++) bindings[i]=(VkDescriptorSetLayoutBinding){
      .binding=i,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.descriptorCount=1,
      .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
   VkDescriptorSetLayoutCreateInfo slci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount=2,.pBindings=bindings};
   VkDescriptorSetLayout set_layout;
   SUCCESS(d->CreateDescriptorSetLayout(device,&slci,NULL,&set_layout));
   VkPushConstantRange push={.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,.size=4};
   VkPipelineLayoutCreateInfo plci={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount=1,.pSetLayouts=&set_layout,.pushConstantRangeCount=1,.pPushConstantRanges=&push};
   VkPipelineLayout layout;
   SUCCESS(d->CreatePipelineLayout(device,&plci,NULL,&layout));
   size_t code_size; uint32_t *code=read_spirv(argv[2],&code_size); REQUIRE(code);
   VkShaderModuleCreateInfo smci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=code_size,.pCode=code};
   VkShaderModule module;
   SUCCESS(d->CreateShaderModule(device,&smci,NULL,&module)); free(code);
   VkSpecializationMapEntry entries[3];
   for (unsigned i=0;i<3;i++) entries[i]=(VkSpecializationMapEntry){.constantID=i,.offset=4*i,.size=4};
   VkPipeline pipelines[5]={0};
   for (unsigned test=0;test<5;test++) {
      VkSpecializationInfo spec={.mapEntryCount=3,.pMapEntries=entries,.dataSize=12,.pData=locals[test]};
      VkComputePipelineCreateInfo pci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
         .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,
                 .module=module,.pName="main",.pSpecializationInfo=&spec},.layout=layout};
      SUCCESS(d->CreateComputePipelines(device,VK_NULL_HANDLE,1,&pci,NULL,&pipelines[test]));
      struct rd_pipeline *pipeline=rd_pipeline_from_handle(pipelines[test]);
      struct ir3_shader_variant *v=pipeline->variant;
      REQUIRE(pipeline->linear_subgroups &&
              pipeline->logical_local_size[0]==locals[test][0] &&
              pipeline->logical_local_size[1]==locals[test][1] &&
              pipeline->logical_local_size[2]==locals[test][2]);
      REQUIRE(v->local_size[0]==locals[test][0]*locals[test][1]*locals[test][2] &&
              v->local_size[1]==1 && v->local_size[2]==1 && !v->pvtmem_size);
      REQUIRE(v->shared_size<=128 && !v->info.double_threadsize && v->num_uavs==2 && !v->has_barrier);
      printf("BASIC COMPILED case=%u local=%u,%u,%u shared=%u full=%d half=%d instrlen=%u barrier=%u\n",
             test,v->local_size[0],v->local_size[1],v->local_size[2],v->shared_size,
             v->info.max_reg+1,v->info.max_half_reg+1,v->instrlen,v->has_barrier);
   }
   d->DestroyShaderModule(device,module,NULL);
   if (argc==4) {
      for (unsigned i=0;i<5;i++) d->DestroyPipeline(device,pipelines[i],NULL);
      d->DestroyPipelineLayout(device,layout,NULL); d->DestroyDescriptorSetLayout(device,set_layout,NULL);
      d->DestroyDevice(device,NULL); DestroyInstance(instance,NULL); dlclose(lib);
      puts("PASS BASIC compile-only: five pipelines, no probe storage or commands"); return 0;
   }
   struct storage output={0},exchange={0};
   REQUIRE(storage_create(d,device,&memory_properties,OUTPUT_WORDS*4,&output)==0);
   REQUIRE(storage_create(d,device,&memory_properties,EXCHANGE_WORDS*4,&exchange)==0);
   VkMappedMemoryRange ranges[2]={
      {.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=output.memory,.size=VK_WHOLE_SIZE},
      {.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=exchange.memory,.size=VK_WHOLE_SIZE}};
   VkDescriptorPoolSize pool_size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2};
   VkDescriptorPoolCreateInfo dpci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets=1,.poolSizeCount=1,.pPoolSizes=&pool_size};
   VkDescriptorPool descriptor_pool;
   SUCCESS(d->CreateDescriptorPool(device,&dpci,NULL,&descriptor_pool));
   VkDescriptorSetAllocateInfo dsai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool=descriptor_pool,.descriptorSetCount=1,.pSetLayouts=&set_layout};
   VkDescriptorSet set;
   SUCCESS(d->AllocateDescriptorSets(device,&dsai,&set));
   VkDescriptorBufferInfo infos[2]={{output.buffer,0,OUTPUT_WORDS*4},{exchange.buffer,0,EXCHANGE_WORDS*4}};
   VkWriteDescriptorSet writes[2];
   for (unsigned i=0;i<2;i++) writes[i]=(VkWriteDescriptorSet){.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet=set,.dstBinding=i,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&infos[i]};
   d->UpdateDescriptorSets(device,2,writes,0,NULL);
   VkCommandPoolCreateInfo cpci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
   VkCommandPool command_pool;
   SUCCESS(d->CreateCommandPool(device,&cpci,NULL,&command_pool));
   VkCommandBufferAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool=command_pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
   VkCommandBuffer commands;
   SUCCESS(d->AllocateCommandBuffers(device,&ai,&commands));
   VkQueue queue; d->GetDeviceQueue(device,0,0,&queue);
   PFN_vkGetDeviceQueue2 GetDeviceQueue2=(PFN_vkGetDeviceQueue2)GetDeviceProcAddr(device,"vkGetDeviceQueue2"); REQUIRE(GetDeviceQueue2);
   VkDeviceQueueInfo2 queue_info={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2};
   VkQueue queue2=VK_NULL_HANDLE; GetDeviceQueue2(device,&queue_info,&queue2); REQUIRE(queue2==queue);
   VkFenceCreateInfo fci={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence;
   SUCCESS(d->CreateFence(device,&fci,NULL,&fence));
   for (unsigned test=0;test<5;test++) {
      for (size_t i=0;i<output.words;i++) output.map[i]=poison(i,test);
      for (size_t i=0;i<exchange.words;i++) exchange.map[i]=poison(i,test);
      SUCCESS(d->FlushMappedMemoryRanges(device,2,ranges));
      SUCCESS(d->ResetCommandBuffer(commands,0));
      VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
         .flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
      SUCCESS(d->BeginCommandBuffer(commands,&begin));
      d->CmdBindPipeline(commands,VK_PIPELINE_BIND_POINT_COMPUTE,pipelines[test]);
      d->CmdBindDescriptorSets(commands,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);
      uint32_t salt=0x23450000u+test*997u;
      d->CmdPushConstants(commands,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&salt);
      d->CmdDispatch(commands,1,1,1);
      VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
      d->CmdPipelineBarrier(commands,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
         0,1,&host,0,NULL,0,NULL);
      SUCCESS(d->EndCommandBuffer(commands));
      SUCCESS(d->ResetFences(device,1,&fence));
      VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&commands};
      printf("BASIC CASE_BEGIN case=%u local=%u,%u,%u\n",test,locals[test][0],locals[test][1],locals[test][2]); fflush(stdout);
      uint64_t before=inside_device->submit_count;
      SUCCESS(d->QueueSubmit(queue,1,&submit,fence));
      SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
      REQUIRE(inside_device->submit_count==before+1);
      SUCCESS(d->InvalidateMappedMemoryRanges(device,2,ranges));
      if (check_output(&output,&exchange,test)!=0) {
         uint32_t count=locals[test][0]*locals[test][1]*locals[test][2];
         for (uint32_t i=0;i<count;i++) {
            fprintf(stderr,"BASIC RECORD case=%u index=%u",test,i);
            for (unsigned field=0;field<16;field++)
               fprintf(stderr," %08x",output.map[GUARD+16*i+field]);
            fputc('\n',stderr);
         }
         for (size_t i=0;i<EXCHANGE_WORDS;i++)
            fprintf(stderr,"BASIC EXCHANGE case=%u word=%zu value=%08x\n",test,i,exchange.map[i]);
         return 1;
      }
      fflush(stdout);
   }
   d->DestroyFence(device,fence,NULL); d->DestroyCommandPool(device,command_pool,NULL);
   d->DestroyDescriptorPool(device,descriptor_pool,NULL);
   struct storage *stores[]={&output,&exchange};
   for (unsigned i=0;i<2;i++) {
      d->UnmapMemory(device,stores[i]->memory); d->DestroyBuffer(device,stores[i]->buffer,NULL);
      d->FreeMemory(device,stores[i]->memory,NULL);
   }
   for (unsigned i=0;i<5;i++) d->DestroyPipeline(device,pipelines[i],NULL);
   d->DestroyPipelineLayout(device,layout,NULL); d->DestroyDescriptorSetLayout(device,set_layout,NULL);
   d->DestroyDevice(device,NULL); DestroyInstance(instance,NULL); dlclose(lib);
   puts("PASS BASIC: partial/multiple subgroup IDs, converged/divergent elect, shared and buffer barriers/atomics, full records and guards");
   return 0;
}
