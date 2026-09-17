// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace corundum::platform {

  /** @brief Metadata for one live texture. */
  struct TextureInfo {
    /** @brief Height in pixels. */
    unsigned height{0};

    /** @brief 1-based texture id; 0 is the invalid sentinel. */
    uint32_t id{0};

    /** @brief Width in pixels. */
    unsigned width{0};
  };

  /** @brief Opaque handles that identify one live texture to the linked backend. */
  struct BackendTexture {
    /** @brief Backend sampler handle; 0 when the texture is not live. */
    uint64_t sampler{0};

    /** @brief Backend view handle; 0 when the texture is not live. */
    uint64_t view{0};
  };

  /** @brief Texture wrap mode for the sampler. */
  enum class WrapMode : std::uint8_t { Clamp, Repeat };

  /** @brief Registry of the textures the platform backend owns for a renderer or an editor.
   *
   * Loads RGBA8 textures from image files, or creates them from a raw RGBA8
   * pixel buffer. Every texture gets its own NEAREST-filtered sampler.
   *
   * Ids are 1-based; 0 is the invalid sentinel. A cache is only valid while the
   * platform GPU context behind it is alive.
   *
   * @note Not thread-safe. Call only from the render thread.
   */
  class TextureCache {
  public:
    TextureCache();
    ~TextureCache();

    TextureCache(const TextureCache &) = delete;
    TextureCache &operator=(const TextureCache &) = delete;
    TextureCache(TextureCache &&) noexcept = delete;
    TextureCache &operator=(TextureCache &&) noexcept = delete;

    /** @brief Load an RGBA8 texture from a PNG, BMP or TGA file.
     *
     * @param[in] path  Path to the image file.
     * @return TextureInfo on success, or std::unexpected with an error message.
     */
    [[nodiscard]] std::expected<TextureInfo, std::string> load(std::string_view path);

    /** @brief Create an RGBA8 texture from a raw pixel buffer.
     *
     * @param[in] w     Width in pixels; must be non-zero.
     * @param[in] h     Height in pixels; must be non-zero.
     * @param[in] rgba  @p w * @p h RGBA8 pixels, borrowed for the call only.
     * @param[in] wrap  Sampler wrap mode (clamp by default).
     * @return TextureInfo on success, or std::unexpected with an error message.
     */
    [[nodiscard]] std::expected<TextureInfo, std::string> create(unsigned w, unsigned h, const void *rgba,
                                                                 WrapMode wrap = WrapMode::Clamp);

    /** @brief Destroy a texture and release its GPU resources.
     *
     * @pre Must be called between frames, not during an active render pass.
     * @param[in] id  Texture id returned by load() or create(). No-op if 0 or already destroyed.
     */
    void destroy(uint32_t id) noexcept;

    /** @brief Query metadata for an existing texture.
     *
     * @param[in] id  Texture id returned by load() or create().
     * @return TextureInfo if the texture is still alive, std::nullopt otherwise.
     */
    [[nodiscard]] std::optional<TextureInfo> info(uint32_t id) const;

    /** @brief Get the handles that identify @p id in the linked backend's own API.
     *
     * @param[in] id  Texture id returned by load() or create().
     * @return Both handles, or a zeroed BackendTexture if @p id is not live.
     */
    [[nodiscard]] BackendTexture backend_handle(uint32_t id) const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };

} // namespace corundum::platform
