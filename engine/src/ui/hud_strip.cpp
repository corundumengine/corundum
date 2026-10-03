// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/ui/hud_strip.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/quest/status.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/dialog_box.hpp>
#include <corundum/ui/journal.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <corundum/world/flags.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>

namespace corundum::ui {

  HudStripData build_hud_strip(const world::FlagStore &flags, const gameplay::quest::Registry &quests,
                               std::string_view zone_id) {
    HudStripData data{};
    data.gold = world::visit_count(flags, std::string{k_gold_flag});

    for (const JournalEntry &entry : build_journal_entries(quests, flags, zone_id)) {
      if (entry.lifecycle != gameplay::quest::Lifecycle::Active)
        continue;
      data.has_quest = true;
      data.quest_name = entry.name;
      data.objective = entry.objective;
      break;
    }
    return data;
  }

  void hud_strip_render(platform::Renderer &r, const DialogBoxStyle &style, const NinePatchBorder &border,
                        const HudStripData &data) {
    constexpr float k_pad_x = 12.f;
    constexpr float k_pad_y = 8.f;
    constexpr float k_margin = 16.f;
    constexpr float k_min_w = 220.f;
    constexpr std::string_view k_separator = "    ";

    std::string line = std::format("Gold: {}", data.gold);
    if (data.has_quest) {
      line += k_separator;
      line += data.quest_name;
      if (!data.objective.empty()) {
        line += " - ";
        line += data.objective;
      }
    }

    // Text must clear the nine-patch corners, whose cells are drawn at natural size; the
    // dialogue box insets content the same way (dialog_layout.hpp's `max(margin, tile)`).
    const float pad_x = std::max(k_pad_x, static_cast<float>(border.tile_w));
    const float pad_y = std::max(k_pad_y, static_cast<float>(border.tile_h));

    const float line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
    const float row_h = line_h + (pad_y * 2.f);
    const float text_w = r.measure_text(style.font_id, line, style.font_size_body);
    const float row_w = std::max(k_min_w, text_w + (pad_x * 2.f));

    // Background and frame are separate calls: the HUD is persistent, so its fill is fully
    // opaque — the dialogue box's translucent style.bg would tint the world showing through it
    // for the whole session.
    core::math::Colour hud_bg = style.bg;
    hud_bg.a = 255;
    const core::math::Vec2 panel_pos{.x = k_margin, .y = k_margin};
    const core::math::Vec2 panel_size{.x = row_w, .y = row_h};
    panel_fill(r, hud_bg, panel_pos, panel_size);
    panel_frame(r, border, panel_pos, panel_size);
    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = line,
        .position = {.x = panel_pos.x + pad_x, .y = panel_pos.y + pad_y},
        .char_size = style.font_size_body,
        .colour = style.body,
    });
  }

} // namespace corundum::ui
