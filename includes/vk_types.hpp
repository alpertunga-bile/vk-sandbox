#pragma once

#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "vma/vk_mem_alloc.h"
#include "vulkan/vk_enum_string_helper.h"
#include "vulkan/vulkan.h"

#include "format.h"

#include "glm/mat4x4.hpp"
#include "glm/vec4.hpp"

#include <format>
#include <stdio.h>

struct AllocatedImage
{
  VkImage       image;
  VkImageView   image_view;
  VmaAllocation allocation;
  VkExtent3D    image_extent;
  VkFormat      image_format;
};

struct AllocatedBuffer
{
  VkBuffer          buffer;
  VmaAllocation     allocation;
  VmaAllocationInfo info;
};

struct Vertex
{
  glm::vec3 position;
  float     uv_x;
  glm::vec3 normal;
  float     uv_y;
  glm::vec4 color;
};

struct GPUMeshBuffers
{
  AllocatedBuffer index_buffer;
  AllocatedBuffer vertex_buffer;
  VkDeviceAddress vertex_buffer_address;
};

struct GPUDrawPushConstants
{
  glm::mat4       world_matrix;
  VkDeviceAddress vertex_buffer;
};

///////////////////////////////////////////////////////////////////////////////////////////
// Debug / Logging

inline void
log_info(const char* msg)
{
  printf("[    INFO] /_\\ %s\n", msg);
}

inline void
log_success(const char* msg)
{
  printf(std::format("\033[0;32m[ SUCCESS] /_\\ {} \033[0m\n", msg).c_str());
}

inline std::string
get_output_string(const char* filename, int line)
{
  return std::format("{} | {}", filename, line);
}

inline void
log_warning(const char* msg, const char* file, int line)
{
  printf(std::format("\033[0;33m[ WARNING] /_\\ {} /_\\ {} \033[0m\n",
                     msg,
                     get_output_string(file, line).c_str())
           .c_str());
}

inline void
log_error(const char* msg, const char* file, int line)
{
  printf(std::format("\033[0;31m[  FAILED] /_\\ {} /_\\ {} \033[0m\n",
                     msg,
                     get_output_string(file, line).c_str())
           .c_str());
  exit(EXIT_FAILURE);
}

#if defined(_DEBUG)
#define LOG_INFO(msg)    log_info(msg)
#define LOG_SUCCESS(msg) log_success(msg);
#define LOG_WARNING(msg) log_warning(msg, __FILE__, __LINE__);
#define LOG_ERROR(msg)   log_error(msg, __FILE__, __LINE__);
#elif defined(_NDEBUG)
#define LOG_INFO(msg)
#define LOG_SUCCESS(msg)
#define LOG_WARNING(msg)
#define LOG_ERROR(msg)
#endif

inline void
vk_check(VkResult result, const char* file, int line)
{
  if (result == VK_SUCCESS) {
#if defined(_DEBUG)
    // LOG_SUCCESS( get_output_string(file, line).c_str() );
#endif
    return;
  }

  std::string result_str = string_VkResult(result);
  std::string msg = result_str + " " + get_output_string(file, line).c_str();

  LOG_ERROR(msg.c_str());
}

inline bool
vk_ret(VkResult result, const char* file, int line)
{
  if (result == VK_SUCCESS) {
#if defined(_DEBUG)
    // LOG_SUCCESS( get_output_string(file, line).c_str() );
#endif
    return true;
  }

  std::string result_str = string_VkResult(result);
  std::string msg = result_str + " " + get_output_string(file, line).c_str();

  LOG_WARNING(msg.c_str());

  return false;
}

#define VK_CHECK(res) vk_check(res, __FILE__, __LINE__)
#define VK_RET(res)   vk_ret(res, __FILE__, __LINE__)