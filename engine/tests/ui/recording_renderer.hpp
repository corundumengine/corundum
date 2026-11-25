// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/resource_ids.hpp>
#include <corundum/ui/font_family.hpp>
#include <corundum/ui/nine_patch.hpp>

#include <cstdint>
#include <deque>
#include <expected>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace corundum::test {

/** Records every draw call into one ordered log so tests can assert both counts and
 *  ordering. measure_text mirrors the null backend's per-glyph width so tests don't
 *  depend on real font metrics.
 */
class RecordingRenderer final : public platform::Renderer {
public:
  using DrawCall = std::variant<platform::DrawRect, platform::DrawSprite, platform::DrawText>;

  std::vector<DrawCall> log{};

  /** DrawText::text is a string_view that may point into temporaries that die when the render
   *  call returns (e.g. formatted inventory labels); the real renderer consumes it
   *  synchronously, but this recorder must keep it alive. A deque (not vector) keeps
   *  references to stored strings stable across push_back, since the recorded DrawText views
   *  point into this container.
   */
  std::deque<std::string> text_owner{};

  /** Lines drawn in world view; screen-space lines are dropped. */
  std::vector<platform::DrawLine> world_lines{};

  /** The last set_world_view's top-left. */
  core::math::Vec2 world_view_camera{};

  /** True between set_world_view() and reset_screen_view(). */
  bool is_world_view{};

  std::expected<platform::TextureId, std::string> load_texture(std::string_view /*path*/) override {
    return platform::TextureId{1};
  }

  std::expected<platform::FontId, std::string> load_font(std::string_view /*path*/) override {
    return platform::FontId{2};
  }

  [[nodiscard]] core::math::Vec2 texture_size(platform::TextureId /*texture_id*/) const override { return {}; }

  void release_texture(platform::TextureId /*texture_id*/) override {}

  void purge_unused_textures() override {}

  void set_world_view(core::math::Vec2 top_left, core::math::Vec2 /*viewport_size*/, float /*zoom*/) override {
    world_view_camera = top_left;
    is_world_view = true;
  }

  void reset_screen_view() override { is_world_view = false; }

  bool begin_frame(core::math::Colour /*clear_colour*/) override { return true; }

  void end_frame() override {}

  void draw(const platform::DrawSprite &cmd) override { log.emplace_back(cmd); }

  void draw(const platform::DrawText &cmd) override {
    text_owner.emplace_back(cmd.text);
    platform::DrawText copy = cmd;
    copy.text = text_owner.back();
    log.emplace_back(copy);
  }

  void draw(const platform::DrawRect &cmd) override { log.emplace_back(cmd); }

  void draw(const platform::DrawLine &cmd) override {
    if (is_world_view)
      world_lines.push_back(cmd);
  }

  [[nodiscard]] float measure_text(platform::FontId /*font_id*/, std::string_view text,
                                   uint32_t /*char_size*/) const override {
    return static_cast<float>(text.size()) * 8.f;
  }

  [[nodiscard]] platform::RendererStats stats() const override { return {}; }
};

inline ui::NinePatchBorder make_border() {
  ui::NinePatchBorder border{};
  border.texture_id = platform::TextureId{1};
  border.tile_w = 4;
  border.tile_h = 4;
  return border;
}

/** A family whose four style ids run from `first` upward, so a case can say which face a run
 *  drew in by number: bold is `first` + 1, italic `first` + 2, bold-italic `first` + 3.
 */
[[nodiscard]] inline ui::FontFamily make_font_family(std::uint32_t first) {
  ui::FontFamily family{};
  for (std::uint32_t style = 0; style < ui::k_font_style_count; ++style)
    family.ids[style] = platform::FontId{first + style};
  return family;
}

} // namespace corundum::test
