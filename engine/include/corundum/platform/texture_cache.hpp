#pragma once
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace corundum::platform {

  class GpuContext;

  /** @brief Lightweight texture descriptor returned by load() and create(). */
  struct TextureInfo {
    uint32_t id;
    unsigned width;
    unsigned height;
  };

  /** @brief Opaque backend handles for embedding the texture in an ImGui draw list.
   *
   * The @c view and @c sampler fields are raw sokol_gfx resource IDs cast to
   * uint64_t so no sokol types leak into the public header. Consumers that
   * link sokol_imgui convert them back via
   * @code
   *   simgui_imtextureid_with_sampler(sg_view{bt.view}, sg_sampler{bt.sampler})
   * @endcode
   */
  struct BackendTexture {
    uint64_t view;
    uint64_t sampler;
  };

  /** @brief Texture wrap mode for the sampler. */
  enum class WrapMode { clamp, repeat };

  /** @brief Thread-safe texture registry backed by sokol_gfx.
   *
   * Loads RGBA8 textures from disk (stb_image) or creates them from raw
   * RGBA8 pixel buffers. Every texture gets its own NEAREST-filtered
   * sampler with the requested wrap mode.
   *
   * IDs are 1-based; 0 is the invalid sentinel.
   *
   * @note destroy() must be called between frames, not mid-pass.
   * @note Not thread-safe. Call only from the render thread.
   */
  class TextureCache {
  public:
    /** @brief Construct a TextureCache tied to the given GPU context.
     *
     * The GPU context must outlive the cache so the sokol device is still
     * alive when the destructor tears down GPU resources.
     */
    explicit TextureCache(GpuContext &ctx);
    ~TextureCache();

    TextureCache(const TextureCache &) = delete;
    TextureCache &operator=(const TextureCache &) = delete;
    TextureCache(TextureCache &&) noexcept = delete;
    TextureCache &operator=(TextureCache &&) noexcept = delete;

    /** @brief Load an RGBA8 texture from a PNG/BMP/TGA file via stb_image.
     *
     * @param[in] path  Path to the image file.
     * @return TextureInfo on success, or std::unexpected with an error message.
     */
    [[nodiscard]] std::expected<TextureInfo, std::string> load(std::string_view path);

    /** @brief Create an RGBA8 texture from a raw pixel buffer.
     *
     * @param[in] w      Width in pixels.
     * @param[in] h      Height in pixels.
     * @param[in] rgba   w * h RGBA8 pixels (ownership lies with the caller).
     * @param[in] wrap   Sampler wrap mode (clamp by default).
     * @return TextureInfo for the created texture.
     */
    [[nodiscard]] TextureInfo create(unsigned w, unsigned h, const uint32_t *rgba, WrapMode wrap = WrapMode::clamp);

    /** @brief Destroy a texture and release its GPU resources.
     *
     * @pre Must be called between frames, not during an active render pass.
     * @param[in] id  Texture ID returned by load() or create(). No-op if 0 or invalid.
     */
    void destroy(uint32_t id) noexcept;

    /** @brief Query metadata for an existing texture.
     *
     * @param[in] id  Texture ID returned by load() or create().
     * @return TextureInfo if the texture is still alive, std::nullopt otherwise.
     */
    [[nodiscard]] std::optional<TextureInfo> info(uint32_t id) const;

    /** @brief Get raw backend handles for ImGui integration.
     *
     * @param[in] id  Texture ID returned by load() or create().
     * @return BackendTexture with both view and sampler IDs, or {0, 0} if invalid.
     */
    [[nodiscard]] BackendTexture backend_handle(uint32_t id) const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };

} // namespace corundum::platform
