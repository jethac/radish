#include "test_common.h"
#include "rd_private.h"
#include <inttypes.h>

enum { LX=4, LY=2, LZ=2, NX=65, NY=2, NZ=2, GUARD=16,
       RECORDS=NX*NY*NZ*LX*LY*LZ, WORDS=2*GUARD+16*RECORDS };
struct params { uint32_t base[4], grid[4]; };
struct storage { VkBuffer buffer; VkDeviceMemory memory; uint32_t *map; size_t words; };
_Static_assert(sizeof(struct params)==32, "push layout");
_Static_assert(WORDS*4 < 1024*1024, "bounded output");

static uint32_t sentinel(size_t i, unsigned round)
{
   return 0xa5000000u ^ ((uint32_t)i*117u) ^ (round*0x01010000u);
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
                          VkDeviceSize bytes, VkBufferUsageFlags usage, struct storage *s)
{
   VkBufferCreateInfo info={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size=bytes,.usage=usage};
   SUCCESS(d->CreateBuffer(device,&info,NULL,&s->buffer));
   VkBufferMemoryRequirementsInfo2 requirement_info={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2,.buffer=s->buffer};
   VkMemoryRequirements2 requirement_result={.sType=VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
   d->GetBufferMemoryRequirements2(device,&requirement_info,&requirement_result);
   VkMemoryRequirements requirements=requirement_result.memoryRequirements;
   REQUIRE(requirements.size>=bytes && requirements.size<=1024*1024 && !(requirements.size%4));
   uint32_t type=UINT32_MAX;
   for (uint32_t i=0;i<properties->memoryTypeCount;i++) {
      VkMemoryPropertyFlags flags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      if ((requirements.memoryTypeBits&(1u<<i)) &&
          (properties->memoryTypes[i].propertyFlags&flags)==flags) { type=i; break; }
   }
   REQUIRE(type!=UINT32_MAX);
   VkMemoryAllocateFlagsInfo mask={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,.flags=VK_MEMORY_ALLOCATE_DEVICE_MASK_BIT,.deviceMask=1};
   VkMemoryAllocateInfo allocation={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext=&mask,.allocationSize=requirements.size,.memoryTypeIndex=type};
   SUCCESS(d->AllocateMemory(device,&allocation,NULL,&s->memory));
   uint32_t index=0;
   VkBindBufferMemoryDeviceGroupInfo group={.sType=VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_DEVICE_GROUP_INFO,.deviceIndexCount=1,.pDeviceIndices=&index};
   VkBindBufferMemoryInfo binding={.sType=VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO,.pNext=&group,.buffer=s->buffer,.memory=s->memory};
   SUCCESS(d->BindBufferMemory2(device,1,&binding));
   SUCCESS(d->MapMemory(device,s->memory,0,VK_WHOLE_SIZE,0,(void **)&s->map));
   s->words=requirements.size/4;
   return 0;
}

static int layout_checks(const struct vk_device_dispatch_table *d, VkDevice device)
{
   VkDescriptorSetLayoutBinding bindings[2]={
      {.binding=4096,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
       .descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT},
      {.binding=7,.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLER,
       .descriptorCount=0,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT}};
   VkDescriptorSetLayoutCreateInfo info={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount=2,.pBindings=bindings};
   VkDescriptorSetLayoutSupport support={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT};
   d->GetDescriptorSetLayoutSupport(device,&info,&support);
   REQUIRE(support.supported);
   VkDescriptorSetLayout layout;
   SUCCESS(d->CreateDescriptorSetLayout(device,&info,NULL,&layout));
   struct rd_set_layout *inside=rd_set_layout_from_handle(layout);
   REQUIRE(inside->binding_count==1 && inside->descriptor_count==1 &&
           inside->bindings[0].binding==4096 && inside->bindings[0].slot==0);
   d->DestroyDescriptorSetLayout(device,layout,NULL);
   info.bindingCount=0;
   d->GetDescriptorSetLayoutSupport(device,&info,&support); REQUIRE(support.supported);
   SUCCESS(d->CreateDescriptorSetLayout(device,&info,NULL,&layout));
   d->DestroyDescriptorSetLayout(device,layout,NULL);
   info.bindingCount=1;
   bindings[0].descriptorCount=RD_MAX_BINDINGS;
   d->GetDescriptorSetLayoutSupport(device,&info,&support); REQUIRE(support.supported);
   SUCCESS(d->CreateDescriptorSetLayout(device,&info,NULL,&layout));
   d->DestroyDescriptorSetLayout(device,layout,NULL);
   bindings[0].descriptorCount=RD_MAX_BINDINGS+1;
   d->GetDescriptorSetLayoutSupport(device,&info,&support); REQUIRE(!support.supported);
   bindings[0].descriptorType=VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK;
   bindings[0].descriptorCount=16;
   d->GetDescriptorSetLayoutSupport(device,&info,&support); REQUIRE(!support.supported);
   puts("API5 layout PASS: empty, sparse/zero-count, exact cap, over cap, unsupported type");
   return 0;
}

static int check_output(const struct storage *out, unsigned round, const uint32_t input[12])
{
   for (size_t i=0;i<out->words;i++) {
      uint32_t expected=sentinel(i,round);
      if (i>=GUARD && i<GUARD+16*RECORDS) {
         uint32_t n=(i-GUARD)/16, field=(i-GUARD)%16;
         uint32_t x=n%(NX*LX),y=n/(NX*LX)%(NY*LY),z=n/(NX*LX)/(NY*LY);
         uint32_t lx=x%LX,ly=y%LY,lz=z%LZ;
         uint32_t record[16]={NX,NY,NZ,3+x/LX,4+y/LY,5+z/LZ,
            3*LX+x,4*LY+y,5*LZ+z,lx,ly,lz,(lz*LY+ly)*LX+lx,
            input[0],input[round?0:4],input[round?0:8]};
         expected=record[field];
      }
      if (out->map[i]!=expected) {
         fprintf(stderr,"FAIL round=%u output_word=%zu got=%08x expected=%08x\n",
                 round,i,out->map[i],expected); return 1;
      }
   }
   return 0;
}

int main(int argc,char **argv)
{
   if (argc!=3 && !(argc==4 && !strcmp(argv[3],"--compile-only"))) {
      fprintf(stderr,"Usage: %s ICD-library probe.spv [--compile-only]\n",argv[0]); return 1;
   }
   REQUIRE(!getenv("RADISH_BATCH_CHUNKS") || !strcmp(getenv("RADISH_BATCH_CHUNKS"),"1"));
   REQUIRE(!getenv("RADISH_TILED_GROUPS_X") || !strcmp(getenv("RADISH_TILED_GROUPS_X"),"1"));
   void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
   if (!lib) { fprintf(stderr,"%s\n",dlerror()); return 1; }
   PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr");
   REQUIRE(gipa);
   VkInstance instance=VK_NULL_HANDLE;
   INSTANCE_FN(CreateInstance);
   VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_0};
   const char *instance_extensions[]={VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
      VK_KHR_DEVICE_GROUP_CREATION_EXTENSION_NAME,VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME};
   VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app,
      .enabledExtensionCount=3,.ppEnabledExtensionNames=instance_extensions};
   SUCCESS(CreateInstance(&ici,NULL,&instance));
   INSTANCE_FN(EnumeratePhysicalDevices); INSTANCE_FN(GetPhysicalDeviceMemoryProperties);
   INSTANCE_FN(GetPhysicalDeviceProperties); INSTANCE_FN(CreateDevice);
   INSTANCE_FN(GetDeviceProcAddr); INSTANCE_FN(DestroyInstance);
   uint32_t count=1; VkPhysicalDevice physical;
   SUCCESS(EnumeratePhysicalDevices(instance,&count,&physical)); REQUIRE(count==1);
   PFN_vkEnumeratePhysicalDeviceGroupsKHR enumerate_groups=(PFN_vkEnumeratePhysicalDeviceGroupsKHR)gipa(instance,"vkEnumeratePhysicalDeviceGroupsKHR");
   REQUIRE(enumerate_groups);
   uint32_t group_count=0; SUCCESS(enumerate_groups(instance,&group_count,NULL)); REQUIRE(group_count==1);
   VkPhysicalDeviceGroupProperties group_properties={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES};
   group_count=0; REQUIRE(enumerate_groups(instance,&group_count,&group_properties)==VK_INCOMPLETE && group_count==0);
   group_count=1; SUCCESS(enumerate_groups(instance,&group_count,&group_properties));
   REQUIRE(group_count==1 && group_properties.physicalDeviceCount==1 && group_properties.physicalDevices[0]==physical && !group_properties.subsetAllocation);
   VkDeviceGroupDeviceCreateInfo group_create={.sType=VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO,
      .physicalDeviceCount=1,.pPhysicalDevices=&physical};
   VkPhysicalDeviceMemoryProperties memory_properties;
   GetPhysicalDeviceMemoryProperties(physical,&memory_properties);
   VkPhysicalDeviceProperties properties;
   GetPhysicalDeviceProperties(physical,&properties);
   REQUIRE(properties.apiVersion==VK_API_VERSION_1_0);
   float priority=1.0f;
   VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueCount=1,.pQueuePriorities=&priority};
   const char *extensions[]={VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME,VK_KHR_MAINTENANCE_3_EXTENSION_NAME,
      VK_KHR_BIND_MEMORY_2_EXTENSION_NAME,VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,VK_KHR_DEVICE_GROUP_EXTENSION_NAME};
   VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.pNext=&group_create,
      .queueCreateInfoCount=1,.pQueueCreateInfos=&qci,.enabledExtensionCount=5,.ppEnabledExtensionNames=extensions};
   VkDevice device;
   SUCCESS(CreateDevice(physical,&dci,NULL,&device));
   PFN_vkGetDeviceProcAddr gdpa=GetDeviceProcAddr;
   DEVICE_FN(CmdExecuteCommands);
   struct rd_device *inside_device=rd_device_from_handle(device);
   struct vk_device_dispatch_table aliases=inside_device->vk.dispatch_table;
   aliases.CmdDispatchBase=(PFN_vkCmdDispatchBase)gdpa(device,"vkCmdDispatchBaseKHR");
   aliases.CmdSetDeviceMask=(PFN_vkCmdSetDeviceMask)gdpa(device,"vkCmdSetDeviceMaskKHR");
   REQUIRE(aliases.CmdDispatchBase && aliases.CmdSetDeviceMask && gdpa(device,"vkGetDeviceGroupPeerMemoryFeaturesKHR"));
   puts("API5 device-group aliases reachable; peer-memory call unexercised: singleton has no distinct indices");
   aliases.GetDescriptorSetLayoutSupport=(PFN_vkGetDescriptorSetLayoutSupport)gdpa(device,"vkGetDescriptorSetLayoutSupportKHR");
   aliases.CreateDescriptorUpdateTemplate=(PFN_vkCreateDescriptorUpdateTemplate)gdpa(device,"vkCreateDescriptorUpdateTemplateKHR");
   aliases.DestroyDescriptorUpdateTemplate=(PFN_vkDestroyDescriptorUpdateTemplate)gdpa(device,"vkDestroyDescriptorUpdateTemplateKHR");
   aliases.UpdateDescriptorSetWithTemplate=(PFN_vkUpdateDescriptorSetWithTemplate)gdpa(device,"vkUpdateDescriptorSetWithTemplateKHR");
   aliases.BindBufferMemory2=(PFN_vkBindBufferMemory2)gdpa(device,"vkBindBufferMemory2KHR");
   aliases.GetBufferMemoryRequirements2=(PFN_vkGetBufferMemoryRequirements2)gdpa(device,"vkGetBufferMemoryRequirements2KHR");
   REQUIRE(aliases.GetDescriptorSetLayoutSupport && aliases.CreateDescriptorUpdateTemplate && aliases.DestroyDescriptorUpdateTemplate && aliases.UpdateDescriptorSetWithTemplate && aliases.BindBufferMemory2 && aliases.GetBufferMemoryRequirements2);
   PFN_vkGetPhysicalDeviceProperties2KHR properties2=(PFN_vkGetPhysicalDeviceProperties2KHR)gipa(instance,"vkGetPhysicalDeviceProperties2KHR");
   REQUIRE(properties2);
   VkPhysicalDeviceIDProperties ids={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
   VkPhysicalDeviceMaintenance3Properties maintenance={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES,.pNext=&ids};
   VkPhysicalDeviceProperties2 properties_chain={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,.pNext=&maintenance};
   properties2(physical,&properties_chain);
   REQUIRE(maintenance.maxPerSetDescriptors==1024 && maintenance.maxMemoryAllocationSize==UINT32_MAX-4095ull);
   uint8_t zeros[VK_UUID_SIZE]={0};
   REQUIRE(memcmp(ids.deviceUUID,zeros,VK_UUID_SIZE) && memcmp(ids.driverUUID,zeros,VK_UUID_SIZE));
   REQUIRE(memcmp(ids.deviceUUID,ids.driverUUID,VK_UUID_SIZE) && !ids.deviceLUIDValid);
   VkPhysicalDeviceIDProperties repeat_ids=ids;
   properties2(physical,&properties_chain);
   REQUIRE(!memcmp(ids.deviceUUID,repeat_ids.deviceUUID,VK_UUID_SIZE) && !memcmp(ids.driverUUID,repeat_ids.driverUUID,VK_UUID_SIZE));
   PFN_vkGetPhysicalDeviceExternalBufferPropertiesKHR external_buffer=(PFN_vkGetPhysicalDeviceExternalBufferPropertiesKHR)gipa(instance,"vkGetPhysicalDeviceExternalBufferPropertiesKHR");
   PFN_vkGetPhysicalDeviceImageFormatProperties2KHR image_properties=(PFN_vkGetPhysicalDeviceImageFormatProperties2KHR)gipa(instance,"vkGetPhysicalDeviceImageFormatProperties2KHR");
   REQUIRE(external_buffer && image_properties);
   VkPhysicalDeviceExternalImageFormatInfo external_image={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT};
   VkPhysicalDeviceImageFormatInfo2 image_info={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,.pNext=&external_image,.format=VK_FORMAT_R32_UINT,.type=VK_IMAGE_TYPE_2D,.tiling=VK_IMAGE_TILING_LINEAR,.usage=VK_IMAGE_USAGE_STORAGE_BIT};
   VkExternalImageFormatProperties external_image_result={.sType=VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
   VkImageFormatProperties2 image_result={.sType=VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,.pNext=&external_image_result};
   REQUIRE(image_properties(physical,&image_info,&image_result)==VK_ERROR_FORMAT_NOT_SUPPORTED);
   external_image.handleType=0; SUCCESS(image_properties(physical,&image_info,&image_result));
   REQUIRE(!external_image_result.externalMemoryProperties.externalMemoryFeatures && !external_image_result.externalMemoryProperties.exportFromImportedHandleTypes && !external_image_result.externalMemoryProperties.compatibleHandleTypes);
   puts("API5 group/ID/external-image PASS: singleton enumeration/create, stable nonzero UUIDs, no external sharing");
   puts("API5 KHR aliases reachable: templates/layout support/bind memory2/memory requirements2/properties2");
   const struct vk_device_dispatch_table *d=&aliases;
   const struct vk_physical_device_dispatch_table *pd=&rd_physical_device_from_handle(physical)->vk.dispatch_table;
   REQUIRE(d->CmdDispatchBase && d->GetDescriptorSetLayoutSupport &&
           d->UpdateDescriptorSetWithTemplate && d->CreateDescriptorUpdateTemplate &&
           d->DestroyDescriptorUpdateTemplate && pd->GetPhysicalDeviceExternalBufferProperties);
   printf("API5 internal dispatch; public core1.1 lookups base=%u support=%u template=%u external=%u api=%u\n",
      gdpa(device,"vkCmdDispatchBase")!=NULL,gdpa(device,"vkGetDescriptorSetLayoutSupport")!=NULL,
      gdpa(device,"vkUpdateDescriptorSetWithTemplate")!=NULL,
      gipa(instance,"vkGetPhysicalDeviceExternalBufferProperties")!=NULL,properties.apiVersion);
   REQUIRE(layout_checks(d,device)==0);
   VkPhysicalDeviceExternalBufferInfo external_info={.sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,
      .usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT};
   VkExternalBufferProperties external={.sType=VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
   memset(&external.externalMemoryProperties,0xff,sizeof(external.externalMemoryProperties));
   external_buffer(physical,&external_info,&external);
   REQUIRE(external.externalMemoryProperties.externalMemoryFeatures==0 &&
           external.externalMemoryProperties.exportFromImportedHandleTypes==0 &&
           external.externalMemoryProperties.compatibleHandleTypes==external_info.handleType);
   puts("API5 external PASS: no import/export feature, compatible queried handle");
   VkDescriptorSetLayoutBinding bindings[5];
   const uint32_t order[5]={3,0,99,2,1};
   for (unsigned i=0;i<5;i++) bindings[i]=(VkDescriptorSetLayoutBinding){
      .binding=order[i],.descriptorType=order[i]==99?VK_DESCRIPTOR_TYPE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount=order[i]==99?0:1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
   VkDescriptorSetLayoutCreateInfo slci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount=5,.pBindings=bindings};
   VkDescriptorSetLayout set_layout;
   SUCCESS(d->CreateDescriptorSetLayout(device,&slci,NULL,&set_layout));
   struct rd_set_layout *il=rd_set_layout_from_handle(set_layout);
   REQUIRE(il->binding_count==4 && il->descriptor_count==4);
   for (unsigned i=0;i<4;i++) REQUIRE(il->bindings[i].binding==i && il->bindings[i].slot==i);
   VkPushConstantRange push={.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,.size=sizeof(struct params)};
   VkPipelineLayoutCreateInfo plci={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount=1,.pSetLayouts=&set_layout,.pushConstantRangeCount=1,.pPushConstantRanges=&push};
   VkPipelineLayout layout;
   SUCCESS(d->CreatePipelineLayout(device,&plci,NULL,&layout));
   size_t code_size; uint32_t *code=read_spirv(argv[2],&code_size); REQUIRE(code);
   VkShaderModuleCreateInfo smci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=code_size,.pCode=code};
   VkShaderModule module;
   SUCCESS(d->CreateShaderModule(device,&smci,NULL,&module)); free(code);
   VkComputePipelineCreateInfo pci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .flags=VK_PIPELINE_CREATE_DISPATCH_BASE_BIT,
      .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,
              .module=module,.pName="main"},.layout=layout};
   VkPipeline pipeline[2];
   SUCCESS(d->CreateComputePipelines(device,VK_NULL_HANDLE,1,&pci,NULL,&pipeline[0]));
   pci.flags |= VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT;
   SUCCESS(d->CreateComputePipelines(device,VK_NULL_HANDLE,1,&pci,NULL,&pipeline[1]));
   REQUIRE(rd_pipeline_from_handle(pipeline[1])->variant->shader_options.nir_options.disable_optimizations);
   d->DestroyShaderModule(device,module,NULL);
   if (argc==4) {
      for (unsigned i=0;i<2;i++) d->DestroyPipeline(device,pipeline[i],NULL); d->DestroyPipelineLayout(device,layout,NULL);
      d->DestroyDescriptorSetLayout(device,set_layout,NULL); d->DestroyDevice(device,NULL);
      DestroyInstance(instance,NULL); dlclose(lib);
      puts("PASS API5 compile-only: query checks and pipeline creation; no probe dispatch"); return 0;
   }
   struct storage output={0},input={0};
   REQUIRE(storage_create(d,device,&memory_properties,WORDS*4,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&output)==0);
   REQUIRE(storage_create(d,device,&memory_properties,64,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,&input)==0);
   VkMappedMemoryRange ranges[2]={
      {.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=output.memory,.size=VK_WHOLE_SIZE},
      {.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=input.memory,.size=VK_WHOLE_SIZE}};
   VkDescriptorPoolSize pool_size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4};
   VkDescriptorPoolCreateInfo dpci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets=1,.poolSizeCount=1,.pPoolSizes=&pool_size};
   VkDescriptorPool descriptor_pool;
   SUCCESS(d->CreateDescriptorPool(device,&dpci,NULL,&descriptor_pool));
   VkDescriptorSetAllocateInfo dsai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool=descriptor_pool,.descriptorSetCount=1,.pSetLayouts=&set_layout};
   VkDescriptorSet set;
   SUCCESS(d->AllocateDescriptorSets(device,&dsai,&set));
   VkDescriptorUpdateTemplate templates[2];
   for (unsigned i=0;i<2;i++) {
      VkDescriptorUpdateTemplateEntry entry={.dstBinding=i?1:0,.descriptorCount=i?3:4,
         .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.offset=1,
         .stride=i?0:sizeof(VkDescriptorBufferInfo)+3};
      VkDescriptorUpdateTemplateCreateInfo ti={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
         .descriptorUpdateEntryCount=1,.pDescriptorUpdateEntries=&entry,
         .templateType=VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET,.descriptorSetLayout=set_layout};
      SUCCESS(d->CreateDescriptorUpdateTemplate(device,&ti,NULL,&templates[i]));
   }
   VkCommandPoolCreateInfo cpci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
   VkCommandPool command_pool;
   SUCCESS(d->CreateCommandPool(device,&cpci,NULL,&command_pool));
   VkCommandBuffer commands[2];
   for (unsigned i=0;i<2;i++) {
      VkCommandBufferAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
         .commandPool=command_pool,.level=i?VK_COMMAND_BUFFER_LEVEL_SECONDARY:VK_COMMAND_BUFFER_LEVEL_PRIMARY,
         .commandBufferCount=1};
      SUCCESS(d->AllocateCommandBuffers(device,&ai,&commands[i]));
   }
   VkQueue queue; d->GetDeviceQueue(device,0,0,&queue);
   VkFenceCreateInfo fci={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence;
   SUCCESS(d->CreateFence(device,&fci,NULL,&fence));
   const struct params params={{3,4,5,GUARD},{NX,NY,NZ,0}};
   for (unsigned round=0;round<2;round++) {
      for (size_t i=0;i<output.words;i++) output.map[i]=sentinel(i,round);
      for (size_t i=0;i<input.words;i++) input.map[i]=sentinel(i,round);
      SUCCESS(d->FlushMappedMemoryRanges(device,2,ranges));
      uint8_t data[1+4*(sizeof(VkDescriptorBufferInfo)+3)]; memset(data,0x5a,sizeof(data));
      for (unsigned i=0;i<4;i++) {
         VkDescriptorBufferInfo info={.buffer=i?input.buffer:output.buffer,
            .offset=i?16*i:0,.range=i?16:WORDS*4};
         memcpy(data+1+i*(sizeof(info)+3),&info,sizeof(info));
      }
      d->UpdateDescriptorSetWithTemplate(device,set,templates[0],data);
      if (round) {
         VkDescriptorBufferInfo info={.buffer=input.buffer,.offset=16,.range=16};
         memcpy(data+1,&info,sizeof(info));
         d->UpdateDescriptorSetWithTemplate(device,set,templates[1],data);
      }
      memset(data,0xc3,sizeof(data));
      struct rd_descriptor_set *is=rd_descriptor_set_from_handle(set);
      for (unsigned i=0;i<4;i++) {
         REQUIRE(is->descriptors[i].buffer==rd_buffer_from_handle(i?input.buffer:output.buffer));
         REQUIRE(is->descriptors[i].offset==(i?(round?16:16*i):0));
         REQUIRE(is->descriptors[i].range==(i?16:WORDS*4));
      }
      SUCCESS(d->ResetCommandBuffer(commands[0],0));
      SUCCESS(d->ResetCommandBuffer(commands[1],0));
      VkCommandBufferInheritanceInfo inheritance={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
      VkDeviceGroupCommandBufferBeginInfo begin_group={.sType=VK_STRUCTURE_TYPE_DEVICE_GROUP_COMMAND_BUFFER_BEGIN_INFO,.deviceMask=1};
      VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
         .pNext=&begin_group,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,.pInheritanceInfo=&inheritance};
      SUCCESS(d->BeginCommandBuffer(commands[1],&begin));
      uint32_t updates[12],saved[12];
      for (unsigned i=0;i<12;i++) saved[i]=updates[i]=0x12340000u+round*0x1000u+i*37u;
      d->CmdUpdateBuffer(commands[1],input.buffer,16,sizeof(updates),updates);
      memset(updates,0xa6,sizeof(updates));
      VkBufferMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
         .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT,
         .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
         .buffer=input.buffer,.offset=16,.size=sizeof(saved)};
      d->CmdPipelineBarrier(commands[1],VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
         0,0,NULL,1,&barrier,0,NULL);
      d->CmdSetDeviceMask(commands[1],1);
      d->CmdBindPipeline(commands[1],VK_PIPELINE_BIND_POINT_COMPUTE,pipeline[round]);
      d->CmdBindDescriptorSets(commands[1],VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);
      d->CmdPushConstants(commands[1],layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(params),&params);
      d->CmdDispatchBase(commands[1],3,4,5,NX,NY,NZ);
      SUCCESS(d->EndCommandBuffer(commands[1]));
      begin.pInheritanceInfo=NULL;
      SUCCESS(d->BeginCommandBuffer(commands[0],&begin));
      CmdExecuteCommands(commands[0],1,&commands[1]);
      VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
      d->CmdPipelineBarrier(commands[0],VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
         VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,NULL,0,NULL);
      SUCCESS(d->EndCommandBuffer(commands[0]));
      struct rd_command_buffer *primary=rd_command_buffer_from_handle(commands[0]);
      struct rd_command_buffer *secondary=rd_command_buffer_from_handle(commands[1]);
      REQUIRE(util_dynarray_num_elements(&primary->commands,struct rd_command)==2);
      REQUIRE(util_dynarray_num_elements(&secondary->commands,struct rd_command)==2);
      struct rd_command *pc=util_dynarray_element(&primary->commands,struct rd_command,0);
      struct rd_command *sc=util_dynarray_element(&secondary->commands,struct rd_command,0);
      REQUIRE(pc->type==RD_CMD_UPDATE && sc->type==RD_CMD_UPDATE);
      REQUIRE(pc->transfer.data!=sc->transfer.data && sc->transfer.data!=updates);
      REQUIRE(pc->transfer.size==sizeof(saved) && sc->transfer.size==sizeof(saved));
      REQUIRE(!memcmp(pc->transfer.data,saved,sizeof(saved)) && !memcmp(sc->transfer.data,saved,sizeof(saved)));
      REQUIRE(pc[1].type==RD_CMD_DISPATCH && !memcmp(pc[1].dispatch.base,params.base,12) &&
              !memcmp(pc[1].dispatch.grid,params.grid,12));
      uint64_t before=inside_device->submit_count;
      SUCCESS(d->ResetFences(device,1,&fence));
      uint32_t submit_mask=1;
      VkDeviceGroupSubmitInfo group_submit={.sType=VK_STRUCTURE_TYPE_DEVICE_GROUP_SUBMIT_INFO,.commandBufferCount=1,.pCommandBufferDeviceMasks=&submit_mask};
      VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.pNext=&group_submit,.commandBufferCount=1,.pCommandBuffers=&commands[0]};
      printf("API5 ROUND_BEGIN round=%u template=%s grid=65,2,2 base=3,4,5\n",round,round?"zero-stride":"unaligned-strided"); fflush(stdout);
      SUCCESS(d->QueueSubmit(queue,1,&submit,fence));
      SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
      SUCCESS(d->InvalidateMappedMemoryRanges(device,2,ranges));
      uint64_t submits=inside_device->submit_count-before;
      REQUIRE(submits>=8);
      REQUIRE(check_output(&output,round,saved)==0);
      for (size_t i=0;i<input.words;i++) {
         uint32_t expected=i>=4 && i<16?saved[i-4]:sentinel(i,round);
         REQUIRE(input.map[i]==expected);
      }
      printf("API5 ROUND_PASS round=%u records=%u output_words=%zu submits=%" PRIu64 " update_deepcopy=1 input_guards=1\n",
         round,RECORDS,output.words,submits); fflush(stdout);
   }
   d->DestroyFence(device,fence,NULL); d->DestroyCommandPool(device,command_pool,NULL);
   for (unsigned i=0;i<2;i++) d->DestroyDescriptorUpdateTemplate(device,templates[i],NULL);
   d->DestroyDescriptorPool(device,descriptor_pool,NULL);
   struct storage *stores[]={&output,&input};
   for (unsigned i=0;i<2;i++) {
      d->UnmapMemory(device,stores[i]->memory); d->DestroyBuffer(device,stores[i]->buffer,NULL);
      d->FreeMemory(device,stores[i]->memory,NULL);
   }
   for (unsigned i=0;i<2;i++) d->DestroyPipeline(device,pipeline[i],NULL); d->DestroyPipelineLayout(device,layout,NULL);
   d->DestroyDescriptorSetLayout(device,set_layout,NULL); d->DestroyDevice(device,NULL);
   DestroyInstance(instance,NULL); dlclose(lib);
   puts("PASS DEVICE_GROUP: public dispatch/mask, allocation/bind/begin/submit masks and DeviceIndex zero oracle");
   puts("PASS NOOPT: second pipeline preserves full base-grid oracle with optional optimizations disabled");
   puts("PASS API5: two real base-grid executions, secondary copy/reset/reuse, templates, layout support and external query");
   return 0;
}
