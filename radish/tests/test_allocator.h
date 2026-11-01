#ifndef RADISH_TEST_ALLOCATOR_H
#define RADISH_TEST_ALLOCATOR_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
struct allocation_state { unsigned live, mismatches; int fail; };
struct allocation_header { void *raw; size_t size; struct allocation_state *owner; };
static void *VKAPI_PTR test_allocate(void *user, size_t size, size_t alignment, VkSystemAllocationScope scope)
{
    struct allocation_state *state=user;
    if (state->fail) return NULL;
    if (alignment < sizeof(void*)) alignment=sizeof(void*);
    if (size > SIZE_MAX-alignment-sizeof(struct allocation_header)) return NULL;
    void *raw=malloc(size+alignment+sizeof(struct allocation_header));
    if (!raw) return NULL;
    uintptr_t address=((uintptr_t)raw+sizeof(struct allocation_header)+alignment-1)&~(uintptr_t)(alignment-1);
    struct allocation_header *header=(struct allocation_header*)address-1;
    *header=(struct allocation_header){raw,size,state};
    state->live++;
    return (void*)address;
}
static void VKAPI_PTR test_free(void *user, void *memory)
{
    if (!memory) return;
    struct allocation_header *header=(struct allocation_header*)memory-1;
    if (header->owner != user) ((struct allocation_state*)user)->mismatches++;
    header->owner->live--;
    free(header->raw);
}
static void *VKAPI_PTR test_reallocate(void *user, void *original, size_t size, size_t alignment, VkSystemAllocationScope scope)
{
    if (!size) { test_free(user,original); return NULL; }
    void *next=test_allocate(user,size,alignment,scope);
    if (!next) return NULL;
    if (original) {
        size_t old_size=((struct allocation_header*)original-1)->size;
        memcpy(next,original,old_size<size?old_size:size);
        test_free(user,original);
    }
    return next;
}
static VkAllocationCallbacks test_allocator(struct allocation_state *state)
{
    return (VkAllocationCallbacks){ .pUserData=state, .pfnAllocation=test_allocate, .pfnReallocation=test_reallocate, .pfnFree=test_free };
}
#endif
