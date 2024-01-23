#include "vk_pipelines.hpp"

#include <fstream>

namespace vk_util {
bool
load_shader_module(const char*     filepath,
                   VkDevice        device,
                   VkShaderModule* out_shader_module)
{
  std::ifstream file(filepath, std::ios::ate | std::ios::binary);

  if (!file.is_open()) {
    return false;
  }

  size_t                file_size = (size_t)file.tellg();
  std::vector<uint32_t> buffer(file_size / sizeof(uint32_t));

  file.seekg(0);
  file.read((char*)buffer.data(), file_size);
  file.close();

  VkShaderModuleCreateInfo smci = {};
  smci.sType                    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  smci.pNext                    = nullptr;
  smci.codeSize                 = buffer.size() * sizeof(uint32_t);
  smci.pCode                    = buffer.data();

  VkShaderModule shader_module;
  if (VK_RET(vkCreateShaderModule(device, &smci, nullptr, &shader_module))) {
    return false;
  }

  *out_shader_module = shader_module;
  return true;
}
}

void
PipelineBuilder::clear()
{
  input_assembly = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
  };
  rasterizer             = { .sType =
                               VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
  color_blend_attachment = {};
  multisampling          = {
             .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
  };
  pipeline_layout = {};
  depth_stencil   = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
  };
  render_info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
  shader_stages.clear();
}

VkPipeline
PipelineBuilder::build_pipeline(VkDevice device)
{
  VkPipelineViewportStateCreateInfo pvstci = {};
  pvstci.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  pvstci.pNext         = nullptr;
  pvstci.viewportCount = 1;
  pvstci.scissorCount  = 1;

  VkPipelineColorBlendStateCreateInfo pcbsci = {};
  pcbsci.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  pcbsci.pNext = nullptr;
  pcbsci.logicOpEnable   = VK_FALSE;
  pcbsci.logicOp         = VK_LOGIC_OP_COPY;
  pcbsci.attachmentCount = 1;
  pcbsci.pAttachments    = &color_blend_attachment;

  VkPipelineVertexInputStateCreateInfo pvistci = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
  };

  VkGraphicsPipelineCreateInfo pipe_create_info = {
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO
  };
  pipe_create_info.pNext               = &render_info;
  pipe_create_info.stageCount          = (uint32_t)shader_stages.size();
  pipe_create_info.pStages             = shader_stages.data();
  pipe_create_info.pVertexInputState   = &pvistci;
  pipe_create_info.pInputAssemblyState = &input_assembly;
  pipe_create_info.pViewportState      = &pvstci;
  pipe_create_info.pRasterizationState = &rasterizer;
  pipe_create_info.pMultisampleState   = &multisampling;
  pipe_create_info.pColorBlendState    = &pcbsci;
  pipe_create_info.pDepthStencilState  = &depth_stencil;
  pipe_create_info.layout              = pipeline_layout;

  VkDynamicState states[] = { VK_DYNAMIC_STATE_VIEWPORT,
                              VK_DYNAMIC_STATE_SCISSOR };

  VkPipelineDynamicStateCreateInfo dynamic_info = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
  };
  dynamic_info.pDynamicStates    = &states[0];
  dynamic_info.dynamicStateCount = 2;

  pipe_create_info.pDynamicState = &dynamic_info;

  VkPipeline pipeline;

  if (!VK_RET(vkCreateGraphicsPipelines(
        device, VK_NULL_HANDLE, 1, &pipe_create_info, nullptr, &pipeline))) {
    return VK_NULL_HANDLE;
  } else {
    return pipeline;
  }
}

void
PipelineBuilder::set_shaders(VkShaderModule vertex_shader,
                             VkShaderModule fragment_shader)
{
  shader_stages.clear();

  shader_stages.push_back(vk_init::pipeline_shader_stage_create_info(
    VK_SHADER_STAGE_VERTEX_BIT, vertex_shader));

  shader_stages.push_back(vk_init::pipeline_shader_stage_create_info(
    VK_SHADER_STAGE_FRAGMENT_BIT, fragment_shader));
}

void
PipelineBuilder::set_input_topology(VkPrimitiveTopology topology)
{
  input_assembly.topology               = topology;
  input_assembly.primitiveRestartEnable = VK_FALSE;
}

void
PipelineBuilder::set_polygon_mode(VkPolygonMode mode)
{
  rasterizer.polygonMode = mode;
  rasterizer.lineWidth   = 1.0f;
}

void
PipelineBuilder::set_cull_mode(VkCullModeFlags cull_mode,
                               VkFrontFace     front_face)
{
  rasterizer.cullMode  = cull_mode;
  rasterizer.frontFace = front_face;
}

void
PipelineBuilder::set_multisampling_none()
{
  multisampling.sampleShadingEnable   = VK_FALSE;
  multisampling.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT;
  multisampling.minSampleShading      = 1.0f;
  multisampling.pSampleMask           = nullptr;
  multisampling.alphaToCoverageEnable = VK_FALSE;
  multisampling.alphaToOneEnable      = VK_FALSE;
}

void
PipelineBuilder::enable_blending_additive()
{
  color_blend_attachment.colorWriteMask =
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  color_blend_attachment.blendEnable         = VK_TRUE;
  color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
  color_blend_attachment.colorBlendOp        = VK_BLEND_OP_ADD;
  color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  color_blend_attachment.alphaBlendOp        = VK_BLEND_OP_ADD;
}

void
PipelineBuilder::enable_blending_alpha_blend()
{
  color_blend_attachment.colorWriteMask =
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  color_blend_attachment.blendEnable = VK_TRUE;
  color_blend_attachment.srcColorBlendFactor =
    VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
  color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
  color_blend_attachment.colorBlendOp        = VK_BLEND_OP_ADD;
  color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  color_blend_attachment.alphaBlendOp        = VK_BLEND_OP_ADD;
}

void
PipelineBuilder::disable_blending()
{
  color_blend_attachment.colorWriteMask =
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  color_blend_attachment.blendEnable = VK_FALSE;
}

void
PipelineBuilder::set_color_attachment_format(VkFormat format)
{
  color_attachment_format = format;

  render_info.colorAttachmentCount    = 1;
  render_info.pColorAttachmentFormats = &color_attachment_format;
}

void
PipelineBuilder::set_depth_format(VkFormat format)
{
  render_info.depthAttachmentFormat = format;
}

void
PipelineBuilder::enable_depthtest(bool depth_write_enable, VkCompareOp op)
{
  depth_stencil.depthTestEnable       = VK_TRUE;
  depth_stencil.depthWriteEnable      = depth_write_enable;
  depth_stencil.depthCompareOp        = op;
  depth_stencil.depthBoundsTestEnable = VK_FALSE;
  depth_stencil.stencilTestEnable     = VK_FALSE;
  depth_stencil.front                 = {};
  depth_stencil.back                  = {};
  depth_stencil.minDepthBounds        = 0.f;
  depth_stencil.maxDepthBounds        = 1.f;
}

void
PipelineBuilder::disable_depth_test()
{
  depth_stencil.depthTestEnable       = VK_FALSE;
  depth_stencil.depthWriteEnable      = VK_FALSE;
  depth_stencil.depthCompareOp        = VK_COMPARE_OP_NEVER;
  depth_stencil.depthBoundsTestEnable = VK_FALSE;
  depth_stencil.stencilTestEnable     = VK_FALSE;
  depth_stencil.front                 = {};
  depth_stencil.back                  = {};
  depth_stencil.minDepthBounds        = 0.0f;
  depth_stencil.maxDepthBounds        = 1.0f;
}
