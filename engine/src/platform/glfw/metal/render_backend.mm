// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

// Metal implementation of the renderer's graphics seam. Compiled only on Apple
// (engine/src/platform/CMakeLists.txt).

#include "render_backend.hpp"

#include <sokol_gfx.h>

namespace corundum::platform::glfw::render_backend {

  namespace {
    constexpr const char *k_vertex_shader = R"(
#include <metal_stdlib>
using namespace metal;
struct Vertex {
    float2 position [[attribute(0)]];
    float2 texcoord [[attribute(1)]];
    float4 color    [[attribute(2)]];
};
struct Varyings {
    float4 clip_pos [[position]];
    float2 texcoord;
    float4 color;
};
struct UB { float4x4 proj; };
vertex Varyings vs_main(Vertex in [[stage_in]], constant UB& ub [[buffer(0)]]) {
    Varyings out;
    out.clip_pos = ub.proj * float4(in.position, 0.0, 1.0);
    out.texcoord = in.texcoord;
    out.color = in.color;
    return out;
}
)";

    constexpr const char *k_fragment_shader = R"(
#include <metal_stdlib>
using namespace metal;
struct Varyings {
    float4 clip_pos [[position]];
    float2 texcoord;
    float4 color;
};
fragment float4 fs_main(Varyings in [[stage_in]],
                        texture2d<float> tex [[texture(0)]],
                        sampler samp [[sampler(0)]]) {
    return tex.sample(samp, in.texcoord) * in.color;
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
    desc.vertex_func.entry = "vs_main";
    desc.fragment_func.entry = "fs_main";
    desc.uniform_blocks[0].msl_buffer_n = 0;
    desc.views[0].texture.msl_texture_n = 0;
    desc.samplers[0].msl_sampler_n = 0;
  }

  float render_scale(int /*fb_w*/, int /*fb_h*/, int /*win_w*/, int /*win_h*/) noexcept {
    // The layer up-scales the drawable to the view, so the game renders one pixel per logical point
    // and fill cost stays decoupled from a high-DPI panel's pixel count.
    return 1.f;
  }

} // namespace corundum::platform::glfw::render_backend
