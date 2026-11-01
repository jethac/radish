#include "test_common.h"
#include "rd_private.h"
#include <inttypes.h>

enum { GUARD=16, MAX_LOCAL=128, OUTPUT_WORDS=2*GUARD+16*MAX_LOCAL, EXCHANGE_WORDS=40 };
struct storage { VkBuffer buffer; VkDeviceMemory memory; uint32_t *map; size_t words; };
static const uint32_t locals[][3]={{32,1,1},{33,1,1},{8,8,1},{128,1,1}};

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
   const uint32_t n=local[0]*local[1]*local[2], groups=(n+31)/32, salt=0x23450000u+test*997u;
   uint32_t members[4]={0}, elected[4]={0}, odd_members[4]={0}, odd_elected[4]={0}, first_index[4]={0};
   uint32_t seen[4]={0};
   for (uint32_t i=0;i<n;i++) {
      const uint32_t *r=out->map+GUARD+16*i;
      REQUIRE(r[0]==i && r[1]==32 && r[2]==groups && r[3]<groups && r[4]<32);
      uint32_t g=r[3], lane=r[4];
      REQUIRE(!(seen[g] & (1u<<lane)));
      seen[g]|=1u<<lane; members[g]++;
      REQUIRE(r[5]<=1 && r[6]<=1 && ((lane&1u) || !r[6]));
      if (r[5]) { elected[g]++; first_index[g]=i; }
      odd_members[g]+=lane&1u;
      odd_elected[g]+=r[6];
   }
   for (uint32_t g=0;g<groups;g++)
      REQUIRE(members[g]>0 && members[g]<=32 && elected[g]==1 && odd_elected[g]==(odd_members[g]>0));
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

int main(int argc,char **argv)
{
   if (argc!=3 && !(argc==4 && !strcmp(argv[3],"--compile-only"))) {
      fprintf(stderr,"Usage: %s ICD-library basic.spv [--compile-only]\n",argv[0]); return 1;
   }
   void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
   if (!lib) { fprintf(stderr,"%s\n",dlerror()); return 1; }
   PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr");
   REQUIRE(gipa);
   VkInstance instance=VK_NULL_HANDLE;
   INSTANCE_FN(CreateInstance);
   VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_0};
   VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
   SUCCESS(CreateInstance(&ici,NULL,&instance));
   INSTANCE_FN(EnumeratePhysicalDevices); INSTANCE_FN(GetPhysicalDeviceMemoryProperties);
   INSTANCE_FN(CreateDevice); INSTANCE_FN(DestroyInstance);
   uint32_t count=1; VkPhysicalDevice physical;
   SUCCESS(EnumeratePhysicalDevices(instance,&count,&physical)); REQUIRE(count==1);
   struct rd_physical_device *inside=rd_physical_device_from_handle(physical);
   REQUIRE(inside->vk.properties.apiVersion==VK_API_VERSION_1_0 && inside->vk.properties.subgroupSize==32);
   VkPhysicalDeviceMemoryProperties memory_properties;
   GetPhysicalDeviceMemoryProperties(physical,&memory_properties);
   inside->vk.properties.subgroupSupportedStages=VK_SHADER_STAGE_COMPUTE_BIT;
   inside->vk.properties.subgroupSupportedOperations=VK_SUBGROUP_FEATURE_BASIC_BIT;
   puts("BASIC private qualification: internal capability enabled for this instance only; public driver remains unchanged");
   float priority=1.0f;
   VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueCount=1,.pQueuePriorities=&priority};
   VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount=1,.pQueueCreateInfos=&qci};
   VkDevice device;
   SUCCESS(CreateDevice(physical,&dci,NULL,&device));
   struct rd_device *inside_device=rd_device_from_handle(device);
   const struct vk_device_dispatch_table *d=&inside_device->vk.dispatch_table;
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
   VkPipeline pipelines[4]={0};
   for (unsigned test=0;test<4;test++) {
      VkSpecializationInfo spec={.mapEntryCount=3,.pMapEntries=entries,.dataSize=12,.pData=locals[test]};
      VkComputePipelineCreateInfo pci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
         .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,
                 .module=module,.pName="main",.pSpecializationInfo=&spec},.layout=layout};
      SUCCESS(d->CreateComputePipelines(device,VK_NULL_HANDLE,1,&pci,NULL,&pipelines[test]));
      struct ir3_shader_variant *v=rd_pipeline_from_handle(pipelines[test])->variant;
      REQUIRE(v->local_size[0]==locals[test][0] && v->local_size[1]==locals[test][1] &&
              v->local_size[2]==locals[test][2] && !v->pvtmem_size);
      REQUIRE(v->shared_size<=128 && !v->info.double_threadsize && v->num_uavs==2 && !v->has_barrier);
      printf("BASIC COMPILED case=%u local=%u,%u,%u shared=%u full=%d half=%d instrlen=%u barrier=%u\n",
             test,v->local_size[0],v->local_size[1],v->local_size[2],v->shared_size,
             v->info.max_reg+1,v->info.max_half_reg+1,v->instrlen,v->has_barrier);
   }
   d->DestroyShaderModule(device,module,NULL);
   if (argc==4) {
      for (unsigned i=0;i<4;i++) d->DestroyPipeline(device,pipelines[i],NULL);
      d->DestroyPipelineLayout(device,layout,NULL); d->DestroyDescriptorSetLayout(device,set_layout,NULL);
      d->DestroyDevice(device,NULL); DestroyInstance(instance,NULL); dlclose(lib);
      puts("PASS BASIC compile-only: four pipelines, no probe storage or commands"); return 0;
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
   VkFenceCreateInfo fci={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence;
   SUCCESS(d->CreateFence(device,&fci,NULL,&fence));
   for (unsigned test=0;test<4;test++) {
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
      REQUIRE(check_output(&output,&exchange,test)==0); fflush(stdout);
   }
   d->DestroyFence(device,fence,NULL); d->DestroyCommandPool(device,command_pool,NULL);
   d->DestroyDescriptorPool(device,descriptor_pool,NULL);
   struct storage *stores[]={&output,&exchange};
   for (unsigned i=0;i<2;i++) {
      d->UnmapMemory(device,stores[i]->memory); d->DestroyBuffer(device,stores[i]->buffer,NULL);
      d->FreeMemory(device,stores[i]->memory,NULL);
   }
   for (unsigned i=0;i<4;i++) d->DestroyPipeline(device,pipelines[i],NULL);
   d->DestroyPipelineLayout(device,layout,NULL); d->DestroyDescriptorSetLayout(device,set_layout,NULL);
   d->DestroyDevice(device,NULL); DestroyInstance(instance,NULL); dlclose(lib);
   puts("PASS BASIC: partial/multiple subgroup IDs, converged/divergent elect, shared and buffer barriers/atomics, full records and guards");
   return 0;
}
