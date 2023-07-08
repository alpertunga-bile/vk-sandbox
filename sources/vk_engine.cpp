#include "../includes/vk_engine.hpp"
#include "../includes/vk_types.hpp"
#include "../includes/vk_initializers.hpp"

#include "../includes/VkBootstrap.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
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
}

void VulkanEngine::initCommands()
{
    VkCommandPoolCreateInfo commandPoolInfo = vkInit::commandPoolCreateInfo(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_commandPool));

    VkCommandBufferAllocateInfo cmdAllocInfo = vkInit::commandBufferAllocateInfo(_commandPool, 1);
    VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_mainCommandBuffer));
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
}

void VulkanEngine::draw()
{

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
        }

        draw();
    }
}

void VulkanEngine::cleanup()
{
    vkDestroyCommandPool(_device, _commandPool, nullptr);

    // images are destroyed with swapchain
    vkDestroySwapchainKHR(_device, _swapchain, nullptr);

    for(int i = 0; i < _swapchainImageViews.size(); i++)
    {
        vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
    }

    vkDestroyDevice(_device, nullptr);
    vkDestroySurfaceKHR(_instance, _surface, nullptr);
    vkb::destroy_debug_utils_messenger(_instance, _debugMessenger);
    vkDestroyInstance(_instance, nullptr);

    if(_window != nullptr)
    {
        SDL_DestroyWindow(_window);
    }
}