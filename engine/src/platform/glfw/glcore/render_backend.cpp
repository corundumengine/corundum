// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

// GLCore implementation of the renderer's graphics seam. Compiled on every non-Apple platform
// (engine/src/platform/CMakeLists.txt).

#include "render_backend.hpp"

#include "render_scale.hpp"

#include <sokol_gfx.h>

namespace corundum::platform::glfw::render_backend {

  namespace {
    constexpr const char *k_vertex_shader = R"(#version 330 core
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texcoord;
layout(location = 2) in vec4 color;
uniform mat4 proj;
out vec2 v_texcoord;
out vec4 v_color;
void main() {
    gl_Position = proj * vec4(position, 0.0, 1.0);
    v_texcoord = texcoord;
    v_color = color;
}
)";

    constexpr const char *k_fragment_shader = R"(#version 330 core
in vec2 v_texcoord;
in vec4 v_color;
uniform sampler2D tex;
out vec4 frag_color;
void main() {
    frag_color = texture(tex, v_texcoord) * v_color;
}
)";
  } // namespace

  const char *vertex_shader_source() noexcept {
    return k_vertex_shader;
  }

  const char *fragment_shader_source() noexcept {
    return k_fragment_shader;
  }

  void configure_shader(sg_shader_desc &desc) noexcept {
    // GLSL uses the fixed `main`; the GL backend ignores the MSL-style entry names.
    desc.vertex_func.entry = "main";
    desc.fragment_func.entry = "main";
    // The GL backend resolves individual uniform locations, so it needs the glsl_uniforms list and
    // a glsl_name for the texture/sampler pair.
    desc.uniform_blocks[0].glsl_uniforms[0] = {.type = SG_UNIFORMTYPE_MAT4, .array_count = 1, .glsl_name = "proj"};
    desc.texture_sampler_pairs[0].glsl_name = "tex";
  }

  float render_scale(int fb_w, int fb_h, int win_w, int win_h) noexcept {
    // No layer up-scales the default framebuffer, so render at the framebuffer size;
    // compute_render_resolution() clamps the result per axis.
    return native_render_scale(fb_w, fb_h, win_w, win_h);
  }

} // namespace corundum::platform::glfw::render_backend
