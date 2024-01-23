#include "vk_camera.hpp"

#include "glm/gtx/quaternion.hpp"

glm::mat4
Camera::get_view_matrix()
{
  glm::mat4 camera_translation = glm::translate(glm::mat4(1.0f), position);
  glm::mat4 camera_rot         = get_rotation_matrix();

  return glm::inverse(camera_translation * camera_rot);
}

glm::mat4
Camera::get_rotation_matrix()
{
  glm::quat pitch_rot = glm::angleAxis(pitch, glm::vec3{ 1.f, 0.f, 0.f });
  glm::quat yaw_rot   = glm::angleAxis(yaw, glm::vec3{ 0.f, -1.0f, 0.f });

  return glm::toMat4(yaw_rot) * glm::toMat4(pitch_rot);
}

void
Camera::process_SDL_events(SDL_Event& e)
{
  switch (e.type) {
    case SDL_KEYDOWN:
      switch (e.key.keysym.sym) {
        case SDLK_w:
          velocity.z = -1;
          break;
        case SDLK_s:
          velocity.z = 1;
          break;
        case SDLK_a:
          velocity.x = -1;
          break;
        case SDLK_d:
          velocity.x = 1;
          break;
        default:
          break;
      }
      break;
    case SDL_KEYUP:
      switch (e.key.keysym.sym) {
        case SDLK_w:
        case SDLK_s:
          velocity.z = 0;
          break;
        case SDLK_a:
        case SDLK_d:
          velocity.x = 0;
          break;
      }
      break;
    case SDL_MOUSEMOTION:
      yaw   += (float)e.motion.xrel / 600.f;
      pitch -= (float)e.motion.yrel / 600.f;
      break;
  }
}

void
Camera::update()
{
  glm::mat4 camera_rotation = get_rotation_matrix();
  position += glm::vec3(camera_rotation * glm::vec4(velocity * 0.1f, 0.f));
}
