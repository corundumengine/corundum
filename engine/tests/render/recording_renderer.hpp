// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/resource_ids.hpp>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace corundum::test {

/// Renderer that records sprite draws and hands out a distinct texture id per path.
class TileRecordingRenderer final : public corundum::platform::Renderer {
public:
  std::vector<corundum::platform::DrawSprite> sprites;

  /// Paths load_texture() refuses with std::unexpected.
  std::vector<std::string> failing_paths;

  /// Path → the id load_texture() handed out for it.
  std::unordered_map<std::string, corundum::platform::TextureId> ids_by_path;

  std::expected<corundum::platform::TextureId, std::string> load_texture(std::string_view path) override {
    const std::string key{path};
    if (std::ranges::find(failing_paths, key) != failing_paths.end())
      return std::unexpected("refused");
    auto [it, inserted] = ids_by_path.try_emplace(key);
    if (inserted)
      it->second = corundum::platform::TextureId{next_id_++};
    return it->second;
  }

  [[nodiscard]] corundum::core::math::Vec2 texture_size(corundum::platform::TextureId /*texture_id*/) const override {
    return {};
  }

  void release_texture(corundum::platform::TextureId /*texture_id*/) override {}

  void purge_unused_textures() override {}

  std::expected<corundum::platform::FontId, std::string> load_font(std::string_view /*path*/) override {
    return corundum::platform::FontId{1};
  }

  void set_world_view(corundum::core::math::Vec2 /*top_left*/, corundum::core::math::Vec2 /*viewport_size*/,
                      float /*zoom*/) override {}

  void reset_screen_view() override {}

  [[nodiscard]] bool begin_frame(corundum::core::math::Colour /*clear_colour*/) override { return true; }

  void end_frame() override {}

  void draw(const corundum::platform::DrawSprite &cmd) override { sprites.push_back(cmd); }

  void draw(const corundum::platform::DrawText & /*cmd*/) override {}

  void draw(const corundum::platform::DrawRect & /*cmd*/) override {}

  void draw(const corundum::platform::DrawLine & /*cmd*/) override {}

  [[nodiscard]] float measure_text(corundum::platform::FontId /*font_id*/, std::string_view /*text*/,
                                   uint32_t /*char_size*/) const override {
    return 0.f;
  }

  [[nodiscard]] corundum::platform::RendererStats stats() const override { return {}; }

private:
  std::uint32_t next_id_{1}; ///< Raw counter, so issuing an id never converts types.
};

} // namespace corundum::test
