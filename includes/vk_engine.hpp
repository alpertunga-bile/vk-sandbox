#pragma once

#include "SDL2/SDL.h"
#undef main

#include "vk_camera.hpp"
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

  DeletionQueue               deletion_queue;
  DescriptorAllocatorGrowable m_frame_descriptors;
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

struct GPUSceneData
{
  glm::mat4 view;
  glm::mat4 proj;
  glm::mat4 view_proj;
  glm::vec4 ambient_col;
  glm::vec4 sunlight_dir;
  glm::vec4 sunlight_col;
};

struct GLTFMetallic_Roughness
{
  MaterialPipeline opaque_pipeline;
  MaterialPipeline transparent_pipeline;

  VkDescriptorSetLayout material_layout;

  struct MaterialConstants
  {
    glm::vec4 color_factors;
    glm::vec4 metal_rough_factors;
    glm::vec4 pad[14];
  };

  struct MaterialResources
  {
    AllocatedImage color_image;
    VkSampler      color_sampler;
    AllocatedImage metal_rough_image;
    VkSampler      metal_rough_sampler;
    VkBuffer       data_buffer;
    uint32_t       data_buffer_offset;
  };

  DescriptorWriter writer;

  void build_pipeline(VulkanEngine* engine);
  void clear_resources(VkDevice device);

  MaterialInstance write_material(
    VkDevice                     device,
    MaterialPass                 pass,
    const MaterialResources&     resources,
    DescriptorAllocatorGrowable& descriptor_allocator);
};

struct MeshNode : public Node
{
  std::shared_ptr<MeshAsset> mesh;

  virtual void Draw(const glm::mat4& top_matrix, DrawContext& ctx) override;
};

struct EngineStats
{
  float frame_time;
  int   triangle_count;
  int   drawcall_count;
  float scene_update_time;
  float mesh_draw_time;
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

  AllocatedBuffer create_buffer(size_t             alloc_size,
                                VkBufferUsageFlags usage_flags,
                                VmaMemoryUsage     memory_usage);
  AllocatedImage  create_image(VkExtent3D        size,
                               VkFormat          format,
                               VkImageUsageFlags usage,
                               bool              mipmapped = false);
  AllocatedImage  create_image(void*             data,
                               VkExtent3D        size,
                               VkFormat          format,
                               VkImageUsageFlags usage,
                               bool              mipmapped = false);

  void destroy_buffer(const AllocatedBuffer& buffer);
  void destroy_image(const AllocatedImage& image);

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
  void init_mesh_pipeline();
  void init_default_data();
  void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& func);
  void init_imgui();
  void create_swapchain(uint32_t width, uint32_t height);
  void resize_swapchain();
  void destroy_swapchain();

  FrameData& get_current_frame()
  {
    return m_frames[m_frame_number % FRAME_OVERLAP];
  }

  void update_scene();

private:
  VkInstance               m_instance;
  VkDebugUtilsMessengerEXT m_debug_messenger;
  VkPhysicalDevice         m_physical_device;
  VkSurfaceKHR             m_surface;

  VkSwapchainKHR           m_swapchain;
  VkFormat                 m_swapchain_image_format;
  std::vector<VkImage>     m_swapchain_images;
  std::vector<VkImageView> m_swapchain_image_views;
  VkExtent2D               m_swapchain_extent;
  bool                     resize_requested = false;

  FrameData m_frames[FRAME_OVERLAP];
  VkQueue   m_graphics_queue;
  uint32_t  m_graphics_queue_family;

  DeletionQueue m_main_deletion_queue;

  VmaAllocator m_allocator;

  VkExtent2D m_draw_extent;
  float      render_scale = 1.0f;

  DescriptorAllocatorGrowable m_global_descriptor_allocator;
  VkDescriptorSet             m_draw_image_descriptors;
  VkDescriptorSetLayout       m_draw_image_descriptor_layout;

  VkPipeline       m_gradient_pipeline;
  VkPipelineLayout m_gradient_pipeline_layout;

  VkFence         m_imm_fence;
  VkCommandBuffer m_imm_command_buffer;
  VkCommandPool   m_imm_command_pool;

  std::vector<ComputeEffect> background_effects;
  int                        current_background_effect{ 0 };

  VkPipelineLayout m_mesh_pipeline_layout;
  VkPipeline       m_mesh_pipeline;

  std::vector<std::shared_ptr<MeshAsset>> test_meshes;

  GPUSceneData scene_data;

  VkDescriptorSetLayout m_single_image_descriptor_layout;

  MaterialInstance default_data;

  DrawContext                                            m_main_draw_context;
  std::unordered_map<std::string, std::shared_ptr<Node>> loaded_nodes;

  Camera m_main_camera;

  std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> m_loaded_scenes;

  EngineStats m_stats;

public:
  bool       m_is_initialized{ false };
  int        m_frame_number{ 0 };
  bool       m_stop_rendering{ false };
  VkExtent2D m_window_extent{ 1700, 900 };

  struct SDL_Window* m_window{ nullptr };

  VkDevice              m_device;
  VkDescriptorSetLayout m_gpu_scene_data_descriptor_layout;
  AllocatedImage        m_draw_image;
  AllocatedImage        m_depth_image;

  AllocatedImage m_white_image;
  AllocatedImage m_black_image;
  AllocatedImage m_grey_image;
  AllocatedImage m_checkboard_image;

  VkSampler m_default_sampler_linear;
  VkSampler m_default_sampler_nearest;

  GLTFMetallic_Roughness metal_rough_material;
};