#include "test_common.h"
#include "test_allocator.h"

static uint32_t *read_spirv(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    if (n <= 0 || n % 4) { fclose(f); return NULL; }
    rewind(f);
    uint32_t *p = malloc((size_t)n);
    if (!p || fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); fclose(f); return NULL; }
    fclose(f);
    *size = (size_t)n;
    return p;
}

int main(int argc, char **argv)
{
    REQUIRE(argc == 4);
    void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");
    REQUIRE(gipa);
    VkInstance instance = VK_NULL_HANDLE;
    INSTANCE_FN(CreateInstance);
    VkApplicationInfo app = { .sType=VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion=VK_API_VERSION_1_0 };
    VkInstanceCreateInfo ici = { .sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo=&app };
    SUCCESS(CreateInstance(&ici, NULL, &instance));
    INSTANCE_FN(EnumeratePhysicalDevices);
    INSTANCE_FN(CreateDevice);
    INSTANCE_FN(GetPhysicalDeviceMemoryProperties);
    INSTANCE_FN(GetDeviceProcAddr);
    INSTANCE_FN(DestroyInstance);
    uint32_t count = 1;
    VkPhysicalDevice physical;
    SUCCESS(EnumeratePhysicalDevices(instance, &count, &physical));
    REQUIRE(count == 1);
    if (getenv("RADISH_COHERENT")) {
        VkPhysicalDeviceMemoryProperties memory_properties;
        GetPhysicalDeviceMemoryProperties(physical,&memory_properties);
        REQUIRE(memory_properties.memoryTypes[0].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }
    float priority = 1;
    VkDeviceQueueCreateInfo qci = { .sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueCount=1, .pQueuePriorities=&priority };
    VkDeviceCreateInfo dci = { .sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount=1, .pQueueCreateInfos=&qci };
    VkDevice device;
    struct allocation_state device_state={0}, pool_state={0};
    VkAllocationCallbacks device_allocator=test_allocator(&device_state);
    VkAllocationCallbacks pool_allocator=test_allocator(&pool_state);
    SUCCESS(CreateDevice(physical, &dci, &device_allocator, &device));
    PFN_vkGetDeviceProcAddr gdpa = GetDeviceProcAddr;
    DEVICE_FN(CreateDescriptorSetLayout);
    DEVICE_FN(DestroyDescriptorSetLayout);
    DEVICE_FN(CreatePipelineLayout);
    DEVICE_FN(DestroyPipelineLayout);
    DEVICE_FN(CreateShaderModule);
    DEVICE_FN(DestroyShaderModule);
    DEVICE_FN(CreateComputePipelines);
    DEVICE_FN(DestroyPipeline);
    DEVICE_FN(CreateDescriptorPool);
    DEVICE_FN(DestroyDescriptorPool);
    DEVICE_FN(AllocateDescriptorSets);
    DEVICE_FN(UpdateDescriptorSets);
    DEVICE_FN(CreateBuffer);
    DEVICE_FN(DestroyBuffer);
    DEVICE_FN(AllocateMemory);
    DEVICE_FN(FreeMemory);
    DEVICE_FN(GetBufferMemoryRequirements);
    DEVICE_FN(BindBufferMemory);
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
    DEVICE_FN(CmdBindPipeline);
    DEVICE_FN(CmdBindDescriptorSets);
    DEVICE_FN(CmdPushConstants);
    DEVICE_FN(CmdDispatch);
    DEVICE_FN(CmdFillBuffer);
    DEVICE_FN(CmdPipelineBarrier);
    DEVICE_FN(GetDeviceQueue);
    DEVICE_FN(QueueSubmit);
    DEVICE_FN(CreateFence);
    DEVICE_FN(DestroyFence);
    DEVICE_FN(ResetFences);
    DEVICE_FN(WaitForFences);
    DEVICE_FN(DestroyDevice);
    const uint32_t binding_numbers[] = {getenv("RADISH_SPAN")?0:2, getenv("RADISH_SPAN")?1:7, getenv("RADISH_SPAN")?2:11};
    VkDescriptorSetLayoutBinding bindings[3] = {0};
    for (unsigned j=0; j<3; j++) bindings[j] = (VkDescriptorSetLayoutBinding) { .binding=binding_numbers[j], .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount=1, .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT };
    VkDescriptorSetLayoutCreateInfo lci = { .sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount=3, .pBindings=bindings };
    VkDescriptorSetLayout set_layout;
    SUCCESS(CreateDescriptorSetLayout(device, &lci, NULL, &set_layout));
    VkDescriptorSetLayoutBinding reverse_bindings[3] = {bindings[2], bindings[1], bindings[0]};
    VkDescriptorSetLayout reverse_layout;
    lci.pBindings=reverse_bindings;
    SUCCESS(CreateDescriptorSetLayout(device, &lci, NULL, &reverse_layout));
    VkPushConstantRange push_range = { .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT, .size=8 };
    VkDescriptorSetLayout pipeline_sets[]={set_layout,set_layout};
    VkPipelineLayoutCreateInfo plci = { .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount=2, .pSetLayouts=pipeline_sets, .pushConstantRangeCount=1, .pPushConstantRanges=&push_range };
    VkPipelineLayout layout;
    SUCCESS(CreatePipelineLayout(device, &plci, NULL, &layout));
    VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 12 };
    VkDescriptorPoolCreateInfo dpci = { .sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets=4, .poolSizeCount=1, .pPoolSizes=&ps };
    VkDescriptorPool pool;
    SUCCESS(CreateDescriptorPool(device, &dpci, &pool_allocator, &pool));
    VkDescriptorSetAllocateInfo sai = { .sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool=pool, .descriptorSetCount=1, .pSetLayouts=&reverse_layout };
    VkDescriptorSet set;
    device_state.fail=1;
    VkResult allocation_result=AllocateDescriptorSets(device, &sai, &set);
    device_state.fail=0;
    SUCCESS(allocation_result);
    VkDescriptorSet index_set;
    SUCCESS(AllocateDescriptorSets(device, &sai, &index_set));
    VkDescriptorSet copy_set;
    SUCCESS(AllocateDescriptorSets(device, &sai, &copy_set));
    VkDescriptorSet chain_set=VK_NULL_HANDLE;
    if (getenv("RADISH_CHAIN")) SUCCESS(AllocateDescriptorSets(device, &sai, &chain_set));
    VkBuffer buffers[3];
    VkMemoryRequirements req;
    for (unsigned j=0; j<3; j++) {
        VkBufferCreateInfo bci = { .sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size=(4099+16)*4, .usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT };
        SUCCESS(CreateBuffer(device, &bci, NULL, &buffers[j]));
    }
    GetBufferMemoryRequirements(device, buffers[0], &req);
    VkMemoryAllocateInfo mai = { .sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize=req.size*3, .memoryTypeIndex=0 };
    VkDeviceMemory memory;
    SUCCESS(AllocateMemory(device, &mai, NULL, &memory));
    VkDescriptorBufferInfo dbi[3];
    VkWriteDescriptorSet writes[3];
    for (unsigned j=0; j<3; j++) {
        SUCCESS(BindBufferMemory(device, buffers[j], memory, j*req.size));
        dbi[j]=(VkDescriptorBufferInfo){ .buffer=buffers[j], .range=VK_WHOLE_SIZE };
        writes[j]=(VkWriteDescriptorSet){ .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet=set, .dstBinding=binding_numbers[j], .descriptorCount=1, .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo=&dbi[j] };
    }
    if (getenv("RADISH_WRITE_SPAN")) {
        VkWriteDescriptorSet span=writes[0];
        span.descriptorCount=3;
        span.pBufferInfo=dbi;
        UpdateDescriptorSets(device, 1, &span, 0, NULL);
    } else UpdateDescriptorSets(device, 3, writes, 0, NULL);
    if (getenv("RADISH_COPY_SPAN")) {
        VkCopyDescriptorSet copy={ .sType=VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET, .srcSet=set, .srcBinding=binding_numbers[0], .dstSet=copy_set, .dstBinding=binding_numbers[0], .descriptorCount=3 };
        UpdateDescriptorSets(device, 0, NULL, 1, &copy);
    } else {
        VkCopyDescriptorSet copies[3];
        for (unsigned j=0;j<3;j++) copies[j]=(VkCopyDescriptorSet){ .sType=VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET, .srcSet=set, .srcBinding=binding_numbers[j], .dstSet=copy_set, .dstBinding=binding_numbers[j], .descriptorCount=1 };
        UpdateDescriptorSets(device, 0, NULL, 3, copies);
    }
    VkWriteDescriptorSet index_write=writes[2];
    index_write.dstSet=index_set;
    UpdateDescriptorSets(device, 1, &index_write, 0, NULL);
    if (chain_set) {
        VkWriteDescriptorSet chain_writes[3]={writes[0],writes[1],writes[2]};
        for (unsigned j=0;j<3;j++) chain_writes[j].dstSet=chain_set;
        chain_writes[0].pBufferInfo=&dbi[2];
        UpdateDescriptorSets(device, 3, chain_writes, 0, NULL);
    }
    uint8_t *mapped;
    SUCCESS(MapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, (void**)&mapped));
    VkMappedMemoryRange mr = { .sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, .memory=memory, .size=VK_WHOLE_SIZE };
    VkCommandPoolCreateInfo cpci = { .sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT };
    VkCommandPool command_pool;
    SUCCESS(CreateCommandPool(device, &cpci, NULL, &command_pool));
    VkCommandBufferAllocateInfo cai = { .sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool=command_pool, .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount=1 };
    VkCommandBuffer command;
    SUCCESS(AllocateCommandBuffers(device, &cai, &command));
    VkQueue queue;
    GetDeviceQueue(device, 0, 0, &queue);
    VkFenceCreateInfo fci = { .sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    SUCCESS(CreateFence(device, &fci, NULL, &fence));
    REQUIRE(WaitForFences(device, 1, &fence, VK_TRUE, 0) == VK_TIMEOUT);
    unsigned dispatches=0;
    const uint32_t sizes[] = {1,31,32,33,127,128,129,4099};
    unsigned local_size=getenv("RADISH_SHARED") ? (unsigned)atoi(getenv("RADISH_LOCAL_SIZE")?getenv("RADISH_LOCAL_SIZE"):"64") : 32;
    REQUIRE(local_size==32 || local_size==64 || local_size==128);
    unsigned shader_count=getenv("RADISH_SMOKE") || getenv("RADISH_SHARED") ? 1 : 2;
    unsigned size_count=getenv("RADISH_SMOKE") ? 1 : 8;
    unsigned repeat_count=getenv("RADISH_SMOKE") ? 1 : 2;
    for (unsigned shader=0; shader<shader_count; shader++) {
        size_t code_size;
        uint32_t *code=read_spirv(argv[2+shader], &code_size);
        REQUIRE(code);
        VkShaderModuleCreateInfo smci = { .sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize=code_size, .pCode=code };
        VkShaderModule module;
        SUCCESS(CreateShaderModule(device, &smci, NULL, &module));
        free(code);
        VkComputePipelineCreateInfo pci = { .sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, .stage={ .sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage=VK_SHADER_STAGE_COMPUTE_BIT, .module=module, .pName="main" }, .layout=layout };
        VkSpecializationMapEntry local_entry={ .constantID=0, .offset=0, .size=sizeof(local_size) };
        VkSpecializationInfo local_specialization={ .mapEntryCount=1, .pMapEntries=&local_entry, .dataSize=sizeof(local_size), .pData=&local_size };
        if (getenv("RADISH_SHARED")) pci.stage.pSpecializationInfo=&local_specialization;
        VkPipeline pipeline;
        VkResult pipeline_result=CreateComputePipelines(device, VK_NULL_HANDLE, 1, &pci, NULL, &pipeline);
        DestroyShaderModule(device, module, NULL);
        if (getenv("RADISH_EXPECT_SHARED_REJECT")) {
            REQUIRE(pipeline_result==VK_ERROR_FEATURE_NOT_PRESENT && pipeline==VK_NULL_HANDLE);
            puts("PASS: unsupported shared allocation rejected before GPU submission");
            continue;
        }
        SUCCESS(pipeline_result);
        if (getenv("RADISH_COMPILE_ONLY")) { DestroyPipeline(device, pipeline, NULL); continue; }
        for (unsigned s=0; s<size_count; s++) for (unsigned repeat=0; repeat<repeat_count; repeat++) {
            uint32_t n=sizes[s], salt=101+repeat*257+s;
            float *a=(float*)mapped, *b=(float*)(mapped+req.size);
            uint32_t *out=(uint32_t*)(mapped+2*req.size);
            for (uint32_t i=0; i<4099+16; i++) {
                a[i]=(float)((int)(i%97)-48)*0.125f+repeat;
                b[i]=(float)((int)(i%31)-15)*0.25f;
                out[i]=0x7fc12345u+repeat;
            }
            if (!getenv("RADISH_COHERENT")) SUCCESS(FlushMappedMemoryRanges(device, 1, &mr));
            SUCCESS(ResetCommandBuffer(command, 0));
            VkCommandBufferBeginInfo cbi = { .sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            SUCCESS(BeginCommandBuffer(command, &cbi));
            if (chain_set) {
                CmdFillBuffer(command,buffers[2],0,VK_WHOLE_SIZE,0x7fc12345u+repeat);
                VkMemoryBarrier fill_barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
                CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&fill_barrier,0,NULL,0,NULL);
            }
            CmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            VkDescriptorSet selected_set=shader || getenv("RADISH_SHARED") ? copy_set : index_set;
            CmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &selected_set, 0, NULL);
            uint32_t push[]={n,salt};
            CmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), push);
            CmdDispatch(command, (n+local_size-1)/local_size, 1, 1);
            if (chain_set && shader) {
                VkMemoryBarrier dependency={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT};
                CmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&dependency,0,NULL,0,NULL);
                CmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&chain_set,0,NULL);
                CmdDispatch(command,(n+31)/32,1,1);
            }
            SUCCESS(EndCommandBuffer(command));
            SUCCESS(ResetFences(device, 1, &fence));
            VkSubmitInfo submit = { .sType=VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount=1, .pCommandBuffers=&command };
            SUCCESS(QueueSubmit(queue, 1, &submit, fence));
            SUCCESS(WaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
            if (!getenv("RADISH_COHERENT")) SUCCESS(InvalidateMappedMemoryRanges(device, 1, &mr));
            for (uint32_t i=0; i<n; i++) {
                uint32_t expected=i*17u+salt;
                if (shader) { float sum=a[i]+b[i]; if(chain_set) sum+=b[i]; memcpy(&expected, &sum, 4); }
                if (getenv("RADISH_SHARED")) {
                    float sum=0;
                    uint32_t first=i/local_size*local_size;
                    for (uint32_t j=first;j<n && j<first+local_size;j++) {
                        if (getenv("RADISH_MIX_ROUNDS")) {
                            unsigned rounds=(unsigned)atoi(getenv("RADISH_MIX_ROUNDS"));
                            REQUIRE(rounds==0 || rounds==128);
                            uint32_t value=(j+1u)^salt;
                            for (unsigned step=0;step<rounds;step++) {
                                value^=value<<13;
                                value^=value>>17;
                                value^=value<<5;
                            }
                            sum+=(float)(value&65535u);
                        } else sum+=a[j];
                    }
                    sum+=(float)salt;
                    memcpy(&expected,&sum,4);
                }
                if (out[i]!=expected) { fprintf(stderr,"FAIL shader=%u n=%u repeat=%u index=%u got=%08x expected=%08x\n",shader,n,repeat,i,out[i],expected); return 1; }
            }
            for (uint32_t i=n; i<4099+16; i++) REQUIRE(out[i]==0x7fc12345u+repeat);
            dispatches+=chain_set && shader ? 2 : 1;
        }
        DestroyPipeline(device, pipeline, NULL);
    }
    DestroyFence(device, fence, NULL);
    DestroyCommandPool(device, command_pool, NULL);
    UnmapMemory(device, memory);
    for (unsigned j=0;j<3;j++) DestroyBuffer(device,buffers[j],NULL);
    FreeMemory(device,memory,NULL);
    DestroyDescriptorPool(device,pool,&pool_allocator);
    DestroyPipelineLayout(device,layout,NULL);
    DestroyDescriptorSetLayout(device,reverse_layout,NULL);
    DestroyDescriptorSetLayout(device,set_layout,NULL);
    DestroyDevice(device,NULL);
    REQUIRE(device_state.live==0 && pool_state.live==0);
    REQUIRE(device_state.mismatches==0 && pool_state.mismatches==0);
    DestroyInstance(instance,NULL);
    dlclose(lib);
    if (getenv("RADISH_COMPILE_ONLY"))
        puts("COMPILE ONLY: pipelines created; no GPU dispatch executed");
    else if (dispatches)
        printf("PASS: %u dispatches, full outputs and guard regions\n",dispatches);
    return 0;
}
