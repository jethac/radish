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
   DEVICE_FN(DestroyDevice);
   DEVICE_FN(CreateCommandPool); DEVICE_FN(DestroyCommandPool); DEVICE_FN(ResetCommandPool);
   DEVICE_FN(AllocateCommandBuffers); DEVICE_FN(BeginCommandBuffer); DEVICE_FN(EndCommandBuffer); DEVICE_FN(CmdPipelineBarrier);
   DEVICE_FN(GetDeviceQueue); DEVICE_FN(CreateFence); DEVICE_FN(DestroyFence); DEVICE_FN(ResetFences); DEVICE_FN(QueueSubmit); DEVICE_FN(WaitForFences);
   VkCommandPoolCreateInfo pool_info={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
   VkCommandPool pool; SUCCESS(CreateCommandPool(device,&pool_info,NULL,&pool));
   VkCommandBufferAllocateInfo command_info={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
   VkCommandBuffer command; SUCCESS(AllocateCommandBuffers(device,&command_info,&command));
   VkFenceCreateInfo fence_info={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence; SUCCESS(CreateFence(device,&fence_info,NULL,&fence));
   VkQueue queue; GetDeviceQueue(device,0,0,&queue);
   const VkSampleCountFlags required_samples=VK_SAMPLE_COUNT_1_BIT|VK_SAMPLE_COUNT_4_BIT;
   REQUIRE((props.limits.framebufferColorSampleCounts&required_samples)==required_samples);
   REQUIRE((props.limits.framebufferDepthSampleCounts&required_samples)==required_samples);
   REQUIRE((props.limits.framebufferStencilSampleCounts&required_samples)==required_samples);
   REQUIRE((props.limits.framebufferNoAttachmentsSampleCounts&required_samples)==required_samples);
   const VkFormat queried_formats[]={
      VK_FORMAT_R8_UNORM,
      VK_FORMAT_R8_SNORM,
      VK_FORMAT_R8_UINT,
      VK_FORMAT_R8_SINT,
      VK_FORMAT_R8G8_UNORM,
      VK_FORMAT_R8G8_SNORM,
      VK_FORMAT_R8G8_UINT,
      VK_FORMAT_R8G8_SINT,
      VK_FORMAT_R8G8B8A8_UNORM,
      VK_FORMAT_R8G8B8A8_SNORM,
      VK_FORMAT_R8G8B8A8_UINT,
      VK_FORMAT_R8G8B8A8_SINT,
      VK_FORMAT_R8G8B8A8_SRGB,
      VK_FORMAT_B8G8R8A8_UNORM,
      VK_FORMAT_B8G8R8A8_SRGB,
      VK_FORMAT_R16_UNORM,
      VK_FORMAT_R16_SNORM,
      VK_FORMAT_R16_UINT,
      VK_FORMAT_R16_SINT,
      VK_FORMAT_R16_SFLOAT,
      VK_FORMAT_R16G16_UNORM,
      VK_FORMAT_R16G16_SNORM,
      VK_FORMAT_R16G16_UINT,
      VK_FORMAT_R16G16_SINT,
      VK_FORMAT_R16G16_SFLOAT,
      VK_FORMAT_R16G16B16A16_UNORM,
      VK_FORMAT_R16G16B16A16_SNORM,
      VK_FORMAT_R16G16B16A16_UINT,
      VK_FORMAT_R16G16B16A16_SINT,
      VK_FORMAT_R16G16B16A16_SFLOAT,
      VK_FORMAT_R32_UINT,
      VK_FORMAT_R32_SINT,
      VK_FORMAT_R32_SFLOAT,
      VK_FORMAT_R32G32_UINT,
      VK_FORMAT_R32G32_SINT,
      VK_FORMAT_R32G32_SFLOAT,
      VK_FORMAT_R32G32B32A32_UINT,
      VK_FORMAT_R32G32B32A32_SINT,
      VK_FORMAT_R32G32B32A32_SFLOAT,
      VK_FORMAT_R5G6B5_UNORM_PACK16,
      VK_FORMAT_A1R5G5B5_UNORM_PACK16,
      VK_FORMAT_A8B8G8R8_UNORM_PACK32,
      VK_FORMAT_A8B8G8R8_SNORM_PACK32,
      VK_FORMAT_A8B8G8R8_UINT_PACK32,
      VK_FORMAT_A8B8G8R8_SINT_PACK32,
      VK_FORMAT_A8B8G8R8_SRGB_PACK32,
      VK_FORMAT_A2R10G10B10_UNORM_PACK32,
      VK_FORMAT_A2R10G10B10_UINT_PACK32,
      VK_FORMAT_A2B10G10R10_UNORM_PACK32,
      VK_FORMAT_A2B10G10R10_UINT_PACK32,
      VK_FORMAT_B10G11R11_UFLOAT_PACK32,
   };
   for(unsigned q=0;q<sizeof(queried_formats)/sizeof(queried_formats[0]);q++) {
      VkFormatProperties fp; GetPhysicalDeviceFormatProperties(physical,queried_formats[q],&fp);
      REQUIRE(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT);
      VkImageFormatProperties ip;
      SUCCESS(GetPhysicalDeviceImageFormatProperties(physical,queried_formats[q],VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,
         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,0,&ip));
      REQUIRE((ip.sampleCounts&required_samples)==required_samples);
      printf("NATIVE_ATTACHMENT_QUERY_PASS index=%u format=%u samples=%u\n",q,queried_formats[q],ip.sampleCounts);
   }
   const VkFormat sampled_only_formats[]={
      VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
      VK_FORMAT_R4G4B4A4_UNORM_PACK16,
      VK_FORMAT_B4G4R4A4_UNORM_PACK16,
      VK_FORMAT_B5G6R5_UNORM_PACK16,
      VK_FORMAT_B8G8R8A8_SNORM,
      VK_FORMAT_B8G8R8A8_UINT,
      VK_FORMAT_B8G8R8A8_SINT,
   };
   for(unsigned q=0;q<sizeof(sampled_only_formats)/sizeof(sampled_only_formats[0]);q++) {
      VkFormatProperties fp; GetPhysicalDeviceFormatProperties(physical,sampled_only_formats[q],&fp);
      REQUIRE(!(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT));
      REQUIRE(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
      VkImageFormatProperties ip;
      SUCCESS(GetPhysicalDeviceImageFormatProperties(physical,sampled_only_formats[q],VK_IMAGE_TYPE_2D,VK_IMAGE_TILING_OPTIMAL,
         VK_IMAGE_USAGE_SAMPLED_BIT,0,&ip));
      REQUIRE(ip.sampleCounts==VK_SAMPLE_COUNT_1_BIT);
      printf("SAMPLED_ONLY_QUERY_PASS index=%u format=%u samples=%u\n",q,sampled_only_formats[q],ip.sampleCounts);
   }
   const VkFormat color_formats[8]={VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R32_UINT,VK_FORMAT_R32_SFLOAT,
      VK_FORMAT_R8G8B8A8_SRGB,VK_FORMAT_R5G6B5_UNORM_PACK16,VK_FORMAT_R16G16B16A16_SFLOAT,
      VK_FORMAT_A2B10G10R10_UNORM_PACK32,VK_FORMAT_R8G8B8A8_UNORM};
   VkDeviceSize single_size[8][2]={{0}};
   for(unsigned f=0;f<8;f++) for(unsigned ms=0;ms<2;ms++) {
      bool transient=f==7;
      VkSampleCountFlagBits samples=ms?VK_SAMPLE_COUNT_4_BIT:VK_SAMPLE_COUNT_1_BIT;
      VkImage images[5]; VkImageView views[5]; VkDeviceMemory allocations[5];
      VkDeviceSize total=0;
      for(unsigned j=0;j<5;j++) {
         bool depth=j==4; VkFormat format=depth?VK_FORMAT_D16_UNORM:color_formats[f];
         VkImageUsageFlags usage=(depth?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)|
            VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT| (transient?VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT:
            VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
         VkFormatProperties fp; GetPhysicalDeviceFormatProperties(physical,format,&fp);
         VkFormatFeatureFlags required=(depth?VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)|
            (transient?0:VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT);
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
         VkImageAspectFlags aspect=depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT;
         VkImageViewCreateInfo vi={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=images[j],.viewType=VK_IMAGE_VIEW_TYPE_2D,.format=format,
            .subresourceRange={aspect,0,1,0,1}}; SUCCESS(CreateImageView(device,&vi,NULL,&views[j]));
         if(j==0||depth) {
            unsigned n=depth?1:0;
            if(!ms) single_size[f][n]=req.size;
            else REQUIRE(req.size>=single_size[f][n]);
            printf("NATIVE_ATTACHMENT_IMAGE format=%u samples=%u size=%llu alignment=%llu aspect=%u transient=%u\n",format,samples,
               (unsigned long long)req.size,(unsigned long long)req.alignment,aspect,transient);
         }
      }
      REQUIRE(total<5*1024*1024);
      SUCCESS(ResetCommandPool(device,pool,0));
      VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; SUCCESS(BeginCommandBuffer(command,&begin));
      const VkImageLayout transitions[4]={VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL,
         VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL};
      VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,
         .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.image=images[4],
         .subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1}};
      for(unsigned t=0;t<4;t++) {
         barrier.newLayout=transitions[t];
         barrier.srcAccessMask=t?VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT:0;
         barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
         CmdPipelineBarrier(command,t?VK_PIPELINE_STAGE_ALL_COMMANDS_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,NULL,0,NULL,1,&barrier);
         barrier.oldLayout=barrier.newLayout;
      }
      for(unsigned j=0;j<4;j++) {
         VkImageMemoryBarrier color_barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout=VK_IMAGE_LAYOUT_UNDEFINED,.newLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .image=images[j],.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},
            .dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT};
         CmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0,0,NULL,0,NULL,1,&color_barrier);
         color_barrier.oldLayout=color_barrier.newLayout; color_barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
         color_barrier.srcAccessMask=color_barrier.dstAccessMask;
         CmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0,0,NULL,0,NULL,1,&color_barrier);
      }
      SUCCESS(EndCommandBuffer(command)); SUCCESS(ResetFences(device,1,&fence));
      VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
      SUCCESS(QueueSubmit(queue,1,&submit,fence)); SUCCESS(WaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
      for(unsigned j=0;j<5;j++){DestroyImageView(device,views[j],NULL);DestroyImage(device,images[j],NULL);FreeMemory(device,allocations[j],NULL);}
      printf("NATIVE_ATTACHMENT_HOST_PASS case=%u format=%u samples=%u transient=%u colors=4 depth_stencil=1 bound_images=5 views=5 transitions=12 no_dispatch=1\n",f*2+ms,color_formats[f],samples,transient); fflush(stdout);
   }
   DestroyFence(device,fence,NULL); DestroyCommandPool(device,pool,NULL);
   DestroyDevice(device,NULL); DestroyInstance(instance,NULL); dlclose(lib);
   puts("PASS NATIVE_ATTACHMENT_HOST: 51 attachment+7 sampled-only queries/16 format-sample cases/80 bound images/192 transitions/16 fences; no graphics-only host objects, initialized sample, dispatch or rendering claim");
   return 0;
}
