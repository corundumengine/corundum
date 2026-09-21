// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/math/vec.hpp>
#include <corundum/item/item.hpp>
#include <corundum/item/registry.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/dialog_box.hpp>
#include <corundum/ui/inventory_panel.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/world/flags.hpp>

#include <corundum/ui/ui_draw.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace corundum::ui {

  namespace {
    constexpr std::string_view k_panel_header = "Inventory";
    constexpr std::string_view k_empty_label = "(empty)";

    /// UI display name for a category. Deliberately separate from item::to_string(),
    /// which is the lowercase serialized/schema form, not presentation text.
    std::string_view category_display_name(item::ItemCategory category) noexcept {
      switch (category) {
        case item::ItemCategory::Apparel:
          return "Apparel";
        case item::ItemCategory::Misc:
          return "Misc";
        case item::ItemCategory::Potion:
          return "Potion";
        case item::ItemCategory::Weapon:
          return "Weapon";
      }
      return "Misc";
    }

    /// A run of consecutive rows sharing one category (input is sorted by (category, name)).
    struct Group {
      item::ItemCategory category = item::ItemCategory::Misc;

      std::size_t first = 0; ///< Index of the group's first label in the labels vector.

      std::size_t count = 0; ///< Number of labels in the group.
    };

    /// Headers are drawn for every category once any non-Misc row is present; an all-Misc
    /// inventory omits them so a handful of un-categorized items don't look over-organized.
    bool show_group_header(item::ItemCategory category, bool has_non_misc) noexcept {
      return category != item::ItemCategory::Misc || has_non_misc;
    }
  } // namespace

  std::vector<InventoryLine> build_inventory_lines(const corundum::world::FlagStore &flags,
                                                   const corundum::item::Registry &items) {
    std::vector<InventoryLine> lines;
    for (const auto &[key, count] : flags) {
      if (!item::is_held_item(key, count))
        continue;
      const std::string_view id = item::item_id_from_flag(key);
      const item::Item *def = items.find(id);
      lines.push_back(InventoryLine{
          .category = def != nullptr ? def->category : item::ItemCategory::Misc,
          .count = count,
          .name = def != nullptr ? def->name : std::string{id},
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

    const float body_line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
    // The panel title and group headers use the (usually larger) speaker font, so they
    // need a taller row than body text.
    const float header_line_h = std::max(body_line_h, static_cast<float>(style.font_size_speaker) + 4.f);
    const float cursor_w = cursor_advance(r, style);
    const float title_w = r.measure_text(style.font_id, k_panel_header, style.font_size_speaker);

    // Build one label per row and group consecutive rows by category. Input is sorted by
    // (category, name), so equal categories are already adjacent.
    std::vector<std::string> labels;
    labels.reserve(lines.size());
    std::vector<Group> groups;
    float widest_body_w = 0.f;
    for (const InventoryLine &line : lines) {
      if (groups.empty() || groups.back().category != line.category)
        groups.push_back(Group{.category = line.category, .first = labels.size()});
      ++groups.back().count;
      labels.push_back(std::format("{}  x{}", line.name, line.count));
      widest_body_w = std::max(widest_body_w, r.measure_text(style.font_id, labels.back(), style.font_size_body));
    }

    const bool has_non_misc =
        std::ranges::any_of(groups, [](const Group &g) { return g.category != item::ItemCategory::Misc; });

    // Width must fit the title, the per-row cursor prefix plus the widest label, and the
    // widest group header.
    float content_w = std::max(title_w, cursor_w + widest_body_w);
    int header_count = 0;
    for (const Group &group : groups) {
      if (!show_group_header(group.category, has_non_misc))
        continue;
      ++header_count;
      content_w = std::max(
          content_w, r.measure_text(style.font_id, category_display_name(group.category), style.font_size_speaker));
    }
    if (lines.empty())
      content_w = std::max(content_w, r.measure_text(style.font_id, k_empty_label, style.font_size_body));

    const float panel_w = std::max(k_min_w, content_w + (k_pad_x * 2.f));
    const float body_rows = static_cast<float>(labels.empty() ? 1 : labels.size());
    // One gap between adjacent groups (there is no gap before the first or after the last).
    const float group_gaps = header_count > 0 ? static_cast<float>(header_count - 1) * k_group_gap : 0.f;
    const float panel_h = (k_pad_y * 2.f) + header_line_h + k_header_gap + (body_rows * body_line_h) +
                          (static_cast<float>(header_count) * header_line_h) + group_gaps;

    const float panel_x = (viewport.x - panel_w) * 0.5f;
    const float panel_y = (viewport.y - panel_h) * 0.5f;

    panel_chrome(r, style.bg, border, {.x = panel_x, .y = panel_y}, {.x = panel_w, .y = panel_h});

    const float header_x = panel_x + ((panel_w - title_w) * 0.5f);
    const float header_y = panel_y + k_pad_y;
    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = k_panel_header,
        .position = {.x = header_x, .y = header_y},
        .char_size = style.font_size_speaker,
        .colour = style.speaker,
    });

    float y = header_y + header_line_h + k_header_gap;

    if (lines.empty()) {
      const float empty_w = r.measure_text(style.font_id, k_empty_label, style.font_size_body);
      const float empty_x = panel_x + ((panel_w - empty_w) * 0.5f);
      r.draw(platform::DrawText{
          .font_id = style.font_id,
          .text = k_empty_label,
          .position = {.x = empty_x, .y = y},
          .char_size = style.font_size_body,
          .colour = style.choice,
      });
      return;
    }

    const int clamped_cursor = std::clamp(cursor, 0, static_cast<int>(lines.size()) - 1);
    const float row_x = panel_x + k_pad_x;
    for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
      const Group &group = groups[group_index];

      if (show_group_header(group.category, has_non_misc)) {
        const std::string_view label = category_display_name(group.category);
        const float label_w = r.measure_text(style.font_id, label, style.font_size_speaker);
        const float label_x = panel_x + ((panel_w - label_w) * 0.5f);
        r.draw(platform::DrawText{
            .font_id = style.font_id,
            .text = label,
            .position = {.x = label_x, .y = y},
            .char_size = style.font_size_speaker,
            .colour = style.speaker,
        });
        y += header_line_h;
      }

      for (std::size_t row = 0; row < group.count; ++row) {
        const std::size_t index = group.first + row;
        draw_option(r, style, labels[index], {.x = row_x, .y = y}, std::cmp_equal(index, clamped_cursor));
        y += body_line_h;
      }

      if (group_index + 1 < groups.size())
        y += k_group_gap;
    }
  }

} // namespace corundum::ui
