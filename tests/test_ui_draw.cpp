#include <doctest/doctest.h>

#include <corundum/gameplay/ui/dialog_box.hpp>
#include <corundum/gameplay/ui/nine_patch.hpp>
#include <corundum/gameplay/ui/ui_draw.hpp>
#include <corundum/platform/renderer.hpp>

#include <expected>
#include <string>
#include <variant>
#include <vector>

namespace {

  using DrawRect = corundum::platform::DrawRect;
  using DrawSprite = corundum::platform::DrawSprite;
  using DrawText = corundum::platform::DrawText;

  /// Records every draw call into one ordered log so tests can assert both
  /// counts and ordering. measure_text mirrors the null backend's per-glyph
  /// width so tests don't depend on real font metrics.
  class RecordingRenderer final : public corundum::platform::Renderer {
  public:
    using DrawCall = std::variant<DrawRect, DrawSprite, DrawText>;
    std::vector<DrawCall> log{};

    std::expected<uint32_t, std::string> load_texture(std::string_view) override {
      return 1u;
    }

    std::expected<uint32_t, std::string> load_font(std::string_view) override {
      return 2u;
    }

    void set_world_view(corundum::core::math::Vec2, corundum::core::math::Vec2, float) override {}

    void reset_screen_view() override {}

    bool begin_frame(corundum::core::math::Colour) override {
      return true;
    }

    void end_frame() override {}

    void draw(const DrawSprite &cmd) override {
      log.push_back(cmd);
    }

    void draw(const DrawText &cmd) override {
      log.push_back(cmd);
    }

    void draw(const DrawRect &cmd) override {
      log.push_back(cmd);
    }

    void draw(const corundum::platform::DrawLine &) override {}

    float measure_text(uint32_t, std::string_view text, uint32_t) const override {
      return static_cast<float>(text.size()) * 8.f;
    }

    corundum::platform::RendererStats stats() const override {
      return {};
    }
  };

  corundum::gameplay::ui::NinePatchBorder make_border() {
    corundum::gameplay::ui::NinePatchBorder b{};
    b.texture_id = 1u;
    b.tile_w = 4;
    b.tile_h = 4;
    return b;
  }

} // namespace

TEST_CASE("ui_draw: panel_chrome emits exactly one DrawRect then the border's sprite commands") {
  RecordingRenderer r;
  const corundum::gameplay::ui::NinePatchBorder border = make_border();
  const corundum::core::math::Colour bg{20, 20, 20, 200};
  const corundum::core::math::Vec2 pos{10.f, 20.f};
  const corundum::core::math::Vec2 size{100.f, 60.f};

  corundum::gameplay::ui::panel_chrome(r, bg, border, pos, size);

  // The border draws 8 sprites (4 corners + 2 horizontal edges + 2 vertical edges).
  REQUIRE(r.log.size() == 9);

  // First call is the fill rect, then the border's sprites — render order matters
  // because the fill must be behind the frame.
  const DrawRect &first = std::get<DrawRect>(r.log[0]);
  CHECK(first.position.x == pos.x);
  CHECK(first.position.y == pos.y);
  CHECK(first.size.x == size.x);
  CHECK(first.size.y == size.y);
  CHECK(first.colour.r == bg.r);
  CHECK(first.colour.a == bg.a);

  for (std::size_t i = 1; i < r.log.size(); ++i)
    CHECK(std::holds_alternative<DrawSprite>(r.log[i]));
}

TEST_CASE("ui_draw: panel_chrome is a no-op for the sprite half when the border has no texture") {
  RecordingRenderer r;
  corundum::gameplay::ui::NinePatchBorder border{};
  border.texture_id = 0;
  border.tile_w = 4;
  border.tile_h = 4;

  corundum::gameplay::ui::panel_chrome(r, {0, 0, 0, 255}, border, {0.f, 0.f}, {50.f, 50.f});

  // The fill still goes out; only the border is skipped.
  REQUIRE(r.log.size() == 1);
  CHECK(std::holds_alternative<DrawRect>(r.log[0]));
}

TEST_CASE("ui_draw: draw_option emits two DrawTexts with selected colours when selected=true") {
  RecordingRenderer r;
  const corundum::gameplay::ui::DialogBoxStyle style{};
  // style.selected defaults to (255, 255, 0, 255); style.choice defaults to (200, 200, 200, 255).

  const float cursor_w = corundum::gameplay::ui::draw_option(r, style, "Hello", {5.f, 7.f}, true);

  REQUIRE(r.log.size() == 2);
  REQUIRE(std::holds_alternative<DrawText>(r.log[0]));
  REQUIRE(std::holds_alternative<DrawText>(r.log[1]));

  // First draw: cursor prefix "> ".
  const DrawText &cursor = std::get<DrawText>(r.log[0]);
  CHECK(cursor.text == "> ");
  CHECK(cursor.position.x == 5.f);
  CHECK(cursor.position.y == 7.f);
  CHECK(cursor.colour.r == style.selected.r);
  CHECK(cursor.colour.g == style.selected.g);
  CHECK(cursor.colour.b == style.selected.b);
  // Second draw: label at pos.x + cursor_w, same colour as cursor.
  const DrawText &label = std::get<DrawText>(r.log[1]);
  CHECK(label.text == "Hello");
  CHECK(label.position.x == 5.f + cursor_w);
  CHECK(label.position.y == 7.f);
  CHECK(label.colour.r == style.selected.r);

  CHECK(cursor_w > 0.f);
}

TEST_CASE("ui_draw: draw_option emits two DrawTexts with choice colours and two-space cursor when selected=false") {
  RecordingRenderer r;
  const corundum::gameplay::ui::DialogBoxStyle style{};

  const float cursor_w = corundum::gameplay::ui::draw_option(r, style, "World", {0.f, 0.f}, false);

  REQUIRE(r.log.size() == 2);
  REQUIRE(std::holds_alternative<DrawText>(r.log[0]));
  REQUIRE(std::holds_alternative<DrawText>(r.log[1]));

  // Cursor is the two-space placeholder; label follows at +cursor_w.
  const DrawText &cursor = std::get<DrawText>(r.log[0]);
  const DrawText &label = std::get<DrawText>(r.log[1]);
  CHECK(cursor.text == "  ");
  CHECK(label.text == "World");
  CHECK(label.position.x == cursor_w);
  // Both writes share the unselected colour (style.choice).
  CHECK(cursor.colour.r == style.choice.r);
  CHECK(cursor.colour.g == style.choice.g);
  CHECK(cursor.colour.b == style.choice.b);
  CHECK(label.colour.r == style.choice.r);
  CHECK(label.colour.g == style.choice.g);
  CHECK(label.colour.b == style.choice.b);
}

TEST_CASE("ui_draw: draw_option returns the same cursor advance regardless of selection state") {
  // Guarantees column alignment: the label x is pos.x + return value in both
  // branches. If a font renders "> " and "  " at different widths the bug would
  // surface here and force us to always measure k_cursor_selected — which
  // draw_option already does.
  RecordingRenderer r;
  const corundum::gameplay::ui::DialogBoxStyle style{};

  const float w_sel = corundum::gameplay::ui::draw_option(r, style, "A", {0.f, 0.f}, true);
  const float w_unsel = corundum::gameplay::ui::draw_option(r, style, "A", {0.f, 0.f}, false);

  CHECK(w_sel == w_unsel);
  CHECK(w_sel > 0.f);
}
