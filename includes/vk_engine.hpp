#pragma once

#include "vk_types.hpp"
#include <vector>
#include <deque>
#include <functional>

struct DeletionQueue
{
    std::deque<std::function<void()>> deletors;

    void pushFunction(std::function<void()>&& function)
    {
        deletors.push_back(function);
    }

    void flush()
    {
        for (auto it = deletors.rbegin(); it != deletors.rend(); it++)
        {
            (*it)();
        }

        deletors.clear();
    }
};

class VulkanEngine
{
    private:
        VkExtent2D _windowExtent{1280, 720};
        struct SDL_Window* _window{nullptr};

        VkInstance _instance;
        VkDebugUtilsMessengerEXT _debugMessenger;
        VkPhysicalDevice _physicalDevice;
        VkDevice _device;
        VkSurfaceKHR _surface;

        VkSwapchainKHR _swapchain;
        VkFormat _swapchainImageFormat;
        std::vector<VkImage> _swapchainImages; // actual image
        std::vector<VkImageView> _swapchainImageViews; // wrapper for image

        VkQueue _graphicsQueue;
        uint32_t _graphicsQueueFamily;

        VkCommandPool _commandPool;
        VkCommandBuffer _mainCommandBuffer;

        VkRenderPass _renderpass;
        std::vector<VkFramebuffer> _framebuffers;

        VkSemaphore _presentSemaphore, _renderSemaphore;
        VkFence _renderFence;

        VkPipelineLayout _trianglePipelineLayout;
        VkPipeline _trianglePipeline;
        VkPipeline _redTrianglePipeline;

        DeletionQueue _mainDeleteionQueue;

        float _framenumber = 0.0f;
        int _selectedShader{ 0 };

    public:
        void init();
        void cleanup();
        void draw();
        void run();

    private:
        void initVulkan();
        void initSwapchain();
        void initCommands();
        void initDefaultRenderpass();
        void initFramebuffers();
        void initSyncStructures();
        void initPipelines();
        bool loadShaderModule(const char* path, VkShaderModule& outShaderModule);
};

class PipelineBuilder
{
public:
    std::vector<VkPipelineShaderStageCreateInfo> _shaderStages;
    VkPipelineVertexInputStateCreateInfo _vertexInputInfo;
    VkPipelineInputAssemblyStateCreateInfo _inputAssembly;
    VkViewport _viewport;
    VkRect2D _scissor;
    VkPipelineRasterizationStateCreateInfo _rasterizer;
    VkPipelineColorBlendAttachmentState _colorBlendAttachment;
    VkPipelineMultisampleStateCreateInfo _multisampling;
    VkPipelineLayout _pipelineLayout;

    VkPipeline buildPipeline(VkDevice device, VkRenderPass pass);
};