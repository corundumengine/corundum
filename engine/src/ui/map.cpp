// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/ui/map.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/location/location.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/dialog_box.hpp>
#include <corundum/ui/input_glyph.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <corundum/world/flags.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::ui {

  std::vector<MapEntry> build_map_entries(const location::Registry &registry, const world::FlagStore &flags,
                                          std::string_view zone_id) {
    std::vector<MapEntry> entries;
    for (const auto &[id, location] : registry) {
      if (!world::has_flag(flags, location::discovery_flag_key(id)))
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

  void map_panel_render(platform::Renderer &r, const DialogBoxStyle &style, const NinePatchBorder &border,
                        const std::vector<MapEntry> &entries, int cursor, core::math::Vec2 viewport,
                        input::InputDevice last_device) {
    constexpr float k_min_w = 280.f;
    constexpr float k_pad_x = 28.f;
    constexpr float k_pad_y = 18.f;
    constexpr float k_title_gap = 12.f;
    constexpr float k_footer_gap = 14.f;
    constexpr std::string_view k_title = "Map";
    constexpr std::string_view k_empty = "(no locations)";
    constexpr std::string_view k_here = " (here)";

    const float line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 6.f);
    const float title_h = std::max(line_h, static_cast<float>(style.font_size_speaker) + 6.f);

    const std::string footer = std::format("{} Travel   {} Close", input_glyph(input::Action::Activate, last_device),
                                           input_glyph(input::Action::Cancel, last_device));

    float widest = std::max({r.measure_text(style.font_id, k_title, style.font_size_speaker),
                             r.measure_text(style.font_id, k_empty, style.font_size_body),
                             r.measure_text(style.font_id, footer, style.font_size_body)});
    for (const MapEntry &entry : entries) {
      const std::string label = entry.current ? entry.name + std::string(k_here) : entry.name;
      widest = std::max(widest, r.measure_text(style.font_id, label, style.font_size_body));
    }

    const float panel_w = std::max(k_min_w, widest + (k_pad_x * 2.f));
    const std::size_t rows = entries.empty() ? 1 : entries.size();
    const float panel_h =
        (k_pad_y * 2.f) + title_h + k_title_gap + (static_cast<float>(rows) * line_h) + k_footer_gap + line_h;
    const float panel_x = (viewport.x - panel_w) * 0.5f;
    const float panel_y = (viewport.y - panel_h) * 0.5f;

    panel_chrome(r, style.bg, border, {.x = panel_x, .y = panel_y}, {.x = panel_w, .y = panel_h});

    const float title_w = r.measure_text(style.font_id, k_title, style.font_size_speaker);
    const float title_y = panel_y + k_pad_y;
    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = k_title,
        .position = {.x = panel_x + ((panel_w - title_w) * 0.5f), .y = title_y},
        .char_size = style.font_size_speaker,
        .colour = style.speaker,
    });

    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = footer,
        .position = {.x = panel_x + ((panel_w - r.measure_text(style.font_id, footer, style.font_size_body)) * 0.5f),
                     .y = panel_y + panel_h - k_pad_y - line_h},
        .char_size = style.font_size_body,
        .colour = style.choice,
    });

    float y = title_y + title_h + k_title_gap;
    if (entries.empty()) {
      r.draw(platform::DrawText{
          .font_id = style.font_id,
          .text = k_empty,
          .position = {.x = panel_x + k_pad_x, .y = y},
          .char_size = style.font_size_body,
          .colour = style.choice,
      });
      return;
    }

    const int clamped_cursor = std::clamp(cursor, 0, static_cast<int>(entries.size()) - 1);
    for (std::size_t i = 0; i < entries.size(); ++i) {
      const MapEntry &entry = entries[i];
      const std::string label = entry.current ? entry.name + std::string(k_here) : entry.name;
      draw_option(r, style, label, {.x = panel_x + k_pad_x, .y = y}, std::cmp_equal(i, clamped_cursor));
      y += line_h;
    }
  }

} // namespace corundum::ui
