// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// The renderer's graphics-backend seam: the shader sources/bindings and render-scale policy that
// differ between graphics backends. The shared renderer calls only these; exactly one backend
// translation unit compiles and links per build, selected in engine/src/platform/CMakeLists.txt,
// so sokol_renderer.cpp carries no graphics-API conditional.

#include <sokol_gfx.h>

namespace corundum::platform::glfw::render_backend {

  /** @brief Vertex-shader source in this backend's shading language. */
  [[nodiscard]] const char *vertex_shader_source() noexcept;

  /** @brief Fragment-shader source in this backend's shading language. */
  [[nodiscard]] const char *fragment_shader_source() noexcept;

  /** @brief Fill the shader-descriptor fields that differ by backend: entry points and bindings.
   *
   * @pre @p desc's backend-independent fields (stages, sizes, formats, slots) are already set.
   */
  void configure_shader(sg_shader_desc &desc) noexcept;

  /** @brief Render scale this backend needs to span a @p fb_w x @p fb_h framebuffer of a
   *         @p win_w x @p win_h logical window.
   */
  [[nodiscard]] float render_scale(int fb_w, int fb_h, int win_w, int win_h) noexcept;

} // namespace corundum::platform::glfw::render_backend
