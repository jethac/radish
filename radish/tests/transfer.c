#include "test_common.h"

int main(int argc, char **argv)
{
    REQUIRE(argc==2);
    void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    REQUIRE(lib);
    PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr");
    REQUIRE(gipa);
    VkInstance instance=VK_NULL_HANDLE;
    INSTANCE_FN(CreateInstance);
    VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    SUCCESS(CreateInstance(&ici,NULL,&instance));
    INSTANCE_FN(EnumeratePhysicalDevices);
    INSTANCE_FN(CreateDevice);
    INSTANCE_FN(GetDeviceProcAddr);
    INSTANCE_FN(DestroyInstance);
    uint32_t count=1;
    VkPhysicalDevice physical;
    SUCCESS(EnumeratePhysicalDevices(instance,&count,&physical));
    REQUIRE(count==1);
    float priority=1;
    VkDeviceQueueCreateInfo qci={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qci};
    VkDevice device;
    SUCCESS(CreateDevice(physical,&dci,NULL,&device));
    PFN_vkGetDeviceProcAddr gdpa=GetDeviceProcAddr;
    DEVICE_FN(CmdFillBuffer);
    DEVICE_FN(CmdCopyBuffer);
    DEVICE_FN(CmdUpdateBuffer);
    DEVICE_FN(CmdPipelineBarrier);
    DEVICE_FN(CreateBuffer);
    DEVICE_FN(DestroyBuffer);
    DEVICE_FN(GetBufferMemoryRequirements);
    DEVICE_FN(BindBufferMemory);
    DEVICE_FN(AllocateMemory);
    DEVICE_FN(FreeMemory);
    DEVICE_FN(MapMemory);
    DEVICE_FN(UnmapMemory);
    DEVICE_FN(FlushMappedMemoryRanges);
    DEVICE_FN(InvalidateMappedMemoryRanges);
    DEVICE_FN(CreateCommandPool);
    DEVICE_FN(DestroyCommandPool);
    DEVICE_FN(AllocateCommandBuffers);
    DEVICE_FN(BeginCommandBuffer);
    DEVICE_FN(EndCommandBuffer);
    DEVICE_FN(ResetCommandBuffer);
    DEVICE_FN(GetDeviceQueue);
    DEVICE_FN(QueueSubmit);
    DEVICE_FN(CreateFence);
    DEVICE_FN(ResetFences);
    DEVICE_FN(WaitForFences);
    DEVICE_FN(DestroyFence);
    DEVICE_FN(DestroyDevice);
    VkBuffer buffers[3];
    for(unsigned j=0;j<3;j++){
        VkBufferCreateInfo bci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=40007,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
        SUCCESS(CreateBuffer(device,&bci,NULL,&buffers[j]));
    }
    VkMemoryRequirements req;
    GetBufferMemoryRequirements(device,buffers[0],&req);
    VkDeviceSize stride=req.size+64;
    VkMemoryAllocateInfo mai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=64+stride*3,.memoryTypeIndex=0};
    VkDeviceMemory memory;
    SUCCESS(AllocateMemory(device,&mai,NULL,&memory));
    for(unsigned j=0;j<3;j++) SUCCESS(BindBufferMemory(device,buffers[j],memory,64+j*stride));
    uint8_t *map;
    SUCCESS(MapMemory(device,memory,0,VK_WHOLE_SIZE,0,(void**)&map));
    VkCommandPoolCreateInfo cpci={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
    VkCommandPool pool;
    SUCCESS(CreateCommandPool(device,&cpci,NULL,&pool));
    VkCommandBufferAllocateInfo cai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer cmd;
    SUCCESS(AllocateCommandBuffers(device,&cai,&cmd));
    VkQueue queue;
    GetDeviceQueue(device,0,0,&queue);
    VkFenceCreateInfo fci={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    SUCCESS(CreateFence(device,&fci,NULL,&fence));
    for(unsigned repeat=0;repeat<3;repeat++){
        memset(map,0xBD,(size_t)mai.allocationSize);
        uint32_t *a=(uint32_t*)(map+64);
        uint32_t *b=(uint32_t*)(map+64+stride);
        uint32_t *c=(uint32_t*)(map+64+2*stride);
        for(unsigned i=0;i<10001;i++) a[i]=i*37u+repeat*111;
        VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,.memory=memory,.size=VK_WHOLE_SIZE};
        SUCCESS(FlushMappedMemoryRanges(device,1,&range));
        SUCCESS(ResetCommandBuffer(cmd,0));
        VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        SUCCESS(BeginCommandBuffer(cmd,&begin));
        uint32_t fill=0x11223344u+repeat;
        CmdFillBuffer(cmd,buffers[1],0,VK_WHOLE_SIZE,fill);
        VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT};
        CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,NULL,0,NULL);
        VkBufferCopy region={.srcOffset=16,.dstOffset=32,.size=8193*4};
        CmdCopyBuffer(cmd,buffers[0],buffers[1],1,&region);
        CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,NULL,0,NULL);
        uint32_t update[5]={7+repeat,11+repeat,13+repeat,17+repeat,19+repeat};
        uint32_t expected_update[5];
        memcpy(expected_update,update,sizeof(update));
        CmdUpdateBuffer(cmd,buffers[1],256,sizeof(update),update);
        memset(update,0,sizeof(update));
        CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,NULL,0,NULL);
        VkBufferCopy all={.size=40000};
        CmdCopyBuffer(cmd,buffers[1],buffers[2],1,&all);
        CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,NULL,0,NULL);
        CmdFillBuffer(cmd,buffers[1],0,64,0xAABBCCDDu+repeat);
        SUCCESS(EndCommandBuffer(cmd));
        SUCCESS(ResetFences(device,1,&fence));
        VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};
        SUCCESS(QueueSubmit(queue,1,&submit,fence));
        SUCCESS(WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
        SUCCESS(InvalidateMappedMemoryRanges(device,1,&range));
        for(unsigned i=0;i<10001;i++){
            uint32_t expected=fill;
            if(i>=8 && i<8+8193) expected=a[i-4];
            if(i>=64 && i<69) expected=expected_update[i-64];
            REQUIRE(b[i]==(i<16?0xAABBCCDDu+repeat:expected));
            REQUIRE(c[i]==(i<10000?expected:0xBDBDBDBDu));
            REQUIRE(a[i]==i*37u+repeat*111);
        }
        for(unsigned j=0;j<3;j++) for(VkDeviceSize i=40004;i<stride;i++) REQUIRE(map[64+j*stride+i]==0xBD);
        for(unsigned i=0;i<64;i++) REQUIRE(map[i]==0xBD);
        if(getenv("RADISH_BYTE_COPY")) {
            uint8_t *expected=malloc((size_t)mai.allocationSize);
            REQUIRE(expected);
            memcpy(expected,map,(size_t)mai.allocationSize);
            SUCCESS(ResetCommandBuffer(cmd,0));
            SUCCESS(BeginCommandBuffer(cmd,&begin));
            uint32_t source_fill=0x76543210u+repeat;
            CmdFillBuffer(cmd,buffers[0],4,8,source_fill);
            memcpy(expected+68,&source_fill,4);
            memcpy(expected+72,&source_fill,4);
            CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,NULL,0,NULL);
            VkBufferCopy bytes[]={
                {.srcOffset=1,.dstOffset=3,.size=1},
                {.srcOffset=5,.dstOffset=10,.size=2},
                {.srcOffset=7,.dstOffset=17,.size=3},
                {.srcOffset=13,.dstOffset=23,.size=5},
                {.srcOffset=0,.dstOffset=50,.size=4097},
                {.srcOffset=39999,.dstOffset=40000,.size=7},
            };
            CmdCopyBuffer(cmd,buffers[0],buffers[1],6,bytes);
            for(unsigned j=0;j<6;j++)
                memcpy(expected+64+stride+bytes[j].dstOffset,
                       expected+64+bytes[j].srcOffset,(size_t)bytes[j].size);
            CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,NULL,0,NULL);
            VkBufferCopy unaligned={.srcOffset=3,.dstOffset=1,.size=40004};
            CmdCopyBuffer(cmd,buffers[1],buffers[2],1,&unaligned);
            memcpy(expected+64+2*stride+1,expected+64+stride+3,40004);
            CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,NULL,0,NULL);
            VkBufferCopy aligned={.size=40004};
            CmdCopyBuffer(cmd,buffers[2],buffers[0],1,&aligned);
            memcpy(expected+64,expected+64+2*stride,40004);
            SUCCESS(EndCommandBuffer(cmd));
            SUCCESS(ResetFences(device,1,&fence));
            SUCCESS(QueueSubmit(queue,1,&submit,fence));
            SUCCESS(WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
            SUCCESS(InvalidateMappedMemoryRanges(device,1,&range));
            for(VkDeviceSize j=0;j<mai.allocationSize;j++) {
                if(map[j]!=expected[j]) {
                    fprintf(stderr,"FAIL byte copy repeat=%u byte=%llu got=%02x expected=%02x\n",repeat,(unsigned long long)j,map[j],expected[j]);
                    return 1;
                }
            }
            free(expected);
        }
    }
    DestroyFence(device,fence,NULL);
    DestroyCommandPool(device,pool,NULL);
    UnmapMemory(device,memory);
    for(unsigned j=0;j<3;j++) DestroyBuffer(device,buffers[j],NULL);
    FreeMemory(device,memory,NULL);
    DestroyDevice(device,NULL);
    DestroyInstance(instance,NULL);
    dlclose(lib);
    puts("PASS: ordered fills/copies/update snapshots, offsets and guards, three repetitions");
    if(getenv("RADISH_BYTE_COPY")) puts("PASS: byte copies, GPU producer/consumer, full allocation and guards");
    return 0;
}
