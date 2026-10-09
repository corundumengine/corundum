// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/character_sheet.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/gameplay/screens/hud_strip.hpp>
#include <corundum/gameplay/screens/inventory_panel.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/font_family.hpp>
#include <corundum/ui/input_glyph.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <corundum/world/flags.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::gameplay::screens {

  namespace {

    constexpr std::string_view k_empty_label = "(empty)";
    constexpr std::string_view k_panel_header = "Character";

    constexpr float k_pad_x = 24.f;
    constexpr float k_pad_y = 16.f;
    constexpr float k_title_gap = 10.f;
    constexpr float k_column_gap = 28.f;
    constexpr float k_footer_gap = 10.f;

    /// A number the game may not have written; absent and negative both read as 0.
    int flag_count(const world::FlagStore &flags, std::string_view key) {
      return std::max(0, world::visit_count(flags, std::string{key}));
    }

    float body_line_height(const ui::PanelStyle &style) noexcept {
      return std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
    }

    float header_line_height(const ui::PanelStyle &style) noexcept {
      return std::max(body_line_height(style), static_cast<float>(style.font_size_speaker) + 4.f);
    }

    /// The three section columns under the title, and the footer baseline they stop above.
    struct SectionGeometry {
      ui::RowRect stats{};
      ui::RowRect equipment{};
      ui::RowRect inventory{};
      float footer_y{};
    };

    SectionGeometry compute_sections(const ui::PanelStyle &style, const ui::PanelRect &panel) {
      const float header_h = header_line_height(style);
      const float body_h = body_line_height(style);
      const float content_top = panel.pos.y + k_pad_y + header_h + k_title_gap;
      const float footer_y = panel.pos.y + panel.size.y - k_pad_y - body_h;
      const float content_h = std::max(0.f, footer_y - k_footer_gap - content_top);
      const float column_w = std::max(0.f, (panel.size.x - (k_pad_x * 2.f) - (k_column_gap * 2.f)) / 3.f);

      const float stats_x = panel.pos.x + k_pad_x;
      const float equipment_x = stats_x + column_w + k_column_gap;
      const float inventory_x = equipment_x + column_w + k_column_gap;
      return SectionGeometry{
          .stats = {.pos = {.x = stats_x, .y = content_top}, .width = column_w, .height = content_h},
          .equipment = {.pos = {.x = equipment_x, .y = content_top}, .width = column_w, .height = content_h},
          .inventory = {.pos = {.x = inventory_x, .y = content_top}, .width = column_w, .height = content_h},
          .footer_y = footer_y,
      };
    }

    std::uint32_t regular_font(const ui::PanelStyle &style) noexcept {
      return style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);
    }

    void draw_text(platform::Renderer &r, const ui::PanelStyle &style, std::string_view text, core::math::Vec2 pos,
                   unsigned char_size, core::math::Colour colour) {
      r.draw(platform::DrawText{
          .font_id = regular_font(style),
          .text = text,
          .position = pos,
          .char_size = char_size,
          .colour = colour,
      });
    }

    void draw_section_header(platform::Renderer &r, const ui::PanelStyle &style, std::string_view title,
                             const ui::RowRect &section) {
      draw_text(r, style, title, section.pos, style.font_size_speaker, style.speaker);
    }

    /// Draw one "label: value" row and return the y of the next row.
    float draw_labeled_row(platform::Renderer &r, const ui::PanelStyle &style, const ui::RowRect &section, float y,
                           std::string_view label, std::string_view value) {
      const std::string text = std::format("{}: {}", label, value);
      draw_text(r, style, text, {.x = section.pos.x, .y = y}, style.font_size_body, style.choice);
      return y + body_line_height(style);
    }

    /// Draw @p rows from @p first_y down @p section, scrolling so @p cursor stays visible.
    /// A negative @p cursor draws every row unselected (a section the cursor is not in).
    void draw_section_rows(platform::Renderer &r, const ui::PanelStyle &style, const ui::RowRect &section,
                           float first_y, const std::vector<std::string> &rows, int cursor) {
      if (rows.empty())
        return;
      const float line_h = body_line_height(style);
      const float available = std::max(0.f, section.pos.y + section.height - first_y);
      const int capacity = std::max(1, static_cast<int>(std::floor(available / line_h)));
      const int count = static_cast<int>(rows.size());
      const int first = ui::clamp_scroll_to_cursor(0, std::max(0, cursor), count, capacity);
      const int last = std::min(count, first + capacity);
      for (int i = first; i < last; ++i) {
        ui::draw_option(r, style, rows[static_cast<std::size_t>(i)],
                        {.x = section.pos.x, .y = first_y + (static_cast<float>(i - first) * line_h)}, i == cursor);
      }
    }

  } // namespace

  CharacterInfo build_character_info(const world::FlagStore &flags, const item::Registry &items) {
    CharacterInfo info{};
    info.level = std::max(1, world::visit_count(flags, std::string{k_level_flag}));
    info.experience = flag_count(flags, k_experience_flag);
    info.experience_to_next_level = flag_count(flags, k_next_level_experience_flag);
    info.health = flag_count(flags, k_health_flag);
    info.max_health = flag_count(flags, k_max_health_flag);
    info.gold = flag_count(flags, k_gold_flag);
    info.equipment = build_equipment_lines(flags, items);
    info.inventory = build_inventory_lines(flags, items);
    info.inventory_count = static_cast<int>(info.inventory.size());
    info.inventory_capacity = flag_count(flags, k_inventory_capacity_flag);
    return info;
  }

  int character_section_row_count(const CharacterInfo &info, int section) noexcept {
    switch (section) {
      case 1:
        return std::max(1, static_cast<int>(info.equipment.size()));
      case 2:
        return std::max(1, static_cast<int>(info.inventory.size()));
      default:
        return info.max_health > 0 ? 4 : 3;
    }
  }

  void move_character_cursor(CharacterSheetState &state, const CharacterInfo &info, int dx, int dy) noexcept {
    constexpr int k_section_count = 3;
    if (dx != 0)
      state.section = (state.section + dx + k_section_count) % k_section_count;
    const int rows = character_section_row_count(info, state.section);
    if (dy != 0)
      state.row = (state.row + dy + rows) % rows;
    else
      state.row = std::clamp(state.row, 0, rows - 1);
  }

  CharacterSheetLayout character_sheet_layout(const ui::PanelStyle &style, core::math::Vec2 viewport) {
    const ui::PanelRect panel = ui::screen_panel_rect(viewport, style);
    const SectionGeometry sections = compute_sections(style, panel);
    return CharacterSheetLayout{
        .panel_pos = panel.pos,
        .panel_size = panel.size,
        .stats = sections.stats,
        .equipment = sections.equipment,
        .inventory = sections.inventory,
    };
  }

  void character_sheet_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                              const CharacterInfo &info, core::math::Vec2 viewport, input::InputDevice last_device,
                              const CharacterSheetState &state) {
    const ui::PanelRect panel = ui::screen_panel_rect(viewport, style);
    const SectionGeometry sections = compute_sections(style, panel);

    ui::screen_backdrop(r, style, viewport);
    ui::panel_chrome(r, style.bg, border, panel.pos, panel.size);

    const float title_w = r.measure_text(regular_font(style), k_panel_header, style.font_size_speaker);
    draw_text(r, style, k_panel_header,
              {.x = panel.pos.x + ((panel.size.x - title_w) * 0.5f), .y = panel.pos.y + k_pad_y},
              style.font_size_speaker, style.speaker);

    draw_section_header(r, style, "Stats", sections.stats);
    std::vector<std::string> stats_rows;
    stats_rows.push_back(std::format("Level: {}", info.level));
    stats_rows.push_back(info.experience_to_next_level > 0
                             ? std::format("Experience: {} / {}", info.experience, info.experience_to_next_level)
                             : std::format("Experience: {}", info.experience));
    if (info.max_health > 0)
      stats_rows.push_back(std::format("Health: {} / {}", info.health, info.max_health));
    stats_rows.push_back(std::format("Gold: {}", info.gold));
    draw_section_rows(r, style, sections.stats, sections.stats.pos.y + header_line_height(style), stats_rows,
                      state.section == 0 ? state.row : -1);

    draw_section_header(r, style, "Equipment", sections.equipment);
    std::vector<std::string> equipment_rows;
    if (info.equipment.empty()) {
      equipment_rows.push_back(std::format("Slots: {}", k_empty_label));
    } else {
      for (const EquipmentLine &line : info.equipment) {
        equipment_rows.push_back(
            std::format("{}: {}", line.slot, line.item_name.empty() ? k_empty_label : line.item_name));
      }
    }
    draw_section_rows(r, style, sections.equipment, sections.equipment.pos.y + header_line_height(style),
                      equipment_rows, state.section == 1 ? state.row : -1);

    draw_section_header(r, style, "Inventory", sections.inventory);
    float y = sections.inventory.pos.y + header_line_height(style);
    y = draw_labeled_row(r, style, sections.inventory, y, "Items",
                         info.inventory_capacity > 0
                             ? std::format("{} / {}", info.inventory_count, info.inventory_capacity)
                             : std::format("{}", info.inventory_count));
    std::vector<std::string> inventory_rows;
    inventory_rows.reserve(info.inventory.size());
    for (const InventoryLine &line : info.inventory) {
      inventory_rows.push_back(line.count > 1 ? std::format("{} x{}", line.name, line.count) : line.name);
    }
    draw_section_rows(r, style, sections.inventory, y, inventory_rows, state.section == 2 ? state.row : -1);

    const std::string footer = std::format(
        "{} Navigate   {} Section   {} Equip   {} Close", ui::input_glyph(input::Action::MoveUp, last_device),
        ui::input_glyph(input::Action::MoveLeft, last_device), ui::input_glyph(input::Action::Activate, last_device),
        ui::input_glyph(input::Action::Cancel, last_device));
    const float footer_w = r.measure_text(regular_font(style), footer, style.font_size_body);
    draw_text(r, style, footer, {.x = panel.pos.x + panel.size.x - k_pad_x - footer_w, .y = sections.footer_y},
              style.font_size_body, style.choice);
  }

} // namespace corundum::gameplay::screens
