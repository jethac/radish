/* Compute microbenchmarks for ALU throughput and memory bandwidth.
 *
 * usage: ubench JOBS
 * Each JOBS line: label spv local groups kind work_per_thread_iter [bytes_mib]
 *   kind = alu      push constant iters; result is FLOP/s with
 *                   work_per_thread_iter FLOPs per thread per iteration
 *   kind = ssbo     binding 0 input SSBO of bytes_mib MiB, binding 1 output;
 *                   push constant iters = vec4 loads per thread;
 *                   work_per_thread_iter bytes per thread per iteration
 *   kind = copy     like ssbo; output the same size as the input
 *   kind = ubo      binding 0 is a 16 KiB uniform buffer
 *   kind = texel    binding 0 is a uniform texel buffer (RGBA32F) of bytes_mib
 *   kind = texel16  same, RGBA16F
 * The output buffer is binding 1 (binding 0 for alu). Iterations are scaled
 * so one dispatch takes about TARGET_MS (default 20) ms of GPU time; the
 * median of 5 timed dispatches (Vulkan timestamps) is reported.
 */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FN(name) static PFN_##name name;
#define FUNCS(X)                                                               \
   X(vkCreateInstance) X(vkEnumeratePhysicalDevices) X(vkCreateDevice)         \
   X(vkGetPhysicalDeviceProperties) X(vkGetPhysicalDeviceMemoryProperties)     \
   X(vkGetDeviceQueue) X(vkCreateBuffer) X(vkGetBufferMemoryRequirements)      \
   X(vkAllocateMemory) X(vkBindBufferMemory) X(vkMapMemory)                    \
   X(vkCreateShaderModule) X(vkCreateDescriptorSetLayout)                      \
   X(vkCreatePipelineLayout) X(vkCreateComputePipelines)                       \
   X(vkCreateDescriptorPool) X(vkAllocateDescriptorSets)                       \
   X(vkUpdateDescriptorSets) X(vkCreateCommandPool)                            \
   X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer)   \
   X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdPushConstants)       \
   X(vkCmdDispatch) X(vkCmdWriteTimestamp) X(vkCmdResetQueryPool)              \
   X(vkCreateQueryPool) X(vkGetQueryPoolResults) X(vkQueueSubmit)              \
   X(vkQueueWaitIdle) X(vkResetCommandBuffer) X(vkDestroyPipeline)             \
   X(vkDestroyShaderModule) X(vkCreateBufferView) X(vkDestroyBufferView)       \
   X(vkFreeDescriptorSets) X(vkDestroyDevice) X(vkDestroyInstance)
FUNCS(FN)
static PFN_vkGetInstanceProcAddr gipa;

#define CHECK(x)                                                               \
   do {                                                                        \
      VkResult r_ = (x);                                                       \
      if (r_ != VK_SUCCESS) {                                                  \
         fprintf(stderr, "%s:%d %s = %d\n", __FILE__, __LINE__, #x, r_);      \
         exit(1);                                                              \
      }                                                                        \
   } while (0)

static VkDevice dev;
static VkPhysicalDevice phys;
static VkPhysicalDeviceMemoryProperties memprops;

struct buf { VkBuffer b; VkDeviceMemory m; void *map; VkDeviceSize size; };

static struct buf
make_buffer(VkDeviceSize size, VkBufferUsageFlags usage)
{
   struct buf r = { .size = size };
   VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size,
                             .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
   CHECK(vkCreateBuffer(dev, &bi, NULL, &r.b));
   VkMemoryRequirements req;
   vkGetBufferMemoryRequirements(dev, r.b, &req);
   uint32_t type = UINT32_MAX;
   for (uint32_t i = 0; i < memprops.memoryTypeCount; i++) {
      VkMemoryPropertyFlags f = memprops.memoryTypes[i].propertyFlags;
      if ((req.memoryTypeBits & (1u << i)) && (f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
         type = i;
         break;
      }
   }
   VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                               .allocationSize = req.size, .memoryTypeIndex = type };
   CHECK(vkAllocateMemory(dev, &ai, NULL, &r.m));
   CHECK(vkBindBufferMemory(dev, r.b, r.m, 0));
   CHECK(vkMapMemory(dev, r.m, 0, VK_WHOLE_SIZE, 0, &r.map));
   return r;
}

static void *
read_file(const char *path, size_t *size)
{
   FILE *f = fopen(path, "rb");
   if (!f) { perror(path); exit(1); }
   fseek(f, 0, SEEK_END);
   *size = ftell(f);
   fseek(f, 0, SEEK_SET);
   void *data = malloc(*size);
   if (fread(data, 1, *size, f) != *size) exit(1);
   fclose(f);
   return data;
}

static int cmp_u64(const void *a, const void *b)
{
   uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
   return x < y ? -1 : x > y;
}

int
main(int argc, char **argv)
{
   if (argc != 2) { fprintf(stderr, "usage: ubench JOBS\n"); return 2; }
   void *lib = dlopen("libvulkan.so.1", RTLD_NOW);
   if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
   gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
   vkCreateInstance = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
   VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1 };
   VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
   VkInstance inst;
   CHECK(vkCreateInstance(&ici, NULL, &inst));
#define LOAD(name) if (!name) name = (PFN_##name)gipa(inst, #name);
   FUNCS(LOAD)
   uint32_t n = 1;
   CHECK(vkEnumeratePhysicalDevices(inst, &n, &phys));
   VkPhysicalDeviceProperties props;
   vkGetPhysicalDeviceProperties(phys, &props);
   vkGetPhysicalDeviceMemoryProperties(phys, &memprops);
   printf("# device %s period %.3f ns\n", props.deviceName, props.limits.timestampPeriod);
   float prio = 1.0f;
   VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueCount = 1,
                                   .pQueuePriorities = &prio };
   const char *robust = getenv("UBENCH_ROBUST");
   VkPhysicalDeviceFeatures feats = { .robustBufferAccess = robust && !strcmp(robust, "1") };
   VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount = 1,
                              .pQueueCreateInfos = &qci, .pEnabledFeatures = &feats };
   CHECK(vkCreateDevice(phys, &dci, NULL, &dev));
   VkQueue queue;
   vkGetDeviceQueue(dev, 0, 0, &queue);

   VkCommandPoolCreateInfo cpi = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                   .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT };
   VkCommandPool pool;
   CHECK(vkCreateCommandPool(dev, &cpi, NULL, &pool));
   VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                       .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                       .commandBufferCount = 1 };
   VkCommandBuffer cmd;
   CHECK(vkAllocateCommandBuffers(dev, &cai, &cmd));
   VkQueryPoolCreateInfo qpi = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                 .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 2 };
   VkQueryPool qp;
   CHECK(vkCreateQueryPool(dev, &qpi, NULL, &qp));
   VkDescriptorPoolSize sizes[] = {
      { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 64 }, { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16 },
      { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 16 } };
   VkDescriptorPoolCreateInfo dpi = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                      .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
                                      .maxSets = 16, .poolSizeCount = 3, .pPoolSizes = sizes };
   VkDescriptorPool dpool;
   CHECK(vkCreateDescriptorPool(dev, &dpi, NULL, &dpool));

   /* Buffers shared by all jobs, sized for the largest. */
   const VkDeviceSize in_max = 64ull << 20, out_max = 64ull << 20;
   struct buf in = make_buffer(in_max, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                               VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT);
   struct buf out = make_buffer(out_max, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
   struct buf ubo = make_buffer(16384, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
   for (size_t i = 0; i < in_max / 4; i++)
      ((float *)in.map)[i] = (float)(i % 251) * (1.0f / 256.0f);
   for (size_t i = 0; i < 16384 / 4; i++)
      ((float *)ubo.map)[i] = (float)(i % 13) * 0.125f;

   int target_ms = getenv("TARGET_MS") ? atoi(getenv("TARGET_MS")) : 20;
   FILE *jobs = fopen(argv[1], "r");
   if (!jobs) { perror(argv[1]); return 1; }
   char line[512];
   printf("# label local groups iters median_ms median_rate best_rate unit\n");
   while (fgets(line, sizeof(line), jobs)) {
      char label[128], spv[256], kind[16];
      unsigned local, groups;
      double work;
      double mib = 0;
      if (line[0] == '#' || sscanf(line, "%127s %255s %u %u %15s %lf %lf", label, spv, &local,
                                   &groups, kind, &work, &mib) < 6)
         continue;
      const char *filter = getenv("UBENCH_FILTER");
      if (filter && !strstr(label, filter))
         continue;
      int alu = !strcmp(kind, "alu"), use_ubo = !strcmp(kind, "ubo");
      int texel = !strncmp(kind, "texel", 5);
      int copy = !strcmp(kind, "copy");
      VkDescriptorType t0 = alu ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER :
                            use_ubo ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER :
                            texel ? VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER :
                            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      VkDescriptorSetLayoutBinding binds[2] = {
         { 0, t0, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL },
         { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL } };
      VkDescriptorSetLayoutCreateInfo dli = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                              .bindingCount = alu ? 1 : 2, .pBindings = binds };
      VkDescriptorSetLayout dsl;
      CHECK(vkCreateDescriptorSetLayout(dev, &dli, NULL, &dsl));
      VkPushConstantRange pcr = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 16 };
      VkPipelineLayoutCreateInfo pli = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                         .setLayoutCount = 1, .pSetLayouts = &dsl,
                                         .pushConstantRangeCount = 1, .pPushConstantRanges = &pcr };
      VkPipelineLayout pl;
      CHECK(vkCreatePipelineLayout(dev, &pli, NULL, &pl));
      size_t code_size;
      void *code = read_file(spv, &code_size);
      VkShaderModuleCreateInfo smi = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                       .codeSize = code_size, .pCode = code };
      VkShaderModule sm;
      CHECK(vkCreateShaderModule(dev, &smi, NULL, &sm));
      VkSpecializationMapEntry me = { 0, 0, 4 };
      VkSpecializationInfo si = { 1, &me, 4, &local };
      VkComputePipelineCreateInfo cpci = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
         .stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                    .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = sm, .pName = "main",
                    .pSpecializationInfo = &si },
         .layout = pl };
      VkPipeline pipe;
      VkResult pr = vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &pipe);
      if (pr != VK_SUCCESS) {
         printf("%s local=%u pipeline_failed=%d\n", label, local, pr);
         fflush(stdout);
         continue;
      }
      VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                           .descriptorPool = dpool, .descriptorSetCount = 1,
                                           .pSetLayouts = &dsl };
      VkDescriptorSet set;
      CHECK(vkAllocateDescriptorSets(dev, &dsai, &set));
      VkDeviceSize in_bytes = mib > 0 ? (VkDeviceSize)(mib * 1048576.0) : in_max;
      if (texel && in_bytes > 16ull << 20)
         in_bytes = 16ull << 20;
      VkDeviceSize out_bytes = copy ? in_bytes : (VkDeviceSize)local * groups * 16;
      VkDescriptorBufferInfo bi0 = { use_ubo ? ubo.b : alu ? out.b : in.b, 0,
                                     use_ubo ? 16384 : alu ? out_bytes : in_bytes };
      VkDescriptorBufferInfo bi1 = { out.b, 0, out_bytes };
      VkBufferView view = VK_NULL_HANDLE;
      if (texel) {
         VkBufferViewCreateInfo bvi = { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO, .buffer = in.b,
            .format = strcmp(kind, "texel16") ? VK_FORMAT_R32G32B32A32_SFLOAT
                                              : VK_FORMAT_R16G16B16A16_SFLOAT,
            .offset = 0, .range = in_bytes };
         CHECK(vkCreateBufferView(dev, &bvi, NULL, &view));
      }
      VkWriteDescriptorSet w[2] = {
         { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 0,
           .descriptorCount = 1, .descriptorType = t0,
           .pBufferInfo = texel ? NULL : &bi0, .pTexelBufferView = texel ? &view : NULL },
         { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 1,
           .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
           .pBufferInfo = &bi1 } };
      vkUpdateDescriptorSets(dev, alu ? 1 : 2, w, 0, NULL);

      /* push constants: iters, element count (vec4) of the input, mask, total threads */
      uint32_t pc[4] = { 4, (uint32_t)(in_bytes / 16), 0, 0 };
      uint32_t elems = (uint32_t)(in_bytes / 16);
      pc[2] = elems & (elems - 1) ? 0 : elems - 1;
      pc[3] = local * groups;
      double gpu_ms = 0;
      uint64_t samples[7];
      for (int pass = 0; pass < 7; pass++) {
         int final = pass == 6;
         int reps = final ? 7 : 1;
         for (int rep = 0; rep < reps; rep++) {
            CHECK(vkResetCommandBuffer(cmd, 0));
            VkCommandBufferBeginInfo cbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            CHECK(vkBeginCommandBuffer(cmd, &cbi));
            vkCmdResetQueryPool(cmd, qp, 0, 2);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &set, 0, NULL);
            vkCmdPushConstants(cmd, pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, pc);
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, qp, 0);
            vkCmdDispatch(cmd, groups, 1, 1);
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp, 1);
            CHECK(vkEndCommandBuffer(cmd));
            VkSubmitInfo sub = { VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1,
                                 .pCommandBuffers = &cmd };
            CHECK(vkQueueSubmit(queue, 1, &sub, VK_NULL_HANDLE));
            CHECK(vkQueueWaitIdle(queue));
            uint64_t ts[2];
            CHECK(vkGetQueryPoolResults(dev, qp, 0, 2, sizeof(ts), ts, 8,
                                        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
            samples[rep] = ts[1] - ts[0];
         }
         if (!final) {
            /* scale iterations toward the target time; once within range,
             * the remaining passes only warm the clocks */
            double ms = samples[0] * props.limits.timestampPeriod / 1e6;
            double scale = ms > 0.05 ? target_ms / ms : 64;
            if (scale > 0.8 && scale < 1.25)
               continue;
            if (scale > 64) scale = 64;
            uint32_t next = (uint32_t)fmax(1, floor(pc[0] * scale));
            if (!alu && next > elems) next = elems;
            if (next > 1u << 20) next = 1u << 20;
            pc[0] = next;
         }
      }
      qsort(samples, 7, sizeof(samples[0]), cmp_u64);
      gpu_ms = samples[3] * props.limits.timestampPeriod / 1e6;
      double best_ms = samples[0] * props.limits.timestampPeriod / 1e6;
      double threads = (double)local * groups;
      double total = threads * pc[0] * work;
      printf("%s %u %u %u %.3f %.3f %.3f %s\n", label, local, groups, pc[0], gpu_ms,
             total / (gpu_ms * 1e-3) / 1e9, total / (best_ms * 1e-3) / 1e9, alu ? "GFLOP/s" : "GB/s");
      fflush(stdout);
      vkFreeDescriptorSets(dev, dpool, 1, &set);
      if (view) vkDestroyBufferView(dev, view, NULL);
      vkDestroyPipeline(dev, pipe, NULL);
      vkDestroyShaderModule(dev, sm, NULL);
      free(code);
   }
   vkDestroyDevice(dev, NULL);
   vkDestroyInstance(inst, NULL);
   return 0;
}
