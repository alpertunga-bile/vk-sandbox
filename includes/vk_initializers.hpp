#pragma once

#include "vk_types.hpp"

namespace vk_init {
VkCommandPoolCreateInfo
command_pool_create_info(uint32_t                 queue_family_index,
                         VkCommandPoolCreateFlags flags = 0);
VkCommandBufferAllocateInfo
command_buffer_allocate_info(VkCommandPool pool, uint32_t count = 1);

VkFenceCreateInfo
fence_create_info(VkFenceCreateFlags flags = 0);
VkSemaphoreCreateInfo
semaphore_create_info(VkSemaphoreCreateFlags flags = 0);

VkCommandBufferBeginInfo
command_buffer_begin_info(VkCommandBufferUsageFlags flags = 0);

VkImageSubresourceRange
image_subresource_range(VkImageAspectFlags flags);
VkImageCreateInfo
image_create_info(VkFormat          format,
                  VkImageUsageFlags usage_flags,
                  VkExtent3D        extent);
VkImageViewCreateInfo
image_view_create_info(VkFormat           format,
                       VkImage            image,
                       VkImageAspectFlags aspect_flags);

VkSemaphoreSubmitInfo
semaphore_submit_info(VkPipelineStageFlags2 stage_mask, VkSemaphore semaphore);
VkCommandBufferSubmitInfo
command_buffer_submit_info(VkCommandBuffer cmd);

VkSubmitInfo2
submit_info(VkCommandBufferSubmitInfo* cbsi,
            VkSemaphoreSubmitInfo*     signal_semaphore_info,
            VkSemaphoreSubmitInfo*     wait_semaphore_info);

VkRenderingAttachmentInfo
attachment_info(VkImageView view, VkClearValue* clear, VkImageLayout layout);

VkRenderingInfo
rendering_info(VkExtent2D                 render_extent,
               VkRenderingAttachmentInfo* color_attach,
               VkRenderingAttachmentInfo* depth_attach);

VkPipelineShaderStageCreateInfo
pipeline_shader_stage_create_info(VkShaderStageFlagBits shader_stage,
                                  VkShaderModule        shader_module);

VkPipelineLayoutCreateInfo
pipeline_layout_create_info();
}
