#include "../includes/vk_engine.hpp"
#include "../includes/vk_types.hpp"
#include "../includes/vk_initializers.hpp"
#include "../includes/vk_textures.hpp"

#include "../includes/VkBootstrap.h"

#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"

#include <glm/gtx/transform.hpp>
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <fstream>
#include <iostream>
#include "vk_engine.hpp"

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"

/*
Define check macro
*/

#define VK_CHECK(res) vk_check(res, __FILE__, __LINE__)

static void vk_check(VkResult result, const char* file, int line)
{
	if (result == VK_SUCCESS)
	{
		return;
	}

	printf("::VK_CHECK:: [%s] %d\n", file, line);
	exit(1);
}

/*
Define VulkanEngine functions
*/

void VulkanEngine::initVulkan()
{
	// ---------------------------------------------------------------------
	// Instance
	// ---------------------------------------------------------------------

	vkb::InstanceBuilder instanceBuilder;

	auto instanceRef = instanceBuilder.set_app_name("Vulkan Sandbox")
		.request_validation_layers(true)
		.require_api_version(1, 1, 0)
		.use_default_debug_messenger()
		.build();

	vkb::Instance vkbInstance = instanceRef.value();

	_instance = vkbInstance.instance;
	_debugMessenger = vkbInstance.debug_messenger;

	// ---------------------------------------------------------------------
	// Physical Device & Device
	// ---------------------------------------------------------------------
	// create actual window to render
	SDL_Vulkan_CreateSurface(_window, _instance, &_surface);

	vkb::PhysicalDeviceSelector deviceSelector{ vkbInstance };
	vkb::PhysicalDevice physicalDevice = deviceSelector.set_minimum_version(1, 1)
		.set_surface(_surface)
		.select()
		.value();

	vkb::DeviceBuilder deviceBuilder{ physicalDevice };
	VkPhysicalDeviceShaderDrawParametersFeatures shader_draw_parameters_features = {};
	shader_draw_parameters_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
	shader_draw_parameters_features.pNext = nullptr;
	shader_draw_parameters_features.shaderDrawParameters = VK_TRUE;

	vkb::Device vkbDevice = deviceBuilder
		.add_pNext(&shader_draw_parameters_features)
		.build()
		.value();

	_device = vkbDevice.device;
	_physicalDevice = physicalDevice.physical_device;

	_graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
	_graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

	VmaAllocatorCreateInfo allocatorInfo = {};
	allocatorInfo.physicalDevice = _physicalDevice;
	allocatorInfo.device = _device;
	allocatorInfo.instance = _instance;

	vmaCreateAllocator(&allocatorInfo, &_allocator);

	_gpuProperties = vkbDevice.physical_device.properties;
	std::cout << "GPU has a min buffer alignment of " << _gpuProperties.limits.minUniformBufferOffsetAlignment << "\n";
}

void VulkanEngine::initImgui()
{
	VkDescriptorPoolSize poolSizes[] =
	{
		{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
		{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
	};

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolInfo.maxSets = 1000;
	poolInfo.poolSizeCount = std::size(poolSizes);
	poolInfo.pPoolSizes = poolSizes;

	VkDescriptorPool imguiPool;
	VK_CHECK(vkCreateDescriptorPool(_device, &poolInfo, nullptr, &imguiPool));

	ImGui::CreateContext();

	ImGui_ImplSDL2_InitForVulkan(_window);

	ImGui_ImplVulkan_InitInfo initInfo = {};
	initInfo.Instance = _instance;
	initInfo.PhysicalDevice = _physicalDevice;
	initInfo.Device = _device;
	initInfo.Queue = _graphicsQueue;
	initInfo.DescriptorPool = imguiPool;
	initInfo.MinImageCount = 3;
	initInfo.ImageCount = 3;
	initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&initInfo, _renderpass);

	immediateSubmit([&](VkCommandBuffer cmd) {
		ImGui_ImplVulkan_CreateFontsTexture(cmd);
		});

	ImGui_ImplVulkan_DestroyFontUploadObjects();

	_mainDeleteionQueue.pushFunction([=]() {
		vkDestroyDescriptorPool(_device, imguiPool, nullptr);
		ImGui_ImplVulkan_Shutdown();
		});
}

void VulkanEngine::initSwapchain()
{
	vkb::SwapchainBuilder swapchainBuilder{ _physicalDevice, _device, _surface };
	vkb::Swapchain vkbSwapchain = swapchainBuilder
		.use_default_format_selection()
		// strong vsync
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_extent(_windowExtent.width, _windowExtent.height)
		.build()
		.value();

	_swapchain = vkbSwapchain.swapchain;
	_swapchainImages = vkbSwapchain.get_images().value();
	_swapchainImageViews = vkbSwapchain.get_image_views().value();
	_swapchainImageFormat = vkbSwapchain.image_format;

	_mainDeleteionQueue.pushFunction([=]() {
		vkDestroySwapchainKHR(_device, _swapchain, nullptr);
		});

	VkExtent3D depthImageExtent = {
		_windowExtent.width,
		_windowExtent.height,
		1
	};

	_depthFormat = VK_FORMAT_D32_SFLOAT;
	VkImageCreateInfo dimgInfo = vkInit::imageCreateInfo(_depthFormat,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		depthImageExtent);

	VmaAllocationCreateInfo dimgAllocInfo = {};
	dimgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	dimgAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	vmaCreateImage(_allocator, &dimgInfo, &dimgAllocInfo, &_depthImage._image, &_depthImage._allocation, nullptr);

	VkImageViewCreateInfo dviewInfo = vkInit::imageviewCreateInfo(_depthFormat, _depthImage._image, VK_IMAGE_ASPECT_DEPTH_BIT);

	VK_CHECK(vkCreateImageView(_device, &dviewInfo, nullptr, &_depthImageView));

	_mainDeleteionQueue.pushFunction([=]() {
		vkDestroyImageView(_device, _depthImageView, nullptr);
		vmaDestroyImage(_allocator, _depthImage._image, _depthImage._allocation);
		});
}

void VulkanEngine::initCommands()
{
	VkCommandPoolCreateInfo commandPoolInfo = vkInit::commandPoolCreateInfo(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

		VkCommandBufferAllocateInfo cmdAllocInfo = vkInit::commandBufferAllocateInfo(_frames[i]._commandPool, 1);

		VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));

		_mainDeleteionQueue.pushFunction([=]() {
			vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);
			});
	}

	VkCommandPoolCreateInfo uploadCommandPoolInfo = vkInit::commandPoolCreateInfo(_graphicsQueueFamily);
	VK_CHECK(vkCreateCommandPool(_device, &uploadCommandPoolInfo, nullptr, &_uploadContext.commandPool));

	_mainDeleteionQueue.pushFunction([=]() {
		vkDestroyCommandPool(_device, _uploadContext.commandPool, nullptr);
		});

	VkCommandBufferAllocateInfo cmdAllocInfo = vkInit::commandBufferAllocateInfo(_uploadContext.commandPool, 1);
	VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_uploadContext.commandBuffer));
}

void VulkanEngine::initDefaultRenderpass()
{
	VkAttachmentDescription colorAttachment = {};
	colorAttachment.format = _swapchainImageFormat;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorAttachmentRef = {};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentDescription depthAttachment = {};
	depthAttachment.flags = 0;
	depthAttachment.format = _depthFormat;
	depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthAttachmentRef = {};
	depthAttachmentRef.attachment = 1;
	depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;
	subpass.pDepthStencilAttachment = &depthAttachmentRef;

	VkAttachmentDescription attachments[2] = { colorAttachment, depthAttachment };

	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkSubpassDependency depthDependency = {};
	depthDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	depthDependency.dstSubpass = 0;
	depthDependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
		VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depthDependency.srcAccessMask = 0;
	depthDependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
		VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	depthDependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	VkSubpassDependency dependencies[2] = { dependency, depthDependency };

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 2;
	renderPassInfo.pAttachments = &attachments[0];
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = 2;
	renderPassInfo.pDependencies = &dependencies[0];

	VK_CHECK(vkCreateRenderPass(_device, &renderPassInfo, nullptr, &_renderpass));

	_mainDeleteionQueue.pushFunction([=]() {
		vkDestroyRenderPass(_device, _renderpass, nullptr);
		});
}

void VulkanEngine::initFramebuffers()
{
	VkFramebufferCreateInfo fbInfo = {};
	fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fbInfo.pNext = nullptr;
	fbInfo.renderPass = _renderpass;
	fbInfo.attachmentCount = 1;
	fbInfo.width = _windowExtent.width;
	fbInfo.height = _windowExtent.height;
	fbInfo.layers = 1;

	const uint32_t swapchainImageCount = _swapchainImages.size();
	_framebuffers = std::vector<VkFramebuffer>(swapchainImageCount);

	for (int i = 0; i < swapchainImageCount; i++)
	{
		VkImageView attachments[2];
		attachments[0] = _swapchainImageViews[i];
		attachments[1] = _depthImageView;

		fbInfo.pAttachments = attachments;
		fbInfo.attachmentCount = 2;
		VK_CHECK(vkCreateFramebuffer(_device, &fbInfo, nullptr, &_framebuffers[i]));

		_mainDeleteionQueue.pushFunction([=]() {
			vkDestroyFramebuffer(_device, _framebuffers[i], nullptr);
			vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
			});
	}
}

void VulkanEngine::initSyncStructures()
{
	VkFenceCreateInfo fenceCreateInfo = vkInit::fenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
	VkSemaphoreCreateInfo semaphoreCreateInfo = vkInit::semaphoreCreateInfo();

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));

		_mainDeleteionQueue.pushFunction([=]() {
			vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
			});

		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._presentSemaphore));
		VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._renderSemaphore));

		_mainDeleteionQueue.pushFunction([=]() {
			vkDestroySemaphore(_device, _frames[i]._presentSemaphore, nullptr);
			vkDestroySemaphore(_device, _frames[i]._renderSemaphore, nullptr);
			});
	}

	VkFenceCreateInfo uploadFenceCreateInfo = vkInit::fenceCreateInfo();
	VK_CHECK(vkCreateFence(_device, &uploadFenceCreateInfo, nullptr, &_uploadContext.uploadFence));
	_mainDeleteionQueue.pushFunction([=]() {
		vkDestroyFence(_device, _uploadContext.uploadFence, nullptr);
		});
}

void VulkanEngine::initPipelines()
{
	// ---------------------------------------------------------------------------------------------------------------
	// -- Colored Triangle
	// ---------------------------------------------------------------------------------------------------------------

	VkShaderModule triangleVertShader;
	if (!loadShaderModule("shaders/colored_triangle_vert.spv", triangleVertShader))
	{
		std::cout << "Error building colored_triangle_vert.spv shader\n";
	}
	else
	{
		std::cout << "colored_triangle_vert.spv is loaded\n";
	}

	VkShaderModule triangleFragShader;
	if (!loadShaderModule("shaders/colored_triangle_frag.spv", triangleFragShader))
	{
		std::cout << "Error building colored_triangle_frag.spv shader\n";
	}
	else
	{
		std::cout << "colored_triangle_frag.spv is loaded\n";
	}


	PipelineBuilder pipelineBuilder;

	pipelineBuilder._shaderStages.push_back(
		vkInit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, triangleVertShader)
	);
	pipelineBuilder._shaderStages.push_back(
		vkInit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, triangleFragShader)
	);

	pipelineBuilder._vertexInputInfo = vkInit::vertexInputStateCreateInfo();
	pipelineBuilder._inputAssembly = vkInit::inputAssemblyCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

	pipelineBuilder._viewport.x = 0.0f;
	pipelineBuilder._viewport.y = 0.0f;
	pipelineBuilder._viewport.width = (float)_windowExtent.width;
	pipelineBuilder._viewport.height = (float)_windowExtent.height;
	pipelineBuilder._viewport.minDepth = 0.0f;
	pipelineBuilder._viewport.maxDepth = 1.0f;

	pipelineBuilder._scissor.offset = { 0, 0 };
	pipelineBuilder._scissor.extent = _windowExtent;

	pipelineBuilder._rasterizer = vkInit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL);

	pipelineBuilder._multisampling = vkInit::multisamplingStateCreateInfo();
	pipelineBuilder._colorBlendAttachment = vkInit::colorBlendAttachmentState();

	// ---------------------------------------------------------------------------------------------------------------
	// -- Red Triangle
	// ---------------------------------------------------------------------------------------------------------------

	VkShaderModule redTriangleVertShader;
	if (!loadShaderModule("shaders/red_triangle_vert.spv", redTriangleVertShader))
	{
		std::cout << "Error building red_triangle_vert.spv shader\n";
	}
	else
	{
		std::cout << "red_triangle_vert.spv is loaded\n";
	}

	VkShaderModule redTriangleFragShader;
	if (!loadShaderModule("shaders/red_triangle_frag.spv", redTriangleFragShader))
	{
		std::cout << "Error building red_triangle_frag.spv shader\n";
	}
	else
	{
		std::cout << "red_triangle_frag.spv is loaded\n";
	}

	// ---------------------------------------------------------------------------------------------------------------
	// -- Mesh
	// ---------------------------------------------------------------------------------------------------------------
	VkPipelineLayoutCreateInfo meshPipelineLayoutInfo = vkInit::pipelineLayoutCreateInfo();

	VkPushConstantRange pushConstant;
	pushConstant.offset = 0;
	pushConstant.size = sizeof(MeshPushConstants);
	pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	meshPipelineLayoutInfo.pPushConstantRanges = &pushConstant;
	meshPipelineLayoutInfo.pushConstantRangeCount = 1;

	VkDescriptorSetLayout setLayouts[] = { _globalSetLayout, _objectSetLayout, _singleTextureSetLayout };

	meshPipelineLayoutInfo.setLayoutCount = 3;
	meshPipelineLayoutInfo.pSetLayouts = setLayouts;

	VK_CHECK(vkCreatePipelineLayout(_device, &meshPipelineLayoutInfo, nullptr, &_meshPipelineLayout));

	VertexInputDescription vertexDescription = Vertex::getVertexDescription();

	pipelineBuilder._vertexInputInfo.pVertexAttributeDescriptions = vertexDescription.attributes.data();
	pipelineBuilder._vertexInputInfo.vertexAttributeDescriptionCount = vertexDescription.attributes.size();

	pipelineBuilder._vertexInputInfo.pVertexBindingDescriptions = vertexDescription.bindings.data();
	pipelineBuilder._vertexInputInfo.vertexBindingDescriptionCount = vertexDescription.bindings.size();

	pipelineBuilder._shaderStages.clear();

	VkShaderModule meshVertShader;
	if (!loadShaderModule("shaders/triangle_mesh_vert.spv", meshVertShader))
	{
		std::cout << "Error building triangle_mesh_vert.spv shader\n";
	}
	else
	{
		std::cout << "triangle_mesh_vert.spv is loaded\n";
	}

	VkShaderModule meshFragShader;
	if (!loadShaderModule("shaders/triangle_mesh_frag.spv", meshFragShader))
	{
		std::cout << "Error building triangle_mesh_frag.spv shader\n";
	}
	else
	{
		std::cout << "triangle_mesh_frag.spv is loaded\n";
	}

	pipelineBuilder._shaderStages.push_back(
		vkInit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, meshVertShader)
	);

	pipelineBuilder._shaderStages.push_back(
		vkInit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, meshFragShader)
	);

	pipelineBuilder._pipelineLayout = _meshPipelineLayout;
	pipelineBuilder._depthStencil = vkInit::depthStencilCreateInfo(
		true, true, VK_COMPARE_OP_LESS_OR_EQUAL
	);

	_meshPipeline = pipelineBuilder.buildPipeline(_device, _renderpass);

	createMaterial(_meshPipeline, _meshPipelineLayout, "texturedmesh");

	vkDestroyShaderModule(_device, redTriangleVertShader, nullptr);
	vkDestroyShaderModule(_device, redTriangleFragShader, nullptr);
	vkDestroyShaderModule(_device, triangleVertShader, nullptr);
	vkDestroyShaderModule(_device, triangleFragShader, nullptr);
	vkDestroyShaderModule(_device, meshVertShader, nullptr);
	vkDestroyShaderModule(_device, meshFragShader, nullptr);

	_mainDeleteionQueue.pushFunction([=]() {
		vkDestroyPipeline(_device, _meshPipeline, nullptr);

		vkDestroyPipelineLayout(_device, _meshPipelineLayout, nullptr);
		});
}

bool VulkanEngine::loadShaderModule(const char* path, VkShaderModule& outShaderModule)
{
	std::ifstream file(path, std::ios::ate | std::ios::binary);

	if (!file.is_open())
	{
		printf("Cannot open %s shader\n", path);
		return false;
	}

	size_t fileSize = (size_t)file.tellg();

	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

	file.seekg(0);
	file.read((char*)buffer.data(), fileSize);
	file.close();

	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;

	createInfo.codeSize = buffer.size() * sizeof(uint32_t);
	createInfo.pCode = buffer.data();

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		return false;
	}

	outShaderModule = shaderModule;
	return true;
}

void VulkanEngine::loadMeshes()
{
	_triangleMesh._vertices.resize(3);

	_triangleMesh._vertices[0].position = { 1.f, 1.f, 0.0f };
	_triangleMesh._vertices[1].position = { -1.f, 1.f, 0.0f };
	_triangleMesh._vertices[2].position = { 0.f,-1.f, 0.0f };

	_triangleMesh._vertices[0].color = { 0.42f, 0.523f, 0.123f };
	_triangleMesh._vertices[1].color = { 0.42f, 0.523f, 0.123f };
	_triangleMesh._vertices[2].color = { 0.42f, 0.523f, 0.123f };

	_modelMesh.loadFromObj("models/monkey_smooth.obj");

	Mesh lostEmpire{};
	lostEmpire.loadFromObj("assets/lost_empire.obj");

	uploadMesh(_triangleMesh);
	uploadMesh(_modelMesh);
	uploadMesh(lostEmpire);

	_meshes["monkey"] = _modelMesh;
	_meshes["triangle"] = _triangleMesh;
	_meshes["empire"] = lostEmpire;
}

void VulkanEngine::uploadMesh(Mesh& mesh)
{
	const size_t bufferSize = mesh._vertices.size() * sizeof(Vertex);

	VkBufferCreateInfo stagingBufferInfo = {};
	stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	stagingBufferInfo.pNext = nullptr;
	stagingBufferInfo.size = bufferSize;
	stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	/*
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = mesh._vertices.size() * sizeof(Vertex);
	bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
	*/

	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

	AllocatedBuffer stagingBuffer;

	VK_CHECK(vmaCreateBuffer(
		_allocator,
		&stagingBufferInfo,
		&vmaallocInfo,
		&stagingBuffer._buffer,
		&stagingBuffer._allocation,
		nullptr
	));

	void* data;
	vmaMapMemory(_allocator, stagingBuffer._allocation, &data);
	memcpy(data, mesh._vertices.data(), mesh._vertices.size() * sizeof(Vertex));
	vmaUnmapMemory(_allocator, stagingBuffer._allocation);

	VkBufferCreateInfo vertexBufferInfo = {};
	vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vertexBufferInfo.pNext = nullptr;
	vertexBufferInfo.size = bufferSize;
	vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	vmaallocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VK_CHECK(vmaCreateBuffer(_allocator, &vertexBufferInfo, &vmaallocInfo, &mesh._vertexBuffer._buffer, &mesh._vertexBuffer._allocation, nullptr));

	immediateSubmit([=](VkCommandBuffer cmd) {
		VkBufferCopy copy;
		copy.dstOffset = 0;
		copy.srcOffset = 0;
		copy.size = bufferSize;
		vkCmdCopyBuffer(cmd, stagingBuffer._buffer, mesh._vertexBuffer._buffer, 1, &copy);
		});

	_mainDeleteionQueue.pushFunction([=]() {
		vmaDestroyBuffer(_allocator, mesh._vertexBuffer._buffer, mesh._vertexBuffer._allocation);
		});

	vmaDestroyBuffer(_allocator, stagingBuffer._buffer, stagingBuffer._allocation);
}

Material* VulkanEngine::createMaterial(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name)
{
	Material mat;
	mat.pipeline = pipeline;
	mat.pipelineLayout = layout;
	_materials[name] = mat;

	return &_materials[name];
}

Material* VulkanEngine::getMaterial(const std::string& name)
{
	auto it = _materials.find(name);
	if (it == _materials.end())
	{
		return nullptr;
	}

	return &(*it).second;
}

Mesh* VulkanEngine::getMesh(const std::string& name)
{
	auto it = _meshes.find(name);
	if (it == _meshes.end())
	{
		return nullptr;
	}

	return &(*it).second;
}

void VulkanEngine::drawObjects(VkCommandBuffer cmd, RenderObject* first, int count)
{
	glm::mat4 view = glm::translate(glm::mat4{ 1.0f }, _camPos);
	glm::mat4 projection = glm::perspective(
		glm::radians(70.0f), 1700.0f / 900.0f, 0.1f, 200.0f
	);
	projection[1][1] *= -1;

	float framed = (_framenumber / 120.0f);
	_sceneParameters.ambientColor = { std::sin(framed), 0.0f, std::cos(framed), 1.0f };

	char* sceneData;
	vmaMapMemory(_allocator, _sceneParameterBuffer._allocation, (void**)&sceneData);

	int frameIndex = _framenumber % FRAME_OVERLAP;

	sceneData += padUniformBufferSize(sizeof(GPUSceneData)) * frameIndex;
	memcpy(sceneData, &_sceneParameters, sizeof(GPUSceneData));
	vmaUnmapMemory(_allocator, _sceneParameterBuffer._allocation);

	GPUCameraData camData;
	camData.proj = projection;
	camData.view = view;
	camData.viewproj = projection * view;

	void* data;
	vmaMapMemory(_allocator, getCurrentFrame().cameraBuffer._allocation, &data);
	memcpy(data, &camData, sizeof(GPUCameraData));
	vmaUnmapMemory(_allocator, getCurrentFrame().cameraBuffer._allocation);

	void* objectData;
	vmaMapMemory(_allocator, getCurrentFrame().objectBuffer._allocation, &objectData);

	GPUObjectData* objectSSBO = (GPUObjectData*)objectData;

	for (int i = 0; i < count; i++)
	{
		RenderObject& object = first[i];
		objectSSBO[i].modelMatrix = object.transformMatrix;
	}

	vmaUnmapMemory(_allocator, getCurrentFrame().objectBuffer._allocation);

	Mesh* lastMesh = nullptr;
	Material* lastMaterial = nullptr;
	for (int i = 0; i < count; i++)
	{
		RenderObject& object = first[i];

		if (object.material == nullptr)
		{
			continue;
		}

		if (object.material != lastMaterial)
		{
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, object.material->pipeline);
			lastMaterial = object.material;

			uint32_t uniform_offset = padUniformBufferSize(sizeof(GPUSceneData)) * frameIndex;

			vkCmdBindDescriptorSets(cmd,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				object.material->pipelineLayout,
				0, 1, &getCurrentFrame().globalDescriptor, 1, &uniform_offset);

			vkCmdBindDescriptorSets(cmd,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				object.material->pipelineLayout,
				1, 1, &getCurrentFrame().objectDescriptor, 0, nullptr
			);

			if (object.material->textureSet != VK_NULL_HANDLE)
			{
				vkCmdBindDescriptorSets(
					cmd,
					VK_PIPELINE_BIND_POINT_GRAPHICS,
					object.material->pipelineLayout,
					2, 1, &object.material->textureSet, 0, nullptr
				);
			}
		}

		MeshPushConstants constants;
		constants.renderMatrix = object.transformMatrix;

		vkCmdPushConstants(cmd, object.material->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(MeshPushConstants), &constants);

		if (object.mesh != lastMesh)
		{
			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(cmd, 0, 1, &object.mesh->_vertexBuffer._buffer, &offset);
			lastMesh = object.mesh;
		}

		vkCmdDraw(cmd, object.mesh->_vertices.size(), 1, 0, i);
	}
}

void VulkanEngine::initScene()
{
	RenderObject monkey;
	monkey.mesh = getMesh("monkey");
	monkey.material = getMaterial("defaultMesh");
	monkey.transformMatrix = glm::mat4(1.0f);

	_renderables.push_back(monkey);

	for (int x = -20; x <= 20; x++)
	{
		for (int y = -20; y <= 20; y++)
		{
			RenderObject tri;
			tri.mesh = getMesh("triangle");
			tri.material = getMaterial("defaultMesh");
			glm::mat4 translation = glm::translate(glm::mat4{ 1.0f }, glm::vec3(x, 0, y));
			glm::mat4 scale = glm::scale(glm::mat4{ 1.0f }, glm::vec3(0.2, 0.2, 0.2));
			tri.transformMatrix = translation * scale;

			_renderables.push_back(tri);
		}
	}

	RenderObject map;
	map.mesh = getMesh("empire");
	map.material = getMaterial("texturedmesh");
	map.transformMatrix = glm::translate(glm::vec3(5, -10, 0));

	VkSamplerCreateInfo samplerInfo = vkInit::samplerCreateInfo(VK_FILTER_NEAREST);

	VkSampler blockySampler;
	VK_CHECK(vkCreateSampler(_device, &samplerInfo, nullptr, &blockySampler));

	Material* texturedMat = getMaterial("texturedmesh");

	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.pNext = nullptr;
	allocInfo.descriptorPool = _descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &_singleTextureSetLayout;

	vkAllocateDescriptorSets(_device, &allocInfo, &texturedMat->textureSet);

	VkDescriptorImageInfo imageBufferInfo;
	imageBufferInfo.sampler = blockySampler;
	imageBufferInfo.imageView = _loadedTextures["empire_diffuse"].imageView;
	imageBufferInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet texture1 = vkInit::writeDescriptorImage(
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, texturedMat->textureSet, &imageBufferInfo, 0
	);

	vkUpdateDescriptorSets(_device, 1, &texture1, 0, nullptr);

	_renderables.push_back(map);
}

AllocatedBuffer VulkanEngine::createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
{
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.pNext = nullptr;

	bufferInfo.size = allocSize;
	bufferInfo.usage = usage;

	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = memoryUsage;

	AllocatedBuffer newBuffer;

	VK_CHECK(vmaCreateBuffer(
		_allocator, &bufferInfo, &vmaallocInfo,
		&newBuffer._buffer, &newBuffer._allocation, nullptr
	));

	return newBuffer;
}

void VulkanEngine::init_descriptors()
{
	std::vector<VkDescriptorPoolSize> sizes =
	{
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10},
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10}
	};

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = 0;
	poolInfo.maxSets = 10;
	poolInfo.poolSizeCount = (uint32_t)sizes.size();
	poolInfo.pPoolSizes = sizes.data();

	vkCreateDescriptorPool(_device, &poolInfo, nullptr, &_descriptorPool);

	VkDescriptorSetLayoutBinding cameraBind = vkInit::descriptorSetLayoutBinding(
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0
	);

	VkDescriptorSetLayoutBinding sceneBind = vkInit::descriptorSetLayoutBinding(
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		1
	);

	VkDescriptorSetLayoutBinding objectBind = vkInit::descriptorSetLayoutBinding(
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		VK_SHADER_STAGE_VERTEX_BIT,
		0
	);

	VkDescriptorSetLayoutBinding textureBind = vkInit::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 0);

	VkDescriptorSetLayoutCreateInfo set3Info = {};
	set3Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set3Info.pNext = nullptr;
	set3Info.bindingCount = 1;
	set3Info.flags = 0;
	set3Info.pBindings = &textureBind;

	vkCreateDescriptorSetLayout(_device, &set3Info, nullptr, &_singleTextureSetLayout);

	VkDescriptorSetLayoutCreateInfo set2Info = {};
	set2Info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set2Info.pNext = nullptr;
	set2Info.flags = 0;
	set2Info.bindingCount = 1;
	set2Info.pBindings = &objectBind;

	vkCreateDescriptorSetLayout(_device, &set2Info, nullptr, &_objectSetLayout);

	VkDescriptorSetLayoutBinding bindings[] = { cameraBind, sceneBind };

	VkDescriptorSetLayoutCreateInfo setInfo = {};
	setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setInfo.pNext = nullptr;
	setInfo.flags = 0;
	setInfo.bindingCount = 2;
	setInfo.pBindings = bindings;

	vkCreateDescriptorSetLayout(_device, &setInfo, nullptr, &_globalSetLayout);

	const size_t sceneParamBufferSize = FRAME_OVERLAP * padUniformBufferSize(sizeof(GPUSceneData));
	_sceneParameterBuffer = createBuffer(sceneParamBufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

	const int MAX_OBJECTS = 10000;

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		_frames[i].objectBuffer = createBuffer(
			sizeof(GPUObjectData) * MAX_OBJECTS,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VMA_MEMORY_USAGE_CPU_TO_GPU
		);

		_frames[i].cameraBuffer = createBuffer(
			sizeof(GPUCameraData),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VMA_MEMORY_USAGE_CPU_TO_GPU
		);

		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.pNext = nullptr;
		allocInfo.descriptorPool = _descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &_globalSetLayout;

		vkAllocateDescriptorSets(_device, &allocInfo, &_frames[i].globalDescriptor);

		VkDescriptorSetAllocateInfo objectSetAlloc = {};
		objectSetAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		objectSetAlloc.pNext = nullptr;
		objectSetAlloc.descriptorPool = _descriptorPool;
		objectSetAlloc.descriptorSetCount = 1;
		objectSetAlloc.pSetLayouts = &_objectSetLayout;

		vkAllocateDescriptorSets(_device, &objectSetAlloc, &_frames[i].objectDescriptor);

		VkDescriptorBufferInfo cameraInfo;
		cameraInfo.buffer = _frames[i].cameraBuffer._buffer;
		cameraInfo.offset = 0;
		cameraInfo.range = sizeof(GPUCameraData);

		VkDescriptorBufferInfo sceneInfo;
		sceneInfo.buffer = _sceneParameterBuffer._buffer;
		sceneInfo.offset = 0;
		sceneInfo.range = sizeof(GPUSceneData);

		VkDescriptorBufferInfo objectInfo;
		objectInfo.buffer = _frames[i].objectBuffer._buffer;
		objectInfo.offset = 0;
		objectInfo.range = sizeof(GPUObjectData) * MAX_OBJECTS;

		VkWriteDescriptorSet cameraWrite = vkInit::writeDescriptorBuffer(
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			_frames[i].globalDescriptor, &cameraInfo, 0
		);

		VkWriteDescriptorSet sceneWrite = vkInit::writeDescriptorBuffer(
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
			_frames[i].globalDescriptor, &sceneInfo, 1
		);

		VkWriteDescriptorSet objectWrite = vkInit::writeDescriptorBuffer(
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			_frames[i].objectDescriptor, &objectInfo, 0
		);

		VkWriteDescriptorSet setWrites[] = { cameraWrite, sceneWrite, objectWrite };

		vkUpdateDescriptorSets(_device, 3, setWrites, 0, nullptr);
	}

	for (int i = 0; i < FRAME_OVERLAP; i++)
	{
		_mainDeleteionQueue.pushFunction([=]() {
			vmaDestroyBuffer(_allocator, _frames[i].objectBuffer._buffer, _frames[i].objectBuffer._allocation);
			vmaDestroyBuffer(_allocator, _frames[i].cameraBuffer._buffer, _frames[i].cameraBuffer._allocation);
			});
	}

	_mainDeleteionQueue.pushFunction([=]() {
		vkDestroyDescriptorSetLayout(_device, _globalSetLayout, nullptr);
		vkDestroyDescriptorSetLayout(_device, _objectSetLayout, nullptr);
		vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);
		vmaDestroyBuffer(_allocator, _sceneParameterBuffer._buffer, _sceneParameterBuffer._allocation);
		});
}

size_t VulkanEngine::padUniformBufferSize(size_t originalSize)
{
	size_t minUboAlignment = _gpuProperties.limits.minUniformBufferOffsetAlignment;
	size_t alignedSize = originalSize;

	if (minUboAlignment > 0)
	{
		alignedSize = (alignedSize + minUboAlignment - 1) & ~(minUboAlignment - 1);
	}

	return alignedSize;
}

void VulkanEngine::loadImages()
{
	Texture lostEmpire;

	vkUtil::loadImageFromFile(this, "assets/lost_empire-RGBA.png", lostEmpire.image);

	VkImageViewCreateInfo imageInfo = vkInit::imageviewCreateInfo(VK_FORMAT_R8G8B8A8_SRGB, lostEmpire.image._image, VK_IMAGE_ASPECT_COLOR_BIT);
	VK_CHECK(vkCreateImageView(_device, &imageInfo, nullptr, &lostEmpire.imageView));

	_mainDeleteionQueue.pushFunction([=]() {
		vkDestroyImageView(_device, lostEmpire.imageView, nullptr);
		});

	_loadedTextures["empire_diffuse"] = lostEmpire;
}

void VulkanEngine::immediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function)
{
	VkCommandBuffer cmd = _uploadContext.commandBuffer;

	VkCommandBufferBeginInfo cmdBeginInfo = vkInit::commandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
	function(cmd);
	VK_CHECK(vkEndCommandBuffer(cmd));
	VkSubmitInfo submit = vkInit::submitInfo(&cmd);

	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, _uploadContext.uploadFence));
	vkWaitForFences(_device, 1, &_uploadContext.uploadFence, true, 9999999999);
	vkResetFences(_device, 1, &_uploadContext.uploadFence);

	vkResetCommandPool(_device, _uploadContext.commandPool, 0);
}

FrameData& VulkanEngine::getCurrentFrame()
{
	return _frames[_framenumber % FRAME_OVERLAP];
}

void VulkanEngine::init()
{
	SDL_Init(SDL_INIT_VIDEO); // initialize window includes input events
	SDL_WindowFlags windowFlags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);

	// create window
	_window = SDL_CreateWindow(
		"Vulkan Sandbox",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		_windowExtent.width,
		_windowExtent.height,
		windowFlags
	);

	initVulkan();
	initSwapchain();
	initCommands();
	initDefaultRenderpass();
	initFramebuffers();
	initSyncStructures();
	init_descriptors();
	initPipelines();

	initImgui();

	loadImages();
	loadMeshes();
	initScene();
}

void VulkanEngine::draw()
{
	ImGui::Render();

	FrameData& currentFrame = getCurrentFrame();
	VK_CHECK(vkWaitForFences(_device, 1, &currentFrame._renderFence, true, 1000000000));
	VK_CHECK(vkResetFences(_device, 1, &currentFrame._renderFence));

	uint32_t swapchainImageIndex;
	VK_CHECK(vkAcquireNextImageKHR(_device, _swapchain, 1000000000, currentFrame._presentSemaphore, nullptr, &swapchainImageIndex));

	VK_CHECK(vkResetCommandBuffer(currentFrame._mainCommandBuffer, 0));

	VkCommandBuffer cmd = currentFrame._mainCommandBuffer;

	VkCommandBufferBeginInfo cmdBeginInfo = {};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.pNext = nullptr;
	cmdBeginInfo.pInheritanceInfo = nullptr;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	VkClearValue clearValue;
	clearValue.color = { {0.0f, 0.0f, 0.0f, 1.0f} };

	VkClearValue depthClear;
	depthClear.depthStencil.depth = 1.0f;

	VkRenderPassBeginInfo rpBeginInfo = {};
	rpBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	rpBeginInfo.pNext = nullptr;
	rpBeginInfo.renderPass = _renderpass;
	rpBeginInfo.renderArea.offset.x = 0;
	rpBeginInfo.renderArea.offset.y = 0;
	rpBeginInfo.renderArea.extent = _windowExtent;
	rpBeginInfo.framebuffer = _framebuffers[swapchainImageIndex];


	VkClearValue clearValues[2] = { clearValue, depthClear };
	rpBeginInfo.clearValueCount = 2;
	rpBeginInfo.pClearValues = &clearValues[0];

	vkCmdBeginRenderPass(cmd, &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	drawObjects(cmd, _renderables.data(), _renderables.size());

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

	vkCmdEndRenderPass(cmd);
	VK_CHECK(vkEndCommandBuffer(cmd));

	VkSubmitInfo submit = {};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.pNext = nullptr;

	VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	submit.pWaitDstStageMask = &waitStage;
	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &currentFrame._presentSemaphore;
	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &currentFrame._renderSemaphore;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &cmd;

	VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, currentFrame._renderFence));

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = nullptr;

	presentInfo.pSwapchains = &_swapchain;
	presentInfo.swapchainCount = 1;

	presentInfo.pWaitSemaphores = &currentFrame._renderSemaphore;
	presentInfo.waitSemaphoreCount = 1;

	presentInfo.pImageIndices = &swapchainImageIndex;

	VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));

	_framenumber += 1;
}

void VulkanEngine::run()
{
	SDL_Event event;
	bool isQuit = false;

	while (!isQuit)
	{
		// handling events
		while (SDL_PollEvent(&event) != 0)
		{
			ImGui_ImplSDL2_ProcessEvent(&event);

			if (event.type == SDL_QUIT)
			{
				isQuit = true;
			}
			else if (event.type == SDL_KEYDOWN)
			{
				if (event.key.keysym.sym == SDLK_SPACE)
				{
					_selectedShader += 1;
					if (_selectedShader > 2)
					{
						_selectedShader = 0;
					}
				}
				if (event.key.keysym.sym == SDLK_w)
				{
					_camPos.z += 3.0f;
				}
				if (event.key.keysym.sym == SDLK_s)
				{
					_camPos.z -= 3.0f;
				}
				if (event.key.keysym.sym == SDLK_a)
				{
					_camPos.x += 3.0f;
				}
				if (event.key.keysym.sym == SDLK_d)
				{
					_camPos.x -= 3.0f;
				}
				if (event.key.keysym.sym == SDLK_SPACE)
				{
					_camPos.y -= 2.0f;
				}
				if (event.key.keysym.sym == SDLK_LSHIFT)
				{
					_camPos.y += 2.0f;
				}
			}
		}

		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplSDL2_NewFrame(_window);

		ImGui::NewFrame();

		ImGui::ShowDemoWindow();

		draw();
	}
}

void VulkanEngine::cleanup()
{
	// wait till GPU finishes
	vkDeviceWaitIdle(_device);

	_mainDeleteionQueue.flush();

	vmaDestroyAllocator(_allocator);

	vkDestroyDevice(_device, nullptr);
	vkDestroySurfaceKHR(_instance, _surface, nullptr);
	vkb::destroy_debug_utils_messenger(_instance, _debugMessenger);
	vkDestroyInstance(_instance, nullptr);

	if (_window != nullptr)
	{
		SDL_DestroyWindow(_window);
	}
}

VkPipeline PipelineBuilder::buildPipeline(VkDevice device, VkRenderPass pass)
{
	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.pNext = nullptr;

	viewportState.viewportCount = 1;
	viewportState.pViewports = &_viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &_scissor;

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.pNext = nullptr;

	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &_colorBlendAttachment;

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = nullptr;

	pipelineInfo.stageCount = _shaderStages.size();
	pipelineInfo.pStages = _shaderStages.data();
	pipelineInfo.pVertexInputState = &_vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &_inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &_rasterizer;
	pipelineInfo.pMultisampleState = &_multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.layout = _pipelineLayout;
	pipelineInfo.renderPass = pass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	pipelineInfo.pDepthStencilState = &_depthStencil;

	VkPipeline newPipeline;
	if (vkCreateGraphicsPipelines(
		device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &newPipeline
	) != VK_SUCCESS)
	{
		std::cout << "Failed to create pipeline\n";
		return VK_NULL_HANDLE;
	}
	else
	{
		return newPipeline;
	}
}
