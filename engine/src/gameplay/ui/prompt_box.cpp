#include <corundum/gameplay/ui/prompt_box.hpp>

#include <corundum/gameplay/ui/ui_draw.hpp>

#include <algorithm>

namespace corundum::gameplay::ui {

  void prompt_box_render(platform::Renderer &r, const DialogBoxStyle &style, const NinePatchBorder &border,
                         std::string_view question, bool yes_selected, core::math::Vec2 viewport) {
    constexpr float k_min_w = 200.f;
    constexpr float k_pad_x = 32.f;
    constexpr float k_pad_y = 24.f;
    constexpr float k_gap = 22.f;
    constexpr float k_opt_gap = 40.f;
    constexpr std::string_view k_yes = "Yes";
    constexpr std::string_view k_no = "No";
    constexpr std::string_view k_cursor = "> ";

    const float q_w = r.measure_text(style.font_id, question, style.font_size_body);
    const float yes_w = r.measure_text(style.font_id, k_yes, style.font_size_body);
    const float no_w = r.measure_text(style.font_id, k_no, style.font_size_body);
    const float cursor_w = r.measure_text(style.font_id, k_cursor, style.font_size_body);
    const float options_row_w = cursor_w + yes_w + k_opt_gap + no_w;

    const float content_w = std::max(q_w, options_row_w);
    const float panel_w = std::max(k_min_w, content_w + k_pad_x * 2.f);
    const float line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
    const float panel_h = k_pad_y * 2.f + line_h + k_gap + line_h;

    const float panel_x = (viewport.x - panel_w) * 0.5f;
    const float panel_y = (viewport.y - panel_h) * 0.5f;

    panel_chrome(r, style.bg, border, {panel_x, panel_y}, {panel_w, panel_h});

    const float q_x = panel_x + (panel_w - q_w) * 0.5f;
    const float q_y = panel_y + k_pad_y;
    r.draw(platform::DrawText{style.font_id, question, {q_x, q_y}, style.font_size_body, style.body});

    const float opt_y = q_y + line_h + k_gap;
    const float row_x = panel_x + (panel_w - options_row_w) * 0.5f;
    draw_option(r, style, k_yes, {row_x, opt_y}, yes_selected);
    draw_option(r, style, k_no, {row_x + cursor_w + yes_w + k_opt_gap, opt_y}, !yes_selected);
  }

} // namespace corundum::gameplay::ui
