#include <corundum/ui/ui_draw.hpp>

namespace corundum::ui {

  namespace {
    constexpr std::string_view k_cursor_selected = "> ";
    constexpr std::string_view k_cursor_unselected = "  ";
  } // namespace

  void panel_chrome(platform::Renderer &r, core::math::Colour bg, const NinePatchBorder &border, core::math::Vec2 pos,
                    core::math::Vec2 size) {
    r.draw(platform::DrawRect{.position = pos, .size = size, .colour = bg});
    border.render(r, pos.x, pos.y, size.x, size.y);
  }

  float draw_option(platform::Renderer &r, const DialogBoxStyle &style, std::string_view label, core::math::Vec2 pos,
                    bool selected) {
    // Advance is always measured against k_cursor_selected so that the cursor column and
    // the label column line up whether the option is selected or not — even on a font
    // where "> " and "  " happen to differ in width.
    const float cursor_w = r.measure_text(style.font_id, k_cursor_selected, style.font_size_body);
    const std::string_view cursor = selected ? k_cursor_selected : k_cursor_unselected;
    const core::math::Colour col = selected ? style.selected : style.choice;

    r.draw(platform::DrawText{style.font_id, cursor, pos, style.font_size_body, col});
    r.draw(platform::DrawText{style.font_id, label, {pos.x + cursor_w, pos.y}, style.font_size_body, col});
    return cursor_w;
  }

} // namespace corundum::ui
