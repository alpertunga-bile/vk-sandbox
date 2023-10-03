#pragma once

#include "vk_types.hpp"
#include "vk_engine.hpp"

namespace vkUtil
{
	bool loadImageFromFile(VulkanEngine* engine, const char* file, AllocatedImage& outImage);
}