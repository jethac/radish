/* First graphics test for Radish: one triangle into a 64x64 RGBA8 image,
 * read back and compared with a CPU reference.
 *
 * usage: triangle [flat|color]
 *   flat   fragment shader writes constant red
 *   color  per-vertex colours, interpolated (checked with a tolerance)
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
   X(vkQueueSubmit) X(vkQueueWaitIdle)
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

#define W 64
#define H 64

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

static double
edge(double ax, double ay, double bx, double by, double px, double py)
{
   return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

int
main(int argc, char **argv)
{
   int color = argc > 1 && !strcmp(argv[1], "color");
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
   VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
   VkSubpassDescription sub = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                                .colorAttachmentCount = 1, .pColorAttachments = &ref };
   VkRenderPassCreateInfo rpci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = 1, .pAttachments = &att, .subpassCount = 1, .pSubpasses = &sub };
   VkRenderPass pass;
   CHECK(vkCreateRenderPass(dev, &rpci, NULL, &pass));
   VkFramebufferCreateInfo fci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = pass,
      .attachmentCount = 1, .pAttachments = &view, .width = W, .height = H, .layers = 1 };
   VkFramebuffer fb;
   CHECK(vkCreateFramebuffer(dev, &fci, NULL, &fb));

   VkPipelineLayoutCreateInfo plci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
   VkPipelineLayout layout;
   CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &layout));
   VkPipelineShaderStageCreateInfo stages[2] = {
      { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = module(vs_spv, sizeof(vs_spv)), .pName = "main" },
      { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = color ? module(color_spv, sizeof(color_spv)) : module(flat_spv, sizeof(flat_spv)),
        .pName = "main" },
   };
   VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
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
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
   VkPipelineColorBlendAttachmentState cba = { .colorWriteMask = 0xf };
   VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1, .pAttachments = &cba };
   VkGraphicsPipelineCreateInfo gci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = 2, .pStages = stages, .pVertexInputState = &vi, .pInputAssemblyState = &ia,
      .pViewportState = &vps, .pRasterizationState = &rs, .pMultisampleState = &ms,
      .pColorBlendState = &cb, .layout = layout, .renderPass = pass };
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
   VkClearColorValue blue = { .float32 = { 0, 0, 1, 1 } };
   vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &blue, 1, &range);
   b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
   b.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
   b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
   b.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1, &b);
   VkRenderPassBeginInfo rpbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = pass,
      .framebuffer = fb, .renderArea = sc };
   vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
   vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
   vkCmdDraw(cmd, 3, 1, 0, 0);
   vkCmdEndRenderPass(cmd);
   b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
   b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
   b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
   vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
   VkBufferImageCopy copy = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                              .imageExtent = { W, H, 1 } };
   vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
   CHECK(vkEndCommandBuffer(cmd));
   VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmd };
   CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
   CHECK(vkQueueWaitIdle(queue));

   /* CPU reference: NDC (-0.5,-0.5) (0.5,-0.5) (0,0.5) -> framebuffer. */
   const double vx[3] = { 16, 48, 32 }, vy[3] = { 16, 16, 48 };
   double area = edge(vx[0], vy[0], vx[1], vy[1], vx[2], vy[2]);
   unsigned checked = 0, bad = 0, inside_n = 0;
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
         if (d0 < 0.75 || d1 < 0.75 || d2 < 0.75)
            continue;
         int inside = w0 > 0 && w1 > 0 && w2 > 0;
         double want[4];
         if (!inside) {
            want[0] = 0; want[1] = 0; want[2] = 1; want[3] = 1;
         } else if (!color) {
            want[0] = 1; want[1] = 0; want[2] = 0; want[3] = 1;
         } else {
            want[0] = w0; want[1] = w1; want[2] = w2; want[3] = 1;
         }
         inside_n += inside;
         const uint8_t *p = pixels + 4 * (y * W + x);
         int ok = 1;
         for (int c = 0; c < 4; c++)
            if (fabs(p[c] / 255.0 - want[c]) > (color ? 3.0 / 255 : 0.5 / 255))
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
   printf("triangle %s: %u pixels checked (%u inside), %u mismatches\n",
          color ? "color" : "flat", checked, inside_n, bad);
   for (int y = 0; y < H; y += 4) {
      for (int x = 0; x < W; x += 2) {
         const uint8_t *p = pixels + 4 * (y * W + x);
         putchar(p[0] > 128 ? 'R' : p[1] > 128 ? 'G' : p[2] > 128 ? '.' : p[0] == 0x55 ? '?' : '#');
      }
      putchar('\n');
   }
   return bad ? 1 : 0;
}
