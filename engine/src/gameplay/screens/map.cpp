// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

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
#include <utility>
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
    constexpr float k_map_min_w = 280.f;
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

  MapLayout map_panel_layout(const platform::Renderer &r, const ui::PanelStyle &style,
                             const std::vector<MapEntry> &entries, core::math::Vec2 viewport,
                             input::InputDevice last_device) {
    const float line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 6.f);
    const float title_h = std::max(line_h, static_cast<float>(style.font_size_speaker) + 6.f);

    const std::string footer = map_footer(last_device);
    const std::uint32_t font_id = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);

    float widest = std::max({
        r.measure_text(font_id, k_map_title, style.font_size_speaker),
        r.measure_text(font_id, k_map_empty, style.font_size_body),
        r.measure_text(font_id, footer, style.font_size_body),
    });
    for (const MapEntry &entry : entries) {
      widest = std::max(widest, r.measure_text(font_id, map_entry_label(entry), style.font_size_body));
    }

    const float panel_w = std::max(k_map_min_w, widest + (k_map_pad_x * 2.f));
    const std::size_t rows = entries.empty() ? 1 : entries.size();
    const float panel_h = (k_map_pad_y * 2.f) + title_h + k_map_title_gap + (static_cast<float>(rows) * line_h) +
                          k_map_footer_gap + line_h;
    const float panel_x = (viewport.x - panel_w) * 0.5f;
    const float panel_y = (viewport.y - panel_h) * 0.5f;

    MapLayout layout{};
    layout.panel_pos = {.x = panel_x, .y = panel_y};
    layout.panel_size = {.x = panel_w, .y = panel_h};
    layout.rows = ui::ListHit{
        .row_pos = {.x = panel_x + k_map_pad_x, .y = panel_y + k_map_pad_y + title_h + k_map_title_gap},
        .row_width = panel_w - (k_map_pad_x * 2.f),
        .row_height = line_h,
        .first_row = 0,
        .visible_rows = static_cast<int>(entries.size()),
    };
    return layout;
  }

  void map_panel_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                        const std::vector<MapEntry> &entries, int cursor, core::math::Vec2 viewport,
                        input::InputDevice last_device) {
    const MapLayout layout = map_panel_layout(r, style, entries, viewport, last_device);
    const float line_h = layout.rows.row_height;

    const std::string footer = map_footer(last_device);
    const std::uint32_t font_id = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);

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

    float y = layout.rows.row_pos.y;
    if (entries.empty()) {
      r.draw(platform::DrawText{
          .font_id = font_id,
          .text = k_map_empty,
          .position = {.x = layout.rows.row_pos.x, .y = y},
          .char_size = style.font_size_body,
          .colour = style.choice,
      });
      return;
    }

    const int clamped_cursor = std::clamp(cursor, 0, static_cast<int>(entries.size()) - 1);
    for (std::size_t i = 0; i < entries.size(); ++i) {
      ui::draw_option(r, style, map_entry_label(entries[i]), {.x = layout.rows.row_pos.x, .y = y},
                      std::cmp_equal(i, clamped_cursor));
      y += line_h;
    }
  }

} // namespace corundum::gameplay::screens
