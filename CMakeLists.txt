cmake_minimum_required(VERSION 3.5.0)
project(vk-sandbox VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
enable_testing()

set(CMAKE_BUILD_TYPE Debug)

find_package(Vulkan REQUIRED)
find_package(SDL)

IF (${SDL_FOUND})
    MESSAGE(STATUS "SDL2 is found")
ELSE (${SDL_FOUND})
    MESSAGE(STATUS "Searching SDL directories")
    find_library(SDL2_LIBRARY NAME SDL2.lib HINTS $ENV{VULKAN_SDK}/Lib)
    find_library(SDL2MAIN_LIBRARY NAME SDL2main.lib HINTS $ENV{VULKAN_SDK}/Lib)

    if(NOT SDL2_LIBRARY)
        message(FATAL_ERROR "SDL2.lib is not found")
    else ()
        message(STATUS ${SDL2_LIBRARY})
    endif ()

    if(NOT SDL2MAIN_LIBRARY)
        message(FATAL_ERROR "SDL2main.lib is not found")
    else ()
        message(STATUS ${SDL2MAIN_LIBRARY})
    endif()

ENDIF (${SDL_FOUND})

file(GLOB cpp_source_files "${CMAKE_SOURCE_DIR}/sources/*.cpp")
file(GLOB cpp_imgui_main_files "third-party/imgui/*.cpp")
file(GLOB cpp_imgui_backend_files "third-party/imgui/backends/*.cpp")

include_directories(
    includes
    ${Vulkan_INCLUDE_DIRS}
    third-party/VkBootstrap
    third-party/stb-image
    third-party/fastgltf/include/fastgltf
    third-party/fmt/include/fmt
    third-party/imgui 
    third-party/imgui/backends
)

link_directories($ENV{VULKAN_SDK}/Lib/)

add_executable(${PROJECT_NAME} main.cpp third-party/VkBootstrap/VkBootstrap.cpp ${cpp_source_files} ${cpp_imgui_main_files} ${cpp_imgui_backend_files})

target_compile_definitions(${PROJECT_NAME} PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)
target_precompile_headers(${PROJECT_NAME} PUBLIC <optional> <vector> <memory> <string> <vector> <unordered_map> <glm/mat4x4.hpp>  <glm/vec4.hpp> <vulkan/vulkan.h>)
target_link_libraries(${PROJECT_NAME} ${Vulkan_LIBRARIES} ${SDL2_LIBRARY} ${SDL2MAIN_LIBRARY} fmt fastgltf fastgltf_simdjson)
add_subdirectory(third-party/fastgltf)
add_subdirectory(third-party/fmt)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)