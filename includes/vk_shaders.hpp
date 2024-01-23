#pragma once

#include "vk_types.hpp"

class Shader
{
public:
  void init(VkDevice device, std::string shader_path);
  void destroy();

  inline VkShaderModule get() { return m_shader_module; }

private:
  void create_spirv_shader(std::string& shader_path, std::string& spirv_path);

private:
  VkDevice m_device = VK_NULL_HANDLE;

  VkShaderModule m_shader_module = VK_NULL_HANDLE;
};