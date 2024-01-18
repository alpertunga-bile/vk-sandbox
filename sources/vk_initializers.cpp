#include "vk_initializers.hpp"

namespace vk_init {
VkCommandPoolCreateInfo
command_pool_create_info(uint32_t                 queue_family_index,
                         VkCommandPoolCreateFlags flags)
{
  VkCommandPoolCreateInfo cpci = {};
  cpci.sType                   = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cpci.pNext                   = nullptr;
  cpci.queueFamilyIndex        = queue_family_index;
  cpci.flags                   = flags;

  return cpci;
}

VkCommandBufferAllocateInfo
command_buffer_allocate_info(VkCommandPool pool, uint32_t count)
{
  VkCommandBufferAllocateInfo cbai = {};
  cbai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cbai.pNext              = nullptr;
  cbai.commandPool        = pool;
  cbai.commandBufferCount = count;
  cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

  return cbai;
}

VkSemaphoreCreateInfo
semaphore_create_info(VkSemaphoreCreateFlags flags)
{
  VkSemaphoreCreateInfo sci = {};
  sci.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  sci.pNext                 = nullptr;
  sci.flags                 = flags;

  return sci;
}

VkCommandBufferBeginInfo
command_buffer_begin_info(VkCommandBufferUsageFlags flags)
{
  VkCommandBufferBeginInfo cbbi = {};
  cbbi.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  cbbi.pNext                    = nullptr;
  cbbi.flags                    = flags;
  cbbi.pInheritanceInfo         = nullptr;

  return cbbi;
}

VkImageSubresourceRange
image_subresource_range(VkImageAspectFlags flags)
{
  VkImageSubresourceRange isr = {};
  isr.aspectMask              = flags;
  isr.baseMipLevel            = 0;
  isr.levelCount              = VK_REMAINING_MIP_LEVELS;
  isr.baseArrayLayer          = 0;
  isr.layerCount              = VK_REMAINING_ARRAY_LAYERS;

  return isr;
}

VkImageCreateInfo
image_create_info(VkFormat          format,
                  VkImageUsageFlags usage_flags,
                  VkExtent3D        extent)
{
  VkImageCreateInfo ici = {};
  ici.sType             = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ici.pNext             = nullptr;

  ici.imageType = VK_IMAGE_TYPE_2D;

  ici.format = format;
  ici.extent = extent;

  ici.mipLevels   = 1;
  ici.arrayLayers = 1;

  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling  = VK_IMAGE_TILING_OPTIMAL;
  ici.usage   = usage_flags;

  return ici;
}

VkImageViewCreateInfo
image_view_create_info(VkFormat           format,
                       VkImage            image,
                       VkImageAspectFlags aspect_flags)
{
  VkImageViewCreateInfo ivci = {};
  ivci.sType                 = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  ivci.pNext                 = nullptr;
  ivci.viewType              = VK_IMAGE_VIEW_TYPE_2D;
  ivci.image                 = image;
  ivci.format                = format;

  ivci.subresourceRange.baseMipLevel   = 0;
  ivci.subresourceRange.levelCount     = 1;
  ivci.subresourceRange.baseArrayLayer = 0;
  ivci.subresourceRange.layerCount     = 1;
  ivci.subresourceRange.aspectMask     = aspect_flags;

  return ivci;
}

VkSemaphoreSubmitInfo
semaphore_submit_info(VkPipelineStageFlags2 stage_mask, VkSemaphore semaphore)
{
  VkSemaphoreSubmitInfo ssi = {};
  ssi.sType                 = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
  ssi.pNext                 = nullptr;
  ssi.semaphore             = semaphore;
  ssi.stageMask             = stage_mask;
  ssi.deviceIndex           = 0;
  ssi.value                 = 1;

  return ssi;
}

VkCommandBufferSubmitInfo
command_buffer_submit_info(VkCommandBuffer cmd)
{
  VkCommandBufferSubmitInfo cbsi = {};
  cbsi.sType                     = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
  cbsi.pNext                     = nullptr;
  cbsi.commandBuffer             = cmd;
  cbsi.deviceMask                = 0;

  return cbsi;
}

VkSubmitInfo2
submit_info(VkCommandBufferSubmitInfo* cbsi,
            VkSemaphoreSubmitInfo*     signal_semaphore_info,
            VkSemaphoreSubmitInfo*     wait_semaphore_info)
{
  VkSubmitInfo2 si = {};
  si.sType         = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
  si.pNext         = nullptr;

  si.waitSemaphoreInfoCount = wait_semaphore_info == nullptr ? 0 : 1;
  si.pWaitSemaphoreInfos    = wait_semaphore_info;

  si.signalSemaphoreInfoCount = signal_semaphore_info == nullptr ? 0 : 1;
  si.pSignalSemaphoreInfos    = signal_semaphore_info;

  si.commandBufferInfoCount = 1;
  si.pCommandBufferInfos    = cbsi;

  return si;
}

VkFenceCreateInfo
fence_create_info(VkFenceCreateFlags flags)
{
  VkFenceCreateInfo fci = {};
  fci.sType             = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fci.pNext             = nullptr;
  fci.flags             = flags;

  return fci;
}

VkRenderingAttachmentInfo
attachment_info(VkImageView view, VkClearValue* clear, VkImageLayout layout)
{
  VkRenderingAttachmentInfo rai = {};
  rai.sType                     = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
  rai.pNext                     = nullptr;
  rai.imageView                 = view;
  rai.imageLayout               = layout;
  rai.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
  rai.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

  if (clear) {
    rai.clearValue = *clear;
  }

  return rai;
}

VkRenderingInfo
rendering_info(VkExtent2D                 render_extent,
               VkRenderingAttachmentInfo* color_attach,
               VkRenderingAttachmentInfo* depth_attach)
{
  VkRenderingInfo ri = {};
  ri.sType           = VK_STRUCTURE_TYPE_RENDERING_INFO;
  ri.pNext           = nullptr;
  ri.renderArea      = VkRect2D{
    VkOffset2D{0, 0},
    render_extent
  };
  ri.layerCount           = 1;
  ri.colorAttachmentCount = 1;
  ri.pColorAttachments    = color_attach;
  ri.pDepthAttachment     = depth_attach;
  ri.pStencilAttachment   = nullptr;

  return ri;
}

VkPipelineShaderStageCreateInfo
pipeline_shader_stage_create_info(VkShaderStageFlagBits shader_stage,
                                  VkShaderModule        shader_module)
{
  VkPipelineShaderStageCreateInfo ci = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO
  };
  ci.pNext  = nullptr;
  ci.stage  = shader_stage;
  ci.module = shader_module;
  ci.pName  = "main";

  return ci;
}
VkPipelineLayoutCreateInfo
pipeline_layout_create_info()
{
  VkPipelineLayoutCreateInfo plci = {};
  plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  plci.pNext                  = nullptr;
  plci.flags                  = 0;
  plci.setLayoutCount         = 0;
  plci.pSetLayouts            = nullptr;
  plci.pushConstantRangeCount = 0;
  plci.pPushConstantRanges    = nullptr;

  return plci;
}
}
