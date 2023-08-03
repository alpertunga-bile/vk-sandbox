#pragma once

#include <vulkan/vulkan.hpp>
#include <vma/vk_mem_alloc.h>

struct AllocatedBuffer
{
	VkBuffer _buffer;
	VmaAllocation _allocation;
};