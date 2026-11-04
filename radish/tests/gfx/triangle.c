/* First graphics test for Radish: one triangle into a 64x64 RGBA8 image,
 * read back and compared with a CPU reference.
 *
 * usage: triangle [flat|color|vbo]
 *   flat   fragment shader writes constant red
 *   color  per-vertex colours, interpolated (checked with a tolerance)
 *   vbo    positions and colours from a vertex buffer, an indexed draw
 *          (uint16 indices), colour scaled by a uniform buffer (1, .5, 1, 1)
 *          and a push constant (1, 1, .5, 1)
 *   depth  D32F depth attachment (set to 1.0 by a buffer copy), test LESS
 *          with writes: green at z=.25 then red at z=.5 -> green
 *   clear  load-op clear to blue (no vkCmdClearColorImage), red triangle,
 *          then vkCmdClearAttachments of the 8x8 top-left corner to green
 *   dclear like depth, with the depth attachment cleared by its load op
 *   blend  colour (1, 0, 0, .5) blended SRC_ALPHA / ONE_MINUS_SRC_ALPHA
 *          over the blue background -> (.5, 0, .5, .75)
 * The image is cleared to blue with vkCmdClearColorImage, drawn through a
 * render pass with LOAD_OP_LOAD, copied to a buffer. Pixels whose centre is
 * within 0.75 px of an edge are skipped (rasterisation rules differ there);
 * every other pixel must match. Prints the mismatch count and exits 0 only
 * when it is zero.
 */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t vs_spv[] =
#include "triangle.vert.inc"
;
static const uint32_t flat_spv[] =
#include "flat.frag.inc"
;
static const uint32_t color_spv[] =
#include "color.frag.inc"
;
static const uint32_t vbo_vs_spv[] =
#include "vbo.vert.inc"
;
static const uint32_t vbo_fs_spv[] =
#include "vbo.frag.inc"
;
static const uint32_t push_vs_spv[] =
#include "push.vert.inc"
;
static const uint32_t push_fs_spv[] =
#include "push.frag.inc"
;

#define FUNCS(X)                                                               \
   X(vkCreateInstance) X(vkEnumeratePhysicalDevices) X(vkCreateDevice)         \
   X(vkGetPhysicalDeviceQueueFamilyProperties)                                 \
   X(vkGetPhysicalDeviceMemoryProperties) X(vkGetDeviceQueue)                  \
   X(vkCreateBuffer) X(vkGetBufferMemoryRequirements) X(vkAllocateMemory)      \
   X(vkBindBufferMemory) X(vkMapMemory) X(vkCreateImage)                       \
   X(vkGetImageMemoryRequirements) X(vkBindImageMemory) X(vkCreateImageView)   \
   X(vkCreateShaderModule) X(vkCreatePipelineLayout)                           \
   X(vkCreateGraphicsPipelines) X(vkCreateRenderPass) X(vkCreateFramebuffer)   \
   X(vkCreateCommandPool) X(vkAllocateCommandBuffers)                          \
   X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCmdBindPipeline)          \
   X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass) X(vkCmdDraw)                  \
   X(vkCmdClearColorImage) X(vkCmdPipelineBarrier) X(vkCmdCopyImageToBuffer)   \
   X(vkQueueSubmit) X(vkQueueWaitIdle) X(vkCreateDescriptorSetLayout)          \
   X(vkCreateDescriptorPool) X(vkAllocateDescriptorSets)                       \
   X(vkUpdateDescriptorSets) X(vkCmdBindDescriptorSets) X(vkCmdPushConstants)  \
   X(vkCmdBindVertexBuffers) X(vkCmdBindIndexBuffer) X(vkCmdDrawIndexed)      \
   X(vkCmdCopyBufferToImage) X(vkCmdClearAttachments) X(vkCmdBlitImage)      \
   X(vkCmdClearDepthStencilImage)
#define FN(name) static PFN_##name name;
FUNCS(FN)

#define CHECK(x)                                                               \
   do {                                                                        \
      VkResult r_ = (x);                                                       \
      if (r_ != VK_SUCCESS) {                                                  \
         fprintf(stderr, "%s:%d %s = %d\n", __FILE__, __LINE__, #x, r_);      \
         exit(2);                                                              \
      }                                                                        \
   } while (0)

#ifndef W
#define W 64
#endif
#ifndef H
#define H 64
#endif

static VkDevice dev;
static VkPhysicalDeviceMemoryProperties memprops;

static VkDeviceMemory
alloc(VkMemoryRequirements req)
{
   for (uint32_t i = 0; i < memprops.memoryTypeCount; i++) {
      if ((req.memoryTypeBits & (1u << i)) &&
          (memprops.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
         VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                     .allocationSize = req.size, .memoryTypeIndex = i };
         VkDeviceMemory m;
         CHECK(vkAllocateMemory(dev, &ai, NULL, &m));
         return m;
      }
   }
   fprintf(stderr, "no memory type\n");
   exit(2);
}

static VkShaderModule
module(const uint32_t *code, size_t size)
{
   VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                   .codeSize = size, .pCode = code };
   VkShaderModule m;
   CHECK(vkCreateShaderModule(dev, &ci, NULL, &m));
   return m;
}

static VkBuffer
host_buffer(VkDeviceSize size, VkBufferUsageFlags usage, const void *data)
{
   VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = usage };
   VkBuffer b;
   CHECK(vkCreateBuffer(dev, &bci, NULL, &b));
   VkMemoryRequirements req;
   vkGetBufferMemoryRequirements(dev, b, &req);
   VkDeviceMemory m = alloc(req);
   CHECK(vkBindBufferMemory(dev, b, m, 0));
   void *map;
   CHECK(vkMapMemory(dev, m, 0, size, 0, &map));
   memcpy(map, data, size);
   return b;
}

static double
edge(double ax, double ay, double bx, double by, double px, double py)
{
   return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

int
main(int argc, char **argv)
{
   int vbo = argc > 1 && !strcmp(argv[1], "vbo");
   int dclear = argc > 1 && (!strcmp(argv[1], "dclear") || !strcmp(argv[1], "dmap") ||
                             !strcmp(argv[1], "dsamp"));
   int depth = dclear || (argc > 1 && (!strcmp(argv[1], "depth") || !strcmp(argv[1], "dsimg")));
   int clear = argc > 1 && !strcmp(argv[1], "clear");
   int blend = argc > 1 && !strcmp(argv[1], "blend");
   /* msaa: flat triangle into a 4-sample attachment cleared to blue, resolved
    * into the single-sample image by the render pass */
   int msaa = argc > 1 && !strcmp(argv[1], "msaa");
   /* blit: render into a second image, blit it back mirrored in x (the
    * triangle is symmetric about x = 32) */
   int blit = argc > 1 && !strcmp(argv[1], "blit");
   /* dsimg: depth mode with the depth image set by vkCmdClearDepthStencilImage */
   int dsimg = argc > 1 && !strcmp(argv[1], "dsimg");
   /* dmap: dclear, then every pixel cleared to its own depth (index / 2^20)
    * with vkCmdClearAttachments; prints where each landed in memory */
   int dsamp = argc > 1 && !strcmp(argv[1], "dsamp");
   int dmap = dsamp || (argc > 1 && !strcmp(argv[1], "dmap"));
   int push_mode = depth || blend;
   int color = vbo || (argc > 1 && !strcmp(argv[1], "color"));
   void *lib = dlopen("libvulkan.so.1", RTLD_NOW);
   if (!lib) {
      fprintf(stderr, "no loader\n");
      return 2;
   }
   PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
   vkCreateInstance = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
   VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1 };
   VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
   VkInstance inst;
   CHECK(vkCreateInstance(&ici, NULL, &inst));
#define LOAD(name) name = (PFN_##name)gipa(inst, #name);
   FUNCS(LOAD)
   uint32_t n = 1;
   VkPhysicalDevice phys;
   CHECK(vkEnumeratePhysicalDevices(inst, &n, &phys));
   vkGetPhysicalDeviceMemoryProperties(phys, &memprops);
   VkQueueFamilyProperties qf;
   n = 1;
   vkGetPhysicalDeviceQueueFamilyProperties(phys, &n, &qf);
   if (!(qf.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
      fprintf(stderr, "queue family 0 has no graphics (set RADISH_GRAPHICS=1)\n");
      return 2;
   }
   float prio = 1.0f;
   VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                   .queueCount = 1, .pQueuePriorities = &prio };
   VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                              .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci };
   CHECK(vkCreateDevice(phys, &dci, NULL, &dev));
   VkQueue queue;
   vkGetDeviceQueue(dev, 0, 0, &queue);

   VkImageCreateInfo imci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
      .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = { W, H, 1 }, .mipLevels = 1, .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
               VK_IMAGE_USAGE_TRANSFER_DST_BIT };
   VkImage image;
   CHECK(vkCreateImage(dev, &imci, NULL, &image));
   VkMemoryRequirements req;
   vkGetImageMemoryRequirements(dev, image, &req);
   CHECK(vkBindImageMemory(dev, image, alloc(req), 0));
   VkImageViewCreateInfo vci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
      .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
   VkImageView view;
   CHECK(vkCreateImageView(dev, &vci, NULL, &view));

   VkImage bimage = VK_NULL_HANDLE;
   if (blit) {
      CHECK(vkCreateImage(dev, &imci, NULL, &bimage));
      vkGetImageMemoryRequirements(dev, bimage, &req);
      CHECK(vkBindImageMemory(dev, bimage, alloc(req), 0));
      VkImage t = image;
      image = bimage;
      bimage = t; /* render into the new image, blit into the read-back one */
      vci.image = image;
      CHECK(vkCreateImageView(dev, &vci, NULL, &view));
   }
   VkImage msimage = VK_NULL_HANDLE;
   VkImageView msview = VK_NULL_HANDLE;
   if (msaa) {
      VkImageCreateInfo mci = imci;
      mci.samples = VK_SAMPLE_COUNT_4_BIT;
      mci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
      CHECK(vkCreateImage(dev, &mci, NULL, &msimage));
      vkGetImageMemoryRequirements(dev, msimage, &req);
      CHECK(vkBindImageMemory(dev, msimage, alloc(req), 0));
      VkImageViewCreateInfo mvci = vci;
      mvci.image = msimage;
      CHECK(vkCreateImageView(dev, &mvci, NULL, &msview));
   }
   VkImage dimage = VK_NULL_HANDLE;
   VkImageView dview = VK_NULL_HANDLE;
   if (depth) {
      VkImageCreateInfo dci2 = imci;
      dci2.format = VK_FORMAT_D32_SFLOAT;
      dci2.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                   VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
      CHECK(vkCreateImage(dev, &dci2, NULL, &dimage));
      vkGetImageMemoryRequirements(dev, dimage, &req);
      CHECK(vkBindImageMemory(dev, dimage, alloc(req), 0));
      VkImageViewCreateInfo dvci = vci;
      dvci.image = dimage;
      dvci.format = VK_FORMAT_D32_SFLOAT;
      dvci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
      CHECK(vkCreateImageView(dev, &dvci, NULL, &dview));
   }
   VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = W * H * 4,
                              .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT };
   VkBuffer buffer;
   CHECK(vkCreateBuffer(dev, &bci, NULL, &buffer));
   vkGetBufferMemoryRequirements(dev, buffer, &req);
   VkDeviceMemory bmem = alloc(req);
   CHECK(vkBindBufferMemory(dev, buffer, bmem, 0));
   uint8_t *pixels;
   CHECK(vkMapMemory(dev, bmem, 0, W * H * 4, 0, (void **)&pixels));
   memset(pixels, 0x55, W * H * 4);

   VkAttachmentDescription att = { .format = VK_FORMAT_R8G8B8A8_UNORM,
      .samples = VK_SAMPLE_COUNT_1_BIT, .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE, .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
   if (clear) {
      att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   }
   VkAttachmentDescription atts[2] = { att, att };
   atts[1].format = VK_FORMAT_D32_SFLOAT;
   if (dclear)
      atts[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
   atts[1].initialLayout = atts[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
   if (msaa) {
      atts[1] = atts[0];
      atts[0].samples = VK_SAMPLE_COUNT_4_BIT;
      atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      atts[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      atts[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      atts[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
   }
   VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
   VkAttachmentReference rref = { 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
   VkAttachmentReference dref = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
   VkSubpassDescription sub = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                                .colorAttachmentCount = 1, .pColorAttachments = &ref,
                                .pResolveAttachments = msaa ? &rref : NULL,
                                .pDepthStencilAttachment = depth ? &dref : NULL };
   VkRenderPassCreateInfo rpci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = depth || msaa ? 2 : 1, .pAttachments = atts, .subpassCount = 1, .pSubpasses = &sub };
   VkRenderPass pass;
   CHECK(vkCreateRenderPass(dev, &rpci, NULL, &pass));
   VkImageView views[2] = { view, dview };
   if (msaa) {
      views[0] = msview;
      views[1] = view;
   }
   VkFramebufferCreateInfo fci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = pass,
      .attachmentCount = depth || msaa ? 2 : 1, .pAttachments = views, .width = W, .height = H, .layers = 1 };
   VkFramebuffer fb;
   CHECK(vkCreateFramebuffer(dev, &fci, NULL, &fb));

   VkDescriptorSetLayoutBinding dslb = { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                         VK_SHADER_STAGE_FRAGMENT_BIT };
   VkDescriptorSetLayoutCreateInfo dslci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                             .bindingCount = 1, .pBindings = &dslb };
   VkDescriptorSetLayout dsl;
   CHECK(vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl));
   VkPushConstantRange pcr = { VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16 };
   if (push_mode)
      pcr = (VkPushConstantRange) { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 20 };
   VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = vbo ? 1 : 0, .pSetLayouts = &dsl,
      .pushConstantRangeCount = vbo || push_mode ? 1 : 0, .pPushConstantRanges = &pcr };
   VkPipelineLayout layout;
   CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &layout));
   VkDescriptorSet set = VK_NULL_HANDLE;
   VkBuffer vb = VK_NULL_HANDLE, ib = VK_NULL_HANDLE;
   if (vbo) {
      const float ubo_data[4] = { 1, 0.5f, 1, 1 };
      VkBuffer ub = host_buffer(sizeof(ubo_data), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, ubo_data);
      VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 };
      VkDescriptorPoolCreateInfo dpci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                          .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &ps };
      VkDescriptorPool dp;
      CHECK(vkCreateDescriptorPool(dev, &dpci, NULL, &dp));
      VkDescriptorSetAllocateInfo dsai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                           .descriptorPool = dp, .descriptorSetCount = 1, .pSetLayouts = &dsl };
      CHECK(vkAllocateDescriptorSets(dev, &dsai, &set));
      VkDescriptorBufferInfo dbi = { ub, 0, VK_WHOLE_SIZE };
      VkWriteDescriptorSet w = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set,
                                 .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                 .pBufferInfo = &dbi };
      vkUpdateDescriptorSets(dev, 1, &w, 0, NULL);
      /* vertices in the order 2, 0, 1 so the indices matter */
      const float vertices[] = { 0.0f, 0.5f, 0, 0, 1,   -0.5f, -0.5f, 1, 0, 0,   0.5f, -0.5f, 0, 1, 0 };
      const uint16_t indices[] = { 1, 2, 0, 0 };
      vb = host_buffer(sizeof(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices);
      ib = host_buffer(sizeof(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indices);
   }
   VkPipelineShaderStageCreateInfo stages[2] = {
      { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = push_mode ? module(push_vs_spv, sizeof(push_vs_spv)) :
                  vbo ? module(vbo_vs_spv, sizeof(vbo_vs_spv)) : module(vs_spv, sizeof(vs_spv)),
        .pName = "main" },
      { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = push_mode ? module(push_fs_spv, sizeof(push_fs_spv)) :
                  vbo ? module(vbo_fs_spv, sizeof(vbo_fs_spv)) :
                  color ? module(color_spv, sizeof(color_spv)) : module(flat_spv, sizeof(flat_spv)),
        .pName = "main" },
   };
   VkVertexInputBindingDescription vib = { 0, 5 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX };
   VkVertexInputAttributeDescription via[2] = {
      { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 }, { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, 2 * sizeof(float) } };
   VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = vbo ? 1 : 0, .pVertexBindingDescriptions = &vib,
      .vertexAttributeDescriptionCount = vbo ? 2 : 0, .pVertexAttributeDescriptions = via };
   VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
   VkViewport vp = { 0, 0, W, H, 0, 1 };
   VkRect2D sc = { { 0, 0 }, { W, H } };
   VkPipelineViewportStateCreateInfo vps = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &sc };
   VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
   VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = msaa ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT };
   VkPipelineColorBlendAttachmentState cba = { .colorWriteMask = 0xf };
   if (blend)
      cba = (VkPipelineColorBlendAttachmentState) { .blendEnable = VK_TRUE,
         .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
         .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .colorBlendOp = VK_BLEND_OP_ADD,
         .srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
         .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, .alphaBlendOp = VK_BLEND_OP_ADD,
         .colorWriteMask = 0xf };
   VkPipelineDepthStencilStateCreateInfo dss = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE, .depthCompareOp = VK_COMPARE_OP_LESS };
   VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1, .pAttachments = &cba };
   VkGraphicsPipelineCreateInfo gci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = 2, .pStages = stages, .pVertexInputState = &vi, .pInputAssemblyState = &ia,
      .pViewportState = &vps, .pRasterizationState = &rs, .pMultisampleState = &ms,
      .pColorBlendState = &cb, .pDepthStencilState = depth ? &dss : NULL,
      .layout = layout, .renderPass = pass };
   VkPipeline pipeline;
   CHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gci, NULL, &pipeline));

   VkCommandPoolCreateInfo cpci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
   VkCommandPool pool;
   CHECK(vkCreateCommandPool(dev, &cpci, NULL, &pool));
   VkCommandBufferAllocateInfo cbai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
   VkCommandBuffer cmd;
   CHECK(vkAllocateCommandBuffers(dev, &cbai, &cmd));
   VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
   CHECK(vkBeginCommandBuffer(cmd, &begin));
   VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
   VkImageMemoryBarrier b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = 0, .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image, .subresourceRange = range };
   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 0, NULL, 0, NULL, 1, &b);
   /* in clear mode the render pass load op must replace this white */
   VkClearColorValue blue = { .float32 = { clear || msaa, clear || msaa, 1, 1 } };
   vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &blue, 1, &range);
   b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
   b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
   b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
   b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1, &b);
   if (dsimg) {
      VkImageMemoryBarrier db0 = b;
      db0.srcAccessMask = 0;
      db0.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      db0.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      db0.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      db0.image = dimage;
      db0.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0, 0, NULL, 0, NULL, 1, &db0);
      VkClearDepthStencilValue one = { 1.0f, 0 };
      vkCmdClearDepthStencilImage(cmd, dimage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &one, 1,
                                  &db0.subresourceRange);
      db0.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      db0.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      db0.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      db0.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, NULL, 0, NULL, 1, &db0);
   } else if (depth && !dclear) {
      static float ones[W * H];
      for (unsigned i = 0; i < W * H; i++)
         ones[i] = 1.0f;
      VkBuffer db = host_buffer(sizeof(ones), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, ones);
      VkImageMemoryBarrier db0 = b;
      db0.srcAccessMask = 0;
      db0.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      db0.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      db0.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      db0.image = dimage;
      db0.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0, 0, NULL, 0, NULL, 1, &db0);
      VkBufferImageCopy dcopy = { .imageSubresource = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1 },
                                  .imageExtent = { W, H, 1 } };
      vkCmdCopyBufferToImage(cmd, db, dimage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &dcopy);
      db0.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      db0.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      db0.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      db0.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, NULL, 0, NULL, 1, &db0);
   }
   if (dclear) {
      VkImageMemoryBarrier db0 = b;
      db0.srcAccessMask = 0;
      db0.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      db0.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      db0.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      db0.image = dimage;
      db0.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, NULL, 0, NULL, 1, &db0);
   }
   VkClearValue clears[2] = { { .color = { .float32 = { 0, 0, 1, 1 } } },
                              { .depthStencil = { 1.0f, 0 } } };
   VkRenderPassBeginInfo rpbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = pass,
      .framebuffer = fb, .renderArea = sc, .clearValueCount = 2, .pClearValues = clears };
   vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
   vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
   if (push_mode) {
      VkShaderStageFlags st = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
      if (depth) {
         const float green[5] = { 0, 1, 0, 1, 0.25f }, red[5] = { 1, 0, 0, 1, 0.5f };
         vkCmdPushConstants(cmd, layout, st, 0, sizeof(green), green);
         vkCmdDraw(cmd, 3, 1, 0, 0);
         vkCmdPushConstants(cmd, layout, st, 0, sizeof(red), red);
         vkCmdDraw(cmd, 3, 1, 0, 0);
      } else {
         const float half_red[5] = { 1, 0, 0, 0.5f, 0 };
         vkCmdPushConstants(cmd, layout, st, 0, sizeof(half_red), half_red);
         vkCmdDraw(cmd, 3, 1, 0, 0);
      }
   } else if (vbo) {
      const float push[4] = { 1, 1, 0.5f, 1 };
      VkDeviceSize zero = 0;
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, NULL);
      vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), push);
      vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &zero);
      vkCmdBindIndexBuffer(cmd, ib, 0, VK_INDEX_TYPE_UINT16);
      vkCmdDrawIndexed(cmd, 3, 1, 0, 0, 0);
   } else {
      vkCmdDraw(cmd, 3, 1, 0, 0);
   }
   if (dmap) {
      for (int y = 0; y < H; y++)
         for (int x = 0; x < W; x++) {
            VkClearAttachment ca = { VK_IMAGE_ASPECT_DEPTH_BIT, 0,
               { .depthStencil = { (y * W + x + 1) / 1048576.0f, 0 } } };
            VkClearRect cr = { { { x, y }, { 1, 1 } }, 0, 1 };
            vkCmdClearAttachments(cmd, 1, &ca, 1, &cr);
         }
   }
   if (clear) {
      VkClearAttachment ca = { VK_IMAGE_ASPECT_COLOR_BIT, 0, { .color = { .float32 = { 0, 1, 0, 1 } } } };
      VkClearRect cr = { { { 0, 0 }, { 8, 8 } }, 0, 1 };
      vkCmdClearAttachments(cmd, 1, &ca, 1, &cr);
   }
   vkCmdEndRenderPass(cmd);
   b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
   b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
   b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
   if (blit) {
      VkImageMemoryBarrier bb = b;
      bb.srcAccessMask = 0;
      bb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      bb.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      bb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      bb.image = bimage;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0, 0, NULL, 0, NULL, 1, &bb);
      VkImageBlit region = { .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
         .srcOffsets = { { 0, 0, 0 }, { W, H, 1 } },
         .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
         .dstOffsets = { { W, 0, 0 }, { 0, H, 1 } } };
      vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, bimage,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST);
      bb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      bb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      bb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      bb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0, 0, NULL, 0, NULL, 1, &bb);
      image = bimage;
   }
   VkBufferImageCopy copy = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                              .imageExtent = { W, H, 1 } };
   vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
   VkBuffer dbuf = VK_NULL_HANDLE;
   float *dpixels = NULL;
   if (depth) {
      /* read the depth back too: 0.25 inside, 1 outside */
      VkBufferCreateInfo dbci = bci;
      CHECK(vkCreateBuffer(dev, &dbci, NULL, &dbuf));
      vkGetBufferMemoryRequirements(dev, dbuf, &req);
      VkDeviceMemory dmem = alloc(req);
      CHECK(vkBindBufferMemory(dev, dbuf, dmem, 0));
      CHECK(vkMapMemory(dev, dmem, 0, W * H * 4, 0, (void **)&dpixels));
      memset(dpixels, 0x55, W * H * 4);
      VkImageMemoryBarrier db1 = b;
      db1.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      db1.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      db1.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      db1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      db1.image = dimage;
      db1.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &db1);
      VkBufferImageCopy dcopy = { .imageSubresource = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1 },
                                  .imageExtent = { W, H, 1 } };
      if (dsamp) {
         /* sample the depth image (experiment: RADISH_EXP_DEPTH_TILE) into R32F */
         VkImageCreateInfo fci2 = imci;
         fci2.format = VK_FORMAT_R32_SFLOAT;
         VkImage fimage;
         CHECK(vkCreateImage(dev, &fci2, NULL, &fimage));
         vkGetImageMemoryRequirements(dev, fimage, &req);
         CHECK(vkBindImageMemory(dev, fimage, alloc(req), 0));
         VkImageMemoryBarrier fb = b;
         fb.srcAccessMask = 0;
         fb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
         fb.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
         fb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
         fb.image = fimage;
         vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                              0, 0, NULL, 0, NULL, 1, &fb);
         VkImageBlit region = { .srcSubresource = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1 },
            .srcOffsets = { { 0, 0, 0 }, { W, H, 1 } },
            .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .dstOffsets = { { 0, 0, 0 }, { W, H, 1 } } };
         vkCmdBlitImage(cmd, dimage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, fimage,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST);
         fb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
         fb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
         fb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
         fb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
         vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                              0, 0, NULL, 0, NULL, 1, &fb);
         dcopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
         vkCmdCopyImageToBuffer(cmd, fimage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dbuf, 1, &dcopy);
      } else {
         vkCmdCopyImageToBuffer(cmd, dimage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dbuf, 1, &dcopy);
      }
   }
   CHECK(vkEndCommandBuffer(cmd));
   VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd };
   CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
   CHECK(vkQueueWaitIdle(queue));

   /* CPU reference: NDC (-0.5,-0.5) (0.5,-0.5) (0,0.5) -> framebuffer. */
   const double vx[3] = { W / 4.0, W * 3 / 4.0, W / 2.0 }, vy[3] = { H / 4.0, H / 4.0, H * 3 / 4.0 };
   double area = edge(vx[0], vy[0], vx[1], vy[1], vx[2], vy[2]);
   unsigned checked = 0, bad = 0, inside_n = 0, mixed = 0;
   for (int y = 0; y < H; y++) {
      for (int x = 0; x < W; x++) {
         double px = x + 0.5, py = y + 0.5;
         double w0 = edge(vx[1], vy[1], vx[2], vy[2], px, py) / area;
         double w1 = edge(vx[2], vy[2], vx[0], vy[0], px, py) / area;
         double w2 = edge(vx[0], vy[0], vx[1], vy[1], px, py) / area;
         /* distance to each edge in pixels */
         double d0 = fabs(edge(vx[1], vy[1], vx[2], vy[2], px, py)) / hypot(vx[2] - vx[1], vy[2] - vy[1]);
         double d1 = fabs(edge(vx[2], vy[2], vx[0], vy[0], px, py)) / hypot(vx[0] - vx[2], vy[0] - vy[2]);
         double d2 = fabs(edge(vx[0], vy[0], vx[1], vy[1], px, py)) / hypot(vx[1] - vx[0], vy[1] - vy[0]);
         if (d0 < 0.75 || d1 < 0.75 || d2 < 0.75) {
            const uint8_t *p = pixels + 4 * (y * W + x);
            if (p[0] > 20 && p[0] < 235 && p[2] > 20 && p[2] < 235)
               mixed++;
            continue;
         }
         int inside = w0 > 0 && w1 > 0 && w2 > 0;
         double want[4];
         if (clear && x < 8 && y < 8) {
            want[0] = 0; want[1] = 1; want[2] = 0; want[3] = 1;
         } else if (!inside) {
            want[0] = 0; want[1] = 0; want[2] = 1; want[3] = 1;
         } else if (depth) {
            want[0] = 0; want[1] = 1; want[2] = 0; want[3] = 1;
         } else if (blend) {
            want[0] = 0.5; want[1] = 0; want[2] = 0.5; want[3] = 0.75;
         } else if (!color) {
            want[0] = 1; want[1] = 0; want[2] = 0; want[3] = 1;
         } else {
            want[0] = w0; want[1] = w1; want[2] = w2; want[3] = 1;
            if (vbo) {
               want[1] *= 0.5;
               want[2] *= 0.5;
            }
         }
         inside_n += inside;
         if (dpixels) {
            float dw = inside ? 0.25f : 1.0f, dg = dpixels[y * W + x];
            if (fabsf(dg - dw) > 1e-4f) {
               if (bad < 8)
                  fprintf(stderr, "depth %d,%d got %f want %f\n", x, y, dg, dw);
               bad++;
            }
         }
         const uint8_t *p = pixels + 4 * (y * W + x);
         int ok = 1;
         for (int c = 0; c < 4; c++)
            if (fabs(p[c] / 255.0 - want[c]) > (color || blend ? 3.0 / 255 : 0.5 / 255))
               ok = 0;
         checked++;
         if (!ok) {
            if (bad < 8)
               fprintf(stderr, "pixel %d,%d got %u %u %u %u want %.3f %.3f %.3f %.3f\n", x, y,
                       p[0], p[1], p[2], p[3], want[0], want[1], want[2], want[3]);
            bad++;
         }
      }
   }
   printf("triangle %s: %u pixels checked (%u inside), %u mismatches, %u mixed edge pixels\n",
          blit ? "blit" : dsimg ? "dsimg" : msaa ? "msaa" : dclear ? "dclear" : depth ? "depth" : clear ? "clear" : blend ? "blend" : vbo ? "vbo" : color ? "color" : "flat",
          checked, inside_n, bad, mixed);
   if (msaa && mixed < 20) {
      fprintf(stderr, "msaa: too few mixed edge pixels\n");
      bad++;
   }
   for (int y = 0; y < H; y += 4 * (H / 64)) {
      for (int x = 0; x < W; x += 2 * (W / 64)) {
         const uint8_t *p = pixels + 4 * (y * W + x);
         putchar(p[0] > 128 ? 'R' : p[1] > 128 ? 'G' : p[2] > 128 ? '.' : p[0] == 0x55 ? '?' : '#');
      }
      putchar('\n');
   }
   if (dmap) {
      for (int i = 0; i < W * H; i++) {
         int id = (int)(dpixels[i] * 1048576.0f + 0.5f) - 1;
         printf("M %d %d\n", i, id);
      }
      return 0;
   }
   if (dpixels && getenv("DEPTH_MAP")) {
      for (int y = 0; y < H; y++) {
         for (int x = 0; x < W; x++) {
            float d = dpixels[y * W + x];
            putchar(d == 1.0f ? '.' : d == 0.25f ? '#' : d == 0.0f ? '0' : '?');
         }
         putchar('\n');
      }
   }
   return bad ? 1 : 0;
}
