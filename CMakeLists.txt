cmake_minimum_required(VERSION 3.0.0)
project(vk-sandbox VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
enable_testing()

find_package(Vulkan REQUIRED)
find_package(SDL2 REQUIRED)

include_directories(includes)
add_executable(${PROJECT_NAME} main.cpp sources/vk_engine.cpp sources/vk_initializers.cpp sources/VkBootstrap.cpp)

target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARIES})
target_link_libraries(${PROJECT_NAME} ${Vulkan_LIBRARY})

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)