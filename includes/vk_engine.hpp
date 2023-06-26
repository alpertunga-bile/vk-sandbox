#pragma once
#include "vk_types.hpp"
#include <vector>

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

    public:
        void init();
        void cleanup();
        void draw();
        void run();

    private:
        void initVulkan();
        void initSwapchain();
};