// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/core/game_config.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/resource_ids.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/render/resources.hpp>
#include <corundum/ui/font_family.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace {

  /// Hands out a distinct id per load (starting at 1, as the real backends do after reserving
  /// the 0 sentinel), so fallback resolution can be asserted by id.
  class CountingRenderer final : public corundum::platform::Renderer {
  public:
    [[nodiscard]] std::expected<corundum::platform::TextureId, std::string>
    load_texture(std::string_view /*path*/) override {
      return corundum::platform::TextureId{1};
    }

    [[nodiscard]] std::expected<corundum::platform::FontId, std::string> load_font(std::string_view path) override {
      // Mimic a real backend: a path with no file component (empty name) cannot load.
      if (path.empty() || path.back() == '/')
        return std::unexpected("no such font: " + std::string{path});
      return corundum::platform::FontId{next_font_id_++};
    }

    [[nodiscard]] corundum::core::math::Vec2 texture_size(corundum::platform::TextureId /*texture_id*/) const override {
      return {};
    }

    void release_texture(corundum::platform::TextureId /*texture_id*/) override {}

    void purge_unused_textures() override {}

    void set_world_view(corundum::core::math::Vec2 /*top_left*/, corundum::core::math::Vec2 /*viewport_size*/,
                        float /*zoom*/) override {}

    void reset_screen_view() override {}

    [[nodiscard]] bool begin_frame(corundum::core::math::Colour /*clear_colour*/) override { return true; }

    void end_frame() override {}

    void draw(const corundum::platform::DrawSprite & /*cmd*/) override {}

    void draw(const corundum::platform::DrawText & /*cmd*/) override {}

    void draw(const corundum::platform::DrawRect & /*cmd*/) override {}

    void draw(const corundum::platform::DrawLine & /*cmd*/) override {}

    [[nodiscard]] float measure_text(corundum::platform::FontId /*font_id*/, std::string_view /*text*/,
                                     std::uint32_t /*char_size*/) const override {
      return 0.f;
    }

    [[nodiscard]] corundum::platform::RendererStats stats() const override { return {}; }

  private:
    std::uint32_t next_font_id_{1}; ///< Raw counter, so issuing an id never converts types.
  };

  using corundum::ui::FontFamilyPaths;
  using corundum::ui::FontRole;
  using corundum::ui::FontStyle;

  constexpr std::size_t role_index(FontRole role) noexcept {
    return static_cast<std::size_t>(role);
  }

} // namespace

TEST_CASE("load_fonts resolves every style and falls back to regular") {
  corundum::core::ResourcePaths paths{};
  paths.font_dir = "fonts";
  paths.fonts[role_index(FontRole::Dialogue)] = FontFamilyPaths{
      .bold = "d_bold.ttf",
      .bold_italic = "d_bi.ttf",
      .italic = "d_italic.ttf",
      .regular = "d_regular.ttf",
  };
  paths.fonts[role_index(FontRole::Quest)] = FontFamilyPaths{.regular = "q_regular.ttf"};
  paths.fonts[role_index(FontRole::Ui)] = FontFamilyPaths{.regular = "u_regular.ttf"};

  CountingRenderer r;
  corundum::render::RenderState state;
  REQUIRE(corundum::render::load_fonts(r, state, paths).has_value());

  const auto &dialogue = state.fonts[role_index(FontRole::Dialogue)];
  CHECK(dialogue.get(FontStyle::Regular) == corundum::platform::FontId{1});
  CHECK(dialogue.get(FontStyle::Bold) == corundum::platform::FontId{2});
  CHECK(dialogue.get(FontStyle::Italic) == corundum::platform::FontId{3});
  CHECK(dialogue.get(FontStyle::BoldItalic) == corundum::platform::FontId{4});

  const auto &quest = state.fonts[role_index(FontRole::Quest)];
  CHECK(quest.get(FontStyle::Regular) == corundum::platform::FontId{5});
  CHECK(quest.get(FontStyle::Bold) == corundum::platform::FontId{5});
  CHECK(quest.get(FontStyle::Italic) == corundum::platform::FontId{5});
  CHECK(quest.get(FontStyle::BoldItalic) == corundum::platform::FontId{5});

  CHECK(state.fonts[role_index(FontRole::Ui)].get(FontStyle::Regular) == corundum::platform::FontId{6});

  // Display was not configured; it resolves to the ui family.
  CHECK(state.fonts[role_index(FontRole::Display)].get(FontStyle::Regular) == corundum::platform::FontId{6});
  CHECK(state.fonts[role_index(FontRole::Display)].get(FontStyle::Bold) == corundum::platform::FontId{6});
}

TEST_CASE("load_fonts loads the display family and falls back its optional styles") {
  corundum::core::ResourcePaths paths{};
  paths.font_dir = "fonts";
  paths.fonts[role_index(FontRole::Dialogue)] = FontFamilyPaths{.regular = "d.ttf"};
  paths.fonts[role_index(FontRole::Quest)] = FontFamilyPaths{.regular = "q.ttf"};
  paths.fonts[role_index(FontRole::Ui)] = FontFamilyPaths{.regular = "u.ttf"};
  paths.fonts[role_index(FontRole::Display)] = FontFamilyPaths{.bold = "x_bold.ttf", .regular = "x.ttf"};

  CountingRenderer r;
  corundum::render::RenderState state;
  REQUIRE(corundum::render::load_fonts(r, state, paths).has_value());

  const auto &display = state.fonts[role_index(FontRole::Display)];
  CHECK(display.get(FontStyle::Regular) == corundum::platform::FontId{4});
  CHECK(display.get(FontStyle::Bold) == corundum::platform::FontId{5});
  CHECK(display.get(FontStyle::Italic) == corundum::platform::FontId{4});
  CHECK(display.get(FontStyle::BoldItalic) == corundum::platform::FontId{5});
}

TEST_CASE("configure_panel_style scales the display sizes independently of the body sizes") {
  corundum::core::GameConfig cfg;
  cfg.dialogue_render.font_size_body = 22;
  cfg.display.heading_size = 48;
  cfg.display.banner_size = 64;

  corundum::render::RenderState state;
  state.ui_scale = 2.f;
  corundum::render::configure_panel_style(state, cfg);

  const auto &style = state.panel_skin.style;
  CHECK(style.font_size_body == 44u);
  CHECK(style.font_size_heading == 96u);
  CHECK(style.font_size_banner == 128u);
}

TEST_CASE("load_fonts falls back bold-italic to italic when bold is absent") {
  corundum::core::ResourcePaths paths{};
  paths.font_dir = "fonts";
  paths.fonts[role_index(FontRole::Dialogue)] = FontFamilyPaths{.regular = "d.ttf"};
  paths.fonts[role_index(FontRole::Quest)] = FontFamilyPaths{.italic = "q_i.ttf", .regular = "q.ttf"};
  paths.fonts[role_index(FontRole::Ui)] = FontFamilyPaths{.regular = "u.ttf"};

  CountingRenderer r;
  corundum::render::RenderState state;
  REQUIRE(corundum::render::load_fonts(r, state, paths).has_value());

  const auto &quest = state.fonts[role_index(FontRole::Quest)];
  CHECK(quest.get(FontStyle::Regular) == corundum::platform::FontId{2});
  CHECK(quest.get(FontStyle::Italic) == corundum::platform::FontId{3});
  CHECK(quest.get(FontStyle::BoldItalic) == corundum::platform::FontId{3});
}

TEST_CASE("a default-constructed FontFamily is invalid") {
  const corundum::ui::FontFamily family{};
  CHECK(family.get(FontStyle::Regular) == corundum::platform::FontId{0});
  CHECK(family.get(FontStyle::Bold) == corundum::platform::FontId{0});
  CHECK(family.get(FontStyle::Italic) == corundum::platform::FontId{0});
  CHECK(family.get(FontStyle::BoldItalic) == corundum::platform::FontId{0});
}

TEST_CASE("load_fonts reports a missing regular face as an error") {
  corundum::core::ResourcePaths paths{};
  paths.font_dir = "fonts";
  paths.fonts[role_index(FontRole::Dialogue)] = FontFamilyPaths{};
  paths.fonts[role_index(FontRole::Quest)] = FontFamilyPaths{.regular = "q.ttf"};
  paths.fonts[role_index(FontRole::Ui)] = FontFamilyPaths{.regular = "u.ttf"};

  CountingRenderer r;
  corundum::render::RenderState state;
  const auto result = corundum::render::load_fonts(r, state, paths);
  REQUIRE_FALSE(result.has_value());
  CHECK_MESSAGE(result.error().starts_with("[renderer] could not load dialogue font '': "), result.error());
}
