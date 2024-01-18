#include "vk_shaders.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>

void
Shader::init(VkDevice device, std::string shader_path)
{
  m_device = device;

  std::string spirv_path;

  create_spirv_shader(shader_path, spirv_path);

  std::ifstream shader_file(spirv_path, std::ios::ate | std::ios::binary);

  if (!shader_file.is_open()) {
    LOG_ERROR(std::format("Cant open {} shader file", shader_path).c_str());
  }

  size_t file_size = (size_t)shader_file.tellg();

  std::vector<uint32_t> buffer(file_size / sizeof(uint32_t));

  shader_file.seekg(0);

  shader_file.read((char*)buffer.data(), file_size);

  shader_file.close();

  VkShaderModuleCreateInfo smci = {};
  smci.sType                    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  smci.pNext                    = nullptr;

  smci.codeSize = buffer.size() * sizeof(uint32_t);
  smci.pCode    = buffer.data();

  VK_CHECK(vkCreateShaderModule(m_device, &smci, nullptr, &m_shader_module));
}

void
Shader::destroy()
{
  vkDestroyShaderModule(m_device, m_shader_module, nullptr);
}

void
Shader::create_spirv_shader(std::string& shader_path, std::string& spirv_path)
{
  std::filesystem::path shaderpath = std::filesystem::path(shader_path);

  std::string name = shaderpath.stem().string();
  std::string ext  = shaderpath.extension().string();
  ext              = ext.substr(1, ext.size());

  spirv_path = "shaders/" + name + "_" + ext + ".spv";

  std::string to_spirv_command =
    std::format("glslangValidator -V {} -o {}", shader_path, spirv_path);

  std::array<char, 128> buffer;
  std::string           result;

  auto pipe = _popen(to_spirv_command.c_str(), "r");

  if (!pipe) {
    LOG_ERROR("_popen func is failed");
  }

  while (!feof(pipe)) {
    if (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
      result += buffer.data();
  }

  auto rc = _pclose(pipe);

  if (rc == EXIT_SUCCESS) {
    LOG_SUCCESS(std::format("{} is created", spirv_path).c_str());
  } else {
    printf(result.c_str());

    LOG_ERROR(std::format("{} cannot created", spirv_path).c_str());
  }
}