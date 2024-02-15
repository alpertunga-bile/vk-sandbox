#version 460

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

#include "input_structures.glsl"

layout(location = 0) out vec3 out_normal;
layout(location = 1) out vec3 out_color;
layout(location = 2) out vec2 out_uv;

struct Vertex
{
  vec3  position;
  float uv_x;
  vec3  normal;
  float uv_y;
  vec4  color;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer
{
  Vertex vertices[];
};

layout(push_constant) uniform constants
{
  mat4         render_matrix;
  VertexBuffer vertex_buffer;
}
PushConstants;

void
main()
{
  Vertex v        = PushConstants.vertex_buffer.vertices[gl_VertexIndex];
  vec4   position = vec4(v.position, 1.0);

  gl_Position = scene_data.view_proj * PushConstants.render_matrix * position;

  out_normal = transpose(mat3(PushConstants.render_matrix)) * v.normal;
  out_color  = v.color.xyz * material_data.color_factors.xyz;
  out_uv.x   = v.uv_x;
  out_uv.y   = v.uv_y;
}