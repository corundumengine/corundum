// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/hud_strip.hpp>
#include <corundum/ui/font_family.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/quest/status.hpp>
#include <corundum/gameplay/screens/journal.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/resource_ids.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <corundum/world/flags.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace corundum::gameplay::screens {

HudStripData build_hud_strip(const world::FlagStore &flags, const gameplay::quest::Registry &quests,
                             std::string_view zone_id) {
  HudStripData data{};
  data.gold = world::visit_count(flags, k_gold_flag);

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

namespace {

constexpr float k_hud_pad_x = 12.f;
constexpr float k_hud_pad_y = 8.f;
constexpr float k_hud_margin = 16.f;
constexpr float k_hud_min_w = 220.f;
constexpr std::string_view k_hud_separator = "    ";
constexpr std::string_view k_hud_dash_separator = " - ";

/** Everything one HUD strip frame needs, measured once: the run texts and widths, the faces
 *  they draw in, and the panel rect around them.
 */
struct HudStripLayout {
  platform::FontId quest_bold{platform::FontId::Invalid};

  platform::FontId quest_regular{platform::FontId::Invalid};

  platform::FontId ui_regular{platform::FontId::Invalid};

  std::string gold_line{};

  float dash_w{};

  float gold_w{};

  float objective_w{};

  float quest_name_w{};

  float separator_w{};

  /** The objective run is measured and drawn: a quest is tracked and its objective is not empty. */
  bool has_objective{};

  float pad_x{};

  float pad_y{};

  core::math::Vec2 panel_pos{};

  core::math::Vec2 panel_size{};
};

/** Measure every run of the strip and size the panel around them. */
HudStripLayout measure_hud_strip(const platform::Renderer &renderer, const ui::PanelStyle &style,
                                 const ui::NinePatchBorder &border, const HudStripData &data) {
  const ui::FontFamily &ui_fonts = style.family(ui::FontRole::Ui);
  const ui::FontFamily &quest_fonts = style.family(ui::FontRole::Quest);
  const platform::FontId ui_regular = ui_fonts.get(ui::FontStyle::Regular);
  const platform::FontId quest_regular = quest_fonts.get(ui::FontStyle::Regular);
  const platform::FontId quest_bold = quest_fonts.get(ui::FontStyle::Bold);

  const std::string gold_line = std::format("Gold: {}", data.gold);
  const float gold_w = renderer.measure_text(ui_regular, gold_line, style.font_size_body);
  const float separator_w =
      data.has_quest ? renderer.measure_text(ui_regular, k_hud_separator, style.font_size_body) : 0.f;
  const float quest_name_w =
      data.has_quest ? renderer.measure_text(quest_bold, data.quest_name, style.font_size_body) : 0.f;
  const bool has_objective = data.has_quest && !data.objective.empty();
  const float dash_w =
      has_objective ? renderer.measure_text(quest_regular, k_hud_dash_separator, style.font_size_body) : 0.f;
  const float objective_w =
      has_objective ? renderer.measure_text(quest_regular, data.objective, style.font_size_body) : 0.f;

  // Text must clear the nine-patch corners, whose cells are drawn at natural size; the
  // dialogue box insets content the same way (dialog_layout.hpp's `max(margin, tile)`).
  const float pad_x = std::max(k_hud_pad_x, static_cast<float>(border.tile_w));
  const float pad_y = std::max(k_hud_pad_y, static_cast<float>(border.tile_h));

  const float line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
  const float row_h = line_h + (pad_y * 2.f);
  const float text_w = gold_w + separator_w + quest_name_w + dash_w + objective_w;
  const float row_w = std::max(k_hud_min_w, text_w + (pad_x * 2.f));

  return {
      .quest_bold = quest_bold,
      .quest_regular = quest_regular,
      .ui_regular = ui_regular,
      .gold_line = gold_line,
      .dash_w = dash_w,
      .gold_w = gold_w,
      .objective_w = objective_w,
      .quest_name_w = quest_name_w,
      .separator_w = separator_w,
      .has_objective = has_objective,
      .pad_x = pad_x,
      .pad_y = pad_y,
      .panel_pos = {.x = k_hud_margin, .y = k_hud_margin},
      .panel_size = {.x = row_w, .y = row_h},
  };
}

/** Gold is HUD chrome (UI role); the tracked quest and objective are the quest tracker and
 *  follow the Quest family, with the quest name bolded.
 */
void draw_hud_runs(platform::Renderer &renderer, const ui::PanelStyle &style, const HudStripData &data,
                   const HudStripLayout &layout) {
  const float y = layout.panel_pos.y + layout.pad_y;
  const auto draw_run = [&](platform::FontId font_id, std::string_view text, float x) {
    renderer.draw(platform::DrawText{
        .font_id = font_id,
        .text = text,
        .position = {.x = x, .y = y},
        .char_size = style.font_size_body,
        .colour = style.body,
    });
  };

  float x = layout.panel_pos.x + layout.pad_x;
  draw_run(layout.ui_regular, layout.gold_line, x);
  x += layout.gold_w;
  if (!data.has_quest)
    return;
  draw_run(layout.ui_regular, k_hud_separator, x);
  x += layout.separator_w;
  draw_run(layout.quest_bold, data.quest_name, x);
  x += layout.quest_name_w;
  if (!layout.has_objective)
    return;
  draw_run(layout.quest_regular, k_hud_dash_separator, x);
  x += layout.dash_w;
  draw_run(layout.quest_regular, data.objective, x);
}

} // namespace

void hud_strip_render(platform::Renderer &renderer, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                      const HudStripData &data) {
  const HudStripLayout layout = measure_hud_strip(renderer, style, border, data);

  // Background and frame are separate calls: the HUD is persistent, so its fill is fully
  // opaque — the dialogue box's translucent style.bg would tint the world showing through it
  // for the whole session.
  core::math::Colour hud_bg = style.bg;
  hud_bg.a = 255;
  ui::panel_fill(renderer, hud_bg, layout.panel_pos, layout.panel_size);
  ui::panel_frame(renderer, border, layout.panel_pos, layout.panel_size);
  draw_hud_runs(renderer, style, data, layout);
}

} // namespace corundum::gameplay::screens
