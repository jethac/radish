#include "test_common.h"
#include "core_names.h"
int main(int argc,char **argv) {
 REQUIRE(argc==2);void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);REQUIRE(lib);
 PFN_vkGetInstanceProcAddr gipa=(PFN_vkGetInstanceProcAddr)dlsym(lib,"vkGetInstanceProcAddr");REQUIRE(gipa);
 printf("CORE_LOOKUP_API public_loader=%s\n",argv[1]);
 VkInstance instance=VK_NULL_HANDLE;INSTANCE_FN(CreateInstance);VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.apiVersion=VK_API_VERSION_1_1};VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};SUCCESS(CreateInstance(&ici,NULL,&instance));
 INSTANCE_FN(EnumeratePhysicalDevices);INSTANCE_FN(GetPhysicalDeviceProperties);INSTANCE_FN(GetPhysicalDeviceMemoryProperties);INSTANCE_FN(GetPhysicalDeviceQueueFamilyProperties);INSTANCE_FN(CreateDevice);INSTANCE_FN(GetDeviceProcAddr);INSTANCE_FN(DestroyInstance);
 uint32_t count=1;VkPhysicalDevice physical;SUCCESS(EnumeratePhysicalDevices(instance,&count,&physical));REQUIRE(count==1);VkPhysicalDeviceProperties p;GetPhysicalDeviceProperties(physical,&p);REQUIRE(p.apiVersion>=VK_API_VERSION_1_1);
 uint32_t families=1;VkQueueFamilyProperties q;GetPhysicalDeviceQueueFamilyProperties(physical,&families,&q);REQUIRE(families==1&&(q.queueFlags&VK_QUEUE_COMPUTE_BIT)&&!(q.queueFlags&VK_QUEUE_GRAPHICS_BIT));
 VkPhysicalDeviceMemoryProperties m;GetPhysicalDeviceMemoryProperties(physical,&m);unsigned lazy=0;for(unsigned i=0;i<m.memoryTypeCount;i++)lazy+=!!(m.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT);
 float priority=1;VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueCount=1,.pQueuePriorities=&priority};VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};VkDevice device;SUCCESS(CreateDevice(physical,&di,NULL,&device));PFN_vkGetDeviceProcAddr gdpa=GetDeviceProcAddr;DEVICE_FN(DestroyDevice);
 unsigned total=0,devices=0,missing_instance=0,missing_device=0;
 for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++) {int instance_ok=gipa(instance,names[i].name)!=NULL;int device_ok=names[i].device?gdpa(device,names[i].name)!=NULL:-1;total++;devices+=names[i].device;missing_instance+=!instance_ok;missing_device+=device_ok==0;printf("CORE_LOOKUP name=%s gipa=%d gdpa=%d\n",names[i].name,instance_ok,device_ok);}
 printf("CORE_LOOKUP_TOTAL total=%u device=%u missing_gipa=%u missing_gdpa=%u graphics_queues=0 lazy_types=%u no_commands=1\n",total,devices,missing_instance,missing_device,lazy);fflush(stdout);
 DestroyDevice(device,NULL);DestroyInstance(instance,NULL);dlclose(lib);
 if(missing_instance||missing_device){puts("FAIL CORE_LOOKUP: required core function pointer missing");return 1;}
 puts("PASS CORE_LOOKUP: original core1.0+1.1 names resolved; no function execution qualification");return 0;
}
