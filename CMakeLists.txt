cmake_minimum_required(VERSION 3.0.0)
project(vk-sandbox VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
enable_testing()

find_package(Vulkan REQUIRED)
find_package(SDL2 REQUIRED)

include_directories(includes)
add_executable(vk-sandbox main.cpp sources/vk_engine.cpp sources/vk_initializers.cpp)

include_directories(vk-sandbox ${SDL2_INCLUDE_DIRS})
target_link_libraries(vk-sandbox ${SDL2_LIBRARIES})

target_compile_features(vk-sandbox PRIVATE cxx_std_17)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
