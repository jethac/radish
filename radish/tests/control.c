#include "test_common.h"

enum {
    LOCAL_SIZE = 64,
    RECORD_WORDS = 4,
    GUARD_WORDS = 16,
    BUFFER_WORDS = 2 * GUARD_WORDS + LOCAL_SIZE * RECORD_WORDS,
    MAX_COUNT = 4100,
    MAX_ALLOCATION_BYTES = 1024 * 1024,
    REPEATS = 3,
    MODE_ARRAY = 1,
    MODE_DIV = 2,
    MODE_LONG = 4,
    MODE_LOAD = 5,
};

static const uint32_t widths[] = { 1, 31, 32, 33, 64, 65, 213, 4099 };
static const char *const mode_names[] = {
    [0] = "loop",
    [MODE_ARRAY] = "array",
    [MODE_DIV] = "div",
    [MODE_ARRAY | MODE_DIV] = "array-div",
    [MODE_LONG] = "loop-long",
    [MODE_LOAD] = "loop-load",
};

struct control_params {
    uint32_t shape[4]; /* count, salt, leading guard words, reserved */
    uint32_t axes[4];
    int32_t divisors[4];
};

_Static_assert(sizeof(struct control_params) == 48, "push-constant layout");
_Static_assert(sizeof(float) == sizeof(uint32_t), "float output bit width");
_Static_assert(LOCAL_SIZE <= 128, "local invocation limit");
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

static int make_params(uint32_t width, unsigned repeat, unsigned case_index,
                       struct control_params *params)
{
    static const uint32_t salts[REPEATS] = { 101u, 0x80000165u, 0xfffff123u };
    static const uint32_t axes[REPEATS][4] = {
        { 0, 1, 2, 3 }, { 3, 2, 1, 0 }, { 2, 0, 3, 1 },
    };
    static const int32_t divisors[REPEATS][2] = { { 1, 3 }, { 7, 11 }, { 31, 64 } };
    if (!width || width >= MAX_COUNT || repeat >= REPEATS) return 0;
    memset(params, 0, sizeof(*params));
    /* Repeats cross a loop boundary as well as changing arithmetic inputs. */
    params->shape[0] = width + (repeat == 1) - (repeat == 2);
    params->shape[1] = salts[repeat] + case_index * 17u;
    params->shape[2] = GUARD_WORDS;
    memcpy(params->axes, axes[repeat], sizeof(params->axes));
    memcpy(params->divisors, divisors[repeat], sizeof(divisors[repeat]));
    unsigned axis_mask = 0;
    for (unsigned axis = 0; axis < 4; axis++) {
        if (params->axes[axis] >= 4) return 0;
        axis_mask |= 1u << params->axes[axis];
    }
    return params->shape[0] <= MAX_COUNT && axis_mask == 15u &&
           params->divisors[0] > 0 && params->divisors[1] > 0 &&
           MAX_COUNT * 37u + 0x7fffu <= INT32_MAX;
}

static uint32_t reference_value(uint32_t t, unsigned mode,
                                const struct control_params *params)
{
    uint32_t salt = params->shape[1];
    uint32_t value = (t + 1u) * 0x9e3779b9u + salt;
    if (mode & MODE_ARRAY) {
        static const uint32_t weights[4] = { 3, 5, 7, 11 };
        const uint32_t sources[4] = {
            value ^ 0xa5a5a5a5u,
            (t + salt) * 17u,
            t * 3u + salt,
            (t ^ salt) + 0x13579bdfu,
        };
        /* Weight each source by its destination, without emulating array writes. */
        value = 0;
        for (unsigned source = 0; source < 4; source++)
            value += sources[source] * weights[params->axes[source]];
    }
    if (mode & MODE_DIV) {
        /* Inputs are positive and fit int32; unsigned CPU division is equivalent. */
        uint32_t numerator = t * 37u + (salt & 0x7fffu);
        uint32_t quotient = numerator / (uint32_t)params->divisors[0];
        uint32_t remainder = numerator % (uint32_t)params->divisors[1];
        value += quotient * 0x045d9f3bu + remainder * 0x27d4eb2du;
    }
    if (mode & MODE_LONG) {
        for (unsigned round = 0; round < 128; round++) {
            value ^= value << 13;
            value ^= value >> 17;
            value ^= value << 5;
        }
    }
    return value;
}

static float input_value(uint32_t t)
{
    return (float)((int32_t)(t * 7u % 101u) - 50) * 0.125f;
}

static int check_output(const uint32_t *output, size_t words, unsigned mode,
                        uint32_t width, unsigned repeat,
                        const struct control_params *params)
{
    static const char *const fields[RECORD_WORDS] = {
        "lane-value", "group-sum", "iteration-count", "variant-tag",
    };
    uint32_t lanes[LOCAL_SIZE] = { 0 };
    uint32_t iterations[LOCAL_SIZE] = { 0 };
    uint32_t sum = 0;
    /* A serial traversal is independent of the shader's divergent loop/tree. */
    if (mode == MODE_LOAD) {
        float float_lanes[LOCAL_SIZE] = { 0.0f };
        float float_sum = 0.0f;
        for (uint32_t t = 0; t < params->shape[0]; t++) {
            float value = input_value(t);
            float_lanes[t % LOCAL_SIZE] += value;
            iterations[t % LOCAL_SIZE]++;
            float_sum += value;
        }
        /* All fixture sums are exact binary fractions within float precision. */
        memcpy(lanes, float_lanes, sizeof(lanes));
        memcpy(&sum, &float_sum, sizeof(sum));
    } else {
        for (uint32_t t = 0; t < params->shape[0]; t++) {
            uint32_t value = reference_value(t, mode, params);
            lanes[t % LOCAL_SIZE] += value;
            iterations[t % LOCAL_SIZE]++;
            sum += value;
        }
    }
    for (size_t word = 0; word < words; word++) {
        uint32_t expected = sentinel(word, params->shape[1]);
        const char *field = "guard/unused-allocation";
        uint32_t lane = UINT32_MAX;
        if (word >= GUARD_WORDS && word < GUARD_WORDS + LOCAL_SIZE * RECORD_WORDS) {
            lane = (uint32_t)(word - GUARD_WORDS) / RECORD_WORDS;
            unsigned component = (unsigned)(word - GUARD_WORDS) % RECORD_WORDS;
            const uint32_t values[RECORD_WORDS] = {
                lanes[lane], sum, iterations[lane], 0xc0170000u | mode,
            };
            expected = values[component];
            field = fields[component];
        }
        if (output[word] != expected) {
            fprintf(stderr, "FAIL control=%s width=%u count=%u repeat=%u word=%zu lane=%u field=%s "
                    "got=%08x expected=%08x\n", mode_names[mode], width, params->shape[0],
                    repeat, word, lane, field, output[word], expected);
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s ICD-library control.spv loop|array|div|array-div|loop-long|loop-load\n", argv[0]);
        return 1;
    }
    unsigned mode;
    for (mode = 0; mode < sizeof(mode_names) / sizeof(mode_names[0]); mode++)
        if (!strcmp(argv[3], mode_names[mode])) break;
    if (mode == sizeof(mode_names) / sizeof(mode_names[0])) {
        fprintf(stderr, "Unknown control mode: %s\n", argv[3]);
        return 1;
    }
    for (unsigned test = 0; test < sizeof(widths) / sizeof(widths[0]); test++) {
        for (unsigned repeat = 0; repeat < REPEATS; repeat++) {
            struct control_params params;
            REQUIRE(make_params(widths[test], repeat, test, &params));
        }
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

    const uint32_t binding_count = mode == MODE_LOAD ? 2 : 1;
    VkDescriptorSetLayoutBinding bindings[2] = {
        { .binding=0, .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount=1, .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT },
        { .binding=1, .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount=1, .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT },
    };
    VkDescriptorSetLayoutCreateInfo slci = {
        .sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount=binding_count, .pBindings=bindings };
    VkDescriptorSetLayout set_layout;
    SUCCESS(CreateDescriptorSetLayout(device, &slci, NULL, &set_layout));
    VkPushConstantRange push_range = { .stageFlags=VK_SHADER_STAGE_COMPUTE_BIT,
                                      .size=sizeof(struct control_params) };
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

    if (getenv("RADISH_CONTROL_COMPILE_ONLY")) {
        DestroyPipeline(device, pipeline, NULL);
        DestroyPipelineLayout(device, layout, NULL);
        DestroyDescriptorSetLayout(device, set_layout, NULL);
        DestroyDevice(device, NULL);
        DestroyInstance(instance, NULL);
        dlclose(lib);
        printf("COMPILE ONLY: control mode=%s pipeline created; no GPU dispatch executed\n", mode_names[mode]);
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

    VkBuffer input_buffer = VK_NULL_HANDLE;
    VkDeviceMemory input_memory = VK_NULL_HANDLE;
    if (mode == MODE_LOAD) {
        VkBufferCreateInfo input_bci = { .sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size=MAX_COUNT * sizeof(float), .usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT };
        SUCCESS(CreateBuffer(device, &input_bci, NULL, &input_buffer));
        VkMemoryRequirements input_requirements;
        GetBufferMemoryRequirements(device, input_buffer, &input_requirements);
        REQUIRE(input_requirements.size >= input_bci.size &&
                input_requirements.size <= MAX_ALLOCATION_BYTES);
        uint32_t input_memory_type = UINT32_MAX;
        for (uint32_t type = 0; type < memory_properties.memoryTypeCount; type++) {
            if ((input_requirements.memoryTypeBits & (1u << type)) &&
                (memory_properties.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                input_memory_type = type;
                break;
            }
        }
        REQUIRE(input_memory_type != UINT32_MAX);
        VkMemoryAllocateInfo input_mai = { .sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize=input_requirements.size, .memoryTypeIndex=input_memory_type };
        SUCCESS(AllocateMemory(device, &input_mai, NULL, &input_memory));
        SUCCESS(BindBufferMemory(device, input_buffer, input_memory, 0));
        float *input_mapped;
        SUCCESS(MapMemory(device, input_memory, 0, VK_WHOLE_SIZE, 0, (void **)&input_mapped));
        memset(input_mapped, 0, (size_t)input_requirements.size);
        for (uint32_t t = 0; t < MAX_COUNT; t++) input_mapped[t] = input_value(t);
        VkMappedMemoryRange input_range = { .sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory=input_memory, .size=VK_WHOLE_SIZE };
        SUCCESS(FlushMappedMemoryRanges(device, 1, &input_range));
        UnmapMemory(device, input_memory);
    }

    VkDescriptorPoolSize pool_size = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, binding_count };
    VkDescriptorPoolCreateInfo dpci = { .sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets=1, .poolSizeCount=1, .pPoolSizes=&pool_size };
    VkDescriptorPool descriptor_pool;
    SUCCESS(CreateDescriptorPool(device, &dpci, NULL, &descriptor_pool));
    VkDescriptorSetAllocateInfo dsai = { .sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=descriptor_pool, .descriptorSetCount=1, .pSetLayouts=&set_layout };
    VkDescriptorSet descriptor_set;
    SUCCESS(AllocateDescriptorSets(device, &dsai, &descriptor_set));
    VkDescriptorBufferInfo dbi[2] = {
        { .buffer=buffer, .range=bci.size },
        { .buffer=input_buffer, .range=MAX_COUNT * sizeof(float) },
    };
    VkWriteDescriptorSet writes[2] = {
        { .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstSet=descriptor_set, .dstBinding=0, .descriptorCount=1,
          .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo=&dbi[0] },
        { .sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstSet=descriptor_set, .dstBinding=1, .descriptorCount=1,
          .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo=&dbi[1] },
    };
    UpdateDescriptorSets(device, binding_count, writes, 0, NULL);

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

    unsigned width_count = getenv("RADISH_CONTROL_SMOKE") ? 1 : sizeof(widths) / sizeof(widths[0]);
    unsigned repeat_count = getenv("RADISH_CONTROL_SMOKE") ? 1 : REPEATS;
    for (unsigned test = 0; test < width_count; test++) {
        for (unsigned repeat = 0; repeat < repeat_count; repeat++) {
            struct control_params params;
            REQUIRE(make_params(widths[test], repeat, test, &params));
            for (size_t word = 0; word < mapped_words; word++)
                mapped[word] = sentinel(word, params.shape[1]);
            SUCCESS(FlushMappedMemoryRanges(device, 1, &range));
            SUCCESS(ResetCommandBuffer(command, 0));
            VkCommandBufferBeginInfo begin = { .sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            SUCCESS(BeginCommandBuffer(command, &begin));
            CmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            CmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout,
                                  0, 1, &descriptor_set, 0, NULL);
            CmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT,
                             0, sizeof(params), &params);
            CmdDispatch(command, 1, 1, 1);
            SUCCESS(EndCommandBuffer(command));
            SUCCESS(ResetFences(device, 1, &fence));
            VkSubmitInfo submit = { .sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .commandBufferCount=1, .pCommandBuffers=&command };
            printf("control=%s width=%u count=%u repeat=%u salt=%08x divisors=%d,%d axes=%u,%u,%u,%u\n",
                   mode_names[mode], widths[test], params.shape[0], repeat, params.shape[1],
                   params.divisors[0], params.divisors[1], params.axes[0], params.axes[1],
                   params.axes[2], params.axes[3]);
            fflush(stdout);
            SUCCESS(QueueSubmit(queue, 1, &submit, fence));
            SUCCESS(WaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
            SUCCESS(InvalidateMappedMemoryRanges(device, 1, &range));
            REQUIRE(check_output(mapped, mapped_words, mode, widths[test], repeat, &params));
            dispatches++;
        }
    }

    DestroyFence(device, fence, NULL);
    DestroyCommandPool(device, command_pool, NULL);
    DestroyDescriptorPool(device, descriptor_pool, NULL);
    UnmapMemory(device, memory);
    DestroyBuffer(device, buffer, NULL);
    FreeMemory(device, memory, NULL);
    if (mode == MODE_LOAD) {
        DestroyBuffer(device, input_buffer, NULL);
        FreeMemory(device, input_memory, NULL);
    }
    DestroyPipeline(device, pipeline, NULL);
    DestroyPipelineLayout(device, layout, NULL);
    DestroyDescriptorSetLayout(device, set_layout, NULL);
    DestroyDevice(device, NULL);
    DestroyInstance(instance, NULL);
    dlclose(lib);
    printf("PASS: control mode=%s, %u one-group dispatches, all 64 lane values, sums, iteration counts and guards\n",
           mode_names[mode], dispatches);
    return 0;
}
