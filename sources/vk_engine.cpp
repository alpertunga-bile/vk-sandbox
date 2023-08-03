#include "../includes/vk_engine.hpp"
#include "../includes/vk_types.hpp"
#include "../includes/vk_initializers.hpp"

#include "../includes/VkBootstrap.h"

#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <fstream>
#include <iostream>
#include "vk_engine.hpp"

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

    vkb::PhysicalDeviceSelector deviceSelector{vkbInstance};
    vkb::PhysicalDevice physicalDevice = deviceSelector.set_minimum_version(1, 1)
                                        .set_surface(_surface)
                                        .select()
                                        .value();

    vkb::DeviceBuilder deviceBuilder{physicalDevice};
    vkb::Device vkbDevice = deviceBuilder.build().value();

    _device = vkbDevice.device;
    _physicalDevice = physicalDevice.physical_device;

    _graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    _graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.physicalDevice = _physicalDevice;
    allocatorInfo.device = _device;
    allocatorInfo.instance = _instance;

    vmaCreateAllocator(&allocatorInfo, &_allocator);
}

void VulkanEngine::initSwapchain()
{
    vkb::SwapchainBuilder swapchainBuilder{_physicalDevice, _device, _surface};
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
}

void VulkanEngine::initCommands()
{
    VkCommandPoolCreateInfo commandPoolInfo = vkInit::commandPoolCreateInfo(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_commandPool));

    VkCommandBufferAllocateInfo cmdAllocInfo = vkInit::commandBufferAllocateInfo(_commandPool, 1);
    VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_mainCommandBuffer));

    _mainDeleteionQueue.pushFunction([=]() {
        vkDestroyCommandPool(_device, _commandPool, nullptr);
        });
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

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

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

    for(int i = 0; i < swapchainImageCount; i++)
    {
        fbInfo.pAttachments = &_swapchainImageViews[i];
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

    VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_renderFence));

    _mainDeleteionQueue.pushFunction([=]() {
        vkDestroyFence(_device, _renderFence, nullptr);
        });

    VkSemaphoreCreateInfo semaphoreCreateInfo = vkInit::semaphoreCreateInfo();

    VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_presentSemaphore));
    VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_renderSemaphore));

    _mainDeleteionQueue.pushFunction([=]() {
        vkDestroySemaphore(_device, _presentSemaphore, nullptr);
        vkDestroySemaphore(_device, _renderSemaphore, nullptr);
        });
}

void VulkanEngine::initPipelines()
{
    // ---------------------------------------------------------------------------------------------------------------
    // -- Colored Triangle
    // ---------------------------------------------------------------------------------------------------------------

    VkShaderModule triangleVertShader;
    if(!loadShaderModule("shaders/colored_triangle_vert.spv", triangleVertShader))
    {
        std::cout << "Error building colored_triangle_vert.spv shader\n";
    }
    else
    {
        std::cout << "colored_triangle_vert.spv is loaded\n";
    }

    VkShaderModule triangleFragShader;
    if(!loadShaderModule("shaders/colored_triangle_frag.spv", triangleFragShader))
    {
        std::cout << "Error building colored_triangle_frag.spv shader\n";
    }
    else
    {
        std::cout << "colored_triangle_frag.spv is loaded\n";
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkInit::pipelineLayoutCreateInfo();

    VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_trianglePipelineLayout));

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

    pipelineBuilder._scissor.offset = {0, 0};
    pipelineBuilder._scissor.extent = _windowExtent;

    pipelineBuilder._rasterizer = vkInit::rasterizationStateCreateInfo(VK_POLYGON_MODE_FILL);

    pipelineBuilder._multisampling = vkInit::multisamplingStateCreateInfo();
    pipelineBuilder._colorBlendAttachment = vkInit::colorBlendAttachmentState();

    pipelineBuilder._pipelineLayout = _trianglePipelineLayout;

    _trianglePipeline = pipelineBuilder.buildPipeline(_device, _renderpass);

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


    pipelineBuilder._shaderStages.clear();

    pipelineBuilder._shaderStages.push_back(
        vkInit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, redTriangleVertShader)
    );

    pipelineBuilder._shaderStages.push_back(
        vkInit::pipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, redTriangleFragShader)
    );

    _redTrianglePipeline = pipelineBuilder.buildPipeline(_device, _renderpass);

    // ---------------------------------------------------------------------------------------------------------------
    // -- Mesh Triangle
    // ---------------------------------------------------------------------------------------------------------------
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

    _meshPipeline = pipelineBuilder.buildPipeline(_device, _renderpass);

    vkDestroyShaderModule(_device, redTriangleVertShader, nullptr);
    vkDestroyShaderModule(_device, redTriangleFragShader, nullptr);
    vkDestroyShaderModule(_device, triangleVertShader, nullptr);
    vkDestroyShaderModule(_device, triangleFragShader, nullptr);
    vkDestroyShaderModule(_device, meshVertShader, nullptr);
    vkDestroyShaderModule(_device, meshFragShader, nullptr);

    _mainDeleteionQueue.pushFunction([=]() {
        vkDestroyPipeline(_device, _redTrianglePipeline, nullptr);
        vkDestroyPipeline(_device, _trianglePipeline, nullptr);
        vkDestroyPipeline(_device, _meshPipeline, nullptr);

        vkDestroyPipelineLayout(_device, _trianglePipelineLayout, nullptr);
        });
}

bool VulkanEngine::loadShaderModule(const char *path, VkShaderModule& outShaderModule)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);

    if(!file.is_open())
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
    if(vkCreateShaderModule(_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
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

    uploadMesh(_triangleMesh);
}

void VulkanEngine::uploadMesh(Mesh& mesh)
{
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = mesh._vertices.size() * sizeof(Vertex);
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

    VmaAllocationCreateInfo vmaallocInfo = {};
    vmaallocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    VK_CHECK(vmaCreateBuffer(
        _allocator,
        &bufferInfo,
        &vmaallocInfo,
        &mesh._vertexBuffer._buffer,
        &mesh._vertexBuffer._allocation,
        nullptr
    ));

    _mainDeleteionQueue.pushFunction([=]() {
        vmaDestroyBuffer(_allocator, mesh._vertexBuffer._buffer, mesh._vertexBuffer._allocation);
        });

    void* data;
    vmaMapMemory(_allocator, mesh._vertexBuffer._allocation, &data);

    memcpy(data, mesh._vertices.data(), mesh._vertices.size() * sizeof(Vertex));

    vmaUnmapMemory(_allocator, mesh._vertexBuffer._allocation);
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
    initPipelines();

    loadMeshes();
}

void VulkanEngine::draw()
{
    VK_CHECK(vkWaitForFences(_device, 1, &_renderFence, true, 1000000000));
    VK_CHECK(vkResetFences(_device, 1, &_renderFence));

    _framenumber += 1.0f;

    uint32_t swapchainImageIndex;
    VK_CHECK(vkAcquireNextImageKHR(_device, _swapchain, 1000000000, _presentSemaphore, nullptr, &swapchainImageIndex));

    VK_CHECK(vkResetCommandBuffer(_mainCommandBuffer, 0));

    VkCommandBuffer cmd = _mainCommandBuffer;

    VkCommandBufferBeginInfo cmdBeginInfo = {};
    cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBeginInfo.pNext = nullptr;
    cmdBeginInfo.pInheritanceInfo = nullptr;
    cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    VkClearValue clearValue;
    clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderPassBeginInfo rpBeginInfo = {};
    rpBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBeginInfo.pNext = nullptr;
    rpBeginInfo.renderPass = _renderpass;
    rpBeginInfo.renderArea.offset.x = 0;
    rpBeginInfo.renderArea.offset.y = 0;
    rpBeginInfo.renderArea.extent = _windowExtent;
    rpBeginInfo.framebuffer = _framebuffers[swapchainImageIndex];

    rpBeginInfo.clearValueCount = 1;
    rpBeginInfo.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &rpBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    if (_selectedShader == 0)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _trianglePipeline);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    else if (_selectedShader == 1)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _redTrianglePipeline);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    else if (_selectedShader == 2)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipeline);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &_triangleMesh._vertexBuffer._buffer, &offset);
        vkCmdDraw(cmd, _triangleMesh._vertices.size(), 1, 0, 0);
    }

    vkCmdEndRenderPass(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.pNext = nullptr;

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    submit.pWaitDstStageMask = &waitStage;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &_presentSemaphore;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &_renderSemaphore;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;

    VK_CHECK(vkQueueSubmit(_graphicsQueue, 1, &submit, _renderFence));

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = nullptr;

    presentInfo.pSwapchains = &_swapchain;
    presentInfo.swapchainCount = 1;

    presentInfo.pWaitSemaphores = &_renderSemaphore;
    presentInfo.waitSemaphoreCount = 1;

    presentInfo.pImageIndices = &swapchainImageIndex;

    VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));
}

void VulkanEngine::run()
{
    SDL_Event event;
    bool isQuit = false;

    while(!isQuit)
    {
        // handling events
        while(SDL_PollEvent(&event) != 0)
        {
            if(event.type == SDL_QUIT)
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
            }
        }

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

    if(_window != nullptr)
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

    VkPipeline newPipeline;
    if(vkCreateGraphicsPipelines(
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
