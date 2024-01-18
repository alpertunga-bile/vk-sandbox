#include "vk_engine.hpp"

#include "vk_images.hpp"
#include "vk_initializers.hpp"
#include "vk_shaders.hpp"

#define VMA_IMPLEMENTATION
#include "vma/vk_mem_alloc.h"

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_vulkan.h"

#include "glm/gtx/transform.hpp"

#include "SDL2/SDL_vulkan.h"
#include "VkBootstrap.h"

#include <chrono>
#include <thread>

#ifndef NDEBUG
constexpr bool bUseValidationLayers = true;
#else
constexpr bool bUseValidationLayers = false;
#endif

VulkanEngine* loaded_engine = nullptr;

void
VulkanEngine::init()
{
  assert(loaded_engine == nullptr);
  loaded_engine = this;

  SDL_Init(SDL_INIT_VIDEO);

  SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);

  m_window = SDL_CreateWindow("Vulkan Engine",
                              SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED,
                              m_window_extent.width,
                              m_window_extent.height,
                              window_flags);

  init_vulkan();
  init_swapchain();
  init_commands();
  init_sync_structures();
  init_descriptors();
  init_pipelines();
  init_imgui();

  init_default_data();

  m_is_initialized = true;
}

void
VulkanEngine::cleanup()
{
  if (m_is_initialized) {
    vkDeviceWaitIdle(m_device);

    m_main_deletion_queue.flush();

    for (int i = 0; i < FRAME_OVERLAP; i++) {
      vkDestroyCommandPool(m_device, m_frames[i].command_pool, nullptr);

      vkDestroyFence(m_device, m_frames[i].render_fence, nullptr);
      vkDestroySemaphore(m_device, m_frames[i].swapchain_semaphore, nullptr);
      vkDestroySemaphore(m_device, m_frames[i].render_semaphore, nullptr);
    }

    destroy_swapchain();

    vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    vkDestroyDevice(m_device, nullptr);

    vkb::destroy_debug_utils_messenger(m_instance, m_debug_messenger);
    vkDestroyInstance(m_instance, nullptr);

    SDL_DestroyWindow(m_window);
  }

  loaded_engine = nullptr;
}

void
VulkanEngine::draw()
{
  uint64_t wait_timeout = 1 * 1000000000;

  VK_CHECK(vkWaitForFences(
    m_device, 1, &get_current_frame().render_fence, true, wait_timeout));

  get_current_frame().deletion_queue.flush();

  VK_CHECK(vkResetFences(m_device, 1, &get_current_frame().render_fence));

  uint32_t swapchain_image_index;
  VK_CHECK(vkAcquireNextImageKHR(m_device,
                                 m_swapchain,
                                 wait_timeout,
                                 get_current_frame().swapchain_semaphore,
                                 nullptr,
                                 &swapchain_image_index));

  VkCommandBuffer cmd = get_current_frame().main_command_buffer;

  VK_CHECK(vkResetCommandBuffer(cmd, 0));

  VkCommandBufferBeginInfo cbbi = vk_init::command_buffer_begin_info(
    VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

  m_draw_extent.width  = m_draw_image.image_extent.width;
  m_draw_extent.height = m_draw_image.image_extent.height;

  VK_CHECK(vkBeginCommandBuffer(cmd, &cbbi));

  vk_util::transition_image(cmd,
                            m_draw_image.image,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_GENERAL);

  draw_background(cmd);

  vk_util::transition_image(cmd,
                            m_draw_image.image,
                            VK_IMAGE_LAYOUT_GENERAL,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

  draw_geometry(cmd);

  vk_util::transition_image(cmd,
                            m_draw_image.image,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

  vk_util::transition_image(cmd,
                            m_swapchain_images[swapchain_image_index],
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  vk_util::copy_image_to_image(cmd,
                               m_draw_image.image,
                               m_swapchain_images[swapchain_image_index],
                               m_draw_extent,
                               m_swapchain_extent);

  vk_util::transition_image(cmd,
                            m_swapchain_images[swapchain_image_index],
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

  draw_imgui(cmd, m_swapchain_image_views[swapchain_image_index]);

  vk_util::transition_image(cmd,
                            m_swapchain_images[swapchain_image_index],
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

  VK_CHECK(vkEndCommandBuffer(cmd));

  VkCommandBufferSubmitInfo cbsi = vk_init::command_buffer_submit_info(cmd);

  VkSemaphoreSubmitInfo wait_info = vk_init::semaphore_submit_info(
    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
    get_current_frame().swapchain_semaphore);

  VkSemaphoreSubmitInfo signal_info = vk_init::semaphore_submit_info(
    VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, get_current_frame().render_semaphore);

  VkSubmitInfo2 submit = vk_init::submit_info(&cbsi, &signal_info, &wait_info);

  VK_CHECK(vkQueueSubmit2(
    m_graphics_queue, 1, &submit, get_current_frame().render_fence));

  VkPresentInfoKHR pi = {};
  pi.sType            = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  pi.pNext            = nullptr;
  pi.pSwapchains      = &m_swapchain;
  pi.swapchainCount   = 1;

  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores    = &get_current_frame().render_semaphore;

  pi.pImageIndices = &swapchain_image_index;

  VK_CHECK(vkQueuePresentKHR(m_graphics_queue, &pi));

  m_frame_number++;
}

void
VulkanEngine::draw_background(VkCommandBuffer cmd)
{
  VkClearColorValue clear_value;
  float             flash = std::abs(std::sin(m_frame_number / 120.0f));
  clear_value             = {
    {0.0f, 0.0f, flash, 1.0f}
  };

  VkImageSubresourceRange clear_range =
    vk_init::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

  ComputeEffect& effect = background_effects[current_background_effect];

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

  vkCmdBindDescriptorSets(cmd,
                          VK_PIPELINE_BIND_POINT_COMPUTE,
                          effect.pipeline_layout,
                          0,
                          1,
                          &m_draw_image_descriptors,
                          0,
                          nullptr);

  vkCmdPushConstants(cmd,
                     m_gradient_pipeline_layout,
                     VK_SHADER_STAGE_COMPUTE_BIT,
                     0,
                     sizeof(ComputePushConstants),
                     &effect.data);

  vkCmdDispatch(cmd,
                std::ceil(m_draw_extent.width / 16.0),
                std::ceil(m_draw_extent.height / 16.0),
                1);
}

void
VulkanEngine::draw_geometry(VkCommandBuffer cmd)
{
  VkRenderingAttachmentInfo rai = vk_init::attachment_info(
    m_draw_image.image_view, nullptr, VK_IMAGE_LAYOUT_GENERAL);

  VkRenderingInfo render_info =
    vk_init::rendering_info(m_draw_extent, &rai, nullptr);
  vkCmdBeginRendering(cmd, &render_info);

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_triangle_pipeline);

  VkViewport viewport = {};
  viewport.x          = 0;
  viewport.y          = 0;
  viewport.width      = m_draw_extent.width;
  viewport.height     = m_draw_extent.height;
  viewport.minDepth   = 0.0f;
  viewport.maxDepth   = 1.0f;

  vkCmdSetViewport(cmd, 0, 1, &viewport);

  VkRect2D scissor      = {};
  scissor.offset.x      = 0;
  scissor.offset.y      = 0;
  scissor.extent.width  = m_draw_extent.width;
  scissor.extent.height = m_draw_extent.height;

  vkCmdSetScissor(cmd, 0, 1, &scissor);

  vkCmdDraw(cmd, 3, 1, 0, 0);

  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_mesh_pipeline);

  GPUDrawPushConstants push_constants;
  push_constants.world_matrix  = glm::mat4(1.0f);
  push_constants.vertex_buffer = rectangle.vertex_buffer_address;

  vkCmdPushConstants(cmd,
                     m_mesh_pipeline_layout,
                     VK_SHADER_STAGE_VERTEX_BIT,
                     0,
                     sizeof(GPUDrawPushConstants),
                     &push_constants);

  vkCmdBindIndexBuffer(
    cmd, rectangle.index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);

  vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);

  glm::mat4 view = glm::translate(glm::vec3(0, 0, -5));

  glm::mat4 proj =
    glm::perspective(glm::radians(70.0f),
                     (float)m_draw_extent.width / (float)m_draw_extent.height,
                     10000.0f,
                     0.1f);

  proj[1][1] *= -1;

  push_constants.world_matrix = proj * view;
  push_constants.vertex_buffer =
    test_meshes[2]->mesh_buffers.vertex_buffer_address;

  vkCmdPushConstants(cmd,
                     m_mesh_pipeline_layout,
                     VK_SHADER_STAGE_VERTEX_BIT,
                     0,
                     sizeof(GPUDrawPushConstants),
                     &push_constants);

  vkCmdBindIndexBuffer(cmd,
                       test_meshes[2]->mesh_buffers.index_buffer.buffer,
                       0,
                       VK_INDEX_TYPE_UINT32);

  vkCmdDrawIndexed(cmd,
                   test_meshes[2]->surfaces[0].count,
                   1,
                   test_meshes[2]->surfaces[0].start_index,
                   0,
                   0);

  vkCmdEndRendering(cmd);
}

void
VulkanEngine::draw_imgui(VkCommandBuffer cmd, VkImageView target_image_view)
{
  VkRenderingAttachmentInfo rai = vk_init::attachment_info(
    target_image_view, nullptr, VK_IMAGE_LAYOUT_GENERAL);
  VkRenderingInfo ri =
    vk_init::rendering_info(m_swapchain_extent, &rai, nullptr);

  vkCmdBeginRendering(cmd, &ri);

  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

  vkCmdEndRendering(cmd);
}

void
VulkanEngine::init_vulkan()
{
  ///////////////////////////////////////////////////////////////////
  // Instance

  vkb::InstanceBuilder builder;

  auto inst_ret = builder.set_app_name("Vulkan Engine")
                    .request_validation_layers(bUseValidationLayers)
                    .use_default_debug_messenger()
                    .require_api_version(1, 3, 0)
                    .build();

  vkb::Instance vkb_inst = inst_ret.value();

  m_instance        = vkb_inst.instance;
  m_debug_messenger = vkb_inst.debug_messenger;

  ///////////////////////////////////////////////////////////////////
  // Physical Device & Logical Device

  SDL_Vulkan_CreateSurface(m_window, m_instance, &m_surface);

  VkPhysicalDeviceVulkan13Features features{};
  features.dynamicRendering = true;
  features.synchronization2 = true;

  VkPhysicalDeviceVulkan12Features features12{};
  features12.bufferDeviceAddress = true;
  features12.descriptorIndexing  = true;

  vkb::PhysicalDeviceSelector selector{ vkb_inst };
  vkb::PhysicalDevice physical_device = selector.set_minimum_version(1, 3)
                                          .set_required_features_13(features)
                                          .set_required_features_12(features12)
                                          .set_surface(m_surface)
                                          .select()
                                          .value();

  vkb::DeviceBuilder device_builder{ physical_device };
  vkb::Device        vkb_device = device_builder.build().value();

  m_device          = vkb_device.device;
  m_physical_device = physical_device.physical_device;

  m_graphics_queue = vkb_device.get_queue(vkb::QueueType::graphics).value();
  m_graphics_queue_family =
    vkb_device.get_queue_index(vkb::QueueType::graphics).value();

  VmaAllocatorCreateInfo vaci = {};
  vaci.physicalDevice         = m_physical_device;
  vaci.device                 = m_device;
  vaci.instance               = m_instance;
  vaci.flags                  = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;

  vmaCreateAllocator(&vaci, &m_allocator);

  m_main_deletion_queue.push_function(
    [&]() { vmaDestroyAllocator(m_allocator); });
}

void
VulkanEngine::init_swapchain()
{
  create_swapchain(m_window_extent.width, m_window_extent.height);
}

void
VulkanEngine::init_commands()
{
  VkCommandPoolCreateInfo cpci = vk_init::command_pool_create_info(
    m_graphics_queue_family, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

  for (int i = 0; i < FRAME_OVERLAP; i++) {
    VK_CHECK(
      vkCreateCommandPool(m_device, &cpci, nullptr, &m_frames[i].command_pool));

    VkCommandBufferAllocateInfo cbai =
      vk_init::command_buffer_allocate_info(m_frames[i].command_pool, 1);

    VK_CHECK(vkAllocateCommandBuffers(
      m_device, &cbai, &m_frames[i].main_command_buffer));
  }

  VK_CHECK(vkCreateCommandPool(m_device, &cpci, nullptr, &m_imm_command_pool));

  VkCommandBufferAllocateInfo cmd_alloc_info =
    vk_init::command_buffer_allocate_info(m_imm_command_pool, 1);

  VK_CHECK(
    vkAllocateCommandBuffers(m_device, &cmd_alloc_info, &m_imm_command_buffer));

  m_main_deletion_queue.push_function(
    [=]() { vkDestroyCommandPool(m_device, m_imm_command_pool, nullptr); });
}

void
VulkanEngine::init_sync_structures()
{
  VkFenceCreateInfo fci =
    vk_init::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
  VkSemaphoreCreateInfo sci = vk_init::semaphore_create_info();

  for (int i = 0; i < FRAME_OVERLAP; i++) {
    VK_CHECK(vkCreateFence(m_device, &fci, nullptr, &m_frames[i].render_fence));

    VK_CHECK(vkCreateSemaphore(
      m_device, &sci, nullptr, &m_frames[i].swapchain_semaphore));
    VK_CHECK(vkCreateSemaphore(
      m_device, &sci, nullptr, &m_frames[i].render_semaphore));
  }

  VK_CHECK(vkCreateFence(m_device, &fci, nullptr, &m_imm_fence));

  m_main_deletion_queue.push_function(
    [=]() { vkDestroyFence(m_device, m_imm_fence, nullptr); });
}

void
VulkanEngine::init_descriptors()
{
  std::vector<DescriptorAllocator::PoolSizeRatio> sizes = {
    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}
  };

  m_global_descriptor_allocator.init_pool(m_device, 10, sizes);

  {
    DescriptorLayoutBuilder builder;
    builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    m_draw_image_descriptor_layout =
      builder.build(m_device, VK_SHADER_STAGE_COMPUTE_BIT);
  }

  m_draw_image_descriptors = m_global_descriptor_allocator.allocate(
    m_device, m_draw_image_descriptor_layout);

  VkDescriptorImageInfo dii = {};
  dii.imageLayout           = VK_IMAGE_LAYOUT_GENERAL;
  dii.imageView             = m_draw_image.image_view;

  VkWriteDescriptorSet diw = {};
  diw.sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  diw.pNext                = nullptr;
  diw.dstBinding           = 0;
  diw.dstSet               = m_draw_image_descriptors;
  diw.descriptorCount      = 1;
  diw.descriptorType       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  diw.pImageInfo           = &dii;

  vkUpdateDescriptorSets(m_device, 1, &diw, 0, nullptr);

  m_main_deletion_queue.push_function([&]() {
    vkDestroyDescriptorSetLayout(
      m_device, m_draw_image_descriptor_layout, nullptr);
    m_global_descriptor_allocator.destroy_pool(m_device);
  });
}

void
VulkanEngine::init_pipelines()
{
  init_background_pipelines();
  init_triangle_pipeline();
  init_mesh_pipeline();
}

void
VulkanEngine::init_background_pipelines()
{

  VkPipelineLayoutCreateInfo plci = {};
  plci.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  plci.pNext          = nullptr;
  plci.pSetLayouts    = &m_draw_image_descriptor_layout;
  plci.setLayoutCount = 1;

  VkPushConstantRange pcr = {};
  pcr.offset              = 0;
  pcr.size                = sizeof(ComputePushConstants);
  pcr.stageFlags          = VK_SHADER_STAGE_COMPUTE_BIT;

  plci.pPushConstantRanges    = &pcr;
  plci.pushConstantRangeCount = 1;

  VK_CHECK(vkCreatePipelineLayout(
    m_device, &plci, nullptr, &m_gradient_pipeline_layout));

  Shader gradient_color_shader;
  gradient_color_shader.init(m_device, "shaders/gradient_color.comp");

  Shader sky_shader;
  sky_shader.init(m_device, "shaders/sky.comp");

  VkPipelineShaderStageCreateInfo pssci = {};
  pssci.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  pssci.pNext  = nullptr;
  pssci.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
  pssci.module = gradient_color_shader.get();
  pssci.pName  = "main";

  VkComputePipelineCreateInfo cpci = {};
  cpci.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  cpci.pNext  = nullptr;
  cpci.layout = m_gradient_pipeline_layout;
  cpci.stage  = pssci;

  ComputeEffect gradient_effect;
  gradient_effect.pipeline_layout = m_gradient_pipeline_layout;
  gradient_effect.name            = "gradient";
  gradient_effect.data            = {};
  gradient_effect.data.data_1     = glm::vec4(1, 0, 0, 1);
  gradient_effect.data.data_2     = glm::vec4(0, 0, 1, 1);

  VK_CHECK(vkCreateComputePipelines(
    m_device, VK_NULL_HANDLE, 1, &cpci, nullptr, &gradient_effect.pipeline));

  cpci.stage.module = sky_shader.get();

  ComputeEffect sky_effect;
  sky_effect.pipeline_layout = m_gradient_pipeline_layout;
  sky_effect.name            = "sky";
  sky_effect.data            = {};
  sky_effect.data.data_1     = glm::vec4(0.1, 0.2, 0.4, 0.97);

  VK_CHECK(vkCreateComputePipelines(
    m_device, VK_NULL_HANDLE, 1, &cpci, nullptr, &sky_effect.pipeline));

  background_effects.push_back(gradient_effect);
  background_effects.push_back(sky_effect);

  gradient_color_shader.destroy();
  sky_shader.destroy();

  m_main_deletion_queue.push_function([&]() {
    vkDestroyPipelineLayout(m_device, m_gradient_pipeline_layout, nullptr);
    vkDestroyPipeline(m_device, gradient_effect.pipeline, nullptr);
    vkDestroyPipeline(m_device, sky_effect.pipeline, nullptr);
  });
}

void
VulkanEngine::init_triangle_pipeline()
{
  Shader triangle_vert_shader;
  Shader triangle_frag_shader;

  triangle_vert_shader.init(m_device, "shaders/colored_triangle.vert");
  triangle_frag_shader.init(m_device, "shaders/colored_triangle.frag");

  VkPipelineLayoutCreateInfo plci = vk_init::pipeline_layout_create_info();

  VK_CHECK(vkCreatePipelineLayout(
    m_device, &plci, nullptr, &m_triangle_pipeline_layout));

  PipelineBuilder pipeline_builder;
  pipeline_builder.pipeline_layout = m_triangle_pipeline_layout;
  pipeline_builder.set_shaders(triangle_vert_shader.get(),
                               triangle_frag_shader.get());
  pipeline_builder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  pipeline_builder.set_polygon_mode(VK_POLYGON_MODE_FILL);
  pipeline_builder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
  pipeline_builder.set_multisampling_none();
  pipeline_builder.disable_blending();
  pipeline_builder.disable_depth_test();
  pipeline_builder.set_color_attachment_format(m_draw_image.image_format);
  pipeline_builder.set_depth_format(VK_FORMAT_UNDEFINED);

  m_triangle_pipeline = pipeline_builder.build_pipeline(m_device);

  triangle_vert_shader.destroy();
  triangle_frag_shader.destroy();

  m_main_deletion_queue.push_function([&]() {
    vkDestroyPipelineLayout(m_device, m_triangle_pipeline_layout, nullptr);
    vkDestroyPipeline(m_device, m_triangle_pipeline, nullptr);
  });
}

void
VulkanEngine::init_mesh_pipeline()
{
  Shader triangle_vert_shader;
  Shader triangle_frag_shader;

  triangle_vert_shader.init(m_device, "shaders/colored_triangle_mesh.vert");
  triangle_frag_shader.init(m_device, "shaders/colored_triangle.frag");

  VkPipelineLayoutCreateInfo plci = vk_init::pipeline_layout_create_info();

  VkPushConstantRange buffer_range{};
  buffer_range.offset     = 0;
  buffer_range.size       = sizeof(GPUDrawPushConstants);
  buffer_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  plci.pushConstantRangeCount = 1;
  plci.pPushConstantRanges    = &buffer_range;

  VK_CHECK(
    vkCreatePipelineLayout(m_device, &plci, nullptr, &m_mesh_pipeline_layout));

  PipelineBuilder pipeline_builder;
  pipeline_builder.pipeline_layout = m_mesh_pipeline_layout;
  pipeline_builder.set_shaders(triangle_vert_shader.get(),
                               triangle_frag_shader.get());
  pipeline_builder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  pipeline_builder.set_polygon_mode(VK_POLYGON_MODE_FILL);
  pipeline_builder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
  pipeline_builder.set_multisampling_none();
  pipeline_builder.disable_blending();
  pipeline_builder.disable_depth_test();
  pipeline_builder.set_color_attachment_format(m_draw_image.image_format);
  pipeline_builder.set_depth_format(VK_FORMAT_UNDEFINED);

  m_mesh_pipeline = pipeline_builder.build_pipeline(m_device);

  triangle_vert_shader.destroy();
  triangle_frag_shader.destroy();

  m_main_deletion_queue.push_function([&]() {
    vkDestroyPipelineLayout(m_device, m_mesh_pipeline_layout, nullptr);
    vkDestroyPipeline(m_device, m_mesh_pipeline, nullptr);
  });
}

void
VulkanEngine::init_default_data()
{
  std::array<Vertex, 4> rect_vertices;

  rect_vertices[0].position = { 0.5, -0.5, 0 };
  rect_vertices[1].position = { 0.5, 0.5, 0 };
  rect_vertices[2].position = { -0.5, -0.5, 0 };
  rect_vertices[3].position = { -0.5, 0.5, 0 };

  rect_vertices[0].color = { 0, 0, 0, 1 };
  rect_vertices[1].color = { 0.5, 0.5, 0.5, 1 };
  rect_vertices[2].color = { 1, 0, 0, 1 };
  rect_vertices[3].color = { 0, 1, 0, 1 };

  std::array<uint32_t, 6> rect_indices;

  rect_indices[0] = 0;
  rect_indices[1] = 1;
  rect_indices[2] = 2;

  rect_indices[3] = 2;
  rect_indices[4] = 1;
  rect_indices[5] = 3;

  rectangle = upload_mesh(rect_indices, rect_vertices);

  test_meshes = load_gltf_meshes(this, "assets/basicmesh.glb").value();
}

void
VulkanEngine::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& func)
{
  VK_CHECK(vkResetFences(m_device, 1, &m_imm_fence));
  VK_CHECK(vkResetCommandBuffer(m_imm_command_buffer, 0));

  VkCommandBuffer main_cmd = m_imm_command_buffer;

  VkCommandBufferBeginInfo cbbi = vk_init::command_buffer_begin_info(
    VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

  VK_CHECK(vkBeginCommandBuffer(main_cmd, &cbbi));

  func(main_cmd);

  VK_CHECK(vkEndCommandBuffer(main_cmd));

  VkCommandBufferSubmitInfo cbsi =
    vk_init::command_buffer_submit_info(main_cmd);
  VkSubmitInfo2 si = vk_init::submit_info(&cbsi, nullptr, nullptr);

  VK_CHECK(vkQueueSubmit2(m_graphics_queue, 1, &si, m_imm_fence));

  VK_CHECK(vkWaitForFences(m_device, 1, &m_imm_fence, true, 9999999999));
}

void
VulkanEngine::init_imgui()
{
  VkDescriptorPoolSize pool_sizes[] = {
    {               VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
    {         VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
    {         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
    {  VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
    {  VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
    {        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
    {        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
    {      VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
  };

  VkDescriptorPoolCreateInfo dpci = {};
  dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  dpci.pNext         = nullptr;
  dpci.maxSets       = 1000;
  dpci.poolSizeCount = (uint32_t)std::size(pool_sizes);
  dpci.pPoolSizes    = pool_sizes;

  VkDescriptorPool imgui_pool;
  VK_CHECK(vkCreateDescriptorPool(m_device, &dpci, nullptr, &imgui_pool));

  ImGui::CreateContext();

  ImGui_ImplSDL2_InitForVulkan(m_window);

  ImGui_ImplVulkan_InitInfo ii = {};
  ii.Instance                  = m_instance;
  ii.PhysicalDevice            = m_physical_device;
  ii.Device                    = m_device;
  ii.Queue                     = m_graphics_queue;
  ii.DescriptorPool            = imgui_pool;
  ii.MinImageCount             = 3;
  ii.ImageCount                = 3;
  ii.UseDynamicRendering       = true;
  ii.ColorAttachmentFormat     = m_swapchain_image_format;
  ii.MSAASamples               = VK_SAMPLE_COUNT_1_BIT;

  ImGui_ImplVulkan_Init(&ii, VK_NULL_HANDLE);

  immediate_submit(
    [&](VkCommandBuffer cmd) { ImGui_ImplVulkan_CreateFontsTexture(cmd); });

  ImGui_ImplVulkan_DestroyFontUploadObjects();

  m_main_deletion_queue.push_function([=]() {
    vkDestroyDescriptorPool(m_device, imgui_pool, nullptr);
    ImGui_ImplVulkan_Shutdown();
  });
}

AllocatedBuffer
VulkanEngine::create_buffer(size_t             alloc_size,
                            VkBufferUsageFlags usage_flags,
                            VmaMemoryUsage     memory_usage)
{
  VkBufferCreateInfo buffer_info = { .sType =
                                       VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
  buffer_info.pNext              = nullptr;
  buffer_info.size               = alloc_size;
  buffer_info.usage              = usage_flags;

  VmaAllocationCreateInfo vma_aci = {};
  vma_aci.usage                   = memory_usage;
  vma_aci.flags                   = VMA_ALLOCATION_CREATE_MAPPED_BIT;

  AllocatedBuffer new_buffer;

  VK_CHECK(vmaCreateBuffer(m_allocator,
                           &buffer_info,
                           &vma_aci,
                           &new_buffer.buffer,
                           &new_buffer.allocation,
                           &new_buffer.info));

  return new_buffer;
}

GPUMeshBuffers
VulkanEngine::upload_mesh(std::span<uint32_t> indices,
                          std::span<Vertex>   vertices)
{
  const size_t vertex_buffer_size = vertices.size() * sizeof(Vertex);
  const size_t index_buffer_size  = indices.size() * sizeof(uint32_t);

  GPUMeshBuffers new_geo;

  new_geo.vertex_buffer = create_buffer(
    vertex_buffer_size,
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
    VMA_MEMORY_USAGE_GPU_ONLY);

  VkBufferDeviceAddressInfo dev_add_info{
    .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
    .buffer = new_geo.vertex_buffer.buffer
  };
  new_geo.vertex_buffer_address =
    vkGetBufferDeviceAddress(m_device, &dev_add_info);

  new_geo.index_buffer = create_buffer(index_buffer_size,
                                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                         VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                       VMA_MEMORY_USAGE_GPU_ONLY);

  AllocatedBuffer staging =
    create_buffer(vertex_buffer_size + index_buffer_size,
                  VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                  VMA_MEMORY_USAGE_CPU_ONLY);

  void* data = staging.allocation->GetMappedData();

  memcpy(data, vertices.data(), vertex_buffer_size);
  memcpy((char*)data + vertex_buffer_size, indices.data(), index_buffer_size);

  immediate_submit([&](VkCommandBuffer cmd) {
    VkBufferCopy vertex_copy{ 0 };
    vertex_copy.dstOffset = 0;
    vertex_copy.srcOffset = 0;
    vertex_copy.size      = vertex_buffer_size;

    vkCmdCopyBuffer(
      cmd, staging.buffer, new_geo.vertex_buffer.buffer, 1, &vertex_copy);

    VkBufferCopy index_copy{ 0 };
    index_copy.dstOffset = 0;
    index_copy.srcOffset = vertex_buffer_size;
    index_copy.size      = index_buffer_size;

    vkCmdCopyBuffer(
      cmd, staging.buffer, new_geo.index_buffer.buffer, 1, &index_copy);
  });

  destroy_buffer(staging);

  return new_geo;
}

void
VulkanEngine::create_swapchain(uint32_t width, uint32_t height)
{
  vkb::SwapchainBuilder swapchain_builder{ m_physical_device,
                                           m_device,
                                           m_surface };

  m_swapchain_image_format = VK_FORMAT_B8G8R8A8_UNORM;

  vkb::Swapchain vkb_swapchain =
    swapchain_builder
      .set_desired_format(
        VkSurfaceFormatKHR{ .format     = m_swapchain_image_format,
                            .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
      .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
      .set_desired_extent(width, height)
      .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
      .build()
      .value();

  m_swapchain_extent      = vkb_swapchain.extent;
  m_swapchain             = vkb_swapchain.swapchain;
  m_swapchain_images      = vkb_swapchain.get_images().value();
  m_swapchain_image_views = vkb_swapchain.get_image_views().value();

  VkExtent3D draw_image_extent = { m_window_extent.width,
                                   m_window_extent.height,
                                   1 };

  m_draw_image.image_format = VK_FORMAT_R16G16B16A16_SFLOAT;
  m_draw_image.image_extent = draw_image_extent;

  VkImageUsageFlags draw_image_usages{};
  draw_image_usages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  draw_image_usages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  draw_image_usages |= VK_IMAGE_USAGE_STORAGE_BIT;
  draw_image_usages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  VkImageCreateInfo rimg_info = vk_init::image_create_info(
    m_draw_image.image_format, draw_image_usages, draw_image_extent);

  VmaAllocationCreateInfo rimg_allocinfo = {};
  rimg_allocinfo.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;
  rimg_allocinfo.requiredFlags =
    VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  vmaCreateImage(m_allocator,
                 &rimg_info,
                 &rimg_allocinfo,
                 &m_draw_image.image,
                 &m_draw_image.allocation,
                 nullptr);

  VkImageViewCreateInfo rview_info = vk_init::image_view_create_info(
    m_draw_image.image_format, m_draw_image.image, VK_IMAGE_ASPECT_COLOR_BIT);

  VK_CHECK(vkCreateImageView(
    m_device, &rview_info, nullptr, &m_draw_image.image_view));

  m_depth_image.image_format = VK_FORMAT_D32_SFLOAT;
  m_depth_image.image_extent = draw_image_extent;

  VkImageUsageFlags depth_image_usages{};
  depth_image_usages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

  VkImageCreateInfo dimg_info = vk_init::image_create_info(
    m_depth_image.image_format, depth_image_usages, draw_image_extent);

  vmaCreateImage(m_allocator,
                 &dimg_info,
                 &rimg_allocinfo,
                 &m_depth_image.image,
                 &m_depth_image.allocation,
                 nullptr);

  VkImageViewCreateInfo dview_info = vk_init::image_view_create_info(
    m_depth_image.image_format, m_depth_image.image, VK_IMAGE_ASPECT_DEPTH_BIT);

  VK_CHECK(vkCreateImageView(
    m_device, &dview_info, nullptr, &m_depth_image.image_view));

  m_main_deletion_queue.push_function([=]() {
    vkDestroyImageView(m_device, m_draw_image.image_view, nullptr);
    vmaDestroyImage(m_allocator, m_draw_image.image, m_draw_image.allocation);

    vkDestroyImageView(m_device, m_depth_image.image_view, nullptr);
    vmaDestroyImage(m_allocator, m_depth_image.image, m_depth_image.allocation);
  });
}

void
VulkanEngine::destroy_swapchain()
{
  vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);

  for (int i = 0; i < m_swapchain_image_views.size(); i++) {
    vkDestroyImageView(m_device, m_swapchain_image_views[i], nullptr);
  }
}

void
VulkanEngine::destroy_buffer(const AllocatedBuffer& buffer)
{
  vmaDestroyBuffer(m_allocator, buffer.buffer, buffer.allocation);
}

void
VulkanEngine::run()
{
  SDL_Event e;
  bool      bQuit = false;

  while (!bQuit) {
    while (SDL_PollEvent(&e) != 0) {
      if (e.type == SDL_QUIT) {
        bQuit = true;
      }

      if (e.type == SDL_WINDOWEVENT) {
        if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
          m_stop_rendering = true;
        } else if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
          m_stop_rendering = false;
        }
      }

      ImGui_ImplSDL2_ProcessEvent(&e);
    }

    if (m_stop_rendering) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame(m_window);
    ImGui::NewFrame();

    if (ImGui::Begin("background")) {
      ComputeEffect& selected = background_effects[current_background_effect];

      ImGui::Text("Selected Effect : ", selected.name);

      ImGui::SliderInt("Effect Index : ",
                       &current_background_effect,
                       0,
                       background_effects.size() - 1);

      ImGui::InputFloat4("data_1", (float*)&selected.data.data_1);
      ImGui::InputFloat4("data_2", (float*)&selected.data.data_2);
      ImGui::InputFloat4("data_3", (float*)&selected.data.data_3);
      ImGui::InputFloat4("data_4", (float*)&selected.data.data_4);

      ImGui::End();
    }

    ImGui::Render();

    draw();
  }
}

VulkanEngine&
VulkanEngine::Get()
{
  return *loaded_engine;
}
