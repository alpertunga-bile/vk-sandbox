#pragma once

#include "SDL2/SDL.h"
#undef main

#include "vk_descriptors.hpp"
#include "vk_loader.hpp"
#include "vk_pipelines.hpp"

struct DeletionQueue
{
  std::deque<std::function<void()>> deleters;

  void push_function(std::function<void()>&& func) { deleters.push_back(func); }

  void flush()
  {
    for (auto it = deleters.rbegin(); it != deleters.rend(); it++) {
      (*it)();
    }

    deleters.clear();
  }
};

struct FrameData
{
  VkCommandPool   command_pool;
  VkCommandBuffer main_command_buffer;

  VkSemaphore swapchain_semaphore, render_semaphore;
  VkFence     render_fence;

  DeletionQueue deletion_queue;
};

struct ComputePushConstants
{
  glm::vec4 data_1;
  glm::vec4 data_2;
  glm::vec4 data_3;
  glm::vec4 data_4;
};

struct ComputeEffect
{
  const char* name;

  VkPipeline       pipeline;
  VkPipelineLayout pipeline_layout;

  ComputePushConstants data;
};

constexpr unsigned int FRAME_OVERLAP = 2;

class VulkanEngine
{
public:
  void init();
  void cleanup();
  void run();

  static VulkanEngine& Get();

  GPUMeshBuffers upload_mesh(std::span<uint32_t> indices,
                             std::span<Vertex>   vertices);

private:
  void draw();
  void draw_background(VkCommandBuffer cmd);
  void draw_geometry(VkCommandBuffer cmd);
  void draw_imgui(VkCommandBuffer cmd, VkImageView target_image_view);

  void init_vulkan();
  void init_swapchain();
  void init_commands();
  void init_sync_structures();
  void init_descriptors();
  void init_pipelines();
  void init_background_pipelines();
  void init_triangle_pipeline();
  void init_mesh_pipeline();
  void init_default_data();
  void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& func);
  void init_imgui();
  AllocatedBuffer create_buffer(size_t             alloc_size,
                                VkBufferUsageFlags usage_flags,
                                VmaMemoryUsage     memory_usage);
  void            create_swapchain(uint32_t width, uint32_t height);
  void            destroy_swapchain();
  void            destroy_buffer(const AllocatedBuffer& buffer);

  FrameData& get_current_frame()
  {
    return m_frames[m_frame_number % FRAME_OVERLAP];
  }

private:
  VkInstance               m_instance;
  VkDebugUtilsMessengerEXT m_debug_messenger;
  VkPhysicalDevice         m_physical_device;
  VkDevice                 m_device;
  VkSurfaceKHR             m_surface;

  VkSwapchainKHR           m_swapchain;
  VkFormat                 m_swapchain_image_format;
  std::vector<VkImage>     m_swapchain_images;
  std::vector<VkImageView> m_swapchain_image_views;
  VkExtent2D               m_swapchain_extent;

  FrameData m_frames[FRAME_OVERLAP];
  VkQueue   m_graphics_queue;
  uint32_t  m_graphics_queue_family;

  DeletionQueue m_main_deletion_queue;

  VmaAllocator m_allocator;

  AllocatedImage m_draw_image;
  AllocatedImage m_depth_image;
  VkExtent2D     m_draw_extent;

  DescriptorAllocator   m_global_descriptor_allocator;
  VkDescriptorSet       m_draw_image_descriptors;
  VkDescriptorSetLayout m_draw_image_descriptor_layout;

  VkPipeline       m_gradient_pipeline;
  VkPipelineLayout m_gradient_pipeline_layout;

  VkFence         m_imm_fence;
  VkCommandBuffer m_imm_command_buffer;
  VkCommandPool   m_imm_command_pool;

  std::vector<ComputeEffect> background_effects;
  int                        current_background_effect{ 0 };

  VkPipelineLayout m_triangle_pipeline_layout;
  VkPipeline       m_triangle_pipeline;

  VkPipelineLayout m_mesh_pipeline_layout;
  VkPipeline       m_mesh_pipeline;
  GPUMeshBuffers   rectangle;

  std::vector<std::shared_ptr<MeshAsset>> test_meshes;

public:
  bool       m_is_initialized{ false };
  int        m_frame_number{ 0 };
  bool       m_stop_rendering{ false };
  VkExtent2D m_window_extent{ 1700, 900 };

  struct SDL_Window* m_window{ nullptr };
};