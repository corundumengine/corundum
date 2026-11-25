// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/hub_tabs.hpp>
#include <corundum/gameplay/screens/journal.hpp>
#include <corundum/ui/font_family.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/quest/quest.hpp>
#include <corundum/gameplay/quest/status.hpp>
#include <corundum/gameplay/quest/system.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/resource_ids.hpp>
#include <corundum/ui/input_glyph.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/styled_text.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <corundum/world/flags.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace corundum::gameplay::screens {

namespace {

constexpr float k_journal_pad_x = 24.f;
constexpr float k_journal_pad_y = 16.f;
constexpr float k_journal_gap = 10.f;
constexpr float k_journal_sub_tab_gap = 18.f;
constexpr float k_journal_footer_gap = 12.f;
constexpr std::string_view k_journal_title = "Journal";
constexpr std::string_view k_journal_empty = "(no quests)";
constexpr std::string_view k_objective_done = "[x] ";
constexpr std::string_view k_objective_pending = "[ ] ";

/** True when `lifecycle` belongs to the section `tab` names. */
bool tab_matches(JournalTab tab, gameplay::quest::Lifecycle lifecycle) noexcept {
  switch (tab) {
    case JournalTab::Active: return lifecycle == gameplay::quest::Lifecycle::Active;
    case JournalTab::Completed: return lifecycle == gameplay::quest::Lifecycle::Completed;
    case JournalTab::Failed: return lifecycle == gameplay::quest::Lifecycle::Failed;
  }
  return false;
}

/** Current objective label: first not-yet-done entry, falling back to the first, or empty. */
std::string current_objective(std::span<const gameplay::quest::ObjectiveView> views) {
  if (views.empty())
    return {};
  for (const gameplay::quest::ObjectiveView &view : views) {
    if (!view.done)
      return std::string(view.text);
  }
  return std::string(views.front().text);
}

/** One line of the panel's vertical draw sequence, with y relative to the body top. */
struct JournalDrawRow {
  enum class Kind : std::uint8_t { Option, Objective, Checklist };

  Kind kind{Kind::Option};

  /** Objective/checklist text; options read entries[index].name. */
  std::string_view text{};

  /** Entry index for Option rows. */
  std::size_t index{};

  /** Checklist done mark. */
  bool checked{false};

  /** Offset from the body top. */
  float y{};
};

struct JournalGeometry {
  float body_line_h{};
  float header_line_h{};
  float panel_x{};
  float panel_y{};
  float panel_w{};
  float panel_h{};
  float title_y{};
  float sub_tab_y{};
  float body_top{};
  float row_x{};
  float objective_indent{};
  int clamped_cursor{};
  int first_row{};
  int visible_rows{};
  std::string footer{};
  std::array<ui::RowRect, k_journal_tabs.size()> sub_tabs{};
  std::vector<JournalDrawRow> draw_rows{};
};

/** The visible slice of the journal list. */
struct JournalWindow {
  int first_row{};

  int visible_rows{};
};

/** Window the journal list to `available` pixels, keeping `cursor` visible. Each entry is an
 *  option line plus either its single objective line or, when highlighted, its full checklist,
 *  so the window is sized by the summed row heights rather than a fixed slot count.
 */
JournalWindow compute_journal_window(int scroll, int cursor, const std::vector<JournalEntry> &entries, float available,
                                     float body_line_h) {
  const int entry_count = static_cast<int>(entries.size());
  const auto entry_height = [&](int i) {
    const JournalEntry &entry = entries[static_cast<std::size_t>(i)];
    float height = body_line_h;
    if (i == cursor && !entry.objectives.empty())
      height += static_cast<float>(entry.objectives.size()) * body_line_h;
    else if (!entry.objective.empty())
      height += body_line_h;
    return height;
  };
  const auto window_end = [&](int start) {
    float used = 0.f;
    int i = start;
    while (i < entry_count) {
      const float height = entry_height(i);
      if (i > start && used + height > available)
        break;
      used += height;
      ++i;
    }
    return i;
  };

  const int estimate = std::max(1, static_cast<int>(available / body_line_h));
  int first = ui::clamp_scroll_to_cursor(scroll, cursor, entry_count, estimate);
  if (entry_count > 0 && cursor >= window_end(first))
    first = cursor;
  return JournalWindow{.first_row = first, .visible_rows = window_end(first) - first};
}

/** Line heights, the panel rect, and the title/sub-tab/body baselines inside it. */
void place_journal_panel(const platform::Renderer &renderer, const ui::PanelStyle &style, core::math::Vec2 viewport,
                         JournalGeometry &geometry) {
  geometry.body_line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
  geometry.header_line_h = std::max(geometry.body_line_h, static_cast<float>(style.font_size_speaker) + 4.f);
  geometry.objective_indent = ui::cursor_advance(renderer, style, ui::FontRole::Quest) + 8.f;

  const ui::PanelRect panel = ui::screen_panel_rect(viewport, style, hub_panel_top_inset(style));
  geometry.panel_x = panel.pos.x;
  geometry.panel_y = panel.pos.y;
  geometry.panel_w = panel.size.x;
  geometry.panel_h = panel.size.y;
  geometry.title_y = geometry.panel_y + k_journal_pad_y;
  geometry.sub_tab_y = geometry.title_y + geometry.header_line_h + k_journal_gap;
  geometry.body_top = geometry.sub_tab_y + geometry.header_line_h + k_journal_gap;
  geometry.row_x = geometry.panel_x + k_journal_pad_x;
}

/** The footer hint line naming the track, tab and close bindings for the last-used device. */
std::string journal_footer(input::InputDevice last_device) {
  return std::format("{} Track   {} Tabs   {} Close", ui::input_glyph(input::Action::Activate, last_device),
                     ui::input_glyph(input::Action::SubTabNext, last_device),
                     ui::input_glyph(input::Action::Cancel, last_device));
}

/** The sub-tab row rects, centered under the title. */
void place_journal_sub_tabs(const platform::Renderer &renderer, const ui::PanelStyle &style,
                            JournalGeometry &geometry) {
  const platform::FontId regular = style.family(ui::FontRole::Quest).get(ui::FontStyle::Regular);
  std::array<float, k_journal_tabs.size()> sub_tab_widths{};
  float sub_tabs_total = 0.f;
  for (std::size_t i = 0; i < k_journal_tabs.size(); ++i) {
    sub_tab_widths[i] = renderer.measure_text(regular, journal_tab_label(k_journal_tabs[i]), style.font_size_speaker);
    sub_tabs_total += sub_tab_widths[i];
  }
  sub_tabs_total += k_journal_sub_tab_gap * static_cast<float>(k_journal_tabs.size() - 1);
  float x = geometry.panel_x + ((geometry.panel_w - sub_tabs_total) * 0.5f);
  for (std::size_t i = 0; i < k_journal_tabs.size(); ++i) {
    geometry.sub_tabs[i] = ui::RowRect{
        .pos = {.x = x, .y = geometry.sub_tab_y},
        .width = sub_tab_widths[i],
        .height = geometry.header_line_h,
    };
    x += sub_tab_widths[i] + k_journal_sub_tab_gap;
  }
}

/** Window the list to the content area above the footer, leaving room for the footer line, then
 *  lay out each visible entry's option line and its objective or checklist lines.
 */
void collect_journal_draw_rows(const std::vector<JournalEntry> &entries, const JournalState &state,
                               JournalGeometry &geometry) {
  const int entry_count = static_cast<int>(entries.size());
  const float footer_y = geometry.panel_y + geometry.panel_h - k_journal_pad_y - geometry.body_line_h;
  const float content_bottom = footer_y - k_journal_footer_gap;
  const float available = std::max(0.f, content_bottom - geometry.body_top);
  const JournalWindow window =
      compute_journal_window(state.scroll, geometry.clamped_cursor, entries, available, geometry.body_line_h);
  geometry.first_row = window.first_row;
  geometry.visible_rows = window.visible_rows;

  const int last_row = std::min(entry_count, geometry.first_row + geometry.visible_rows);
  float y = 0.f;
  for (int i = geometry.first_row; i < last_row; ++i) {
    const JournalEntry &entry = entries[static_cast<std::size_t>(i)];
    geometry.draw_rows.push_back(
        JournalDrawRow{.kind = JournalDrawRow::Kind::Option, .index = static_cast<std::size_t>(i), .y = y});
    y += geometry.body_line_h;

    const bool highlighted = i == geometry.clamped_cursor;
    if (highlighted && !entry.objectives.empty()) {
      for (const JournalObjective &objective : entry.objectives) {
        geometry.draw_rows.push_back(JournalDrawRow{
            .kind = JournalDrawRow::Kind::Checklist,
            .text = objective.text,
            .checked = objective.done,
            .y = y,
        });
        y += geometry.body_line_h;
      }
    } else if (!entry.objective.empty()) {
      geometry.draw_rows.push_back(
          JournalDrawRow{.kind = JournalDrawRow::Kind::Objective, .text = entry.objective, .y = y});
      y += geometry.body_line_h;
    }
  }
}

JournalGeometry compute_journal_geometry(const platform::Renderer &renderer, const ui::PanelStyle &style,
                                         const std::vector<JournalEntry> &entries, const JournalState &state,
                                         core::math::Vec2 viewport, input::InputDevice last_device) {
  JournalGeometry geometry{};
  place_journal_panel(renderer, style, viewport, geometry);
  geometry.clamped_cursor = std::clamp(state.cursor, 0, std::max(0, static_cast<int>(entries.size()) - 1));
  geometry.footer = journal_footer(last_device);
  place_journal_sub_tabs(renderer, style, geometry);
  collect_journal_draw_rows(entries, state, geometry);
  return geometry;
}

/** Backdrop, frame, title, sub-tab strip and footer: everything drawn around the list. */
void draw_journal_chrome(platform::Renderer &renderer, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                         const JournalGeometry &geometry, core::math::Vec2 viewport, JournalTab active_tab) {
  const ui::FontFamily &ui_fonts = style.family(ui::FontRole::Ui);
  const platform::FontId ui_bold = ui_fonts.get(ui::FontStyle::Bold);
  const platform::FontId ui_regular = ui_fonts.get(ui::FontStyle::Regular);

  ui::screen_backdrop(renderer, style, viewport);
  ui::panel_chrome(renderer, style.bg, border, {.x = geometry.panel_x, .y = geometry.panel_y},
                   {.x = geometry.panel_w, .y = geometry.panel_h});

  const float title_w = renderer.measure_text(ui_bold, k_journal_title, style.font_size_speaker);
  renderer.draw(platform::DrawText{
      .font_id = ui_bold,
      .text = k_journal_title,
      .position = {.x = geometry.panel_x + ((geometry.panel_w - title_w) * 0.5f), .y = geometry.title_y},
      .char_size = style.font_size_speaker,
      .colour = style.speaker,
  });

  // The open tab draws in the speaker colour, the rest in the choice colour.
  for (std::size_t i = 0; i < k_journal_tabs.size(); ++i) {
    renderer.draw(platform::DrawText{
        .font_id = ui_regular,
        .text = journal_tab_label(k_journal_tabs[i]),
        .position = geometry.sub_tabs[i].pos,
        .char_size = style.font_size_speaker,
        .colour = k_journal_tabs[i] == active_tab ? style.speaker : style.choice,
    });
  }

  // The footer is bottom-anchored, so it draws before the body.
  const float footer_w = renderer.measure_text(ui_regular, geometry.footer, style.font_size_body);
  renderer.draw(platform::DrawText{
      .font_id = ui_regular,
      .text = geometry.footer,
      .position =
          {
              .x = geometry.panel_x + ((geometry.panel_w - footer_w) * 0.5f),
              .y = geometry.panel_y + geometry.panel_h - k_journal_pad_y - geometry.body_line_h,
          },
      .char_size = style.font_size_body,
      .colour = style.choice,
  });
}

/** The centered "no quests" line an empty list shows instead of rows. */
void draw_journal_empty(platform::Renderer &renderer, const ui::PanelStyle &style, const JournalGeometry &geometry) {
  const platform::FontId ui_regular = style.family(ui::FontRole::Ui).get(ui::FontStyle::Regular);
  const float empty_w = renderer.measure_text(ui_regular, k_journal_empty, style.font_size_body);
  renderer.draw(platform::DrawText{
      .font_id = ui_regular,
      .text = k_journal_empty,
      .position = {.x = geometry.panel_x + ((geometry.panel_w - empty_w) * 0.5f), .y = geometry.body_top},
      .char_size = style.font_size_body,
      .colour = style.choice,
  });
}

/** One list row: an option line, an objective line, or a checklist mark plus its text. */
void draw_journal_row(platform::Renderer &renderer, const ui::PanelStyle &style, const JournalGeometry &geometry,
                      const JournalDrawRow &row, const std::vector<JournalEntry> &entries) {
  const ui::FontFamily &quest_fonts = style.family(ui::FontRole::Quest);
  const float row_y = geometry.body_top + row.y;
  switch (row.kind) {
    case JournalDrawRow::Kind::Option: {
      const JournalEntry &entry = entries[row.index];
      const std::string label = entry.tracked ? std::format("* {}", entry.name) : entry.name;
      ui::draw_option(renderer, style, ui::FontRole::Quest, ui::FontStyle::Bold, label,
                      {.x = geometry.row_x, .y = row_y},
                      std::cmp_equal(row.index, static_cast<std::size_t>(geometry.clamped_cursor)));
      return;
    }
    case JournalDrawRow::Kind::Objective:
      ui::draw_styled(renderer, quest_fonts, ui::parse_styled(row.text), style.font_size_body, style.choice,
                      {.x = geometry.row_x + geometry.objective_indent, .y = row_y});
      return;
    case JournalDrawRow::Kind::Checklist: {
      const platform::FontId quest_regular = quest_fonts.get(ui::FontStyle::Regular);
      const std::string_view mark = row.checked ? k_objective_done : k_objective_pending;
      const core::math::Colour colour = row.checked ? style.body : style.choice;
      const float mark_x = geometry.row_x + geometry.objective_indent;
      renderer.draw(platform::DrawText{
          .font_id = quest_regular,
          .text = mark,
          .position = {.x = mark_x, .y = row_y},
          .char_size = style.font_size_body,
          .colour = colour,
      });
      ui::draw_styled(renderer, quest_fonts, ui::parse_styled(row.text), style.font_size_body, colour,
                      {
                          .x = mark_x + renderer.measure_text(quest_regular, mark, style.font_size_body),
                          .y = row_y,
                      });
      return;
    }
  }
}

} // namespace

std::string_view journal_tab_label(JournalTab tab) noexcept {
  switch (tab) {
    case JournalTab::Active: return "Active";
    case JournalTab::Completed: return "Completed";
    case JournalTab::Failed: return "Failed";
  }
  return "Unknown";
}

JournalTab cycle_journal_tab(JournalTab tab, int direction) noexcept {
  const auto count = static_cast<int>(k_journal_tabs.size());
  const int index = static_cast<int>(tab);
  const int next = (((index + direction) % count) + count) % count;
  return k_journal_tabs[static_cast<std::size_t>(next)];
}

std::vector<JournalEntry> build_journal_entries(const gameplay::quest::Registry &quests, const world::FlagStore &flags,
                                                JournalTab tab, std::string_view zone_id) {
  std::vector<JournalEntry> entries;
  for (const gameplay::quest::Quest *quest : gameplay::quest::started_quests(quests, flags)) {
    if (quest == nullptr)
      continue;
    const gameplay::quest::Lifecycle lifecycle = gameplay::quest::lifecycle(*quest, flags);
    if (!tab_matches(tab, lifecycle))
      continue;

    const std::vector<gameplay::quest::ObjectiveView> views =
        gameplay::quest::objectives(*quest, flags, &quests, zone_id);

    JournalEntry entry{};
    entry.id = quest->quest_id;
    entry.name = quest->name;
    entry.objective = current_objective(views);
    entry.lifecycle = lifecycle;
    entry.tracked = world::has_flag(flags, gameplay::quest::tracked_flag_key(quest->quest_id));
    entry.objectives.reserve(views.size());
    for (const gameplay::quest::ObjectiveView &view : views)
      entry.objectives.push_back(JournalObjective{.text = std::string(view.text), .done = view.done});
    entries.push_back(std::move(entry));
  }
  std::ranges::sort(entries, {}, &JournalEntry::name);
  return entries;
}

JournalLayout journal_panel_layout(const platform::Renderer &renderer, const ui::PanelStyle &style,
                                   const std::vector<JournalEntry> &entries, const JournalState &state,
                                   core::math::Vec2 viewport, input::InputDevice last_device) {
  const JournalGeometry geometry = compute_journal_geometry(renderer, style, entries, state, viewport, last_device);
  JournalLayout layout{};
  layout.panel_pos = {.x = geometry.panel_x, .y = geometry.panel_y};
  layout.panel_size = {.x = geometry.panel_w, .y = geometry.panel_h};
  layout.sub_tabs = geometry.sub_tabs;
  layout.first_row = geometry.first_row;
  layout.visible_rows = geometry.visible_rows;
  for (const JournalDrawRow &row : geometry.draw_rows) {
    if (row.kind != JournalDrawRow::Kind::Option)
      continue;
    layout.rows.push_back(ui::RowRect{
        .pos = {.x = geometry.row_x, .y = geometry.body_top + row.y},
        .width = geometry.panel_w - (k_journal_pad_x * 2.f),
        .height = geometry.body_line_h,
    });
  }
  return layout;
}

void journal_panel_render(platform::Renderer &renderer, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                          const std::vector<JournalEntry> &entries, const JournalState &state,
                          core::math::Vec2 viewport, input::InputDevice last_device) {
  const JournalGeometry geometry = compute_journal_geometry(renderer, style, entries, state, viewport, last_device);
  draw_journal_chrome(renderer, style, border, geometry, viewport, state.tab);
  if (entries.empty()) {
    draw_journal_empty(renderer, style, geometry);
    return;
  }
  for (const JournalDrawRow &row : geometry.draw_rows)
    draw_journal_row(renderer, style, geometry, row, entries);
}

} // namespace corundum::gameplay::screens
