// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/item/item.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/gameplay/screens/hub_tabs.hpp>
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
    constexpr std::string_view k_equipment_header = "Equipment";
    constexpr std::string_view k_weapon_slot = "weapon";

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

    /// The equipment slot an item occupies, or empty when it has none.
    std::string item_slot(const gameplay::item::Item &item) {
      if (item.apparel && !item.apparel->slot.empty())
        return item.apparel->slot;
      if (item.weapon)
        return std::string{k_weapon_slot};
      return {};
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

    constexpr float k_pad_x = 24.f;
    constexpr float k_pad_y = 16.f;
    constexpr float k_header_gap = 10.f;
    constexpr float k_group_gap = 6.f;
    constexpr float k_equipment_col_w = 240.f;
    constexpr float k_column_gap = 32.f;

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
      float equip_x{};
      float list_x{};
      float list_w{};
      float content_top{};
      float content_bottom{};
      int first_row{};
      int visible_rows{};
      float description_y{};
      std::vector<InventoryDrawRow> draw_rows;
    };

    /// Row height of entry @p index: an item line, plus a header line when it starts a group.
    float entry_row_height(std::size_t index, const std::vector<InventoryLine> &lines, float body_h, float header_h,
                           bool has_non_misc) {
      float height = body_h;
      const bool starts_group = index == 0 || lines[index].category != lines[index - 1].category;
      if (starts_group && show_group_header(lines[index].category, has_non_misc))
        height += header_h;
      return height;
    }

    /// The visible slice of the inventory list.
    struct RowWindow {
      int first_row{};

      int visible_rows{};
    };

    /// First visible entry: keeps @p cursor inside the window and fits row/header heights.
    RowWindow compute_row_window(int scroll, int cursor, const std::vector<InventoryLine> &lines, float available,
                                 float body_h, float header_h, bool has_non_misc) {
      const int entry_count = static_cast<int>(lines.size());
      if (entry_count <= 0)
        return {};

      const auto window_end = [&](int start) {
        float used = 0.f;
        int i = start;
        while (i < entry_count) {
          const float height = entry_row_height(static_cast<std::size_t>(i), lines, body_h, header_h, has_non_misc);
          if (i > start && used + height > available)
            break;
          used += height;
          ++i;
        }
        return i;
      };

      const int estimate = std::max(1, static_cast<int>(available / body_h));
      int first = ui::clamp_scroll_to_cursor(scroll, cursor, entry_count, estimate);
      if (cursor >= window_end(first))
        first = cursor;
      return RowWindow{.first_row = first, .visible_rows = window_end(first) - first};
    }

    InventoryGeometry compute_inventory_geometry(const platform::Renderer &r, const ui::PanelStyle &style,
                                                 const std::vector<InventoryLine> &lines,
                                                 const std::vector<EquipmentLine> &equipment, int cursor, int scroll,
                                                 core::math::Vec2 viewport) {
      InventoryGeometry geometry{};
      geometry.body_line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
      geometry.header_line_h = std::max(geometry.body_line_h, static_cast<float>(style.font_size_speaker) + 4.f);

      const ui::PanelRect panel = ui::screen_panel_rect(viewport, style, hub_panel_top_inset(style));
      geometry.panel_x = panel.pos.x;
      geometry.panel_y = panel.pos.y;
      geometry.panel_w = panel.size.x;
      geometry.panel_h = panel.size.y;
      geometry.title_y = geometry.panel_y + k_pad_y;
      geometry.content_top = geometry.title_y + geometry.header_line_h + k_header_gap;

      const bool has_equipment = !equipment.empty();
      if (has_equipment) {
        geometry.equip_x = geometry.panel_x + k_pad_x;
        geometry.list_x = geometry.equip_x + k_equipment_col_w + k_column_gap;
        geometry.list_w = geometry.panel_x + geometry.panel_w - k_pad_x - geometry.list_x;
      } else {
        geometry.list_x = geometry.panel_x + k_pad_x;
        geometry.list_w = geometry.panel_w - (k_pad_x * 2.f);
      }

      for (const InventoryLine &line : lines)
        geometry.labels.push_back(std::format("{}  x{}", line.name, line.count));

      geometry.has_non_misc = std::ranges::any_of(
          lines, [](const InventoryLine &line) { return line.category != gameplay::item::ItemCategory::Misc; });

      geometry.clamped_cursor = lines.empty() ? -1 : std::clamp(cursor, 0, static_cast<int>(lines.size()) - 1);
      geometry.description_lines =
          geometry.clamped_cursor >= 0
              ? wrap_description(lines[static_cast<std::size_t>(geometry.clamped_cursor)].description, r, style)
              : std::vector<std::string>{};

      const float description_h = static_cast<float>(geometry.description_lines.size()) * geometry.body_line_h;
      geometry.description_y = geometry.panel_y + geometry.panel_h - k_pad_y - description_h;
      geometry.content_bottom = geometry.description_y - (geometry.description_lines.empty() ? 0.f : k_header_gap);

      const float available = std::max(0.f, geometry.content_bottom - geometry.content_top);
      const RowWindow window = compute_row_window(scroll, std::max(0, geometry.clamped_cursor), lines, available,
                                                  geometry.body_line_h, geometry.header_line_h, geometry.has_non_misc);
      geometry.first_row = window.first_row;
      geometry.visible_rows = window.visible_rows;

      const int entry_count = static_cast<int>(lines.size());
      const int last_row = std::min(entry_count, geometry.first_row + geometry.visible_rows);
      float y = geometry.content_top;
      for (int i = geometry.first_row; i < last_row; ++i) {
        const InventoryLine &line = lines[static_cast<std::size_t>(i)];
        const bool starts_group = i == 0 || line.category != lines[static_cast<std::size_t>(i - 1)].category;
        if (starts_group && show_group_header(line.category, geometry.has_non_misc)) {
          geometry.draw_rows.push_back(InventoryDrawRow{
              .header = true,
              .header_text = category_display_name(line.category),
              .y = y,
          });
          y += geometry.header_line_h;
        }
        geometry.draw_rows.push_back(InventoryDrawRow{
            .header = false,
            .line_index = static_cast<std::size_t>(i),
            .y = y,
        });
        y += geometry.body_line_h;
        if (i + 1 < last_row && lines[static_cast<std::size_t>(i) + 1u].category != line.category)
          y += k_group_gap;
      }
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

  std::vector<EquipmentLine> build_equipment_lines(const corundum::world::FlagStore &flags,
                                                   const corundum::gameplay::item::Registry &items) {
    std::vector<EquipmentLine> lines;
    for (const auto &[key, count] : flags) {
      if (!gameplay::item::is_held_item(key, count))
        continue;
      const std::string_view id = gameplay::item::item_id_from_flag(key);
      const gameplay::item::Item *def = items.find(id);
      if (def == nullptr)
        continue;
      const std::string slot = item_slot(*def);
      if (slot.empty())
        continue;

      auto line = std::ranges::find(lines, slot, &EquipmentLine::slot);
      if (line == lines.end())
        line = lines.insert(lines.end(), EquipmentLine{.slot = slot});
      // First equipped item wins, matching the one-equipped-item-per-slot convention.
      if (line->item_name.empty() && world::has_flag(flags, std::format("equip.{}.{}", slot, id)))
        line->item_name = def->name;
    }
    std::ranges::sort(lines, {}, &EquipmentLine::slot);
    return lines;
  }

  InventoryLayout inventory_panel_layout(const platform::Renderer &r, const ui::PanelStyle &style,
                                         const std::vector<InventoryLine> &lines,
                                         const std::vector<EquipmentLine> &equipment, int cursor, int scroll,
                                         core::math::Vec2 viewport) {
    const InventoryGeometry geometry = compute_inventory_geometry(r, style, lines, equipment, cursor, scroll, viewport);
    InventoryLayout layout{};
    layout.panel_pos = {.x = geometry.panel_x, .y = geometry.panel_y};
    layout.panel_size = {.x = geometry.panel_w, .y = geometry.panel_h};
    layout.first_row = geometry.first_row;
    layout.visible_rows = geometry.visible_rows;
    for (const InventoryDrawRow &row : geometry.draw_rows) {
      if (row.header)
        continue;
      layout.rows.push_back(ui::RowRect{
          .pos = {.x = geometry.list_x, .y = row.y},
          .width = geometry.list_w,
          .height = geometry.body_line_h,
      });
    }
    return layout;
  }

  void inventory_panel_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                              const std::vector<InventoryLine> &lines, const std::vector<EquipmentLine> &equipment,
                              int cursor, int scroll, core::math::Vec2 viewport) {
    const InventoryGeometry geometry = compute_inventory_geometry(r, style, lines, equipment, cursor, scroll, viewport);

    ui::screen_backdrop(r, style, viewport);
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

    if (!equipment.empty()) {
      r.draw(platform::DrawText{
          .font_id = font_id,
          .text = k_equipment_header,
          .position = {.x = geometry.equip_x, .y = geometry.content_top},
          .char_size = style.font_size_speaker,
          .colour = style.speaker,
      });
      float y = geometry.content_top + geometry.header_line_h;
      for (const EquipmentLine &line : equipment) {
        const std::string text =
            std::format("{}: {}", line.slot, line.item_name.empty() ? k_empty_label : line.item_name);
        r.draw(platform::DrawText{
            .font_id = font_id,
            .text = text,
            .position = {.x = geometry.equip_x, .y = y},
            .char_size = style.font_size_body,
            .colour = style.choice,
        });
        y += geometry.body_line_h;
      }
    }

    if (lines.empty()) {
      r.draw(platform::DrawText{
          .font_id = font_id,
          .text = k_empty_label,
          .position = {.x = geometry.list_x, .y = geometry.content_top},
          .char_size = style.font_size_body,
          .colour = style.choice,
      });
      return;
    }

    for (const InventoryDrawRow &row : geometry.draw_rows) {
      if (row.header) {
        r.draw(platform::DrawText{
            .font_id = font_id,
            .text = row.header_text,
            .position = {.x = geometry.list_x, .y = row.y},
            .char_size = style.font_size_speaker,
            .colour = style.speaker,
        });
      } else {
        ui::draw_option(r, style, geometry.labels[row.line_index], {.x = geometry.list_x, .y = row.y},
                        std::cmp_equal(row.line_index, static_cast<std::size_t>(geometry.clamped_cursor)));
      }
    }

    if (!geometry.description_lines.empty())
      draw_description(r, style, geometry.description_lines, geometry.list_x, geometry.description_y,
                       geometry.body_line_h);
  }

} // namespace corundum::gameplay::screens
