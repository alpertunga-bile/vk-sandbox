#include "vk_loader.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <iostream>

#include "glm/gtx/quaternion.hpp"
#include "vk_engine.hpp"

#include "fastgltf/glm_element_traits.hpp"
#include "fastgltf/parser.hpp"
#include "fastgltf/tools.hpp"

VkFilter
extract_filter(fastgltf::Filter filter)
{
  switch (filter) {
    case fastgltf::Filter::Nearest:
    case fastgltf::Filter::NearestMipMapNearest:
    case fastgltf::Filter::NearestMipMapLinear:
      return VK_FILTER_NEAREST;
    case fastgltf::Filter::Linear:
    case fastgltf::Filter::LinearMipMapNearest:
    case fastgltf::Filter::LinearMipMapLinear:
    default:
      return VK_FILTER_LINEAR;
  }
}

VkSamplerMipmapMode
extract_mipmap_mode(fastgltf::Filter filter)
{
  switch (filter) {
    case fastgltf::Filter::NearestMipMapNearest:
    case fastgltf::Filter::LinearMipMapNearest:
      return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    case fastgltf::Filter::NearestMipMapLinear:
    case fastgltf::Filter::LinearMipMapLinear:
    default:
      return VK_SAMPLER_MIPMAP_MODE_LINEAR;
  }
}

std::optional<AllocatedImage>
load_image(VulkanEngine* engine, fastgltf::Asset& asset, fastgltf::Image& image)
{
  AllocatedImage new_image{};
  int            width, height, nr_channels;

  std::visit(fastgltf::visitor{
               [](auto& arg) {},
               [&](fastgltf::sources::URI& filepath) {
                 assert(filepath.fileByteOffset == 0);
                 assert(filepath.uri.isLocalPath());

                 const std::string path(filepath.uri.path().begin(),
                                        filepath.uri.path().end());
                 unsigned char*    data =
                   stbi_load(path.c_str(), &width, &height, &nr_channels, 4);

                 if (data) {
                   VkExtent3D image_size;
                   image_size.width  = width;
                   image_size.height = height;
                   image_size.depth  = 1;

                   new_image = engine->create_image(data,
                                                    image_size,
                                                    VK_FORMAT_R8G8B8A8_UNORM,
                                                    VK_IMAGE_USAGE_SAMPLED_BIT,
                                                    true);

                   stbi_image_free(data);
                 }
               },
               [&](fastgltf::sources::Vector& vector) {
                 unsigned char* data =
                   stbi_load_from_memory(vector.bytes.data(),
                                         static_cast<int>(vector.bytes.size()),
                                         &width,
                                         &height,
                                         &nr_channels,
                                         4);

                 if (data) {
                   VkExtent3D image_size;
                   image_size.width  = width;
                   image_size.height = height;
                   image_size.depth  = 1;

                   new_image = engine->create_image(data,
                                                    image_size,
                                                    VK_FORMAT_R8G8B8A8_UNORM,
                                                    VK_IMAGE_USAGE_SAMPLED_BIT,
                                                    true);

                   stbi_image_free(data);
                 }
               },
               [&](fastgltf::sources::BufferView& view) {
                 auto& buffer_view = asset.bufferViews[view.bufferViewIndex];
                 auto& buffer      = asset.buffers[buffer_view.bufferIndex];

                 std::visit(fastgltf::visitor{
                              [](auto& arg) {},
                              [&](fastgltf::sources::Vector& vector) {
                                unsigned char* data = stbi_load_from_memory(
                                  vector.bytes.data() + buffer_view.byteOffset,
                                  static_cast<int>(buffer_view.byteLength),
                                  &width,
                                  &height,
                                  &nr_channels,
                                  4);

                                if (data) {
                                  VkExtent3D image_size;
                                  image_size.width  = width;
                                  image_size.height = height;
                                  image_size.depth  = 1;

                                  new_image = engine->create_image(
                                    data,
                                    image_size,
                                    VK_FORMAT_R8G8B8A8_UNORM,
                                    VK_IMAGE_USAGE_SAMPLED_BIT,
                                    true);

                                  stbi_image_free(data);
                                }
                              } },
                            buffer.data);
               },
             },
             image.data);

  if (new_image.image == VK_NULL_HANDLE) {
    return {};
  } else {
    return new_image;
  }
}

std::optional<std::shared_ptr<LoadedGLTF>>
load_gltf(VulkanEngine* engine, std::string_view filepath)
{
  LOG_INFO(std::format("Loading {} GLTF scene", filepath.data()).c_str());

  std::shared_ptr<LoadedGLTF> scene = std::make_shared<LoadedGLTF>();
  scene->creator                    = engine;
  LoadedGLTF& file                  = *scene.get();

  fastgltf::Parser parser{};

  constexpr auto gltf_options = fastgltf::Options::DontRequireValidAssetMember |
                                fastgltf::Options::AllowDouble |
                                fastgltf::Options::LoadExternalBuffers;

  fastgltf::GltfDataBuffer data;
  data.loadFromFile(filepath);

  fastgltf::Asset       gltf;
  std::filesystem::path path = filepath;

  auto type = fastgltf::determineGltfFileType(&data);

  switch (type) {
    case fastgltf::GltfType::glTF: {
      auto load = parser.loadGltf(&data, path.parent_path(), gltf_options);
      if (load) {
        gltf = std::move(load.get());
      } else {
        LOG_WARNING(std::format("Failed to load {}",
                                fastgltf::to_underlying(load.error()))
                      .c_str());
        return {};
      }
    } break;
    case fastgltf::GltfType::GLB: {
      auto load =
        parser.loadGltfBinary(&data, path.parent_path(), gltf_options);
      if (load) {
        gltf = std::move(load.get());
      } else {
        LOG_WARNING(std::format("Failed to load {}",
                                fastgltf::to_underlying(load.error()))
                      .c_str());
        return {};
      }
    } break;
    default:
      LOG_WARNING("Failed to determine glTF container");
      return {};
      break;
  }

  std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes = {
    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3},
    {        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
    {        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}
  };

  file.descriptor_pool.init(engine->m_device, gltf.materials.size(), sizes);

  for (fastgltf::Sampler& sampler : gltf.samplers) {
    VkSamplerCreateInfo sampl = { .sType =
                                    VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                  .pNext = nullptr };
    sampl.maxLod              = VK_LOD_CLAMP_NONE;
    sampl.minLod              = 0;
    sampl.magFilter =
      extract_filter(sampler.magFilter.value_or(fastgltf::Filter::Nearest));
    sampl.minFilter =
      extract_filter(sampler.minFilter.value_or(fastgltf::Filter::Nearest));
    sampl.mipmapMode = extract_mipmap_mode(
      sampler.minFilter.value_or(fastgltf::Filter::Nearest));

    VkSampler new_sampler;
    vkCreateSampler(engine->m_device, &sampl, nullptr, &new_sampler);

    file.samplers.push_back(new_sampler);
  }

  std::vector<std::shared_ptr<MeshAsset>>    meshes;
  std::vector<std::shared_ptr<Node>>         nodes;
  std::vector<AllocatedImage>                images;
  std::vector<std::shared_ptr<GLTFMaterial>> materials;

  for (fastgltf::Image& image : gltf.images) {

    images.push_back(engine->m_checkboard_image);
  }

  file.material_data_buffer = engine->create_buffer(
    sizeof(GLTFMetallic_Roughness::MaterialConstants) * gltf.materials.size(),
    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
    VMA_MEMORY_USAGE_CPU_TO_GPU);

  int data_index = 0;

  GLTFMetallic_Roughness::MaterialConstants* scene_material_constants =
    (GLTFMetallic_Roughness::MaterialConstants*)
      file.material_data_buffer.info.pMappedData;

  for (fastgltf::Material& mat : gltf.materials) {
    std::shared_ptr<GLTFMaterial> new_mat = std::make_shared<GLTFMaterial>();
    materials.push_back(new_mat);
    file.materials[mat.name.c_str()] = new_mat;

    GLTFMetallic_Roughness::MaterialConstants constants;
    constants.color_factors.x       = mat.pbrData.baseColorFactor[0];
    constants.color_factors.y       = mat.pbrData.baseColorFactor[1];
    constants.color_factors.z       = mat.pbrData.baseColorFactor[2];
    constants.color_factors.w       = mat.pbrData.baseColorFactor[3];
    constants.metal_rough_factors.x = mat.pbrData.metallicFactor;
    constants.metal_rough_factors.y = mat.pbrData.roughnessFactor;

    scene_material_constants[data_index] = constants;

    MaterialPass pass_type = MaterialPass::MAIN_COLOR;
    if (mat.alphaMode == fastgltf::AlphaMode::Blend) {
      pass_type = MaterialPass::TRANSPARENT;
    }

    GLTFMetallic_Roughness::MaterialResources material_resources;
    material_resources.color_image         = engine->m_white_image;
    material_resources.color_sampler       = engine->m_default_sampler_linear;
    material_resources.metal_rough_image   = engine->m_white_image;
    material_resources.metal_rough_sampler = engine->m_default_sampler_linear;
    material_resources.data_buffer         = file.material_data_buffer.buffer;
    material_resources.data_buffer_offset =
      data_index * sizeof(GLTFMetallic_Roughness::MaterialConstants);

    if (mat.pbrData.baseColorTexture.has_value()) {
      size_t img =
        gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex]
          .imageIndex.value();
      size_t sampler =
        gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex]
          .samplerIndex.value();

      material_resources.color_image   = images[img];
      material_resources.color_sampler = file.samplers[sampler];
    }
    new_mat->data = engine->metal_rough_material.write_material(
      engine->m_device, pass_type, material_resources, file.descriptor_pool);

    data_index++;
  }

  std::vector<uint32_t> indices;
  std::vector<Vertex>   vertices;

  for (fastgltf::Mesh& mesh : gltf.meshes) {
    std::shared_ptr<MeshAsset> new_mesh = std::make_shared<MeshAsset>();
    meshes.push_back(new_mesh);
    file.meshes[mesh.name.c_str()] = new_mesh;
    new_mesh->name                 = mesh.name;

    indices.clear();
    vertices.clear();

    for (auto&& p : mesh.primitives) {
      GeomSurface new_surface;
      new_surface.start_index = (uint32_t)indices.size();
      new_surface.count =
        (uint32_t)gltf.accessors[p.indicesAccessor.value()].count;

      size_t initial_vertex = vertices.size();

      {
        fastgltf::Accessor& index_accessor =
          gltf.accessors[p.indicesAccessor.value()];
        indices.reserve(indices.size() + index_accessor.count);

        fastgltf::iterateAccessor<std::uint32_t>(
          gltf, index_accessor, [&](std::uint32_t idx) {
            indices.push_back(idx + initial_vertex);
          });
      }

      {
        fastgltf::Accessor& pos_accessor =
          gltf.accessors[p.findAttribute("POSITION")->second];
        vertices.resize(vertices.size() + pos_accessor.count);

        fastgltf::iterateAccessorWithIndex<glm::vec3>(
          gltf, pos_accessor, [&](glm::vec3 v, size_t index) {
            Vertex new_vert;
            new_vert.position = v;
            new_vert.normal   = { 1, 0, 0 };
            new_vert.color    = glm::vec4{ 1.0f };
            new_vert.uv_x     = 0;
            new_vert.uv_y     = 0;

            vertices[initial_vertex + index] = new_vert;
          });
      }

      auto normals = p.findAttribute("NORMAL");
      if (normals != p.attributes.end()) {
        fastgltf::iterateAccessorWithIndex<glm::vec3>(
          gltf,
          gltf.accessors[(*normals).second],
          [&](glm::vec3 v, size_t index) {
            vertices[initial_vertex + index].normal = v;
          });
      }

      auto uv = p.findAttribute("TEXCOORD_0");
      if (uv != p.attributes.end()) {
        fastgltf::iterateAccessorWithIndex<glm::vec2>(
          gltf, gltf.accessors[(*uv).second], [&](glm::vec2 v, size_t index) {
            vertices[initial_vertex + index].uv_x = v.x;
            vertices[initial_vertex + index].uv_y = v.y;
          });
      }

      auto colors = p.findAttribute("COLOR_0");
      if (colors != p.attributes.end()) {
        fastgltf::iterateAccessorWithIndex<glm::vec4>(
          gltf,
          gltf.accessors[(*colors).second],
          [&](glm::vec4 v, size_t index) {
            vertices[initial_vertex + index].color = v;
          });
      }

      if (p.materialIndex.has_value()) {
        new_surface.material = materials[p.materialIndex.value()];
      } else {
        new_surface.material = materials[0];
      }

      glm::vec3 min_pos = vertices[initial_vertex].position;
      glm::vec3 max_pos = vertices[initial_vertex].position;

      for (int i = initial_vertex; i < vertices.size(); i++) {
        min_pos = glm::min(min_pos, vertices[i].position);
        max_pos = glm::max(max_pos, vertices[i].position);
      }

      new_surface.bounds.origin  = (max_pos + min_pos) / 2.f;
      new_surface.bounds.extents = (max_pos - min_pos) / 2.f;
      new_surface.bounds.sphere_radius =
        glm::length(new_surface.bounds.extents);

      new_mesh->surfaces.push_back(new_surface);
    }

    new_mesh->mesh_buffers = engine->upload_mesh(indices, vertices);
  }

  for (fastgltf::Node& node : gltf.nodes) {
    std::shared_ptr<Node> new_node;

    if (node.meshIndex.has_value()) {
      new_node = std::make_shared<MeshNode>();
      static_cast<MeshNode*>(new_node.get())->mesh = meshes[*node.meshIndex];
    } else {
      new_node = std::make_shared<Node>();
    }

    nodes.push_back(new_node);
    file.nodes[node.name.c_str()];

    std::visit(
      fastgltf::visitor{
        [&](fastgltf::Node::TransformMatrix matrix) {
          memcpy(&new_node->local_transform, matrix.data(), sizeof(matrix));
        },
        [&](fastgltf::TRS transform) {
          glm::vec3 tl(transform.translation[0],
                       transform.translation[1],
                       transform.translation[2]);
          glm::quat rot(transform.rotation[3],
                        transform.rotation[0],
                        transform.rotation[1],
                        transform.rotation[2]);
          glm::vec3 sc(
            transform.scale[0], transform.scale[1], transform.scale[2]);

          glm::mat4 tm = glm::translate(glm::mat4(1.f), tl);
          glm::mat4 rm = glm::toMat4(rot);
          glm::mat4 sm = glm::scale(glm::mat4(1.0f), sc);

          new_node->local_transform = tm * rm * sm;
        } },
      node.transform);
  }

  for (int i = 0; i < gltf.nodes.size(); i++) {
    fastgltf::Node&        node       = gltf.nodes[i];
    std::shared_ptr<Node>& scene_node = nodes[i];

    for (auto& c : node.children) {
      scene_node->children.push_back(nodes[c]);
      nodes[c]->parent = scene_node;
    }
  }

  for (auto& node : nodes) {
    if (node->parent.lock() == nullptr) {
      file.top_nodes.push_back(node);
      node->refresh_transform(glm::mat4{ 1.0f });
    }
  }

  for (fastgltf::Image& image : gltf.images) {
    std::optional<AllocatedImage> img = load_image(engine, gltf, image);

    if (img.has_value()) {
      images.push_back(*img);
      file.images[image.name.c_str()] = *img;
    } else {
      images.push_back(engine->m_checkboard_image);
      LOG_WARNING(
        std::format("glTF failed to load {} texture", image.name).c_str());
    }
  }

  return scene;
}

std::optional<std::vector<std::shared_ptr<MeshAsset>>>
load_gltf_meshes(VulkanEngine* engine, std::filesystem::path filepath)
{
  LOG_INFO(std::format("Loading {} GLTF scene", filepath.string()).c_str());

  fastgltf::GltfDataBuffer data;
  data.loadFromFile(filepath);

  constexpr auto gltf_options =
    fastgltf::Options::LoadGLBBuffers | fastgltf::Options::LoadExternalBuffers;

  fastgltf::Asset  gltf;
  fastgltf::Parser parser{};

  auto load =
    parser.loadGltfBinary(&data, filepath.parent_path(), gltf_options);

  if (load) {
    gltf = std::move(load.get());
  } else {
    LOG_WARNING(
      std::format("Failed to load {}", fastgltf::to_underlying(load.error()))
        .c_str());

    return {};
  }

  std::vector<std::shared_ptr<MeshAsset>> meshes;

  std::vector<uint32_t> indices;
  std::vector<Vertex>   vertices;

  for (fastgltf::Mesh& mesh : gltf.meshes) {
    MeshAsset new_mesh;

    new_mesh.name = mesh.name;

    indices.clear();
    vertices.clear();

    for (auto&& p : mesh.primitives) {
      GeomSurface new_surface;
      new_surface.start_index = (uint32_t)indices.size();
      new_surface.count =
        (uint32_t)gltf.accessors[p.indicesAccessor.value()].count;

      size_t initial_vertex = vertices.size();

      {
        fastgltf::Accessor& index_accessor =
          gltf.accessors[p.indicesAccessor.value()];
        indices.reserve(indices.size() + index_accessor.count);

        fastgltf::iterateAccessor<std::uint32_t>(
          gltf, index_accessor, [&](std::uint32_t idx) {
            indices.push_back(idx + initial_vertex);
          });
      }

      {
        fastgltf::Accessor& pos_accessor =
          gltf.accessors[p.findAttribute("POSITION")->second];
        vertices.resize(vertices.size() + pos_accessor.count);

        fastgltf::iterateAccessorWithIndex<glm::vec3>(
          gltf, pos_accessor, [&](glm::vec3 v, size_t index) {
            Vertex new_vertex;

            new_vertex.position = v;
            new_vertex.normal   = { 1, 0, 0 };
            new_vertex.color    = glm::vec4{ 1.0f };
            new_vertex.uv_x     = 0;
            new_vertex.uv_y     = 0;

            vertices[initial_vertex + index] = new_vertex;
          });
      }

      auto normals = p.findAttribute("NORMAL");
      if (normals != p.attributes.end()) {
        fastgltf::iterateAccessorWithIndex<glm::vec3>(
          gltf,
          gltf.accessors[(*normals).second],
          [&](glm::vec3 v, size_t index) {
            vertices[initial_vertex + index].normal = v;
          });
      }

      auto uv = p.findAttribute("TEXCOORD_0");
      if (uv != p.attributes.end()) {
        fastgltf::iterateAccessorWithIndex<glm::vec2>(
          gltf, gltf.accessors[(*uv).second], [&](glm::vec2 v, size_t index) {
            vertices[initial_vertex + index].uv_x = v.x;
            vertices[initial_vertex + index].uv_y = v.y;
          });
      }

      auto colors = p.findAttribute("COLOR_0");
      if (colors != p.attributes.end()) {
        fastgltf::iterateAccessorWithIndex<glm::vec4>(
          gltf,
          gltf.accessors[(*colors).second],
          [&](glm::vec4 v, size_t index) {
            vertices[initial_vertex + index].color = v;
          });
      }

      new_mesh.surfaces.push_back(new_surface);
    }

    constexpr bool override_colors = false;
    if (override_colors) {
      for (Vertex& vertex : vertices) {
        vertex.color = glm::vec4(vertex.normal, 1.0f);
      }
    }
    new_mesh.mesh_buffers = engine->upload_mesh(indices, vertices);

    meshes.emplace_back(std::make_shared<MeshAsset>(std::move(new_mesh)));
  }

  return meshes;
}

void
LoadedGLTF::Draw(const glm::mat4& top_matrix, DrawContext& ctx)
{
  for (auto& n : top_nodes) {
    n->Draw(top_matrix, ctx);
  }
}

void
LoadedGLTF::clear_all()
{
  VkDevice dev = creator->m_device;

  descriptor_pool.destroy_pools(dev);
  creator->destroy_buffer(material_data_buffer);

  for (auto& [k, v] : meshes) {
    creator->destroy_buffer(v->mesh_buffers.index_buffer);
    creator->destroy_buffer(v->mesh_buffers.vertex_buffer);
  }

  for (auto& [k, v] : images) {
    if (v.image == creator->m_checkboard_image.image) {
      continue;
    }

    creator->destroy_image(v);
  }

  for (auto& sampler : samplers) {
    vkDestroySampler(dev, sampler, nullptr);
  }
}
