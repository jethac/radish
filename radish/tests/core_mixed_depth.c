#include "test_common.h"
#include <stdbool.h>

int main(int argc, char **argv)
{
   REQUIRE(argc==2);
   void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); REQUIRE(lib);
   PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr"); REQUIRE(gipa);
   VkInstance instance=VK_NULL_HANDLE; INSTANCE_FN(CreateInstance);
   VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_1};
   VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
   SUCCESS(CreateInstance(&ici,NULL,&instance));
   INSTANCE_FN(EnumeratePhysicalDevices); INSTANCE_FN(GetPhysicalDeviceProperties);
   INSTANCE_FN(GetPhysicalDeviceMemoryProperties); INSTANCE_FN(GetPhysicalDeviceFormatProperties);
   INSTANCE_FN(GetPhysicalDeviceImageFormatProperties); INSTANCE_FN(CreateDevice);
   INSTANCE_FN(GetDeviceProcAddr); INSTANCE_FN(DestroyInstance);
   uint32_t count=1; VkPhysicalDevice physical;
   SUCCESS(EnumeratePhysicalDevices(instance,&count,&physical)); REQUIRE(count==1);
   VkPhysicalDeviceProperties props; GetPhysicalDeviceProperties(physical,&props);
   REQUIRE(props.apiVersion>=VK_API_VERSION_1_1&&props.limits.maxColorAttachments>=4);
   REQUIRE(props.limits.maxFramebufferWidth>=4096&&props.limits.maxFramebufferHeight>=4096&&props.limits.maxFramebufferLayers>=256);
   VkPhysicalDeviceMemoryProperties memory; GetPhysicalDeviceMemoryProperties(physical,&memory);
   float priority=1; VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueCount=1,.pQueuePriorities=&priority};
   VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};
   VkDevice device; SUCCESS(CreateDevice(physical,&di,NULL,&device)); PFN_vkGetDeviceProcAddr gdpa=GetDeviceProcAddr;
   DEVICE_FN(CreateImage); DEVICE_FN(DestroyImage); DEVICE_FN(GetImageMemoryRequirements);
   DEVICE_FN(AllocateMemory); DEVICE_FN(FreeMemory); DEVICE_FN(BindImageMemory);
   DEVICE_FN(CreateImageView); DEVICE_FN(DestroyImageView);
   DEVICE_FN(CreateRenderPass); DEVICE_FN(DestroyRenderPass); DEVICE_FN(GetRenderAreaGranularity);
   DEVICE_FN(CreateFramebuffer); DEVICE_FN(DestroyFramebuffer); DEVICE_FN(DestroyDevice);
   DEVICE_FN(CreateCommandPool); DEVICE_FN(DestroyCommandPool); DEVICE_FN(ResetCommandPool);
   DEVICE_FN(AllocateCommandBuffers); DEVICE_FN(BeginCommandBuffer); DEVICE_FN(EndCommandBuffer); DEVICE_FN(CmdPipelineBarrier);
   DEVICE_FN(GetDeviceQueue); DEVICE_FN(CreateFence); DEVICE_FN(DestroyFence); DEVICE_FN(ResetFences); DEVICE_FN(QueueSubmit); DEVICE_FN(WaitForFences);
   VkCommandPoolCreateInfo pool_info={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
   VkCommandPool pool; SUCCESS(CreateCommandPool(device,&pool_info,NULL,&pool));
   VkCommandBufferAllocateInfo command_info={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
   VkCommandBuffer command; SUCCESS(AllocateCommandBuffers(device,&command_info,&command));
   VkFenceCreateInfo fence_info={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence; SUCCESS(CreateFence(device,&fence_info,NULL,&fence));
   VkQueue queue; GetDeviceQueue(device,0,0,&queue);
   const VkFormat depth_formats[3]={VK_FORMAT_D16_UNORM,VK_FORMAT_D32_SFLOAT,VK_FORMAT_S8_UINT};
   VkDeviceSize single_size[3][2]={{0}};
   for(unsigned f=0;f<3;f++) for(unsigned ms=0;ms<2;ms++) {
      VkSampleCountFlagBits samples=ms?VK_SAMPLE_COUNT_4_BIT:VK_SAMPLE_COUNT_1_BIT;
      VkImage images[5]; VkImageView views[5]; VkDeviceMemory allocations[5];
      VkAttachmentDescription attachments[5]; VkAttachmentReference colors[4];
      VkDeviceSize total=0;
      for(unsigned j=0;j<5;j++) {
         bool depth=j==4; VkFormat format=depth?depth_formats[f]:VK_FORMAT_R8G8B8A8_UNORM;
         VkImageUsageFlags usage=(depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)|
            VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
         VkFormatProperties fp; GetPhysicalDeviceFormatProperties(physical,format,&fp);
         VkFormatFeatureFlags required=(depth?VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)|
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
         REQUIRE((fp.optimalTilingFeatures&required)==required);
         VkImageFormatProperties ip; SUCCESS(GetPhysicalDeviceImageFormatProperties(physical,format,VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,usage,0,&ip));
         REQUIRE(ip.sampleCounts&samples); REQUIRE(ip.maxExtent.width>=4&&ip.maxExtent.height>=4&&ip.maxArrayLayers>=1);
         VkImageCreateInfo ci={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=format,
            .extent={4,4,1},.mipLevels=1,.arrayLayers=1,.samples=samples,.tiling=VK_IMAGE_TILING_OPTIMAL,.usage=usage,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
         SUCCESS(CreateImage(device,&ci,NULL,&images[j])); VkMemoryRequirements req; GetImageMemoryRequirements(device,images[j],&req);
         REQUIRE(req.size&&req.size<1024*1024&&req.alignment&&req.memoryTypeBits);
         uint32_t type=UINT32_MAX; for(unsigned m=0;m<memory.memoryTypeCount;m++) if(req.memoryTypeBits&(1u<<m)){type=m;break;}
         REQUIRE(type!=UINT32_MAX); VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=type};
         SUCCESS(AllocateMemory(device,&ai,NULL,&allocations[j])); SUCCESS(BindImageMemory(device,images[j],allocations[j],0)); total+=req.size;
         VkImageAspectFlags aspect=depth?(f==2?VK_IMAGE_ASPECT_STENCIL_BIT:VK_IMAGE_ASPECT_DEPTH_BIT):VK_IMAGE_ASPECT_COLOR_BIT;
         VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=images[j],.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=format,
            .subresourceRange={aspect,0,1,0,1}}; SUCCESS(CreateImageView(device,&vi,NULL,&views[j]));
         VkImageLayout final=depth?VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
         attachments[j]=(VkAttachmentDescription){.format=format,.samples=samples,.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout=VK_IMAGE_LAYOUT_UNDEFINED,.finalLayout=final};
         if(!depth) colors[j]=(VkAttachmentReference){j,final};
         if(j==0||depth) {
            unsigned n=depth?1:0;
            if(!ms) single_size[f][n]=req.size;
            else REQUIRE(req.size>=single_size[f][n]);
            printf("ATTACHMENT_HOST_IMAGE format=%u samples=%u size=%llu alignment=%llu aspect=%u\n",format,samples,
               (unsigned long long)req.size,(unsigned long long)req.alignment,aspect);
         }
      }
      REQUIRE(total<5*1024*1024);
      VkAttachmentReference depth={4,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
      VkSubpassDescription sub={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=4,.pColorAttachments=colors,.pDepthStencilAttachment=&depth};
      VkRenderPassCreateInfo ri={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=5,.pAttachments=attachments,.subpassCount=1,.pSubpasses=&sub};
      VkRenderPass pass; SUCCESS(CreateRenderPass(device,&ri,NULL,&pass));
      VkExtent2D granularity={0}; GetRenderAreaGranularity(device,pass,&granularity); REQUIRE(granularity.width&&granularity.height);
      VkFramebufferCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=pass,.attachmentCount=5,.pAttachments=views,.width=4,.height=4,.layers=1};
      VkFramebuffer framebuffer; SUCCESS(CreateFramebuffer(device,&fi,NULL,&framebuffer));
      SUCCESS(ResetCommandPool(device,pool,0));
      VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; SUCCESS(BeginCommandBuffer(command,&begin));
      const VkImageLayout transitions[4]={VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL,
         VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL};
      VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,
         .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=images[4],
         .subresourceRange={f==2?VK_IMAGE_ASPECT_STENCIL_BIT:VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1}};
      for(unsigned t=0;t<4;t++) {
         barrier.newLayout=transitions[t];
         barrier.srcAccessMask=t?VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT:0;
         barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
         CmdPipelineBarrier(command,t?VK_PIPELINE_STAGE_ALL_COMMANDS_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,NULL,0,NULL,1,&barrier);
         barrier.oldLayout=barrier.newLayout;
      }
      SUCCESS(EndCommandBuffer(command)); SUCCESS(ResetFences(device,1,&fence));
      VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
      SUCCESS(QueueSubmit(queue,1,&submit,fence)); SUCCESS(WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
      printf("MIXED_DEPTH_LAYOUT_PASS format_index=%u samples=%u transitions=4 final_general=1 no_dispatch=1\n",f,samples);
      DestroyFramebuffer(device,framebuffer,NULL); DestroyRenderPass(device,pass,NULL);
      for(unsigned j=0;j<5;j++){DestroyImageView(device,views[j],NULL);DestroyImage(device,images[j],NULL);FreeMemory(device,allocations[j],NULL);}
      printf("ATTACHMENT_HOST_PASS format_index=%u samples=%u colors=4 depth_stencil=1 bound_images=5 framebuffer=4x4x1 four_layout_transitions=1\n",f,samples); fflush(stdout);
   }
   DestroyFence(device,fence,NULL); DestroyCommandPool(device,pool,NULL);
   DestroyDevice(device,NULL); DestroyInstance(instance,NULL); dlclose(lib);
   puts("PASS MIXED_DEPTH_LAYOUTS: six format/sample cases/four maintenance2 transitions each/fenced reuse; no initialized sample, dispatch or rendering claim");
   return 0;
}
