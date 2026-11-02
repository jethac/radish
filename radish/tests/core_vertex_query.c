#include "test_common.h"
int main(int argc,char **argv) {
 REQUIRE(argc==2);void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);REQUIRE(lib);
 PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vk_icdGetInstanceProcAddr");REQUIRE(gipa);
 VkInstance instance=VK_NULL_HANDLE;INSTANCE_FN(CreateInstance);VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_1};VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};SUCCESS(CreateInstance(&ici,NULL,&instance));
 INSTANCE_FN(EnumeratePhysicalDevices);INSTANCE_FN(GetPhysicalDeviceProperties);INSTANCE_FN(GetPhysicalDeviceQueueFamilyProperties);INSTANCE_FN(GetPhysicalDeviceFormatProperties);INSTANCE_FN(GetPhysicalDeviceFormatProperties2);INSTANCE_FN(DestroyInstance);
 VkPhysicalDevice physical;uint32_t count=1;SUCCESS(EnumeratePhysicalDevices(instance,&count,&physical));REQUIRE(count==1);VkPhysicalDeviceProperties props;GetPhysicalDeviceProperties(physical,&props);REQUIRE(props.apiVersion>=VK_API_VERSION_1_1);VkQueueFamilyProperties q;count=0;GetPhysicalDeviceQueueFamilyProperties(physical,&count,NULL);REQUIRE(count==1);GetPhysicalDeviceQueueFamilyProperties(physical,&count,&q);REQUIRE(count==1&&(q.queueFlags&VK_QUEUE_COMPUTE_BIT)&&!(q.queueFlags&VK_QUEUE_GRAPHICS_BIT));
 const VkFormat required[]={
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
  VK_FORMAT_B8G8R8A8_UNORM,
  VK_FORMAT_A8B8G8R8_UNORM_PACK32,
  VK_FORMAT_A8B8G8R8_SNORM_PACK32,
  VK_FORMAT_A8B8G8R8_UINT_PACK32,
  VK_FORMAT_A8B8G8R8_SINT_PACK32,
  VK_FORMAT_A2B10G10R10_UNORM_PACK32,
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
  VK_FORMAT_R32G32B32_UINT,
  VK_FORMAT_R32G32B32_SINT,
  VK_FORMAT_R32G32B32_SFLOAT,
  VK_FORMAT_R32G32B32A32_UINT,
  VK_FORMAT_R32G32B32A32_SINT,
  VK_FORMAT_R32G32B32A32_SFLOAT,
 };
 const VkFormat negative[]={VK_FORMAT_R8G8B8A8_SRGB,VK_FORMAT_D16_UNORM,VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK,VK_FORMAT_R64_UINT};
 for(unsigned pass=0;pass<2;pass++)for(unsigned i=0;i<(pass?4:45);i++) {
  VkFormat format=pass?negative[i]:required[i];VkFormatProperties fp;GetPhysicalDeviceFormatProperties(physical,format,&fp);VkFormatProperties2 fp2={.sType=VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};GetPhysicalDeviceFormatProperties2(physical,format,&fp2);
  REQUIRE(fp.linearTilingFeatures==fp2.formatProperties.linearTilingFeatures&&fp.optimalTilingFeatures==fp2.formatProperties.optimalTilingFeatures&&fp.bufferFeatures==fp2.formatProperties.bufferFeatures);
  if(pass)REQUIRE(!(fp.bufferFeatures&VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT));else REQUIRE(fp.bufferFeatures&VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT);
  REQUIRE(!(fp.linearTilingFeatures&VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)&&!(fp.optimalTilingFeatures&VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT));
  printf("VERTEX_QUERY_PASS kind=%s index=%u format=%u buffer=%08x legacy_core2_equal=1 no_device=1\n",pass?"negative":"required",i,format,fp.bufferFeatures);
 }
 DestroyInstance(instance,NULL);dlclose(lib);puts("PASS REQUIRED_VERTEX_QUERY:45 mandatory native-backed VFD traits+4 nonvertex controls/legacy+core2; no device, graphics objects or vertex execution");return 0;
}
