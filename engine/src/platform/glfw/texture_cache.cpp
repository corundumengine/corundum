// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/platform/texture_cache.hpp>

#include "sokol_texture_upload.hpp"
#include "texture_byte_size.hpp"

#include <corundum/platform/texture_slot_table.hpp>
#include <sokol_gfx.h>
#include <stb_image.h>

#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace corundum::platform {

  namespace {

    /** @brief Backend handles belonging to one published texture. */
    struct TextureHandles {
      /** @brief Image the view was created from; 0 when creation failed. */
      sg_image image{};

      /** @brief Sampler owned together with the image. */
      sg_sampler sampler{};

      /** @brief View through which draws sample the image. */
      sg_view view{};
    };

    using SlotTable = TextureSlotTable<TextureHandles>;

    sg_sampler make_sampler(WrapMode wrap) {
      const sg_wrap mode = (wrap == WrapMode::Repeat) ? SG_WRAP_REPEAT : SG_WRAP_CLAMP_TO_EDGE;

      sg_sampler_desc desc{};
      desc.min_filter = SG_FILTER_NEAREST;
      desc.mag_filter = SG_FILTER_NEAREST;
      desc.wrap_u = mode;
      desc.wrap_v = mode;
      desc.label = "texture_cache";

      return sg_make_sampler(&desc);
    }

    /** @brief False unless every handle in @p handles is usable for drawing. */
    bool handles_valid(const TextureHandles &handles) {
      return sg_query_image_state(handles.image) == SG_RESOURCESTATE_VALID &&
             sg_query_sampler_state(handles.sampler) == SG_RESOURCESTATE_VALID &&
             sg_query_view_state(handles.view) == SG_RESOURCESTATE_VALID;
    }

    /** @brief Release every valid handle in @p handles. */
    void destroy_handles(TextureHandles handles) {
      if (handles.view.id != 0)
        sg_destroy_view(handles.view);
      if (handles.sampler.id != 0)
        sg_destroy_sampler(handles.sampler);
      if (handles.image.id != 0)
        sg_destroy_image(handles.image);
    }

    /** @brief Largest pixel dimension the sokol upload path accepts. */
    constexpr unsigned k_max_texture_dimension = static_cast<unsigned>(std::numeric_limits<int>::max());

    /** @brief Publish a freshly created handle set, or release it and report @p failure.
     *
     * @param[in] failure  Message returned when the backend rejected any handle.
     * @return The published texture's metadata, or std::unexpected with @p failure.
     */
    std::expected<TextureInfo, std::string> publish(SlotTable &slots, sg_image image, unsigned width, unsigned height,
                                                    WrapMode wrap, std::string failure) {
      const TextureHandles handles{
          .image = image,
          .sampler = make_sampler(wrap),
          .view = glfw::make_texture_view(image, "texture_cache"),
      };
      if (!handles_valid(handles)) {
        destroy_handles(handles);
        return std::unexpected(std::move(failure));
      }

      const uint32_t id = slots.adopt(handles, width, height);
      return TextureInfo{.height = height, .id = id, .width = width};
    }

  } // namespace

  struct TextureCache::Impl {
    SlotTable slots;
  };

  TextureCache::TextureCache() : impl_{std::make_unique<Impl>()} {}

  TextureCache::~TextureCache() {
    // Retire id by id rather than as a batch: releasing every handle in one pass
    // would have to hand them back in a container, and teardown has no room to allocate.
    const std::size_t count = impl_->slots.slot_count();
    for (uint32_t id = 1; id <= count; ++id) {
      const std::optional<TextureHandles> handles = impl_->slots.release(id);
      if (handles)
        destroy_handles(*handles);
    }
  }

  std::expected<TextureInfo, std::string> TextureCache::load(std::string_view path) {
    const std::string path_str{path};

    int w{};
    int h{};
    int channels{};
    stbi_uc *pixels = stbi_load(path_str.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (pixels == nullptr)
      return std::unexpected(std::string{"stb_image: "} + stbi_failure_reason());

    const sg_image image =
        glfw::make_rgba8_image(std::span<const uint8_t>{pixels, glfw::rgba8_byte_count(w, h)}, w, h, "texture_cache");
    stbi_image_free(pixels);

    return publish(impl_->slots, image, static_cast<unsigned>(w), static_cast<unsigned>(h), WrapMode::Clamp,
                   std::string{"TextureCache: could not create GPU resources for '"} + path_str + "'");
  }

  std::expected<TextureInfo, std::string> TextureCache::create(unsigned w, unsigned h, const void *rgba,
                                                               WrapMode wrap) {
    if (w == 0 || h == 0)
      return std::unexpected(std::string{"TextureCache: texture size must be non-zero"});
    if (rgba == nullptr)
      return std::unexpected(std::string{"TextureCache: texture pixels are null"});
    if (w > k_max_texture_dimension || h > k_max_texture_dimension)
      return std::unexpected(std::string{"TextureCache: texture size out of range"});

    const auto *pixels = static_cast<const uint8_t *>(rgba);
    const sg_image image = glfw::make_rgba8_image(
        std::span<const uint8_t>{pixels, glfw::rgba8_byte_count(static_cast<int>(w), static_cast<int>(h))},
        static_cast<int>(w), static_cast<int>(h), "texture_cache");

    return publish(impl_->slots, image, w, h, wrap, "TextureCache: could not create GPU resources");
  }

  void TextureCache::destroy(uint32_t id) noexcept {
    const std::optional<TextureHandles> handles = impl_->slots.release(id);
    if (handles)
      destroy_handles(*handles);
  }

  std::optional<TextureInfo> TextureCache::info(uint32_t id) const {
    const SlotTable::Slot *slot = impl_->slots.peek(id);
    if (slot == nullptr)
      return std::nullopt;

    return TextureInfo{.height = slot->height, .id = id, .width = slot->width};
  }

  BackendTexture TextureCache::backend_handle(uint32_t id) const noexcept {
    const SlotTable::Slot *slot = impl_->slots.peek(id);
    if (slot == nullptr)
      return BackendTexture{};

    return BackendTexture{.sampler = slot->payload.sampler.id, .view = slot->payload.view.id};
  }

} // namespace corundum::platform
