#pragma once

#include "vk_types.hpp"
#include "vk-mesh.hpp"
#include <vector>
#include <deque>
#include <functional>
#include <glm/glm.hpp>
#include <unordered_map>

constexpr unsigned int FRAME_OVERLAP = 2;

struct MeshPushConstants
{
    glm::vec4 data;
    glm::mat4 renderMatrix;
};

struct Material
{
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
};

struct RenderObject
{
    Mesh* mesh;
    Material* material;
    glm::mat4 transformMatrix;
};

struct GPUCameraData
{
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 viewproj;
};

struct GPUSceneData
{
    glm::vec4 fogColor;
    glm::vec4 fogDistance;
    glm::vec4 ambientColor;
    glm::vec4 sunlightDirection;
    glm::vec4 sunlightColor;
};

struct FrameData
{
    VkSemaphore _presentSemaphore, _renderSemaphore;
    VkFence _renderFence;

    VkCommandPool _commandPool;
    VkCommandBuffer _mainCommandBuffer;

    AllocatedBuffer cameraBuffer;
    VkDescriptorSet globalDescriptor;
};

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

        VkRenderPass _renderpass;
        std::vector<VkFramebuffer> _framebuffers;

        DeletionQueue _mainDeleteionQueue;

        VmaAllocator _allocator;
        VkPipeline _meshPipeline;
        Mesh _triangleMesh;

        VkPipelineLayout _meshPipelineLayout;

        Mesh _modelMesh;

        VkImageView _depthImageView;
        AllocatedImage _depthImage;
        VkFormat _depthFormat;

        std::vector<RenderObject> _renderables;
        std::unordered_map<std::string, Material> _materials;
        std::unordered_map<std::string, Mesh> _meshes;

        unsigned int _framenumber = 0;
        int _selectedShader{ 0 };
        glm::vec3 _camPos = { 0.0f, -6.0f, -10.0f };
        
        FrameData _frames[FRAME_OVERLAP];

        VkDescriptorSetLayout _globalSetLayout;
        VkDescriptorPool _descriptorPool;

        VkPhysicalDeviceProperties _gpuProperties;

        GPUSceneData _sceneParameters;
        AllocatedBuffer _sceneParameterBuffer;
        
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
        void loadMeshes();
        void uploadMesh(Mesh& mesh);
        Material* createMaterial(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name);
        Material* getMaterial(const std::string& name);
        Mesh* getMesh(const std::string& name);
        void drawObjects(VkCommandBuffer cmd, RenderObject* first, int count);
        void initScene();
        FrameData& getCurrentFrame();
        AllocatedBuffer createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
        void init_descriptors();
        size_t padUniformBufferSize(size_t originalSize);
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
    VkPipelineDepthStencilStateCreateInfo _depthStencil;

    VkPipeline buildPipeline(VkDevice device, VkRenderPass pass);
};