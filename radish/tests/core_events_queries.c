#include "test_common.h"
#include "rd_private.h"
#include <inttypes.h>
#include <pthread.h>
#include <time.h>

struct storage { VkBuffer buffer; VkDeviceMemory memory; uint32_t *map; size_t words; };
struct params { uint32_t offset, salt; };
enum { GUARD=16, DIRECT_WORDS=3*2*32, INDIRECT_WORDS=2*32, TOTAL_WORDS=2*GUARD+DIRECT_WORDS+INDIRECT_WORDS };

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
   VkMemoryRequirements requirements;
   d->GetBufferMemoryRequirements(device,s->buffer,&requirements);
   REQUIRE(requirements.size>=bytes && requirements.size<=1024*1024 && !(requirements.size%4));
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

static int submit_wait(const struct vk_device_dispatch_table *d, VkDevice device,
                       VkQueue queue, VkCommandBuffer command, VkFence fence)
{
   SUCCESS(d->EndCommandBuffer(command));
   SUCCESS(d->ResetFences(device,1,&fence));
   VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount=1,.pCommandBuffers=&command};
   SUCCESS(d->QueueSubmit(queue,1,&submit,fence));
   SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
   return 0;
}

static int unavailable(const struct vk_device_dispatch_table *d, VkDevice device,
                       VkQueryPool pool)
{
   uint64_t values[8];
   memset(values,0xa5,sizeof(values));
   REQUIRE(d->GetQueryPoolResults(device,pool,0,2,sizeof(values),values,32,
      VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)==VK_NOT_READY);
   for (unsigned i=0;i<8;i++)
      REQUIRE(values[i]==((i==2 || i==6)?0:UINT64_C(0xa5a5a5a5a5a5a5a5)));
   SUCCESS(d->GetQueryPoolResults(device,pool,0,2,sizeof(values),values,32,
      VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT|VK_QUERY_RESULT_PARTIAL_BIT));
   for (unsigned i=0;i<8;i++)
      REQUIRE(values[i]==((i==3 || i==7)?UINT64_C(0xa5a5a5a5a5a5a5a5):0));
   return 0;
}

struct loss_wait {
   const struct vk_device_dispatch_table *d;
   VkDevice device;
   VkFence fence;
   int idle, started, done;
   VkResult result;
};

static void *loss_wait_thread(void *data)
{
   struct loss_wait *wait=data;
   p_atomic_set(&wait->started,1);
   wait->result=wait->idle?wait->d->DeviceWaitIdle(wait->device):
      wait->d->WaitForFences(wait->device,1,&wait->fence,VK_TRUE,UINT64_MAX);
   p_atomic_set(&wait->done,1);
   return NULL;
}

static int loss_wake(const struct vk_device_dispatch_table *d, VkDevice device,
                     VkQueue queue, VkCommandBuffer command, VkFence fence, VkEvent event)
{
   VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   SUCCESS(d->BeginCommandBuffer(command,&begin));
   d->CmdWaitEvents(command,1,&event,VK_PIPELINE_STAGE_HOST_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,NULL,0,NULL,0,NULL);
   SUCCESS(d->EndCommandBuffer(command));
   VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount=1,.pCommandBuffers=&command};
   SUCCESS(d->QueueSubmit(queue,1,&submit,fence));
   REQUIRE(d->WaitForFences(device,1,&fence,VK_TRUE,1000000)==VK_TIMEOUT);
   struct loss_wait waits[2]={{.d=d,.device=device,.fence=fence},
      {.d=d,.device=device,.fence=fence,.idle=1}};
   pthread_t threads[2];
   for (unsigned i=0;i<2;i++) REQUIRE(!pthread_create(&threads[i],NULL,loss_wait_thread,&waits[i]));
   struct timespec delay={.tv_nsec=50000000};
   while (!p_atomic_read(&waits[0].started) || !p_atomic_read(&waits[1].started))
      nanosleep(&delay,NULL);
   nanosleep(&delay,NULL);
   REQUIRE(!p_atomic_read(&waits[0].done) && !p_atomic_read(&waits[1].done));
   struct rd_device *inside=rd_device_from_handle(device);
   inside->vk._lost.reported=true;
   p_atomic_inc(&inside->vk._lost.lost);
   for (unsigned i=0;i<2;i++) {
      REQUIRE(!pthread_join(threads[i],NULL));
      REQUIRE(waits[i].result==VK_ERROR_DEVICE_LOST);
   }
   struct vk_queue *inside_queue=vk_queue_from_handle(queue);
   for (unsigned i=0;!p_atomic_read(&inside_queue->_lost.lost) && i<100;i++) {
      struct timespec pause={.tv_nsec=1000000}; nanosleep(&pause,NULL);
   }
   REQUIRE(p_atomic_read(&inside_queue->_lost.lost));
   REQUIRE(d->GetEventStatus(device,event)==VK_ERROR_DEVICE_LOST);
   puts("EVENT_QUERY LOSS_WAKE PASS: synthetic host loss, fence wait and queue drain returned; no GPU fault generated");
   return 0;
}

int main(int argc,char **argv)
{
   bool compile_only=argc==4 && !strcmp(argv[3],"--compile-only");
   bool loss_only=argc==4 && !strcmp(argv[3],"--loss-wake");
   if (argc!=3 && !compile_only && !loss_only) {
      fprintf(stderr,"Usage: %s ICD-library probe.spv [--compile-only|--loss-wake]\n",argv[0]); return 1;
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
   INSTANCE_FN(GetPhysicalDeviceProperties); INSTANCE_FN(GetPhysicalDeviceFeatures);
   INSTANCE_FN(CreateDevice); INSTANCE_FN(DestroyInstance);
   uint32_t count=1; VkPhysicalDevice physical;
   SUCCESS(EnumeratePhysicalDevices(instance,&count,&physical)); REQUIRE(count==1);
   VkPhysicalDeviceMemoryProperties memory_properties;
   GetPhysicalDeviceMemoryProperties(physical,&memory_properties);
   VkPhysicalDeviceProperties properties;
   GetPhysicalDeviceProperties(physical,&properties);
   VkPhysicalDeviceFeatures available;
   GetPhysicalDeviceFeatures(physical,&available); REQUIRE(available.pipelineStatisticsQuery);
   VkPhysicalDeviceFeatures enabled={.pipelineStatisticsQuery=VK_TRUE};
   float priority=1.0f;
   VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueCount=1,.pQueuePriorities=&priority};
   VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount=1,.pQueueCreateInfos=&qci,.pEnabledFeatures=&enabled};
   VkDevice device;
   SUCCESS(CreateDevice(physical,&dci,NULL,&device));
   const struct vk_device_dispatch_table *d=&rd_device_from_handle(device)->vk.dispatch_table;
   REQUIRE(d->CreateEvent && d->GetEventStatus && d->SetEvent && d->ResetEvent &&
      d->CmdSetEvent && d->CmdResetEvent && d->CmdWaitEvents && d->CreateQueryPool &&
      d->GetQueryPoolResults && d->CmdBeginQuery && d->CmdEndQuery &&
      d->CmdResetQueryPool && d->CmdWriteTimestamp && d->CmdCopyQueryPoolResults);
   VkDescriptorSetLayoutBinding binding={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount=1,.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT};
   VkDescriptorSetLayoutCreateInfo slci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount=1,.pBindings=&binding};
   VkDescriptorSetLayout set_layout;
   SUCCESS(d->CreateDescriptorSetLayout(device,&slci,NULL,&set_layout));
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
      .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=module,.pName="main"},.layout=layout};
   VkPipeline pipeline;
   SUCCESS(d->CreateComputePipelines(device,VK_NULL_HANDLE,1,&pci,NULL,&pipeline));
   d->DestroyShaderModule(device,module,NULL);
   if (compile_only) {
      d->DestroyPipeline(device,pipeline,NULL); d->DestroyPipelineLayout(device,layout,NULL);
      d->DestroyDescriptorSetLayout(device,set_layout,NULL); d->DestroyDevice(device,NULL);
      DestroyInstance(instance,NULL); dlclose(lib);
      puts("PASS EVENT_QUERY compile-only: no probe allocations, commands or uploads"); return 0;
   }
   VkCommandPoolCreateInfo cpci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
   VkCommandPool command_pool;
   SUCCESS(d->CreateCommandPool(device,&cpci,NULL,&command_pool));
   VkCommandBufferAllocateInfo cbai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool=command_pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
   VkCommandBuffer command;
   SUCCESS(d->AllocateCommandBuffers(device,&cbai,&command));
   VkFenceCreateInfo fci={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence;
   SUCCESS(d->CreateFence(device,&fci,NULL,&fence));
   VkQueue queue; d->GetDeviceQueue(device,0,0,&queue);
   VkEventCreateInfo eci={.sType=VK_STRUCTURE_TYPE_EVENT_CREATE_INFO}; VkEvent events[2];
   for (unsigned i=0;i<2;i++) {
      SUCCESS(d->CreateEvent(device,&eci,NULL,&events[i]));
      REQUIRE(d->GetEventStatus(device,events[i])==VK_EVENT_RESET);
      SUCCESS(d->SetEvent(device,events[i]));
      REQUIRE(d->GetEventStatus(device,events[i])==VK_EVENT_SET);
      SUCCESS(d->ResetEvent(device,events[i]));
   }
   if (loss_only) {
      REQUIRE(loss_wake(d,device,queue,command,fence,events[0])==0);
      for (unsigned i=0;i<2;i++) d->DestroyEvent(device,events[i],NULL);
      d->DestroyFence(device,fence,NULL); d->DestroyCommandPool(device,command_pool,NULL);
      d->DestroyPipeline(device,pipeline,NULL); d->DestroyPipelineLayout(device,layout,NULL);
      d->DestroyDescriptorSetLayout(device,set_layout,NULL); d->DestroyDevice(device,NULL);
      DestroyInstance(instance,NULL); dlclose(lib);
      puts("PASS EVENT_QUERY loss-wake including device destruction"); return 0;
   }
   VkQueryPoolCreateInfo qpci={.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
      .queryType=VK_QUERY_TYPE_PIPELINE_STATISTICS,.queryCount=2,
      .pipelineStatistics=VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT|
         VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT};
   VkQueryPool stats,timestamps;
   SUCCESS(d->CreateQueryPool(device,&qpci,NULL,&stats));
   qpci.queryType=VK_QUERY_TYPE_TIMESTAMP; qpci.pipelineStatistics=0;
   SUCCESS(d->CreateQueryPool(device,&qpci,NULL,&timestamps));
   struct storage output={0},copies={0},indirect={0};
   REQUIRE(!storage_create(d,device,&memory_properties,TOTAL_WORDS*4,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,&output));
   REQUIRE(!storage_create(d,device,&memory_properties,512,VK_BUFFER_USAGE_TRANSFER_DST_BIT,&copies));
   REQUIRE(!storage_create(d,device,&memory_properties,64,
      VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT,&indirect));
   VkMappedMemoryRange ranges[3]={
      {.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=output.memory,.size=VK_WHOLE_SIZE},
      {.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=copies.memory,.size=VK_WHOLE_SIZE},
      {.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=indirect.memory,.size=VK_WHOLE_SIZE}};
   VkDescriptorPoolSize pool_size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};
   VkDescriptorPoolCreateInfo dpci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets=1,.poolSizeCount=1,.pPoolSizes=&pool_size};
   VkDescriptorPool descriptor_pool;
   SUCCESS(d->CreateDescriptorPool(device,&dpci,NULL,&descriptor_pool));
   VkDescriptorSetAllocateInfo dsai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool=descriptor_pool,.descriptorSetCount=1,.pSetLayouts=&set_layout};
   VkDescriptorSet set;
   SUCCESS(d->AllocateDescriptorSets(device,&dsai,&set));
   VkDescriptorBufferInfo bi={.buffer=output.buffer,.range=TOTAL_WORDS*4};
   VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet=set,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&bi};
   d->UpdateDescriptorSets(device,1,&write,0,NULL);
   VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   const VkQueryResultFlags wide=VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT;
   for (unsigned round=0;round<2;round++) {
      SUCCESS(d->ResetCommandBuffer(command,0)); SUCCESS(d->BeginCommandBuffer(command,&begin));
      d->CmdResetQueryPool(command,stats,0,2); d->CmdResetQueryPool(command,timestamps,0,2);
      REQUIRE(!submit_wait(d,device,queue,command,fence));
      REQUIRE(!unavailable(d,device,stats));
      for (size_t i=0;i<output.words;i++) output.map[i]=0xa5a5a5a5u;
      for (size_t i=0;i<copies.words;i++) copies.map[i]=0xa5a5a5a5u;
      for (size_t i=0;i<indirect.words;i++) indirect.map[i]=0xa5a5a5a5u;
      SUCCESS(d->FlushMappedMemoryRanges(device,3,ranges));
      SUCCESS(d->ResetEvent(device,events[0]));
      SUCCESS(d->ResetCommandBuffer(command,0)); SUCCESS(d->BeginCommandBuffer(command,&begin));
      d->CmdWaitEvents(command,1,&events[0],VK_PIPELINE_STAGE_HOST_BIT,
         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,NULL,0,NULL,0,NULL);
      d->CmdWriteTimestamp(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,timestamps,0);
      d->CmdBeginQuery(command,stats,0,0);
      d->CmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
      d->CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);
      struct params p={GUARD,0x10203040u+round};
      d->CmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(p),&p);
      d->CmdDispatch(command,3,2,1);
      d->CmdSetEvent(command,events[1],VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
      VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT};
      d->CmdWaitEvents(command,1,&events[1],VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,1,&barrier,0,NULL,0,NULL);
      d->CmdResetEvent(command,events[1],VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
      uint32_t args[3]={2,1,1};
      d->CmdUpdateBuffer(command,indirect.buffer,16,sizeof(args),args);
      VkBufferMemoryBarrier ib={.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
         .srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
         .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
         .buffer=indirect.buffer,.offset=16,.size=sizeof(args)};
      d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
         0,0,NULL,1,&ib,0,NULL);
      p.offset=GUARD+DIRECT_WORDS; p.salt+=7;
      d->CmdPushConstants(command,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(p),&p);
      d->CmdDispatchIndirect(command,indirect.buffer,16);
      d->CmdDispatch(command,0,2,1);
      d->CmdEndQuery(command,stats,0);
      d->CmdBeginQuery(command,stats,1,0); d->CmdDispatch(command,0,1,1); d->CmdEndQuery(command,stats,1);
      d->CmdWriteTimestamp(command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,timestamps,1);
      d->CmdCopyQueryPoolResults(command,stats,0,2,copies.buffer,16,32,wide|VK_QUERY_RESULT_WAIT_BIT);
      d->CmdCopyQueryPoolResults(command,stats,0,2,copies.buffer,112,16,VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
      d->CmdCopyQueryPoolResults(command,timestamps,0,2,copies.buffer,192,24,wide|VK_QUERY_RESULT_WAIT_BIT);
      VkMemoryBarrier host={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
      d->CmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_HOST_BIT,
         0,1,&host,0,NULL,0,NULL);
      SUCCESS(d->EndCommandBuffer(command)); SUCCESS(d->ResetFences(device,1,&fence));
      VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
      SUCCESS(d->QueueSubmit(queue,1,&submit,fence));
      REQUIRE(d->WaitForFences(device,1,&fence,VK_TRUE,1000000)==VK_TIMEOUT);
      SUCCESS(d->SetEvent(device,events[0]));
      uint64_t stats64[8]; memset(stats64,0xa5,sizeof(stats64));
      SUCCESS(d->GetQueryPoolResults(device,stats,0,2,sizeof(stats64),stats64,32,wide|VK_QUERY_RESULT_WAIT_BIT));
      SUCCESS(d->WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
      SUCCESS(d->InvalidateMappedMemoryRanges(device,3,ranges));
      REQUIRE(stats64[0]==0 && stats64[1]==256 && stats64[2]==1 &&
         stats64[4]==0 && stats64[5]==0 && stats64[6]==1);
      REQUIRE(stats64[3]==UINT64_C(0xa5a5a5a5a5a5a5a5) && stats64[7]==stats64[3]);
      uint32_t stats32[8]; memset(stats32,0xa5,sizeof(stats32));
      SUCCESS(d->GetQueryPoolResults(device,stats,0,2,sizeof(stats32),stats32,16,VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
      REQUIRE(stats32[0]==0 && stats32[1]==256 && stats32[2]==1 && stats32[3]==0xa5a5a5a5u &&
         stats32[4]==0 && stats32[5]==0 && stats32[6]==1 && stats32[7]==0xa5a5a5a5u);
      uint64_t ticks[6]; memset(ticks,0xa5,sizeof(ticks));
      SUCCESS(d->GetQueryPoolResults(device,timestamps,0,2,sizeof(ticks),ticks,24,wide));
      REQUIRE(ticks[0]>0 && ticks[3]>ticks[0] && ticks[1]==1 && ticks[4]==1 &&
         ticks[2]==UINT64_C(0xa5a5a5a5a5a5a5a5) && ticks[5]==ticks[2]);
      uint32_t low[4];
      SUCCESS(d->GetQueryPoolResults(device,timestamps,0,2,sizeof(low),low,8,VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
      REQUIRE(low[0]==(uint32_t)ticks[0] && low[1]==1 && low[2]==(uint32_t)ticks[3] && low[3]==1);
      uint32_t expected[128]; memset(expected,0xa5,sizeof(expected));
      memcpy((uint8_t *)expected+16,stats64,sizeof(stats64)); memcpy((uint8_t *)expected+112,stats32,sizeof(stats32));
      memcpy((uint8_t *)expected+192,ticks,sizeof(ticks));
      for (size_t i=0;i<copies.words;i++)
         REQUIRE(copies.map[i]==(i<128?expected[i]:0xa5a5a5a5u));
      for (size_t i=0;i<output.words;i++) {
         uint32_t value=0xa5a5a5a5u;
         if (i>=GUARD && i<GUARD+DIRECT_WORDS)
            value=(((uint32_t)i-GUARD)*1664525u+1013904223u)^(0x10203040u+round);
         if (i>=GUARD+DIRECT_WORDS && i<GUARD+DIRECT_WORDS+INDIRECT_WORDS)
            value=(((uint32_t)i-GUARD-DIRECT_WORDS)*1664525u+1013904223u)^(0x10203047u+round);
         REQUIRE(output.map[i]==value);
      }
      for (size_t i=0;i<indirect.words;i++)
         REQUIRE(indirect.map[i]==(i>=4 && i<7?args[i-4]:0xa5a5a5a5u));
      REQUIRE(d->GetEventStatus(device,events[0])==VK_EVENT_SET);
      REQUIRE(d->GetEventStatus(device,events[1])==VK_EVENT_RESET);
      printf("EVENT_QUERY ROUND_PASS round=%u invocations=256 output_values=256 ticks=%" PRIu64
         ",%" PRIu64 " period_ns=%.9g host_release=1 stats_copy=1 timestamp_copy=1 guards=1\n",
         round,ticks[0],ticks[3],properties.limits.timestampPeriod);
   }
   SUCCESS(d->DeviceWaitIdle(device));
   d->DestroyQueryPool(device,timestamps,NULL); d->DestroyQueryPool(device,stats,NULL);
   for (unsigned i=0;i<2;i++) d->DestroyEvent(device,events[i],NULL);
   d->DestroyFence(device,fence,NULL); d->DestroyCommandPool(device,command_pool,NULL);
   d->DestroyDescriptorPool(device,descriptor_pool,NULL);
   struct storage *stores[]={&output,&copies,&indirect};
   for (unsigned i=0;i<3;i++) {
      d->UnmapMemory(device,stores[i]->memory); d->DestroyBuffer(device,stores[i]->buffer,NULL);
      d->FreeMemory(device,stores[i]->memory,NULL);
   }
   d->DestroyPipeline(device,pipeline,NULL); d->DestroyPipelineLayout(device,layout,NULL);
   d->DestroyDescriptorSetLayout(device,set_layout,NULL); d->DestroyDevice(device,NULL);
   DestroyInstance(instance,NULL); dlclose(lib);
   puts("PASS EVENT_QUERY: two rounds, host-release, GPU events, direct/indirect/zero statistics, timestamps, availability, copy, reset/reuse");
   return 0;
}
