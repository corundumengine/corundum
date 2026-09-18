// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>

#include <expected>
#include <memory>
#include <string>
#include <utility>

namespace corundum::platform {

  class Window;

  /** @brief Owns the GPU device and drives the default render pass.
   *
   * A live context is what keeps every GPU resource valid, so it must outlive the
   * renderer and the texture cache that draw through it. It exists so the game
   * renderer and the editor tool host share one device without either of them
   * knowing which graphics backend sits behind it.
   *
   * @note The device is process-wide, so at most one context may be live; a second
   *       create() is reported as an error rather than left to the backend.
   * @note Not thread-safe. Call only from the render thread.
   */
  class GpuContext {
  public:
    /** @brief Create a GPU device for the given window.
     *
     * @pre @p window must come from the linked platform backend; a window from
     *      another backend is reported as an error rather than downcast unchecked.
     * @pre @p window must outlive the returned context, which borrows its native
     *      handle and render target.
     * @return Owning pointer on success, or std::unexpected with the reason.
     */
    [[nodiscard]] static std::expected<std::unique_ptr<GpuContext>, std::string> create(Window &window);

    ~GpuContext();

    GpuContext(const GpuContext &) = delete;
    GpuContext &operator=(const GpuContext &) = delete;
    GpuContext(GpuContext &&) noexcept = delete;
    GpuContext &operator=(GpuContext &&) noexcept = delete;

    /** @brief Start the default render pass, clearing to @p clear.
     *
     * @param[in] clear  RGBA clear colour (8-bit channels).
     * @return @c true if the pass was started; @c false if the frame was skipped
     *         because no render target was ready, in which case the caller must
     *         issue no draws this frame.
     */
    [[nodiscard]] bool begin_default_pass(core::math::Colour clear);

    /** @brief End a pass started by begin_default_pass() and present the frame.
     *
     * No-op if begin_default_pass() returned @c false earlier in the frame.
     */
    void end_frame();

    /** @brief Logical window size in screen coordinates. */
    [[nodiscard]] std::pair<int, int> window_size() const noexcept;

    /** @brief Physical framebuffer size in pixels (high-DPI aware). */
    [[nodiscard]] std::pair<int, int> framebuffer_size() const noexcept;

    /** @brief Content scale factor of the window's monitor (x axis; e.g. 2.0 on a high-DPI display). */
    [[nodiscard]] float dpi_scale() const noexcept;

  private:
    GpuContext();
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };

} // namespace corundum::platform
