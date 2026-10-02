// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/ui/hud_strip.hpp>

#include <corundum/platform/renderer.hpp>
#include <corundum/quest/status.hpp>
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

  namespace {
    /// FlagStore key holding the player's currency.
    constexpr std::string_view k_gold_flag = "gold";
  } // namespace

  HudStripData build_hud_strip(const world::FlagStore &flags, const quest::Registry &quests, std::string_view zone_id) {
    HudStripData data{};
    data.gold = world::visit_count(flags, std::string{k_gold_flag});

    for (const JournalEntry &entry : build_journal_entries(quests, flags, zone_id)) {
      if (entry.lifecycle != quest::Lifecycle::Active)
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

    const float line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
    const float row_h = line_h + (k_pad_y * 2.f);
    const float text_w = r.measure_text(style.font_id, line, style.font_size_body);
    const float row_w = text_w + (k_pad_x * 2.f);

    panel_chrome(r, style.bg, border, {.x = k_margin, .y = k_margin}, {.x = row_w, .y = row_h});
    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = line,
        .position = {.x = k_margin + k_pad_x, .y = k_margin + k_pad_y},
        .char_size = style.font_size_body,
        .colour = style.body,
    });
  }

} // namespace corundum::ui
