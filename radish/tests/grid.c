#include "test_common.h"

enum {
    LOCAL_X = 4,
    LOCAL_Y = 2,
    LOCAL_Z = 2,
    RECORD_WORDS = 16,
    RECORD_CAPACITY = 65 * 2 * 2 * LOCAL_X * LOCAL_Y * LOCAL_Z,
    GUARD_WORDS = 16,
    BUFFER_WORDS = 2 * GUARD_WORDS + RECORD_CAPACITY * RECORD_WORDS,
    MAX_ALLOCATION_BYTES = 1024 * 1024,
};

struct grid_case {
    const char *name;
    uint32_t groups[3];
};

static const struct grid_case cases[] = {
    { "one-group", { 1, 1, 1 } },
    { "large-x-3d", { 65, 2, 2 } },
    { "small-x-3d", { 3, 5, 5 } },
    { "large-y", { 1, 65, 1 } },
    { "large-z", { 1, 1, 65 } },
    { "zero-x", { 0, 2, 2 } },
    { "zero-y", { 2, 0, 2 } },
    { "zero-z", { 2, 2, 0 } },
};

struct grid_params {
    uint32_t grid[4];
    uint32_t extent[4];
    uint32_t bounds[4];
};

_Static_assert(sizeof(struct grid_params) == 48, "push-constant layout");
_Static_assert(LOCAL_X * LOCAL_Y * LOCAL_Z <= 128, "local invocation limit");
_Static_assert(BUFFER_WORDS * sizeof(uint32_t) <= MAX_ALLOCATION_BYTES,
               "fixed output allocation limit");

static uint32_t *read_spirv(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END)) { fclose(file); return NULL; }
    long length = ftell(file);
    if (length <= 0 || length % 4 || (unsigned long)length > 1024 * 1024) {
        fclose(file);
        return NULL;
    }
    rewind(file);
    uint32_t *code = malloc((size_t)length);
    if (!code || fread(code, 1, (size_t)length, file) != (size_t)length) {
        free(code);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return code;
}

static uint32_t sentinel(size_t word, uint32_t salt)
{
    return 0xcafe0000u | (((uint32_t)word * 17u + salt) & 0xffffu);
}

static int make_params(const struct grid_case *test, unsigned repeat,
                       unsigned case_index, struct grid_params *params)
{
    const uint32_t local[] = { LOCAL_X, LOCAL_Y, LOCAL_Z };
    uint64_t records = 1;
    memset(params, 0, sizeof(*params));
    for (unsigned axis = 0; axis < 3; axis++) {
        uint64_t groups = test->groups[axis] ? test->groups[axis] : 1;
        uint64_t extent = groups * local[axis];
        if (extent > UINT32_MAX || records > RECORD_CAPACITY / extent)
            return 0;
        params->grid[axis] = test->groups[axis];
        params->extent[axis] = (uint32_t)extent;
        params->bounds[axis] = (uint32_t)extent - (repeat != 0);
        records *= extent;
    }
    params->grid[3] = 101u + case_index * 17u + repeat * 257u;
    params->extent[3] = RECORD_CAPACITY;
    params->bounds[3] = GUARD_WORDS;
    return records <= RECORD_CAPACITY;
}

static int check_output(const uint32_t *output, size_t words,
                        const struct grid_case *test, unsigned repeat,
                        const struct grid_params *params)
{
    static const char *fields[RECORD_WORDS] = {
        "NumWorkgroups.x", "NumWorkgroups.y", "NumWorkgroups.z",
        "WorkgroupId.x", "WorkgroupId.y", "WorkgroupId.z",
        "GlobalInvocationId.x", "GlobalInvocationId.y", "GlobalInvocationId.z",
        "LocalInvocationId.x", "LocalInvocationId.y", "LocalInvocationId.z",
        "LocalInvocationIndex", "logical-group-index", "salt", "shared-peer",
    };
    const uint32_t salt = params->grid[3];
    const uint32_t width = params->extent[0];
    const uint32_t height = params->extent[1];
    const uint32_t depth = params->extent[2];
    const uint32_t active_records = width * height * depth;
    const int dispatch_has_work = test->groups[0] && test->groups[1] && test->groups[2];

    for (size_t word = 0; word < words; word++) {
        uint32_t expected = sentinel(word, salt);
        const char *field = "guard/unused/partial";
        uint32_t record = UINT32_MAX;
        if (word >= GUARD_WORDS && word < GUARD_WORDS + RECORD_CAPACITY * RECORD_WORDS) {
            record = (uint32_t)(word - GUARD_WORDS) / RECORD_WORDS;
            uint32_t x = record % width;
            uint32_t y = record / width % height;
            uint32_t z = record / width / height;
            if (dispatch_has_work && record < active_records &&
                x < params->bounds[0] && y < params->bounds[1] && z < params->bounds[2]) {
                uint32_t gx = x / LOCAL_X, gy = y / LOCAL_Y, gz = z / LOCAL_Z;
                uint32_t lx = x % LOCAL_X, ly = y % LOCAL_Y, lz = z % LOCAL_Z;
                uint32_t lane = (lz * LOCAL_Y + ly) * LOCAL_X + lx;
                uint32_t peer = (lane + 5u) % (LOCAL_X * LOCAL_Y * LOCAL_Z);
                uint32_t values[RECORD_WORDS] = {
                    test->groups[0], test->groups[1], test->groups[2],
                    gx, gy, gz, x, y, z, lx, ly, lz, lane,
                    (gz * test->groups[1] + gy) * test->groups[0] + gx,
                    salt, gx * 65537u + gy * 257u + gz * 17u + salt + peer * 31u,
                };
                unsigned component = (unsigned)(word - GUARD_WORDS) % RECORD_WORDS;
                expected = values[component];
                field = fields[component];
            }
        }
        if (output[word] != expected) {
            fprintf(stderr, "FAIL grid=%s repeat=%u word=%zu record=%u field=%s "
                    "got=%08x expected=%08x\n", test->name, repeat, word, record,
                    field, output[word], expected);
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s ICD-library grid.spv\n", argv[0]);
        return 1;
    }
    for (unsigned test = 0; test < sizeof(cases) / sizeof(cases[0]); test++) {
        struct grid_params params;
        REQUIRE(make_params(&cases[test], 0, test, &params));
        REQUIRE(make_params(&cases[test], 1, test, &params));
    }
    void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    PFN_vkGetInstanceProcAddr gipa =
        (PFN_vkGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");
    REQUIRE(gipa);
    VkInstance instance = VK_NULL_HANDLE;
    INSTANCE_FN(CreateInstance);
    VkApplicationInfo app = { .sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,
                             .apiVersion=VK_API_VERSION_1_0 };
    VkInstanceCreateInfo ici = { .sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                .pApplicationInfo=&app };
    SUCCESS(CreateInstance(&ici, NULL, &instance));
    INSTANCE_FN(EnumeratePhysicalDevices);
    INSTANCE_FN(GetPhysicalDeviceMemoryProperties);
    INSTANCE_FN(CreateDevice);
    INSTANCE_FN(GetDeviceProcAddr);
    INSTANCE_FN(DestroyInstance);
    uint32_t count = 1;
    VkPhysicalDevice physical;
    SUCCESS(EnumeratePhysicalDevices(instance, &count, &physical));
    REQUIRE(count == 1);
    VkPhysicalDeviceMemoryProperties memory_properties;
    GetPhysicalDeviceMemoryProperties(physical, &memory_properties);
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = { .sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                    .queueCount=1, .pQueuePriorities=&priority };
    VkDeviceCreateInfo dci = { .sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                              .queueCreateInfoCount=1, .pQueueCreateInfos=&qci };
    VkDevice device;
    SUCCESS(CreateDevice(physical, &dci, NULL, &device));
    PFN_vkGetDeviceProcAddr gdpa = GetDeviceProcAddr;
    DEVICE_FN(CreateDescriptorSetLayout);
    DEVICE_FN(DestroyDescriptorSetLayout);
    DEVICE_FN(CreatePipelineLayout);
    DEVICE_FN(DestroyPipelineLayout);
    DEVICE_FN(CreateShaderModule);
    DEVICE_FN(DestroyShaderModule);
    DEVICE_FN(CreateComputePipelines);
    DEVICE_FN(DestroyPipeline);
    DEVICE_FN(DestroyDevice);

    VkDescriptorSetLayoutBinding binding = { .binding=0,
        .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount=1,
        .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT };
    VkDescriptorSetLayoutCreateInfo slci = {
        .sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount=1, .pBindings=&binding };
    VkDescriptorSetLayout set_layout;
    SUCCESS(CreateDescriptorSetLayout(device, &slci, NULL, &set_layout));
    VkPushConstantRange push_range = { .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,
                                      .size=sizeof(struct grid_params) };
    VkPipelineLayoutCreateInfo plci = { .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount=1, .pSetLayouts=&set_layout,
        .pushConstantRangeCount=1, .pPushConstantRanges=&push_range };
    VkPipelineLayout layout;
    SUCCESS(CreatePipelineLayout(device, &plci, NULL, &layout));
    size_t code_size;
    uint32_t *code = read_spirv(argv[2], &code_size);
    REQUIRE(code);
    VkShaderModuleCreateInfo smci = { .sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                      .codeSize=code_size, .pCode=code };
    VkShaderModule module;
    SUCCESS(CreateShaderModule(device, &smci, NULL, &module));
    free(code);
    VkComputePipelineCreateInfo pci = { .sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage={ .sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                 .stage=VK_SHADER_STAGE_COMPUTE_BIT, .module=module, .pName="main" },
        .layout=layout };
    VkPipeline pipeline;
    SUCCESS(CreateComputePipelines(device, VK_NULL_HANDLE, 1, &pci, NULL, &pipeline));
    DestroyShaderModule(device, module, NULL);

    if (getenv("RADISH_GRID_COMPILE_ONLY")) {
        DestroyPipeline(device, pipeline, NULL);
        DestroyPipelineLayout(device, layout, NULL);
        DestroyDescriptorSetLayout(device, set_layout, NULL);
        DestroyDevice(device, NULL);
        DestroyInstance(instance, NULL);
        dlclose(lib);
        puts("COMPILE ONLY: grid pipeline created; no GPU dispatch executed");
        return 0;
    }

    DEVICE_FN(CreateDescriptorPool);
    DEVICE_FN(DestroyDescriptorPool);
    DEVICE_FN(AllocateDescriptorSets);
    DEVICE_FN(UpdateDescriptorSets);
    DEVICE_FN(CreateBuffer);
    DEVICE_FN(DestroyBuffer);
    DEVICE_FN(GetBufferMemoryRequirements);
    DEVICE_FN(AllocateMemory);
    DEVICE_FN(FreeMemory);
    DEVICE_FN(BindBufferMemory);
    DEVICE_FN(MapMemory);
    DEVICE_FN(UnmapMemory);
    DEVICE_FN(FlushMappedMemoryRanges);
    DEVICE_FN(InvalidateMappedMemoryRanges);
    DEVICE_FN(CreateCommandPool);
    DEVICE_FN(DestroyCommandPool);
    DEVICE_FN(AllocateCommandBuffers);
    DEVICE_FN(ResetCommandBuffer);
    DEVICE_FN(BeginCommandBuffer);
    DEVICE_FN(EndCommandBuffer);
    DEVICE_FN(CmdBindPipeline);
    DEVICE_FN(CmdBindDescriptorSets);
    DEVICE_FN(CmdPushConstants);
    DEVICE_FN(CmdDispatch);
    DEVICE_FN(GetDeviceQueue);
    DEVICE_FN(QueueSubmit);
    DEVICE_FN(CreateFence);
    DEVICE_FN(DestroyFence);
    DEVICE_FN(ResetFences);
    DEVICE_FN(WaitForFences);

    VkBufferCreateInfo bci = { .sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size=BUFFER_WORDS * sizeof(uint32_t), .usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT };
    VkBuffer buffer;
    SUCCESS(CreateBuffer(device, &bci, NULL, &buffer));
    VkMemoryRequirements requirements;
    GetBufferMemoryRequirements(device, buffer, &requirements);
    REQUIRE(requirements.size >= bci.size && requirements.size <= MAX_ALLOCATION_BYTES);
    REQUIRE(requirements.size % sizeof(uint32_t) == 0);
    uint32_t memory_type = UINT32_MAX;
    for (uint32_t type = 0; type < memory_properties.memoryTypeCount; type++) {
        if ((requirements.memoryTypeBits & (1u << type)) &&
            (memory_properties.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            memory_type = type;
            break;
        }
    }
    REQUIRE(memory_type != UINT32_MAX);
    VkMemoryAllocateInfo mai = { .sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize=requirements.size, .memoryTypeIndex=memory_type };
    VkDeviceMemory memory;
    SUCCESS(AllocateMemory(device, &mai, NULL, &memory));
    SUCCESS(BindBufferMemory(device, buffer, memory, 0));
    uint32_t *mapped;
    SUCCESS(MapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, (void **)&mapped));
    VkMappedMemoryRange range = { .sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                                  .memory=memory, .size=VK_WHOLE_SIZE };
    size_t mapped_words = (size_t)requirements.size / sizeof(uint32_t);

    VkDescriptorPoolSize pool_size = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 };
    VkDescriptorPoolCreateInfo dpci = { .sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets=1, .poolSizeCount=1, .pPoolSizes=&pool_size };
    VkDescriptorPool descriptor_pool;
    SUCCESS(CreateDescriptorPool(device, &dpci, NULL, &descriptor_pool));
    VkDescriptorSetAllocateInfo dsai = { .sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=descriptor_pool, .descriptorSetCount=1, .pSetLayouts=&set_layout };
    VkDescriptorSet descriptor_set;
    SUCCESS(AllocateDescriptorSets(device, &dsai, &descriptor_set));
    VkDescriptorBufferInfo dbi = { .buffer=buffer, .range=bci.size };
    VkWriteDescriptorSet write = { .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet=descriptor_set, .dstBinding=0, .descriptorCount=1,
        .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo=&dbi };
    UpdateDescriptorSets(device, 1, &write, 0, NULL);

    VkCommandPoolCreateInfo cpci = { .sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT };
    VkCommandPool command_pool;
    SUCCESS(CreateCommandPool(device, &cpci, NULL, &command_pool));
    VkCommandBufferAllocateInfo cbai = { .sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool=command_pool, .level=VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount=1 };
    VkCommandBuffer command;
    SUCCESS(AllocateCommandBuffers(device, &cbai, &command));
    VkQueue queue;
    GetDeviceQueue(device, 0, 0, &queue);
    VkFenceCreateInfo fci = { .sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    SUCCESS(CreateFence(device, &fci, NULL, &fence));
    unsigned dispatches = 0;

    for (unsigned test = 0; test < sizeof(cases) / sizeof(cases[0]); test++) {
        for (unsigned repeat = 0; repeat < 2; repeat++) {
            struct grid_params params;
            REQUIRE(make_params(&cases[test], repeat, test, &params));
            for (size_t word = 0; word < mapped_words; word++)
                mapped[word] = sentinel(word, params.grid[3]);
            SUCCESS(FlushMappedMemoryRanges(device, 1, &range));
            SUCCESS(ResetCommandBuffer(command, 0));
            VkCommandBufferBeginInfo begin = { .sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            SUCCESS(BeginCommandBuffer(command, &begin));
            CmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            CmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout,
                                  0, 1, &descriptor_set, 0, NULL);
            CmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT,
                             0, sizeof(params), &params);
            CmdDispatch(command, cases[test].groups[0], cases[test].groups[1], cases[test].groups[2]);
            SUCCESS(EndCommandBuffer(command));
            SUCCESS(ResetFences(device, 1, &fence));
            VkSubmitInfo submit = { .sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount=1, .pCommandBuffers=&command };
            printf("grid=%s groups=%u,%u,%u bounds=%u,%u,%u repeat=%u\n",
                   cases[test].name, cases[test].groups[0], cases[test].groups[1],
                   cases[test].groups[2], params.bounds[0], params.bounds[1], params.bounds[2], repeat);
            fflush(stdout);
            SUCCESS(QueueSubmit(queue, 1, &submit, fence));
            SUCCESS(WaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
            SUCCESS(InvalidateMappedMemoryRanges(device, 1, &range));
            REQUIRE(check_output(mapped, mapped_words, &cases[test], repeat, &params));
            dispatches++;
        }
    }

    DestroyFence(device, fence, NULL);
    DestroyCommandPool(device, command_pool, NULL);
    DestroyDescriptorPool(device, descriptor_pool, NULL);
    UnmapMemory(device, memory);
    DestroyBuffer(device, buffer, NULL);
    FreeMemory(device, memory, NULL);
    DestroyPipeline(device, pipeline, NULL);
    DestroyPipelineLayout(device, layout, NULL);
    DestroyDescriptorSetLayout(device, set_layout, NULL);
    DestroyDevice(device, NULL);
    DestroyInstance(instance, NULL);
    dlclose(lib);
    printf("PASS: %u grid dispatches, all builtin records, partial bounds, zero grids and guards\n",
           dispatches);
    return 0;
}
