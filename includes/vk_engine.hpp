#pragma once
#include "vk_types.hpp"

class VulkanEngine
{
    public:
        VkExtent2D _windowExtent{1280, 720};
        struct SDL_Window* _window{nullptr};


    public:
        void init();
        void cleanup();
        void draw();
        void run();
};