#include <corundum/gameplay/ui/inventory_panel.hpp>

#include <corundum/gameplay/ui/ui_draw.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>

namespace corundum::gameplay::ui {

  namespace {
    constexpr std::string_view k_item_flag_prefix = "item.";
    constexpr std::string_view k_panel_header = "Inventory";
  } // namespace

  std::vector<InventoryLine> build_inventory_lines(const corundum::gameplay::FlagStore &flags,
                                                   const corundum::gameplay::item::Registry &items) {
    std::vector<InventoryLine> lines;
    for (const auto &[key, count] : flags) {
      if (!key.starts_with(k_item_flag_prefix) || count <= 0)
        continue;
      const std::string_view id = std::string_view{key}.substr(k_item_flag_prefix.size());
      const item::Item *def = items.find(id);
      lines.push_back(InventoryLine{def ? def->name : std::string{id}, count});
    }
    std::ranges::sort(lines, {}, [](const InventoryLine &l) { return l.name; });
    return lines;
  }

  void inventory_panel_render(platform::Renderer &r, const DialogBoxStyle &style, const NinePatchBorder &border,
                              const std::vector<InventoryLine> &lines, int cursor, core::math::Vec2 viewport) {
    constexpr float k_min_w = 220.f;
    constexpr float k_pad_x = 24.f;
    constexpr float k_pad_y = 16.f;
    constexpr float k_header_gap = 10.f;
    constexpr std::string_view k_empty_label = "(empty)";

    const float line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
    const float header_w = r.measure_text(style.font_id, k_panel_header, style.font_size_speaker);

    float content_w = header_w;
    std::vector<std::string> labels;
    labels.reserve(lines.size());
    for (const auto &line : lines) {
      labels.push_back(std::format("{}  x{}", line.name, line.count));
      content_w = std::max(content_w, r.measure_text(style.font_id, labels.back(), style.font_size_body));
    }
    if (lines.empty())
      content_w = std::max(content_w, r.measure_text(style.font_id, k_empty_label, style.font_size_body));

    const float panel_w = std::max(k_min_w, content_w + k_pad_x * 2.f);
    const float rows = static_cast<float>(lines.empty() ? 1 : lines.size());
    const float panel_h = k_pad_y * 2.f + line_h + k_header_gap + rows * line_h;

    const float panel_x = (viewport.x - panel_w) * 0.5f;
    const float panel_y = (viewport.y - panel_h) * 0.5f;

    panel_chrome(r, style.bg, border, {panel_x, panel_y}, {panel_w, panel_h});

    const float header_x = panel_x + (panel_w - header_w) * 0.5f;
    const float header_y = panel_y + k_pad_y;
    r.draw(platform::DrawText{
        style.font_id, k_panel_header, {header_x, header_y}, style.font_size_speaker, style.speaker});

    if (lines.empty()) {
      const float empty_w = r.measure_text(style.font_id, k_empty_label, style.font_size_body);
      const float empty_x = panel_x + (panel_w - empty_w) * 0.5f;
      r.draw(platform::DrawText{style.font_id,
                                k_empty_label,
                                {empty_x, header_y + line_h + k_header_gap},
                                style.font_size_body,
                                style.choice});
      return;
    }

    const int clamped_cursor = std::clamp(cursor, 0, static_cast<int>(lines.size()) - 1);
    const float row_x = panel_x + k_pad_x;
    for (std::size_t i = 0; i < labels.size(); ++i) {
      const float y = header_y + line_h + k_header_gap + static_cast<float>(i) * line_h;
      draw_option(r, style, labels[i], {row_x, y}, static_cast<int>(i) == clamped_cursor);
    }
  }

} // namespace corundum::gameplay::ui
