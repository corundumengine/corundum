// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// The GPU context's graphics-backend seam: the part of GpuContext that differs between graphics
// backends. The shared GpuContext code calls only these; exactly one backend translation unit
// compiles and links per build, selected in engine/src/platform/CMakeLists.txt, so gpu_context.cpp
// carries no graphics-API conditional.

#include <expected>
#include <memory>
#include <string>

#include <sokol_gfx.h>

struct GLFWwindow;

namespace corundum::platform::glfw::gpu_backend {

/** @brief Per-context graphics state, defined separately by each backend's translation unit.
 *
 * A backend that holds native device/swapchain handles owns them here; one with none uses a null
 * handle. Incomplete on purpose: the shared GPU-context code only stores and forwards it.
 */
struct Context;

/** @brief Frees a Context; defined by each backend's translation unit beside create(). */
struct ContextDeleter {
  void operator()(Context *context) const noexcept;
};

/** @brief Owning handle to a backend's per-context state; empty when the backend needs none. */
using ContextHandle = std::unique_ptr<Context, ContextDeleter>;

/** @brief Create the backend state for the GLFW window @p win.
 *
 * @pre @p win is a valid GLFW window whose graphics surface is already set up.
 * @return The context, or an error when the backend's native surface is unavailable.
 */
[[nodiscard]] std::expected<ContextHandle, std::string> create(GLFWwindow *win);

/** @brief Fill the sokol device fields this backend needs before sg_setup().
 *
 * @return An error when the backend's device is unavailable.
 */
[[nodiscard]] std::expected<void, std::string> configure_sokol(sg_desc &desc, const Context *context);

/** @brief Complete @p swapchain for a @p width x @p height pass and acquire this frame's drawable.
 *
 * @return false when the backend has no drawable ready, so the caller skips the frame.
 */
[[nodiscard]] bool begin_pass(sg_swapchain &swapchain, Context *context, int width, int height) noexcept;

/** @brief Present or release this frame's native resources, after sg_commit(). */
void end_pass(Context *context, GLFWwindow *win) noexcept;

} // namespace corundum::platform::glfw::gpu_backend
