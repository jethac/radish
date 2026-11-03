#ifndef RADISH_TEST_COMMON_H
#define RADISH_TEST_COMMON_H
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
#define SUCCESS(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "FAIL line %d: %s = %d\n", __LINE__, #x, r_); return 1; } } while (0)
#define INSTANCE_FN(name) PFN_vk##name name = (PFN_vk##name)gipa(instance, "vk" #name); REQUIRE(name)
#define DEVICE_FN(name) PFN_vk##name name = (PFN_vk##name)gdpa(device, "vk" #name); REQUIRE(name)


#endif
