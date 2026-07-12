#pragma once
#include <corundum/core/math/vec.hpp>

#include <expected>
#include <memory>
#include <string>
#include <utility>

namespace corundum::platform {

  class Window;

  /** @brief RAII sokol_gfx device lifecycle and per-frame render pass.
   *
   * Owns the sokol_gfx device (sg_setup/sg_shutdown), the default render
   * pass (swapchain + sg_begin_pass/sg_end_pass), and frame finalisation
   * (sg_commit + present).  Designed to be shared by a game Renderer
   * (SokolRenderer) and an editor ToolHost without pulling in sokol or
   * GLFW types.
   *
   * @note Not thread-safe. Call only from the render thread.
   */
  class GpuContext {
  public:
    /** @brief Create a sokol_gfx device for the given window.
     *
     * @param[in] window  A fully-initialised platform Window.
     * @pre @p window must be a GLFWWindow (downcast internally).
     * @return Owning pointer on success, or std::unexpected with an error message.
     */
    [[nodiscard]] static std::expected<std::unique_ptr<GpuContext>, std::string> create(Window &window);

    ~GpuContext();

    GpuContext(const GpuContext &) = delete;
    GpuContext &operator=(const GpuContext &) = delete;
    GpuContext(GpuContext &&) noexcept = delete;
    GpuContext &operator=(GpuContext &&) noexcept = delete;

    /** @brief Start a new default render pass.
     *
     * Builds the swapchain from the current framebuffer size and clears to
     * the given colour. On macOS Metal this may fail if there is no drawable
     * ready — in that case the pass is skipped and @c false is returned so
     * the caller can avoid issuing draw commands.
     *
     * @param[in] clear  RGBA clear colour (8-bit channels).
     * @return @c true if the pass was started; @c false if the frame was skipped.
     */
    bool begin_default_pass(core::math::Colour clear);

    /** @brief End the current render pass and present the frame.
     *
     * If begin_default_pass() returned @c false earlier in the frame this
     * is a no-op.
     */
    void end_frame();

    /** @brief Logical window size in screen coordinates. */
    [[nodiscard]] std::pair<int, int> window_size() const noexcept;

    /** @brief Physical framebuffer size in pixels (retina-aware). */
    [[nodiscard]] std::pair<int, int> framebuffer_size() const noexcept;

    /** @brief Content scale factor (e.g. 2.0 on Retina displays). */
    [[nodiscard]] float dpi_scale() const noexcept;

  private:
    GpuContext();
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };

} // namespace corundum::platform
