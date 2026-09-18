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
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

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
    void destroy_handles(const TextureHandles &handles) {
      if (handles.view.id != 0)
        sg_destroy_view(handles.view);
      if (handles.sampler.id != 0)
        sg_destroy_sampler(handles.sampler);
      if (handles.image.id != 0)
        sg_destroy_image(handles.image);
    }

  } // namespace

  struct TextureCache::Impl {
    SlotTable slots;
  };

  TextureCache::TextureCache() : impl_{std::make_unique<Impl>()} {}

  TextureCache::~TextureCache() {
    // Retire id by id rather than as a batch: releasing every handle in one pass
    // would have to hand them back in a container, and a destructor must not throw.
    for (uint32_t id = 1; id <= impl_->slots.slot_count(); ++id) {
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

    const TextureHandles handles{
        .image = image,
        .sampler = make_sampler(WrapMode::Clamp),
        .view = glfw::make_texture_view(image, "texture_cache"),
    };
    if (!handles_valid(handles)) {
      destroy_handles(handles);
      return std::unexpected(std::string{"TextureCache: could not create GPU resources for '"} + path_str + "'");
    }

    const unsigned width = static_cast<unsigned>(w);
    const unsigned height = static_cast<unsigned>(h);
    const uint32_t id = impl_->slots.adopt(handles, width, height);

    return TextureInfo{.height = height, .id = id, .width = width};
  }

  std::expected<TextureInfo, std::string> TextureCache::create(unsigned w, unsigned h, const void *rgba,
                                                               WrapMode wrap) {
    if (w == 0 || h == 0)
      return std::unexpected(std::string{"TextureCache: texture size must be non-zero"});
    if (rgba == nullptr)
      return std::unexpected(std::string{"TextureCache: texture pixels are null"});

    const auto *pixels = static_cast<const uint8_t *>(rgba);
    const sg_image image = glfw::make_rgba8_image(
        std::span<const uint8_t>{pixels, glfw::rgba8_byte_count(static_cast<int>(w), static_cast<int>(h))},
        static_cast<int>(w), static_cast<int>(h), "texture_cache");
    const TextureHandles handles{
        .image = image,
        .sampler = make_sampler(wrap),
        .view = glfw::make_texture_view(image, "texture_cache"),
    };
    if (!handles_valid(handles)) {
      destroy_handles(handles);
      return std::unexpected(std::string{"TextureCache: could not create GPU resources"});
    }

    const uint32_t id = impl_->slots.adopt(handles, w, h);
    return TextureInfo{.height = h, .id = id, .width = w};
  }

  void TextureCache::destroy(uint32_t id) noexcept {
    const std::optional<TextureHandles> handles = impl_->slots.release(id);
    if (handles)
      destroy_handles(*handles);
  }

  std::optional<TextureInfo> TextureCache::info(uint32_t id) const {
    const std::optional<SlotTable::Slot> slot = impl_->slots.find(id);
    if (!slot)
      return std::nullopt;

    return TextureInfo{.height = slot->height, .id = id, .width = slot->width};
  }

  BackendTexture TextureCache::backend_handle(uint32_t id) const noexcept {
    const std::optional<SlotTable::Slot> slot = impl_->slots.find(id);
    if (!slot)
      return BackendTexture{};

    return BackendTexture{.sampler = slot->payload.sampler.id, .view = slot->payload.view.id};
  }

} // namespace corundum::platform
