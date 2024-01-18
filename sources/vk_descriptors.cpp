#include "vk_descriptors.hpp"

void
DescriptorLayoutBuilder::add_binding(uint32_t binding, VkDescriptorType type)
{
  VkDescriptorSetLayoutBinding new_bind{};
  new_bind.binding         = binding;
  new_bind.descriptorCount = 1;
  new_bind.descriptorType  = type;

  bindings.push_back(new_bind);
}

void
DescriptorLayoutBuilder::clear()
{
  bindings.clear();
}

VkDescriptorSetLayout
DescriptorLayoutBuilder::build(VkDevice           device,
                               VkShaderStageFlags shader_stages)
{
  for (auto& b : bindings) {
    b.stageFlags |= shader_stages;
  }

  VkDescriptorSetLayoutCreateInfo info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
  };
  info.pNext        = nullptr;
  info.pBindings    = bindings.data();
  info.bindingCount = (uint32_t)bindings.size();
  info.flags        = 0;

  VkDescriptorSetLayout set;
  VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &set));

  return set;
}

void
DescriptorAllocator::init_pool(VkDevice                 device,
                               uint32_t                 max_sets,
                               std::span<PoolSizeRatio> pool_ratios)
{
  std::vector<VkDescriptorPoolSize> pool_sizes;

  for (PoolSizeRatio ratio : pool_ratios) {
    pool_sizes.push_back(VkDescriptorPoolSize{
      .type            = ratio.type,
      .descriptorCount = uint32_t(ratio.ratio * max_sets) });
  }

  VkDescriptorPoolCreateInfo pool_info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
  };
  pool_info.flags         = 0;
  pool_info.maxSets       = max_sets;
  pool_info.poolSizeCount = (uint32_t)pool_sizes.size();
  pool_info.pPoolSizes    = pool_sizes.data();

  vkCreateDescriptorPool(device, &pool_info, nullptr, &pool);
}

void
DescriptorAllocator::clear_descriptors(VkDevice device)
{
  vkResetDescriptorPool(device, pool, 0);
}

void
DescriptorAllocator::destroy_pool(VkDevice device)
{
  vkDestroyDescriptorPool(device, pool, nullptr);
}

VkDescriptorSet
DescriptorAllocator::allocate(VkDevice device, VkDescriptorSetLayout layout)
{
  VkDescriptorSetAllocateInfo alloc_info = {
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
  };
  alloc_info.pNext              = nullptr;
  alloc_info.descriptorPool     = pool;
  alloc_info.descriptorSetCount = 1;
  alloc_info.pSetLayouts        = &layout;

  VkDescriptorSet ds;
  VK_CHECK(vkAllocateDescriptorSets(device, &alloc_info, &ds));

  return ds;
}
