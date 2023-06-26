#include <iostream>
#include "includes/vk_engine.hpp"

int main()
{
    VulkanEngine engine;

    engine.init();
    engine.run();
    engine.cleanup();

    return 0;
}