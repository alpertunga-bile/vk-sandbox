#pragma once

#include "vk_descriptors.hpp"
#include <filesystem>
#include <unordered_map>

struct GLTFMaterial
{
  MaterialInstance data;
};

struct GeomSurface
{
  uint32_t                      start_index;
  uint32_t                      count;
  Bounds                        bounds;
  std::shared_ptr<GLTFMaterial> material;
};

struct MeshAsset
{
  std::string              name;
  std::vector<GeomSurface> surfaces;
  GPUMeshBuffers           mesh_buffers;
};

class VulkanEngine;

class LoadedGLTF : public IRenderable
{
public:
  ~LoadedGLTF() { clear_all(); }

  virtual void Draw(const glm::mat4& top_matrix, DrawContext& ctx);

private:
  void clear_all();

public:
  std::unordered_map<std::string, std::shared_ptr<MeshAsset>>    meshes;
  std::unordered_map<std::string, std::shared_ptr<Node>>         nodes;
  std::unordered_map<std::string, AllocatedImage>                images;
  std::unordered_map<std::string, std::shared_ptr<GLTFMaterial>> materials;

  std::vector<std::shared_ptr<Node>> top_nodes;
  std::vector<VkSampler>             samplers;

  DescriptorAllocatorGrowable descriptor_pool;
  AllocatedBuffer             material_data_buffer;

  VulkanEngine* creator;
};

std::optional<std::shared_ptr<LoadedGLTF>>
load_gltf(VulkanEngine* engine, std::string_view filepath);

std::optional<std::vector<std::shared_ptr<MeshAsset>>>
load_gltf_meshes(VulkanEngine* engine, std::filesystem::path filepath);