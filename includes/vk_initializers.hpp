#pragma once
#include "vk_types.hpp"

namespace vkInit
{
    // -- Command ------------------------
    VkCommandPoolCreateInfo commandPoolCreateInfo(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags=0);
    VkCommandBufferAllocateInfo commandBufferAllocateInfo(VkCommandPool commandPool, uint32_t count = 1, VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);   
    
    // -- Pipeline ------------------------
    VkPipelineShaderStageCreateInfo pipelineShaderStageCreateInfo(VkShaderStageFlagBits stage, VkShaderModule shaderModule);
    VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo();
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyCreateInfo(VkPrimitiveTopology topology);
    VkPipelineRasterizationStateCreateInfo rasterizationStateCreateInfo(VkPolygonMode polygonMode);
    VkPipelineMultisampleStateCreateInfo multisamplingStateCreateInfo();
    VkPipelineColorBlendAttachmentState colorBlendAttachmentState();
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo();

    // -- Sync ------------------------
    VkFenceCreateInfo fenceCreateInfo(VkFenceCreateFlags flags = 0);
    VkSemaphoreCreateInfo semaphoreCreateInfo(VkSemaphoreCreateFlags flags = 0);

    // -- Image ------------------------
    VkImageCreateInfo imageCreateInfo(VkFormat format, VkImageUsageFlags flags, VkExtent3D extent);
    VkImageViewCreateInfo imageviewCreateInfo(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags);

    // -- Depth ------------------------
    VkPipelineDepthStencilStateCreateInfo depthStencilCreateInfo(bool bDepthTest, bool bDepthWrite, VkCompareOp compareOp);

    // -- Descriptor ------------------------
    VkDescriptorSetLayoutBinding descriptorSetLayoutBinding(VkDescriptorType type, VkShaderStageFlags stageFlags, uint32_t binding);
    VkWriteDescriptorSet writeDescriptorBuffer(VkDescriptorType type, VkDescriptorSet dstSet, VkDescriptorBufferInfo* bufferInfo, uint32_t binding);
}