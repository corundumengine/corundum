// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/item/item.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/gameplay/screens/inventory_panel.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/font_family.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/world/flags.hpp>

#include <corundum/ui/ui_draw.hpp>
#include <corundum/ui/word_wrap.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace corundum::gameplay::screens {

  namespace {
    constexpr std::string_view k_panel_header = "Inventory";
    constexpr std::string_view k_empty_label = "(empty)";

    /// UI display name for a category. Deliberately separate from gameplay::item::to_string(),
    /// which is the lowercase serialized/schema form, not presentation text.
    std::string_view category_display_name(gameplay::item::ItemCategory category) noexcept {
      switch (category) {
        case gameplay::item::ItemCategory::Apparel:
          return "Apparel";
        case gameplay::item::ItemCategory::Misc:
          return "Misc";
        case gameplay::item::ItemCategory::Potion:
          return "Potion";
        case gameplay::item::ItemCategory::Weapon:
          return "Weapon";
      }
      return "Misc";
    }

    /// A run of consecutive rows sharing one category (input is sorted by (category, name)).
    struct Group {
      gameplay::item::ItemCategory category = gameplay::item::ItemCategory::Misc;

      std::size_t first = 0; ///< Index of the group's first label in the labels vector.

      std::size_t count = 0; ///< Number of labels in the group.
    };

    /// Headers are drawn for every category once any non-Misc row is present; an all-Misc
    /// inventory omits them so a handful of un-categorized items don't look over-organized.
    bool show_group_header(gameplay::item::ItemCategory category, bool has_non_misc) noexcept {
      return category != gameplay::item::ItemCategory::Misc || has_non_misc;
    }

    /// Wrap a row's description to the tooltip's reading width; empty when there is none.
    std::vector<std::string> wrap_description(std::string_view description, const platform::Renderer &r,
                                              const ui::PanelStyle &style) {
      if (description.empty())
        return {};
      constexpr float k_max_description_width = 360.f;
      const std::uint32_t font_id = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);
      const auto measure = [&](std::string_view text) { return r.measure_text(font_id, text, style.font_size_body); };
      return ui::wrap_text(description, k_max_description_width, measure);
    }

    /// Draw the wrapped tooltip starting at (@p x, @p y).
    void draw_description(platform::Renderer &r, const ui::PanelStyle &style,
                          const std::vector<std::string> &description_lines, float x, float y, float line_height) {
      for (const std::string &line : description_lines) {
        r.draw(platform::DrawText{
            .font_id = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular),
            .text = line,
            .position = {.x = x, .y = y},
            .char_size = style.font_size_body,
            .colour = style.choice,
        });
        y += line_height;
      }
    }

    constexpr float k_min_w = 220.f;
    constexpr float k_pad_x = 24.f;
    constexpr float k_pad_y = 16.f;
    constexpr float k_header_gap = 10.f;
    constexpr float k_group_gap = 6.f;

    /// One row of the panel's vertical draw sequence: a group header or an item option.
    struct InventoryDrawRow {
      bool header{false};

      std::string_view header_text{}; ///< Category name, valid only when @c header.

      std::size_t line_index{}; ///< Index into @c labels, valid only when not @c header.

      float y{};
    };

    /// Everything both the draw and the hit-test need, derived once from the same formulas.
    struct InventoryGeometry {
      std::vector<std::string> labels;
      bool has_non_misc{false};
      int clamped_cursor{-1};
      std::vector<std::string> description_lines;
      float body_line_h{};
      float header_line_h{};
      float panel_x{};
      float panel_y{};
      float panel_w{};
      float panel_h{};
      float title_y{};
      float body_top{};
      float row_x{};
      float description_y{};
      std::vector<InventoryDrawRow> draw_rows;
    };

    InventoryGeometry compute_inventory_geometry(const platform::Renderer &r, const ui::PanelStyle &style,
                                                 const std::vector<InventoryLine> &lines, int cursor,
                                                 core::math::Vec2 viewport) {
      InventoryGeometry geometry{};
      geometry.body_line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
      geometry.header_line_h = std::max(geometry.body_line_h, static_cast<float>(style.font_size_speaker) + 4.f);
      const float cursor_w = ui::cursor_advance(r, style);
      const std::uint32_t font_id = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);
      const float title_w = r.measure_text(font_id, k_panel_header, style.font_size_speaker);
      const auto measure_body = [&](std::string_view text) {
        return r.measure_text(font_id, text, style.font_size_body);
      };

      std::vector<Group> groups;
      float widest_body_w = 0.f;
      for (const InventoryLine &line : lines) {
        if (groups.empty() || groups.back().category != line.category)
          groups.push_back(Group{.category = line.category, .first = geometry.labels.size()});
        ++groups.back().count;
        geometry.labels.push_back(std::format("{}  x{}", line.name, line.count));
        widest_body_w = std::max(widest_body_w, r.measure_text(font_id, geometry.labels.back(), style.font_size_body));
      }

      geometry.has_non_misc =
          std::ranges::any_of(groups, [](const Group &g) { return g.category != gameplay::item::ItemCategory::Misc; });

      float content_w = std::max(title_w, cursor_w + widest_body_w);
      int header_count = 0;
      for (const Group &group : groups) {
        if (!show_group_header(group.category, geometry.has_non_misc))
          continue;
        ++header_count;
        content_w = std::max(content_w,
                             r.measure_text(font_id, category_display_name(group.category), style.font_size_speaker));
      }
      if (lines.empty())
        content_w = std::max(content_w, r.measure_text(font_id, k_empty_label, style.font_size_body));

      geometry.clamped_cursor = lines.empty() ? -1 : std::clamp(cursor, 0, static_cast<int>(lines.size()) - 1);
      geometry.description_lines =
          geometry.clamped_cursor >= 0
              ? wrap_description(lines[static_cast<std::size_t>(geometry.clamped_cursor)].description, r, style)
              : std::vector<std::string>{};
      float description_w = 0.f;
      for (const std::string &line : geometry.description_lines)
        description_w = std::max(description_w, measure_body(line));
      content_w = std::max(content_w, description_w);

      geometry.panel_w = std::max(k_min_w, content_w + (k_pad_x * 2.f));
      const float body_rows = static_cast<float>(geometry.labels.empty() ? 1 : geometry.labels.size());
      const float group_gaps = header_count > 0 ? static_cast<float>(header_count - 1) * k_group_gap : 0.f;
      const float description_gap = geometry.description_lines.empty() ? 0.f : k_header_gap;
      const float description_h = static_cast<float>(geometry.description_lines.size()) * geometry.body_line_h;
      geometry.panel_h = (k_pad_y * 2.f) + geometry.header_line_h + k_header_gap + (body_rows * geometry.body_line_h) +
                         (static_cast<float>(header_count) * geometry.header_line_h) + group_gaps + description_gap +
                         description_h;

      geometry.panel_x = (viewport.x - geometry.panel_w) * 0.5f;
      geometry.panel_y = (viewport.y - geometry.panel_h) * 0.5f;
      geometry.title_y = geometry.panel_y + k_pad_y;
      geometry.row_x = geometry.panel_x + k_pad_x;
      geometry.body_top = geometry.title_y + geometry.header_line_h + k_header_gap;

      float y = geometry.body_top;
      for (std::size_t group_index = 0; group_index < groups.size(); ++group_index) {
        const Group &group = groups[group_index];
        if (show_group_header(group.category, geometry.has_non_misc)) {
          geometry.draw_rows.push_back(InventoryDrawRow{
              .header = true,
              .header_text = category_display_name(group.category),
              .y = y,
          });
          y += geometry.header_line_h;
        }
        for (std::size_t row = 0; row < group.count; ++row) {
          geometry.draw_rows.push_back(InventoryDrawRow{
              .header = false,
              .line_index = group.first + row,
              .y = y,
          });
          y += geometry.body_line_h;
        }
        if (group_index + 1 < groups.size())
          y += k_group_gap;
      }
      geometry.description_y = y + k_header_gap;
      return geometry;
    }

  } // namespace

  std::vector<InventoryLine> build_item_lines(const corundum::world::FlagStore &flags,
                                              const corundum::gameplay::item::Registry &items,
                                              std::string_view flag_prefix) {
    std::vector<InventoryLine> lines;
    for (const auto &[key, count] : flags) {
      if (count <= 0 || !key.starts_with(flag_prefix))
        continue;
      std::string_view id{key};
      id.remove_prefix(flag_prefix.size());
      if (id.empty())
        continue;
      const gameplay::item::Item *def = items.find(id);
      lines.push_back(InventoryLine{
          .category = def != nullptr ? def->category : gameplay::item::ItemCategory::Misc,
          .count = count,
          .name = def != nullptr ? def->name : std::string{id},
          .description = def != nullptr ? def->description : std::string{},
          .id = std::string{id},
      });
    }
    std::ranges::sort(lines, {}, [](const InventoryLine &l) { return std::tuple{l.category, l.name}; });
    return lines;
  }

  std::vector<InventoryLine> build_inventory_lines(const corundum::world::FlagStore &flags,
                                                   const corundum::gameplay::item::Registry &items) {
    return build_item_lines(flags, items, gameplay::item::k_flag_prefix);
  }

  InventoryLayout inventory_panel_layout(const platform::Renderer &r, const ui::PanelStyle &style,
                                         const std::vector<InventoryLine> &lines, int cursor,
                                         core::math::Vec2 viewport) {
    const InventoryGeometry geometry = compute_inventory_geometry(r, style, lines, cursor, viewport);
    InventoryLayout layout{};
    layout.panel_pos = {.x = geometry.panel_x, .y = geometry.panel_y};
    layout.panel_size = {.x = geometry.panel_w, .y = geometry.panel_h};
    for (const InventoryDrawRow &row : geometry.draw_rows) {
      if (row.header)
        continue;
      layout.rows.push_back(ui::RowRect{
          .pos = {.x = geometry.row_x, .y = row.y},
          .width = geometry.panel_w - (k_pad_x * 2.f),
          .height = geometry.body_line_h,
      });
    }
    return layout;
  }

  void inventory_panel_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                              const std::vector<InventoryLine> &lines, int cursor, core::math::Vec2 viewport) {
    const InventoryGeometry geometry = compute_inventory_geometry(r, style, lines, cursor, viewport);

    ui::panel_chrome(r, style.bg, border, {.x = geometry.panel_x, .y = geometry.panel_y},
                     {.x = geometry.panel_w, .y = geometry.panel_h});

    const std::uint32_t font_id = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);
    const float title_w = r.measure_text(font_id, k_panel_header, style.font_size_speaker);
    const float header_x = geometry.panel_x + ((geometry.panel_w - title_w) * 0.5f);
    r.draw(platform::DrawText{
        .font_id = font_id,
        .text = k_panel_header,
        .position = {.x = header_x, .y = geometry.title_y},
        .char_size = style.font_size_speaker,
        .colour = style.speaker,
    });

    if (lines.empty()) {
      const float empty_w = r.measure_text(font_id, k_empty_label, style.font_size_body);
      const float empty_x = geometry.panel_x + ((geometry.panel_w - empty_w) * 0.5f);
      r.draw(platform::DrawText{
          .font_id = font_id,
          .text = k_empty_label,
          .position = {.x = empty_x, .y = geometry.body_top},
          .char_size = style.font_size_body,
          .colour = style.choice,
      });
      return;
    }

    for (const InventoryDrawRow &row : geometry.draw_rows) {
      if (row.header) {
        const float label_w = r.measure_text(font_id, row.header_text, style.font_size_speaker);
        const float label_x = geometry.panel_x + ((geometry.panel_w - label_w) * 0.5f);
        r.draw(platform::DrawText{
            .font_id = font_id,
            .text = row.header_text,
            .position = {.x = label_x, .y = row.y},
            .char_size = style.font_size_speaker,
            .colour = style.speaker,
        });
      } else {
        ui::draw_option(r, style, geometry.labels[row.line_index], {.x = geometry.row_x, .y = row.y},
                        std::cmp_equal(row.line_index, geometry.clamped_cursor));
      }
    }

    if (!geometry.description_lines.empty()) {
      draw_description(r, style, geometry.description_lines, geometry.panel_x + k_pad_x, geometry.description_y,
                       geometry.body_line_h);
    }
  }

} // namespace corundum::gameplay::screens
