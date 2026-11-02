#include "test_common.h"
int main(int argc,char **argv) {
 REQUIRE(argc==2);void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);REQUIRE(lib);
 PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr");REQUIRE(gipa);
 VkInstance instance=VK_NULL_HANDLE;INSTANCE_FN(CreateInstance);VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_1};VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};SUCCESS(CreateInstance(&ici,NULL,&instance));
 INSTANCE_FN(EnumeratePhysicalDevices);INSTANCE_FN(GetPhysicalDeviceProperties);INSTANCE_FN(GetPhysicalDeviceQueueFamilyProperties);INSTANCE_FN(GetPhysicalDeviceFormatProperties);INSTANCE_FN(GetPhysicalDeviceFormatProperties2);INSTANCE_FN(DestroyInstance);
 VkPhysicalDevice physical;uint32_t count=1;SUCCESS(EnumeratePhysicalDevices(instance,&count,&physical));REQUIRE(count==1);VkPhysicalDeviceProperties props;GetPhysicalDeviceProperties(physical,&props);REQUIRE(props.apiVersion>=VK_API_VERSION_1_1);VkQueueFamilyProperties q;count=0;GetPhysicalDeviceQueueFamilyProperties(physical,&count,NULL);REQUIRE(count==1);GetPhysicalDeviceQueueFamilyProperties(physical,&count,&q);REQUIRE(count==1&&(q.queueFlags&VK_QUEUE_COMPUTE_BIT)&&!(q.queueFlags&VK_QUEUE_GRAPHICS_BIT));
 const VkFormat required[]={
  VK_FORMAT_B4G4R4A4_UNORM_PACK16,
  VK_FORMAT_R5G6B5_UNORM_PACK16,
  VK_FORMAT_A1R5G5B5_UNORM_PACK16,
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
  VK_FORMAT_A8B8G8R8_UNORM_PACK32,
  VK_FORMAT_A8B8G8R8_SNORM_PACK32,
  VK_FORMAT_A8B8G8R8_UINT_PACK32,
  VK_FORMAT_A8B8G8R8_SINT_PACK32,
  VK_FORMAT_A8B8G8R8_SRGB_PACK32,
  VK_FORMAT_A2B10G10R10_UNORM_PACK32,
  VK_FORMAT_A2B10G10R10_UINT_PACK32,
  VK_FORMAT_R16_UINT,
  VK_FORMAT_R16_SINT,
  VK_FORMAT_R16_SFLOAT,
  VK_FORMAT_R16G16_UINT,
  VK_FORMAT_R16G16_SINT,
  VK_FORMAT_R16G16_SFLOAT,
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
  VK_FORMAT_B10G11R11_UFLOAT_PACK32,
  VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
  VK_FORMAT_D16_UNORM,
  VK_FORMAT_D32_SFLOAT,
 };
 const VkBool32 destination[]={VK_FALSE,VK_TRUE,VK_TRUE,VK_TRUE,VK_FALSE,VK_TRUE,VK_TRUE,VK_TRUE,VK_FALSE,VK_TRUE,VK_TRUE,VK_TRUE,VK_FALSE,VK_TRUE,VK_TRUE,VK_TRUE,VK_TRUE,VK_TRUE,VK_TRUE,VK_FALSE,VK_TRUE,VK_TRUE,VK_TRUE,VK_TRUE,VK_TRUE,VK_TRUE,VK_TRUE,VK_TRUE,VK_TRUE,VK_TRUE,VK_TRUE,VK_TRUE,VK_TRUE,VK_TRUE,VK_TRUE,VK_TRUE,VK_TRUE,VK_TRUE,VK_TRUE,VK_TRUE,VK_TRUE,VK_TRUE,VK_TRUE,VK_FALSE,VK_FALSE,VK_FALSE,VK_FALSE};
 const VkFormat negative[]={VK_FORMAT_UNDEFINED,VK_FORMAT_R64_UINT,VK_FORMAT_BC1_RGB_UNORM_BLOCK};
 for(unsigned pass=0;pass<2;pass++)for(unsigned i=0;i<(pass?3:47);i++) {
  VkFormat format=pass?negative[i]:required[i];VkFormatProperties fp;GetPhysicalDeviceFormatProperties(physical,format,&fp);VkFormatProperties2 fp2={.sType=VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};GetPhysicalDeviceFormatProperties2(physical,format,&fp2);
  REQUIRE(fp.linearTilingFeatures==fp2.formatProperties.linearTilingFeatures&&fp.optimalTilingFeatures==fp2.formatProperties.optimalTilingFeatures&&fp.bufferFeatures==fp2.formatProperties.bufferFeatures);
  VkFormatFeatureFlags need=pass?0:VK_FORMAT_FEATURE_BLIT_SRC_BIT|(destination[i]?VK_FORMAT_FEATURE_BLIT_DST_BIT:0);
  if(pass)REQUIRE(!(fp.optimalTilingFeatures&(VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT)));else REQUIRE((fp.optimalTilingFeatures&need)==need);
  REQUIRE(!(fp.linearTilingFeatures&(VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT))&&!(fp.bufferFeatures&(VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT)));
  printf("BLIT_QUERY_PASS kind=%s index=%u format=%u optimal=%08x required_dst=%u legacy_core2_equal=1 no_device=1\n",pass?"negative":"required",i,format,fp.optimalTilingFeatures,pass?0:destination[i]);
 }
 DestroyInstance(instance,NULL);dlclose(lib);puts("PASS REQUIRED_BLIT_QUERY:47 mandatory SRC+38 DST traits/3 unsupported controls/legacy+core2; no device, graphics commands or executed blit");return 0;
}
