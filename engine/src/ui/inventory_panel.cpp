#include <corundum/ui/inventory_panel.hpp>

#include <corundum/ui/ui_draw.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace corundum::ui {

  namespace {
    constexpr std::string_view k_item_flag_prefix = "item.";
    constexpr std::string_view k_panel_header = "Inventory";
    constexpr std::string_view k_misc_header = "Misc";

    /// A run of consecutive lines sharing one category (input is sorted by (category, name)).
    struct Group {
      item::ItemCategory category = item::ItemCategory::Misc;
      std::vector<std::size_t> indices{}; ///< Indices into the caller's `lines` vector.
    };

    // The category the previous row belonged to, for one header per group. k_misc_header
    // renders only when a group other than Misc is present, so a handful of un-categorized
    // items don't look over-organized.
    std::optional<item::ItemCategory> maybe_group_header(const item::ItemCategory current, bool has_non_misc) {
      if (current == item::ItemCategory::Misc && !has_non_misc)
        return std::nullopt;
      return current;
    }
  } // namespace

  std::vector<InventoryLine> build_inventory_lines(const corundum::world::FlagStore &flags,
                                                   const corundum::item::Registry &items) {
    std::vector<InventoryLine> lines;
    for (const auto &[key, count] : flags) {
      if (!key.starts_with(k_item_flag_prefix) || count <= 0)
        continue;
      const std::string_view id = std::string_view{key}.substr(k_item_flag_prefix.size());
      const item::Item *def = items.find(id);
      lines.push_back(InventoryLine{
          .category = def ? def->category : item::ItemCategory::Misc,
          .name = def ? def->name : std::string{id},
          .count = count,
      });
    }
    std::ranges::sort(lines, {}, [](const InventoryLine &l) { return std::tuple{l.category, l.name}; });
    return lines;
  }

  void inventory_panel_render(platform::Renderer &r, const DialogBoxStyle &style, const NinePatchBorder &border,
                              const std::vector<InventoryLine> &lines, int cursor, core::math::Vec2 viewport) {
    constexpr float k_min_w = 220.f;
    constexpr float k_pad_x = 24.f;
    constexpr float k_pad_y = 16.f;
    constexpr float k_header_gap = 10.f;
    constexpr float k_group_gap = 6.f;
    constexpr std::string_view k_empty_label = "(empty)";

    const float line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
    const float header_w = r.measure_text(style.font_id, k_panel_header, style.font_size_speaker);

    // Group the already-sorted lines by category so we can label each run and size the panel.
    std::vector<Group> groups;
    for (const auto &line : lines) {
      if (groups.empty() || groups.back().category != line.category)
        groups.push_back(Group{line.category});
      groups.back().indices.push_back(&line - lines.data());
    }

    const bool has_non_misc =
        std::ranges::any_of(groups, [](const Group &g) { return g.category != item::ItemCategory::Misc; });

    std::vector<std::string> labels;
    labels.reserve(lines.size());
    float content_w = header_w;
    float group_rows = 0.f;
    for (const auto &group : groups) {
      const bool show_header = maybe_group_header(group.category, has_non_misc).has_value();
      if (show_header)
        group_rows += 1.f;
      for (const auto &idx : group.indices) {
        labels.push_back(std::format("{}  x{}", lines[idx].name, lines[idx].count));
        content_w = std::max(content_w, r.measure_text(style.font_id, labels.back(), style.font_size_body));
      }
    }
    if (lines.empty())
      content_w = std::max(content_w, r.measure_text(style.font_id, k_empty_label, style.font_size_body));

    const float panel_w = std::max(k_min_w, content_w + k_pad_x * 2.f);
    const float rows = static_cast<float>(lines.empty() ? 1 : lines.size());
    // Each group contributes its rows plus one header row (when labeled); groups are
    // separated by k_group_gap, and there is no extra gap after the final group.
    const float group_extra =
        group_rows * line_h + std::max(0.f, static_cast<float>(groups.size()) - 1.f) * k_group_gap;
    const float panel_h = k_pad_y * 2.f + line_h + k_header_gap + rows * line_h + group_extra;

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
    float y = header_y + line_h + k_header_gap;
    std::size_t label_index = 0;
    for (const auto &group : groups) {
      if (maybe_group_header(group.category, has_non_misc).has_value()) {
        const std::string_view label =
            group.category == item::ItemCategory::Misc ? k_misc_header : item::to_string(group.category);
        const float label_w = r.measure_text(style.font_id, label, style.font_size_speaker);
        const float label_x = panel_x + (panel_w - label_w) * 0.5f;
        r.draw(platform::DrawText{style.font_id, label, {label_x, y}, style.font_size_speaker, style.speaker});
        y += line_h;
      }
      for (const auto &idx : group.indices) {
        draw_option(r, style, labels[label_index++], {row_x, y}, static_cast<int>(idx) == clamped_cursor);
        y += line_h;
      }
      y += k_group_gap;
    }
  }

} // namespace corundum::ui
