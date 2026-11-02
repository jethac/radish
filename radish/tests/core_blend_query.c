#include "test_common.h"
int main(int argc,char **argv) {
 REQUIRE(argc==2);void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);REQUIRE(lib);
 PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr");REQUIRE(gipa);
 VkInstance instance=VK_NULL_HANDLE;INSTANCE_FN(CreateInstance);VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_1};VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};SUCCESS(CreateInstance(&ici,NULL,&instance));
 INSTANCE_FN(EnumeratePhysicalDevices);INSTANCE_FN(GetPhysicalDeviceProperties);INSTANCE_FN(GetPhysicalDeviceQueueFamilyProperties);INSTANCE_FN(GetPhysicalDeviceFormatProperties);INSTANCE_FN(GetPhysicalDeviceFormatProperties2);INSTANCE_FN(DestroyInstance);
 VkPhysicalDevice physical;uint32_t count=1;SUCCESS(EnumeratePhysicalDevices(instance,&count,&physical));REQUIRE(count==1);VkPhysicalDeviceProperties props;GetPhysicalDeviceProperties(physical,&props);REQUIRE(props.apiVersion>=VK_API_VERSION_1_1);VkQueueFamilyProperties q;count=0;GetPhysicalDeviceQueueFamilyProperties(physical,&count,NULL);REQUIRE(count==1);GetPhysicalDeviceQueueFamilyProperties(physical,&count,&q);REQUIRE(count==1&&(q.queueFlags&VK_QUEUE_COMPUTE_BIT)&&!(q.queueFlags&VK_QUEUE_GRAPHICS_BIT));
 const VkFormat required[]={
  VK_FORMAT_R5G6B5_UNORM_PACK16,
  VK_FORMAT_A1R5G5B5_UNORM_PACK16,
  VK_FORMAT_R8_UNORM,
  VK_FORMAT_R8G8_UNORM,
  VK_FORMAT_R8G8B8A8_UNORM,
  VK_FORMAT_R8G8B8A8_SRGB,
  VK_FORMAT_B8G8R8A8_UNORM,
  VK_FORMAT_B8G8R8A8_SRGB,
  VK_FORMAT_A8B8G8R8_UNORM_PACK32,
  VK_FORMAT_A8B8G8R8_SRGB_PACK32,
  VK_FORMAT_A2B10G10R10_UNORM_PACK32,
  VK_FORMAT_R16_SFLOAT,
  VK_FORMAT_R16G16_SFLOAT,
  VK_FORMAT_R16G16B16A16_SFLOAT,
 };
 const VkFormat negative[]={VK_FORMAT_R32_UINT,VK_FORMAT_R32_SINT,VK_FORMAT_D24_UNORM_S8_UINT,VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK};
 for(unsigned pass=0;pass<2;pass++)for(unsigned i=0;i<(pass?4:14);i++) {
  VkFormat format=pass?negative[i]:required[i];VkFormatProperties fp;GetPhysicalDeviceFormatProperties(physical,format,&fp);VkFormatProperties2 fp2={.sType=VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};GetPhysicalDeviceFormatProperties2(physical,format,&fp2);
  REQUIRE(fp.linearTilingFeatures==fp2.formatProperties.linearTilingFeatures&&fp.optimalTilingFeatures==fp2.formatProperties.optimalTilingFeatures&&fp.bufferFeatures==fp2.formatProperties.bufferFeatures);
  VkFormatFeatureFlags need=VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
  if(pass)REQUIRE(!(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT));else REQUIRE((fp.optimalTilingFeatures&need)==need);
  REQUIRE(!(fp.linearTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT)&&!(fp.bufferFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT));
  printf("BLEND_QUERY_PASS kind=%s index=%u format=%u optimal=%08x legacy_core2_equal=1 no_device=1\n",pass?"negative":"required",i,format,fp.optimalTilingFeatures);
 }
 DestroyInstance(instance,NULL);dlclose(lib);puts("PASS REQUIRED_BLEND_QUERY: 14 mandatory blend formats+4 nonblend controls/legacy+core2; no device, graphics objects, commands or blending execution");return 0;
}
