#pragma once

#include "vk_types.hpp"
#include <filesystem>
#include <unordered_map>

struct GeomSurface
{
  uint32_t start_index;
  uint32_t count;
};

struct MeshAsset
{
  std::string              name;
  std::vector<GeomSurface> surfaces;
  GPUMeshBuffers           mesh_buffers;
};

class VulkanEngine;

std::optional<std::vector<std::shared_ptr<MeshAsset>>>
load_gltf_meshes(VulkanEngine* engine, std::filesystem::path filepath);