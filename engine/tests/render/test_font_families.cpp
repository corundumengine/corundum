// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <doctest/doctest.h>

#include <corundum/core/game_config.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/render/render_system.hpp>
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
    [[nodiscard]] std::expected<std::uint32_t, std::string> load_texture(std::string_view /*path*/) override {
      return 1u;
    }

    [[nodiscard]] std::expected<std::uint32_t, std::string> load_font(std::string_view path) override {
      // Mimic a real backend: a path with no file component (empty name) cannot load.
      if (path.empty() || path.back() == '/')
        return std::unexpected("no such font: " + std::string{path});
      return next_font_id_++;
    }

    void set_world_view(corundum::core::math::Vec2 /*top_left*/, corundum::core::math::Vec2 /*viewport_size*/,
                        float /*zoom*/) override {}

    void reset_screen_view() override {}

    [[nodiscard]] bool begin_frame(corundum::core::math::Colour /*clear_colour*/) override {
      return true;
    }

    void end_frame() override {}

    void draw(const corundum::platform::DrawSprite & /*cmd*/) override {}

    void draw(const corundum::platform::DrawText & /*cmd*/) override {}

    void draw(const corundum::platform::DrawRect & /*cmd*/) override {}

    void draw(const corundum::platform::DrawLine & /*cmd*/) override {}

    [[nodiscard]] float measure_text(std::uint32_t /*font_id*/, std::string_view /*text*/,
                                     std::uint32_t /*char_size*/) const override {
      return 0.f;
    }

    [[nodiscard]] corundum::platform::RendererStats stats() const override {
      return {};
    }

  private:
    std::uint32_t next_font_id_{1};
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
  CHECK(dialogue.get(FontStyle::Regular) == 1u);
  CHECK(dialogue.get(FontStyle::Bold) == 2u);
  CHECK(dialogue.get(FontStyle::Italic) == 3u);
  CHECK(dialogue.get(FontStyle::BoldItalic) == 4u);

  const auto &quest = state.fonts[role_index(FontRole::Quest)];
  CHECK(quest.get(FontStyle::Regular) == 5u);
  CHECK(quest.get(FontStyle::Bold) == 5u);
  CHECK(quest.get(FontStyle::Italic) == 5u);
  CHECK(quest.get(FontStyle::BoldItalic) == 5u);

  CHECK(state.fonts[role_index(FontRole::Ui)].get(FontStyle::Regular) == 6u);
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
  CHECK(quest.get(FontStyle::Regular) == 2u);
  CHECK(quest.get(FontStyle::Italic) == 3u);
  CHECK(quest.get(FontStyle::BoldItalic) == 3u);
}

TEST_CASE("a default-constructed FontFamily is invalid") {
  const corundum::ui::FontFamily family{};
  CHECK(family.get(FontStyle::Regular) == 0u);
  CHECK(family.get(FontStyle::Bold) == 0u);
  CHECK(family.get(FontStyle::Italic) == 0u);
  CHECK(family.get(FontStyle::BoldItalic) == 0u);
}

TEST_CASE("load_fonts reports a missing regular face as an error") {
  corundum::core::ResourcePaths paths{};
  paths.font_dir = "fonts";
  paths.fonts[role_index(FontRole::Dialogue)] = FontFamilyPaths{};
  paths.fonts[role_index(FontRole::Quest)] = FontFamilyPaths{.regular = "q.ttf"};
  paths.fonts[role_index(FontRole::Ui)] = FontFamilyPaths{.regular = "u.ttf"};

  CountingRenderer r;
  corundum::render::RenderState state;
  CHECK_FALSE(corundum::render::load_fonts(r, state, paths).has_value());
}
