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

bool
is_visible(const RenderObject& obj, const glm::mat4& viewproj)
{
  std::array<glm::vec3, 8> corners{
    glm::vec3{ 1,  1,  1},
    glm::vec3{ 1,  1, -1},
    glm::vec3{ 1, -1,  1},
    glm::vec3{ 1, -1, -1},
    glm::vec3{-1,  1,  1},
    glm::vec3{-1,  1, -1},
    glm::vec3{-1, -1,  1},
    glm::vec3{-1, -1, -1}
  };

  glm::mat4 matrix = viewproj * obj.transform;

  glm::vec3 min = { 1.5, 1.5, 1.5 };
  glm::vec3 max = { -1.5, -1.5, -1.5 };

  for (int c = 0; c < 8; c++) {
    glm::vec4 v =
      matrix *
      glm::vec4(obj.bounds.origin + (corners[c] * obj.bounds.extents), 1.f);

    v.x = v.x / v.w;
    v.y = v.y / v.w;
    v.z = v.z / v.w;

    min = glm::min(glm::vec3{ v.x, v.y, v.z }, min);
    max = glm::max(glm::vec3{ v.x, v.y, v.z }, max);
  }

  if (min.z > 1.f || max.z < 0.f || min.x > 1.f || max.x < -1.f ||
      min.y > 1.f || max.y < -1.f) {
    return false;
  } else {
    return true;
  }
}

VulkanEngine* loaded_engine = nullptr;

void
VulkanEngine::init()
{
  assert(loaded_engine == nullptr);
  loaded_engine = this;

  SDL_Init(SDL_INIT_VIDEO);

  SDL_WindowFlags window_flags =
    (SDL_WindowFlags)(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

  m_window = SDL_CreateWindow("Vulkan Engine",
                              SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED,
                              m_window_extent.width,
                              m_window_extent.height,
                              window_flags);

  SDL_SetRelativeMouseMode(SDL_TRUE);

  init_vulkan();
  init_swapchain();
  init_commands();
  init_sync_structures();
  init_descriptors();
  init_pipelines();
  init_imgui();

  init_default_data();

  m_main_camera.velocity = glm::vec3(0.f);
  m_main_camera.position = glm::vec3(30.0f, -00.f, -085.f);
  m_main_camera.pitch    = 0;
  m_main_camera.yaw      = 0;

  std::string structure_path = { "assets/structure.glb" };
  auto        structure_file = load_gltf(this, structure_path);

  assert(structure_file.has_value());

  m_loaded_scenes["structure"] = *structure_file;

  m_is_initialized = true;
}

void
VulkanEngine::cleanup()
{
  if (m_is_initialized) {
    vkDeviceWaitIdle(m_device);

    m_loaded_scenes.clear();

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

  m_draw_extent.height =
    std::min(m_swapchain_extent.height, m_draw_image.image_extent.height) *
    render_scale;
  m_draw_extent.width =
    std::min(m_swapchain_extent.width, m_draw_image.image_extent.width) *
    render_scale;

  update_scene();

  VK_CHECK(vkWaitForFences(
    m_device, 1, &get_current_frame().render_fence, true, wait_timeout));

  get_current_frame().deletion_queue.flush();
  get_current_frame().m_frame_descriptors.clear_pools(m_device);

  VK_CHECK(vkResetFences(m_device, 1, &get_current_frame().render_fence));

  uint32_t swapchain_image_index;
  VkResult res = vkAcquireNextImageKHR(m_device,
                                       m_swapchain,
                                       wait_timeout,
                                       get_current_frame().swapchain_semaphore,
                                       nullptr,
                                       &swapchain_image_index);

  if (res == VK_ERROR_OUT_OF_DATE_KHR) {
    resize_requested = true;
    return;
  }

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

  vk_util::transition_image(cmd,
                            m_depth_image.image,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

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

  res = vkQueuePresentKHR(m_graphics_queue, &pi);

  if (res == VK_ERROR_OUT_OF_DATE_KHR) {
    resize_requested = true;
  }

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
  auto start = std::chrono::system_clock::now();

  m_stats.drawcall_count = 0;
  m_stats.triangle_count = 0;

  std::vector<uint32_t> opaque_draws;
  opaque_draws.reserve(m_main_draw_context.opaque_surfaces.size());

  for (uint32_t i = 0; i < m_main_draw_context.opaque_surfaces.size(); i++) {
    if (is_visible(m_main_draw_context.opaque_surfaces[i],
                   scene_data.view_proj)) {
      opaque_draws.push_back(i);
    }
  }

  std::sort(opaque_draws.begin(),
            opaque_draws.end(),
            [&](const auto& iA, const auto& iB) {
              const RenderObject& A = m_main_draw_context.opaque_surfaces[iA];
              const RenderObject& B = m_main_draw_context.opaque_surfaces[iB];

              if (A.material == B.material) {
                return A.index_buffer < B.index_buffer;
              } else {
                return A.material < B.material;
              }
            });

  VkRenderingAttachmentInfo rai = vk_init::attachment_info(
    m_draw_image.image_view, nullptr, VK_IMAGE_LAYOUT_GENERAL);

  VkRenderingAttachmentInfo depth_attachment = vk_init::depth_attachment_info(
    m_depth_image.image_view, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

  VkRenderingInfo render_info =
    vk_init::rendering_info(m_draw_extent, &rai, &depth_attachment);
  vkCmdBeginRendering(cmd, &render_info);

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

  AllocatedBuffer gpuSceneDataBuffer =
    create_buffer(sizeof(GPUSceneData),
                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                  VMA_MEMORY_USAGE_CPU_TO_GPU);

  get_current_frame().deletion_queue.push_function(
    [=, this]() { destroy_buffer(gpuSceneDataBuffer); });

  GPUSceneData* sceneUniformData =
    (GPUSceneData*)gpuSceneDataBuffer.allocation->GetMappedData();
  *sceneUniformData = scene_data;

  VkDescriptorSet globalDescriptor =
    get_current_frame().m_frame_descriptors.allocate(
      m_device, m_gpu_scene_data_descriptor_layout);

  {
    DescriptorWriter writer;
    writer.write_buffer(0,
                        gpuSceneDataBuffer.buffer,
                        sizeof(GPUSceneData),
                        0,
                        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.update_set(m_device, globalDescriptor);
  }

  MaterialPipeline* last_pipeline     = nullptr;
  MaterialInstance* last_material     = nullptr;
  VkBuffer          last_index_buffer = VK_NULL_HANDLE;

  auto draw = [&](const RenderObject& r) {
    if (r.material != last_material) {
      last_material = r.material;

      if (r.material->pipeline != last_pipeline) {
        last_pipeline = r.material->pipeline;

        vkCmdBindPipeline(
          cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r.material->pipeline->pipeline);
        vkCmdBindDescriptorSets(cmd,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                r.material->pipeline->layout,
                                0,
                                1,
                                &globalDescriptor,
                                0,
                                nullptr);

        VkViewport viewport = {};
        viewport.x          = 0;
        viewport.y          = 0;
        viewport.width      = (float)m_window_extent.width;
        viewport.height     = (float)m_window_extent.height;
        viewport.minDepth   = 0.f;
        viewport.maxDepth   = 1.f;

        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor      = {};
        scissor.offset.x      = 0;
        scissor.offset.y      = 0;
        scissor.extent.width  = m_window_extent.width;
        scissor.extent.height = m_window_extent.height;

        vkCmdSetScissor(cmd, 0, 1, &scissor);
      }

      vkCmdBindDescriptorSets(cmd,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              r.material->pipeline->layout,
                              1,
                              1,
                              &r.material->material_set,
                              0,
                              nullptr);
    }

    if (r.index_buffer != last_index_buffer) {
      last_index_buffer = r.index_buffer;
      vkCmdBindIndexBuffer(cmd, r.index_buffer, 0, VK_INDEX_TYPE_UINT32);
    }

    GPUDrawPushConstants push_constants;
    push_constants.world_matrix  = r.transform;
    push_constants.vertex_buffer = r.vertex_buffer_address;

    vkCmdPushConstants(cmd,
                       r.material->pipeline->layout,
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0,
                       sizeof(GPUDrawPushConstants),
                       &push_constants);

    vkCmdDrawIndexed(cmd, r.index_count, 1, r.first_index, 0, 0);

    m_stats.drawcall_count++;
    m_stats.triangle_count += r.index_count / 3;
  };

  for (auto& r : opaque_draws) {
    draw(m_main_draw_context.opaque_surfaces[r]);
  }

  for (auto& r : m_main_draw_context.transparent_surfaces) {
    draw(r);
  }

  vkCmdEndRendering(cmd);

  auto end = std::chrono::system_clock::now();

  auto elapsed =
    std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  m_stats.mesh_draw_time = elapsed.count() / 1000.f;
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
  std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes = {
    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1.0f}
  };

  m_global_descriptor_allocator.init(m_device, 10, sizes);

  {
    DescriptorLayoutBuilder builder;
    builder.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    m_draw_image_descriptor_layout =
      builder.build(m_device, VK_SHADER_STAGE_COMPUTE_BIT);
  }

  {
    DescriptorLayoutBuilder builder;
    builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    m_gpu_scene_data_descriptor_layout = builder.build(
      m_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
  }

  {
    DescriptorLayoutBuilder builder;
    builder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    m_single_image_descriptor_layout =
      builder.build(m_device, VK_SHADER_STAGE_FRAGMENT_BIT);
  }

  m_draw_image_descriptors = m_global_descriptor_allocator.allocate(
    m_device, m_draw_image_descriptor_layout);

  DescriptorWriter writer;
  writer.write_image(0,
                     m_draw_image.image_view,
                     VK_NULL_HANDLE,
                     VK_IMAGE_LAYOUT_GENERAL,
                     VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
  writer.update_set(m_device, m_draw_image_descriptors);

  for (int i = 0; i < FRAME_OVERLAP; i++) {
    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = {
      {         VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
      {        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
      {        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
    };

    m_frames[i].m_frame_descriptors = DescriptorAllocatorGrowable{};
    m_frames[i].m_frame_descriptors.init(m_device, 1000, frame_sizes);

    m_main_deletion_queue.push_function(
      [&, i]() { m_frames[i].m_frame_descriptors.destroy_pools(m_device); });
  }

  m_main_deletion_queue.push_function([&]() {
    vkDestroyDescriptorSetLayout(
      m_device, m_draw_image_descriptor_layout, nullptr);
    m_global_descriptor_allocator.destroy_pools(m_device);
  });
}

void
VulkanEngine::init_pipelines()
{
  init_background_pipelines();
  init_mesh_pipeline();

  metal_rough_material.build_pipeline(this);
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
VulkanEngine::init_mesh_pipeline()
{
  Shader triangle_vert_shader;
  Shader triangle_frag_shader;

  triangle_vert_shader.init(m_device, "shaders/colored_triangle_mesh.vert");
  triangle_frag_shader.init(m_device, "shaders/tex_image.frag");

  VkPushConstantRange buffer_range{};
  buffer_range.offset     = 0;
  buffer_range.size       = sizeof(GPUDrawPushConstants);
  buffer_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  VkPipelineLayoutCreateInfo plci = vk_init::pipeline_layout_create_info();
  plci.pushConstantRangeCount     = 1;
  plci.pPushConstantRanges        = &buffer_range;
  plci.setLayoutCount             = 1;
  plci.pSetLayouts                = &m_single_image_descriptor_layout;

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
  pipeline_builder.enable_blending_additive();
  pipeline_builder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
  pipeline_builder.set_color_attachment_format(m_draw_image.image_format);
  pipeline_builder.set_depth_format(m_depth_image.image_format);

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
  test_meshes = load_gltf_meshes(this, "assets/basicmesh.glb").value();

  m_main_deletion_queue.push_function([&]() {
    for (auto&& mesh_asset : test_meshes) {
      destroy_buffer(mesh_asset->mesh_buffers.index_buffer);
      destroy_buffer(mesh_asset->mesh_buffers.vertex_buffer);
    }
  });

  uint32_t white = 0xFFFFFFFF;
  m_white_image  = create_image((void*)&white,
                               VkExtent3D{ 1, 1, 1 },
                               VK_FORMAT_R8G8B8A8_UNORM,
                               VK_IMAGE_USAGE_SAMPLED_BIT);

  uint32_t grey = 0xAAAAAAFF;
  m_grey_image  = create_image((void*)&grey,
                              VkExtent3D{ 1, 1, 1 },
                              VK_FORMAT_R8G8B8A8_UNORM,
                              VK_IMAGE_USAGE_SAMPLED_BIT);

  uint32_t black = 0x000000FF;
  m_black_image  = create_image((void*)&black,
                               VkExtent3D{ 1, 1, 1 },
                               VK_FORMAT_R8G8B8A8_UNORM,
                               VK_IMAGE_USAGE_SAMPLED_BIT);

  uint32_t                      magenta = 0xFF00FFFF;
  std::array<uint32_t, 16 * 16> pixels;
  for (int x = 0; x < 16; x++) {
    for (int y = 0; y < 16; y++) {
      pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
    }
  }

  m_checkboard_image = create_image(pixels.data(),
                                    VkExtent3D{ 16, 16, 1 },
                                    VK_FORMAT_R8G8B8A8_UNORM,
                                    VK_IMAGE_USAGE_SAMPLED_BIT);

  VkSamplerCreateInfo samp_ci = { .sType =
                                    VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
  samp_ci.magFilter           = VK_FILTER_NEAREST;
  samp_ci.minFilter           = VK_FILTER_NEAREST;

  vkCreateSampler(m_device, &samp_ci, nullptr, &m_default_sampler_nearest);

  samp_ci.magFilter = VK_FILTER_LINEAR;
  samp_ci.minFilter = VK_FILTER_LINEAR;

  vkCreateSampler(m_device, &samp_ci, nullptr, &m_default_sampler_linear);

  GLTFMetallic_Roughness::MaterialResources material_resources;
  material_resources.color_image         = m_white_image;
  material_resources.color_sampler       = m_default_sampler_linear;
  material_resources.metal_rough_image   = m_white_image;
  material_resources.metal_rough_sampler = m_default_sampler_linear;

  AllocatedBuffer material_constants =
    create_buffer(sizeof(GLTFMetallic_Roughness::MaterialResources),
                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                  VMA_MEMORY_USAGE_CPU_TO_GPU);

  GLTFMetallic_Roughness::MaterialConstants* scene_uniform_data =
    (GLTFMetallic_Roughness::MaterialConstants*)
      material_constants.allocation->GetMappedData();
  scene_uniform_data->color_factors       = glm::vec4{ 1, 1, 1, 1 };
  scene_uniform_data->metal_rough_factors = glm::vec4{ 1, 0.5, 0, 0 };

  m_main_deletion_queue.push_function(
    [=, this]() { destroy_buffer(material_constants); });

  material_resources.data_buffer        = material_constants.buffer;
  material_resources.data_buffer_offset = 0;

  default_data =
    metal_rough_material.write_material(m_device,
                                        MaterialPass::MAIN_COLOR,
                                        material_resources,
                                        m_global_descriptor_allocator);

  for (auto& m : test_meshes) {
    std::shared_ptr<MeshNode> new_node = std::make_shared<MeshNode>();
    new_node->mesh                     = m;
    new_node->local_transform          = glm::mat4{ 1.0f };
    new_node->world_transform          = glm::mat4{ 1.0f };

    for (auto& s : new_node->mesh->surfaces) {
      s.material = std::make_shared<GLTFMaterial>(default_data);
    }

    loaded_nodes[m->name] = std::move(new_node);
  }

  m_main_deletion_queue.push_function([&]() {
    destroy_image(m_white_image);
    destroy_image(m_grey_image);
    destroy_image(m_black_image);
    destroy_image(m_checkboard_image);
    vkDestroySampler(m_device, m_default_sampler_nearest, nullptr);
    vkDestroySampler(m_device, m_default_sampler_linear, nullptr);
  });
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

AllocatedImage
VulkanEngine::create_image(VkExtent3D        size,
                           VkFormat          format,
                           VkImageUsageFlags usage,
                           bool              mipmapped)
{
  AllocatedImage new_image;
  new_image.image_format = format;
  new_image.image_extent = size;

  VkImageCreateInfo image_info =
    vk_init::image_create_info(format, usage, size);

  if (mipmapped) {
    image_info.mipLevels = static_cast<uint32_t>(std::floor(
                             std::log2(std::max(size.width, size.height)))) +
                           1;
  }

  VmaAllocationCreateInfo alloc_info = {};
  alloc_info.usage                   = VMA_MEMORY_USAGE_GPU_ONLY;
  alloc_info.requiredFlags =
    VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  VK_CHECK(vmaCreateImage(m_allocator,
                          &image_info,
                          &alloc_info,
                          &new_image.image,
                          &new_image.allocation,
                          nullptr));

  VkImageAspectFlags aspect_flags = VK_IMAGE_ASPECT_COLOR_BIT;
  if (format == VK_FORMAT_D32_SFLOAT) {
    aspect_flags = VK_IMAGE_ASPECT_DEPTH_BIT;
  }

  VkImageViewCreateInfo view_info =
    vk_init::image_view_create_info(format, new_image.image, aspect_flags);
  view_info.subresourceRange.levelCount = image_info.mipLevels;

  VK_CHECK(
    vkCreateImageView(m_device, &view_info, nullptr, &new_image.image_view));

  return new_image;
}

AllocatedImage
VulkanEngine::create_image(void*             data,
                           VkExtent3D        size,
                           VkFormat          format,
                           VkImageUsageFlags usage,
                           bool              mipmapped)
{
  size_t          data_size     = size.depth * size.width * size.height * 4;
  AllocatedBuffer upload_buffer = create_buffer(
    data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

  memcpy(upload_buffer.info.pMappedData, data, data_size);

  AllocatedImage new_image = create_image(
    size,
    format,
    usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
    mipmapped);

  immediate_submit([&](VkCommandBuffer cmd) {
    vk_util::transition_image(cmd,
                              new_image.image,
                              VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy copy_region               = {};
    copy_region.bufferOffset                    = 0;
    copy_region.bufferRowLength                 = 0;
    copy_region.bufferImageHeight               = 0;
    copy_region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.mipLevel       = 0;
    copy_region.imageSubresource.baseArrayLayer = 0;
    copy_region.imageSubresource.layerCount     = 1;
    copy_region.imageExtent                     = size;

    vkCmdCopyBufferToImage(cmd,
                           upload_buffer.buffer,
                           new_image.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1,
                           &copy_region);

    if (mipmapped) {
      vk_init::generate_mipmaps(cmd,
                                new_image.image,
                                VkExtent2D{ new_image.image_extent.width,
                                            new_image.image_extent.height });
    } else {
      vk_util::transition_image(cmd,
                                new_image.image,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
  });

  destroy_buffer(upload_buffer);
  return new_image;
}

void
VulkanEngine::destroy_image(const AllocatedImage& image)
{
  vkDestroyImageView(m_device, image.image_view, nullptr);
  vmaDestroyImage(m_allocator, image.image, image.allocation);
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
VulkanEngine::resize_swapchain()
{
  vkDeviceWaitIdle(m_device);

  destroy_swapchain();

  int width, height;
  SDL_GetWindowSize(m_window, &width, &height);
  m_window_extent.width  = width;
  m_window_extent.height = height;

  create_swapchain(m_window_extent.width, m_window_extent.height);

  resize_requested = false;
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
VulkanEngine::update_scene()
{
  auto start = std::chrono::system_clock::now();

  m_main_draw_context.opaque_surfaces.clear();
  m_main_draw_context.transparent_surfaces.clear();

  m_main_camera.update();

  m_loaded_scenes["structure"]->Draw(glm::mat4{ 1.0f }, m_main_draw_context);

  /*   loaded_nodes["Suzanne"]->Draw(glm::mat4{ 1.0f }, m_main_draw_context);

    for (int x = -3; x < 3; x++) {
      glm::mat4 scale       = glm::scale(glm::vec3{ 0.2 });
      glm::mat4 translation = glm::translate(glm::vec3{ x, 1, 0 });

      loaded_nodes["Cube"]->Draw(translation * scale, m_main_draw_context);
    }
   */
  glm::mat4 view  = m_main_camera.get_view_matrix();
  glm::mat4 proj  = glm::perspective(glm::radians(70.0f),
                                    (float)m_window_extent.width /
                                      (float)m_window_extent.height,
                                    1000.0f,
                                    0.1f);
  proj[1][1]     *= -1;

  scene_data.view         = view;
  scene_data.proj         = proj;
  scene_data.view_proj    = proj * view;
  scene_data.ambient_col  = glm::vec4{ 0.1f };
  scene_data.sunlight_col = glm::vec4(1.0f);
  scene_data.sunlight_dir = glm::vec4(0, 1, 0.5, 1.);

  auto end = std::chrono::system_clock::now();
  auto elapsed =
    std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  m_stats.scene_update_time = elapsed.count() / 1000.f;
}

void
VulkanEngine::run()
{
  SDL_Event e;
  bool      bQuit = false;

  while (!bQuit) {
    auto start = std::chrono::system_clock::now();

    while (SDL_PollEvent(&e) != 0) {
      if (e.type == SDL_QUIT) {
        bQuit = true;
      }

      if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        bQuit = true;
        break;
      }

      m_main_camera.process_SDL_events(e);

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

    if (resize_requested) {
      resize_swapchain();
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame(m_window);
    ImGui::NewFrame();

    ImGui::Begin("Stats");

    ImGui::Text("Frame  Time : %f ms", m_stats.frame_time);
    ImGui::Text("Draw   Time : %f ms", m_stats.mesh_draw_time);
    ImGui::Text("Update Time : %f ms", m_stats.scene_update_time);
    ImGui::Text("Triangles   : %i", m_stats.triangle_count);
    ImGui::Text("Draws       : %i", m_stats.drawcall_count);

    ImGui::End();

    if (ImGui::Begin("background")) {
      ImGui::SliderFloat("Render Scale", &render_scale, 0.3f, 1.0f);

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

    auto end = std::chrono::system_clock::now();

    auto elapsed =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    m_stats.frame_time = elapsed.count() / 1000.f;
  }
}

VulkanEngine&
VulkanEngine::Get()
{
  return *loaded_engine;
}

void
GLTFMetallic_Roughness::build_pipeline(VulkanEngine* engine)
{
  Shader mesh_vert_shader;
  Shader mesh_frag_shader;

  mesh_vert_shader.init(engine->m_device, "shaders/mesh.vert");
  mesh_frag_shader.init(engine->m_device, "shaders/mesh.frag");

  VkPushConstantRange matrix_range = {};
  matrix_range.offset              = 0;
  matrix_range.size                = sizeof(GPUDrawPushConstants);
  matrix_range.stageFlags          = VK_SHADER_STAGE_VERTEX_BIT;

  DescriptorLayoutBuilder layout_builder;
  layout_builder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
  layout_builder.add_binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
  layout_builder.add_binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

  material_layout = layout_builder.build(engine->m_device,
                                         VK_SHADER_STAGE_VERTEX_BIT |
                                           VK_SHADER_STAGE_FRAGMENT_BIT);

  VkDescriptorSetLayout layouts[] = {
    engine->m_gpu_scene_data_descriptor_layout, material_layout
  };

  VkPipelineLayoutCreateInfo mesh_layout_info =
    vk_init::pipeline_layout_create_info();
  mesh_layout_info.setLayoutCount         = 2;
  mesh_layout_info.pSetLayouts            = layouts;
  mesh_layout_info.pPushConstantRanges    = &matrix_range;
  mesh_layout_info.pushConstantRangeCount = 1;

  VkPipelineLayout new_layout;
  VK_CHECK(vkCreatePipelineLayout(
    engine->m_device, &mesh_layout_info, nullptr, &new_layout));

  opaque_pipeline.layout      = new_layout;
  transparent_pipeline.layout = new_layout;

  PipelineBuilder pipeline_builder;
  pipeline_builder.set_shaders(mesh_vert_shader.get(), mesh_frag_shader.get());
  pipeline_builder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
  pipeline_builder.set_polygon_mode(VK_POLYGON_MODE_FILL);
  pipeline_builder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
  pipeline_builder.set_multisampling_none();
  pipeline_builder.disable_blending();
  pipeline_builder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

  pipeline_builder.set_color_attachment_format(
    engine->m_draw_image.image_format);
  pipeline_builder.set_depth_format(engine->m_depth_image.image_format);

  pipeline_builder.pipeline_layout = new_layout;

  opaque_pipeline.pipeline = pipeline_builder.build_pipeline(engine->m_device);

  pipeline_builder.enable_blending_additive();
  pipeline_builder.enable_depthtest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);

  transparent_pipeline.pipeline =
    pipeline_builder.build_pipeline(engine->m_device);

  mesh_vert_shader.destroy();
  mesh_frag_shader.destroy();
}

MaterialInstance
GLTFMetallic_Roughness::write_material(
  VkDevice                     device,
  MaterialPass                 pass,
  const MaterialResources&     resources,
  DescriptorAllocatorGrowable& descriptor_allocator)
{
  MaterialInstance mat_data;
  mat_data.pass_type = pass;

  if (pass == MaterialPass::TRANSPARENT) {
    mat_data.pipeline = &transparent_pipeline;
  } else {
    mat_data.pipeline = &opaque_pipeline;
  }

  mat_data.material_set =
    descriptor_allocator.allocate(device, material_layout);

  writer.clear();
  writer.write_buffer(0,
                      resources.data_buffer,
                      sizeof(MaterialConstants),
                      resources.data_buffer_offset,
                      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
  writer.write_image(1,
                     resources.color_image.image_view,
                     resources.color_sampler,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
  writer.write_image(2,
                     resources.metal_rough_image.image_view,
                     resources.metal_rough_sampler,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
  writer.update_set(device, mat_data.material_set);

  return mat_data;
}

void
MeshNode::Draw(const glm::mat4& top_matrix, DrawContext& ctx)
{
  glm::mat4 node_matrix = top_matrix * world_transform;

  for (auto& s : mesh->surfaces) {
    RenderObject def;
    def.index_count           = s.count;
    def.first_index           = s.start_index;
    def.index_buffer          = mesh->mesh_buffers.index_buffer.buffer;
    def.material              = &s.material->data;
    def.bounds                = s.bounds;
    def.transform             = node_matrix;
    def.vertex_buffer_address = mesh->mesh_buffers.vertex_buffer_address;

    switch (s.material->data.pass_type) {
      case MaterialPass::TRANSPARENT:
        ctx.transparent_surfaces.push_back(def);
        break;
      case MaterialPass::MAIN_COLOR:
      default:
        ctx.opaque_surfaces.push_back(def);
        break;
    }
  }

  Node::Draw(top_matrix, ctx);
}
