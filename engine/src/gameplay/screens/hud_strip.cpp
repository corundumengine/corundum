// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/hud_strip.hpp>
#include <corundum/ui/font_family.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/quest/status.hpp>
#include <corundum/gameplay/screens/journal.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <corundum/world/flags.hpp>

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::gameplay::screens {

  HudStripData build_hud_strip(const world::FlagStore &flags, const gameplay::quest::Registry &quests,
                               std::string_view zone_id) {
    HudStripData data{};
    data.gold = world::visit_count(flags, std::string{k_gold_flag});

    const std::vector<JournalEntry> active = build_journal_entries(quests, flags, JournalTab::Active, zone_id);
    if (!active.empty()) {
      const auto tracked = std::ranges::find_if(active, &JournalEntry::tracked);
      const JournalEntry &chosen = tracked != active.end() ? *tracked : active.front();
      data.has_quest = true;
      data.quest_name = chosen.name;
      data.objective = chosen.objective;
    }
    return data;
  }

  void hud_strip_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                        const HudStripData &data) {
    constexpr float k_pad_x = 12.f;
    constexpr float k_pad_y = 8.f;
    constexpr float k_margin = 16.f;
    constexpr float k_min_w = 220.f;
    constexpr std::string_view k_separator = "    ";
    constexpr std::string_view k_dash_separator = " - ";

    const ui::FontFamily &ui_fonts = style.family(ui::FontRole::Ui);
    const ui::FontFamily &quest_fonts = style.family(ui::FontRole::Quest);
    const std::uint32_t ui_regular = ui_fonts.get(ui::FontStyle::Regular);
    const std::uint32_t quest_regular = quest_fonts.get(ui::FontStyle::Regular);
    const std::uint32_t quest_bold = quest_fonts.get(ui::FontStyle::Bold);

    const std::string gold_line = std::format("Gold: {}", data.gold);
    const float gold_w = r.measure_text(ui_regular, gold_line, style.font_size_body);
    const float separator_w = data.has_quest ? r.measure_text(ui_regular, k_separator, style.font_size_body) : 0.f;
    const float quest_name_w = data.has_quest ? r.measure_text(quest_bold, data.quest_name, style.font_size_body) : 0.f;
    const bool has_objective = data.has_quest && !data.objective.empty();
    const float dash_w = has_objective ? r.measure_text(quest_regular, k_dash_separator, style.font_size_body) : 0.f;
    const float objective_w = has_objective ? r.measure_text(quest_regular, data.objective, style.font_size_body) : 0.f;

    // Text must clear the nine-patch corners, whose cells are drawn at natural size; the
    // dialogue box insets content the same way (dialog_layout.hpp's `max(margin, tile)`).
    const float pad_x = std::max(k_pad_x, static_cast<float>(border.tile_w));
    const float pad_y = std::max(k_pad_y, static_cast<float>(border.tile_h));

    const float line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
    const float row_h = line_h + (pad_y * 2.f);
    const float text_w = gold_w + separator_w + quest_name_w + dash_w + objective_w;
    const float row_w = std::max(k_min_w, text_w + (pad_x * 2.f));

    // Background and frame are separate calls: the HUD is persistent, so its fill is fully
    // opaque — the dialogue box's translucent style.bg would tint the world showing through it
    // for the whole session.
    core::math::Colour hud_bg = style.bg;
    hud_bg.a = 255;
    const core::math::Vec2 panel_pos{.x = k_margin, .y = k_margin};
    const core::math::Vec2 panel_size{.x = row_w, .y = row_h};
    ui::panel_fill(r, hud_bg, panel_pos, panel_size);
    ui::panel_frame(r, border, panel_pos, panel_size);

    // Gold is HUD chrome (UI role); the tracked quest and objective are the quest tracker and
    // follow the Quest family, with the quest name bolded.
    const float y = panel_pos.y + pad_y;
    const auto draw_run = [&](std::uint32_t font_id, std::string_view text, float x) {
      r.draw(platform::DrawText{
          .font_id = font_id,
          .text = text,
          .position = {.x = x, .y = y},
          .char_size = style.font_size_body,
          .colour = style.body,
      });
    };

    float x = panel_pos.x + pad_x;
    draw_run(ui_regular, gold_line, x);
    x += gold_w;
    if (data.has_quest) {
      draw_run(ui_regular, k_separator, x);
      x += separator_w;
      draw_run(quest_bold, data.quest_name, x);
      x += quest_name_w;
      if (has_objective) {
        draw_run(quest_regular, k_dash_separator, x);
        x += dash_w;
        draw_run(quest_regular, data.objective, x);
      }
    }
  }

} // namespace corundum::gameplay::screens
