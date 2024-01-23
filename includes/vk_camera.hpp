#pragma once

#include "SDL2/SDL_events.h"
#include "vk_types.hpp"

class Camera
{
public:
  glm::vec3 velocity;
  glm::vec3 position;

  float pitch{ 0.f };
  float yaw{ 0.f };

  glm::mat4 get_view_matrix();
  glm::mat4 get_rotation_matrix();

  void process_SDL_events(SDL_Event& e);

  void update();
};