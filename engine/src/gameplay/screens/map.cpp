// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/hub_tabs.hpp>
#include <corundum/gameplay/screens/map.hpp>
#include <corundum/ui/font_family.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/location/location.hpp>
#include <corundum/gameplay/location/registry.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/input_glyph.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <corundum/world/flags.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::gameplay::screens {

  std::vector<MapEntry> build_map_entries(const gameplay::location::Registry &registry, const world::FlagStore &flags,
                                          std::string_view zone_id) {
    std::vector<MapEntry> entries;
    for (const auto &[id, location] : registry) {
      if (!world::has_flag(flags, gameplay::location::discovery_flag_key(id)))
        continue;
      entries.push_back(MapEntry{
          .id = location.id,
          .current = !zone_id.empty() && location.zone == zone_id,
          .name = location.name,
      });
    }
    std::ranges::sort(entries, {}, [](const MapEntry &entry) { return entry.name; });
    return entries;
  }

  namespace {
    constexpr float k_map_pad_x = 28.f;
    constexpr float k_map_pad_y = 18.f;
    constexpr float k_map_title_gap = 12.f;
    constexpr float k_map_footer_gap = 14.f;
    constexpr std::string_view k_map_title = "Map";
    constexpr std::string_view k_map_empty = "(no locations)";
    constexpr std::string_view k_map_here = " (here)";

    /// A destination's display label, tagged when it is the zone the player is already in.
    std::string map_entry_label(const MapEntry &entry) {
      return entry.current ? entry.name + std::string(k_map_here) : entry.name;
    }

    /// Footer hint, using the last-used device's glyphs.
    std::string map_footer(input::InputDevice last_device) {
      return std::format("{} Travel   {} Close", ui::input_glyph(input::Action::Activate, last_device),
                         ui::input_glyph(input::Action::Cancel, last_device));
    }
  } // namespace

  MapLayout map_panel_layout(const platform::Renderer & /*r*/, const ui::PanelStyle &style,
                             const std::vector<MapEntry> &entries, int cursor, int scroll, core::math::Vec2 viewport,
                             input::InputDevice /*last_device*/) {
    const float line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 6.f);
    const float title_h = std::max(line_h, static_cast<float>(style.font_size_speaker) + 6.f);
    const ui::PanelRect panel = ui::screen_panel_rect(viewport, style, hub_panel_top_inset(style));

    const float list_top = panel.pos.y + k_map_pad_y + title_h + k_map_title_gap;
    const float footer_y = panel.pos.y + panel.size.y - k_map_pad_y - line_h;
    const float list_bottom = footer_y - k_map_footer_gap;
    const int row_count = static_cast<int>(entries.size());
    const int fits = std::max(1, static_cast<int>((list_bottom - list_top) / line_h));
    const int visible_rows = std::min(row_count, fits);
    const int first_row = ui::clamp_scroll_to_cursor(scroll, cursor, row_count, visible_rows);

    MapLayout layout{};
    layout.panel_pos = panel.pos;
    layout.panel_size = panel.size;
    layout.rows = ui::ListHit{
        .row_pos = {.x = panel.pos.x + k_map_pad_x, .y = list_top},
        .row_width = panel.size.x - (k_map_pad_x * 2.f),
        .row_height = line_h,
        .first_row = first_row,
        .visible_rows = visible_rows,
    };
    return layout;
  }

  void map_panel_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                        const std::vector<MapEntry> &entries, int cursor, int scroll, core::math::Vec2 viewport,
                        input::InputDevice last_device) {
    const MapLayout layout = map_panel_layout(r, style, entries, cursor, scroll, viewport, last_device);
    const float line_h = layout.rows.row_height;

    const std::string footer = map_footer(last_device);
    const std::uint32_t font_id = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);

    ui::screen_backdrop(r, style, viewport);
    ui::panel_chrome(r, style.bg, border, layout.panel_pos, layout.panel_size);

    const float title_w = r.measure_text(font_id, k_map_title, style.font_size_speaker);
    const float title_y = layout.panel_pos.y + k_map_pad_y;
    r.draw(platform::DrawText{
        .font_id = font_id,
        .text = k_map_title,
        .position = {.x = layout.panel_pos.x + ((layout.panel_size.x - title_w) * 0.5f), .y = title_y},
        .char_size = style.font_size_speaker,
        .colour = style.speaker,
    });

    r.draw(platform::DrawText{
        .font_id = font_id,
        .text = footer,
        .position =
            {
                .x = layout.panel_pos.x +
                     ((layout.panel_size.x - r.measure_text(font_id, footer, style.font_size_body)) * 0.5f),
                .y = layout.panel_pos.y + layout.panel_size.y - k_map_pad_y - line_h,
            },
        .char_size = style.font_size_body,
        .colour = style.choice,
    });

    if (entries.empty()) {
      r.draw(platform::DrawText{
          .font_id = font_id,
          .text = k_map_empty,
          .position = {.x = layout.rows.row_pos.x, .y = layout.rows.row_pos.y},
          .char_size = style.font_size_body,
          .colour = style.choice,
      });
      return;
    }

    const int clamped_cursor = std::clamp(cursor, 0, static_cast<int>(entries.size()) - 1);
    float y = layout.rows.row_pos.y;
    const int last_row = std::min(layout.rows.first_row + layout.rows.visible_rows, static_cast<int>(entries.size()));
    for (int i = layout.rows.first_row; i < last_row; ++i) {
      ui::draw_option(r, style, map_entry_label(entries[static_cast<std::size_t>(i)]),
                      {.x = layout.rows.row_pos.x, .y = y}, i == clamped_cursor);
      y += line_h;
    }
  }

} // namespace corundum::gameplay::screens
