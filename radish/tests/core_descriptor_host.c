#include "test_common.h"

int main(int argc, char **argv)
{
   REQUIRE(argc == 2);
   void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
   REQUIRE(lib);
   PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");
   REQUIRE(gipa);
   VkInstance instance = VK_NULL_HANDLE;
   INSTANCE_FN(CreateInstance);
   VkApplicationInfo app = {.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion=VK_API_VERSION_1_1};
   VkInstanceCreateInfo ici = {.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo=&app};
   SUCCESS(CreateInstance(&ici, NULL, &instance));
   INSTANCE_FN(EnumeratePhysicalDevices);
   INSTANCE_FN(GetPhysicalDeviceProperties);
   INSTANCE_FN(GetPhysicalDeviceFeatures);
   INSTANCE_FN(CreateDevice);
   INSTANCE_FN(GetDeviceProcAddr);
   INSTANCE_FN(DestroyInstance);
   VkPhysicalDevice physical;
   uint32_t count=1;
   SUCCESS(EnumeratePhysicalDevices(instance, &count, &physical));
   REQUIRE(count==1);
   VkPhysicalDeviceProperties p;
   GetPhysicalDeviceProperties(physical, &p);
   REQUIRE(p.apiVersion>=VK_API_VERSION_1_1);
   REQUIRE(p.limits.maxPerStageDescriptorInputAttachments>=4 && p.limits.maxDescriptorSetInputAttachments>=4);
   VkPhysicalDeviceFeatures f;
   GetPhysicalDeviceFeatures(physical, &f);
   REQUIRE(!f.largePoints && !f.wideLines && !f.multiViewport && !f.multiDrawIndirect && !f.fullDrawIndexUint32);
   REQUIRE(p.limits.pointSizeRange[0]==1 && p.limits.pointSizeRange[1]==1);
   REQUIRE(p.limits.lineWidthRange[0]==1 && p.limits.lineWidthRange[1]==1);
   REQUIRE(p.limits.maxViewports==1 && p.limits.maxDrawIndirectCount==1);
   REQUIRE(p.limits.maxDrawIndexedIndexValue==0xffffffu);
   float priority=1;
   VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueCount=1, .pQueuePriorities=&priority};
   VkDeviceCreateInfo di={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .queueCreateInfoCount=1, .pQueueCreateInfos=&qi};
   VkDevice device;
   SUCCESS(CreateDevice(physical, &di, NULL, &device));
   PFN_vkGetDeviceProcAddr gdpa=GetDeviceProcAddr;
   DEVICE_FN(GetDescriptorSetLayoutSupport);
   DEVICE_FN(CreateDescriptorSetLayout);
   DEVICE_FN(DestroyDescriptorSetLayout);
   DEVICE_FN(CreatePipelineLayout);
   DEVICE_FN(DestroyPipelineLayout);
   DEVICE_FN(CreateDescriptorPool);
   DEVICE_FN(DestroyDescriptorPool);
   DEVICE_FN(AllocateDescriptorSets);
   DEVICE_FN(FreeDescriptorSets);
   DEVICE_FN(ResetDescriptorPool);
   DEVICE_FN(CreateDescriptorUpdateTemplate);
   DEVICE_FN(DestroyDescriptorUpdateTemplate);
   DEVICE_FN(DestroyDevice);
   VkDescriptorSetLayout layouts[2];
   VkPipelineLayout pipelines[2];
   for (unsigned i=0;i<2;i++) {
      VkDescriptorSetLayoutBinding bindings[2]={{.binding=7, .descriptorType=VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
         .descriptorCount=i?2:4, .stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT},
         {.binding=11, .descriptorType=VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, .descriptorCount=2, .stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT}};
      VkDescriptorSetLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
         .bindingCount=i?2:1, .pBindings=bindings};
      VkDescriptorSetLayoutSupport support={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT};
      GetDescriptorSetLayoutSupport(device, &li, &support);
      REQUIRE(support.supported && support.sType==VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT && !support.pNext);
      SUCCESS(CreateDescriptorSetLayout(device, &li, NULL, &layouts[i]));
      VkPipelineLayoutCreateInfo pi={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount=1, .pSetLayouts=&layouts[i]};
      SUCCESS(CreatePipelineLayout(device, &pi, NULL, &pipelines[i]));
   }
   VkDescriptorUpdateTemplateEntry entry={.dstBinding=7, .descriptorCount=4,
      .descriptorType=VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, .offset=0, .stride=sizeof(VkDescriptorImageInfo)};
   VkDescriptorUpdateTemplateCreateInfo ti={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO,
      .descriptorUpdateEntryCount=1, .pDescriptorUpdateEntries=&entry,
      .templateType=VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET, .descriptorSetLayout=layouts[0]};
   VkDescriptorUpdateTemplate update;
   SUCCESS(CreateDescriptorUpdateTemplate(device, &ti, NULL, &update));
   VkDescriptorPoolSize size={VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,8};
   VkDescriptorPoolCreateInfo pool_info={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags=VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, .maxSets=2, .poolSizeCount=1, .pPoolSizes=&size};
   VkDescriptorPool pool;
   SUCCESS(CreateDescriptorPool(device, &pool_info, NULL, &pool));
   for (unsigned round=0;round<2;round++) {
      VkDescriptorSet sets[2];
      VkDescriptorSetAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
         .descriptorPool=pool, .descriptorSetCount=2, .pSetLayouts=layouts};
      SUCCESS(AllocateDescriptorSets(device, &ai, sets));
      SUCCESS(FreeDescriptorSets(device, pool, 1, &sets[0]));
      SUCCESS(ResetDescriptorPool(device, pool, 0));
   }
   DestroyDescriptorPool(device, pool, NULL);
   DestroyDescriptorUpdateTemplate(device, update, NULL);
   for (unsigned i=0;i<2;i++) {
      DestroyPipelineLayout(device, pipelines[i], NULL);
      DestroyDescriptorSetLayout(device, layouts[i], NULL);
   }
   DestroyDevice(device, NULL);
   DestroyInstance(instance, NULL);
   dlclose(lib);
   puts("PASS DESCRIPTOR_HOST: required defaults; four fragment input attachments; layout/pool/allocation/free/reset/template/pipeline-layout lifetimes; no image update or GPU commands");
   return 0;
}
