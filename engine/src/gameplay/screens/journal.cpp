// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/journal.hpp>
#include <corundum/ui/font_family.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/quest/quest.hpp>
#include <corundum/gameplay/quest/status.hpp>
#include <corundum/gameplay/quest/system.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
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

    constexpr float k_journal_min_w = 260.f;
    constexpr float k_journal_pad_x = 24.f;
    constexpr float k_journal_pad_y = 16.f;
    constexpr float k_journal_gap = 10.f;
    constexpr float k_journal_sub_tab_gap = 18.f;
    constexpr float k_journal_footer_gap = 12.f;
    constexpr std::string_view k_journal_title = "Journal";
    constexpr std::string_view k_journal_empty = "(no quests)";
    constexpr std::string_view k_objective_done = "[x] ";
    constexpr std::string_view k_objective_pending = "[ ] ";

    /// True when @p lifecycle belongs to the section @p tab names.
    bool tab_matches(JournalTab tab, gameplay::quest::Lifecycle lifecycle) noexcept {
      switch (tab) {
        case JournalTab::Active:
          return lifecycle == gameplay::quest::Lifecycle::Active;
        case JournalTab::Completed:
          return lifecycle == gameplay::quest::Lifecycle::Completed;
        case JournalTab::Failed:
          return lifecycle == gameplay::quest::Lifecycle::Failed;
      }
      return false;
    }

    /// Current objective label: first not-yet-done entry, falling back to the first, or empty.
    std::string current_objective(std::span<const gameplay::quest::ObjectiveView> views) {
      if (views.empty())
        return {};
      for (const gameplay::quest::ObjectiveView &view : views) {
        if (!view.done)
          return std::string(view.text);
      }
      return std::string(views.front().text);
    }

    /// One line of the panel's vertical draw sequence, with y relative to the body top.
    struct JournalDrawRow {
      enum class Kind : std::uint8_t { Option, Objective, Checklist };

      Kind kind{Kind::Option};

      std::string_view text{}; ///< Objective/checklist text; options read entries[index].name.

      std::size_t index{}; ///< Entry index for Option rows.

      bool checked{false}; ///< Checklist done mark.

      float y{}; ///< Offset from the body top.
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
      std::string footer{};
      std::array<ui::RowRect, k_journal_tabs.size()> sub_tabs{};
      std::vector<JournalDrawRow> draw_rows{};
    };

    JournalGeometry compute_journal_geometry(const platform::Renderer &r, const ui::PanelStyle &style,
                                             const std::vector<JournalEntry> &entries, const JournalState &state,
                                             core::math::Vec2 viewport, input::InputDevice last_device) {
      JournalGeometry geometry{};
      geometry.body_line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
      geometry.header_line_h = std::max(geometry.body_line_h, static_cast<float>(style.font_size_speaker) + 4.f);
      const float cursor_w = ui::cursor_advance(r, style, ui::FontRole::Quest);
      geometry.objective_indent = cursor_w + 8.f;
      geometry.clamped_cursor = std::clamp(state.cursor, 0, std::max(0, static_cast<int>(entries.size()) - 1));

      const ui::FontFamily &fonts = style.family(ui::FontRole::Quest);
      const std::uint32_t regular = fonts.get(ui::FontStyle::Regular);
      const std::uint32_t bold = fonts.get(ui::FontStyle::Bold);
      const auto measure_objective = [&](std::string_view text) {
        return ui::measure_styled(r, fonts, ui::parse_styled(text), style.font_size_body);
      };
      const float title_w = r.measure_text(bold, k_journal_title, style.font_size_speaker);
      geometry.footer = std::format(
          "{} Track   {} Tabs   {} Close", ui::input_glyph(input::Action::Activate, last_device),
          ui::input_glyph(input::Action::SubTabNext, last_device), ui::input_glyph(input::Action::Cancel, last_device));
      const float footer_w = r.measure_text(regular, geometry.footer, style.font_size_body);

      std::array<float, k_journal_tabs.size()> sub_tab_widths{};
      float sub_tabs_total = 0.f;
      for (std::size_t i = 0; i < k_journal_tabs.size(); ++i) {
        sub_tab_widths[i] = r.measure_text(regular, journal_tab_label(k_journal_tabs[i]), style.font_size_speaker);
        sub_tabs_total += sub_tab_widths[i];
      }
      sub_tabs_total += k_journal_sub_tab_gap * static_cast<float>(k_journal_tabs.size() - 1);

      float widest = std::max({
          title_w,
          r.measure_text(regular, k_journal_empty, style.font_size_body),
          footer_w,
          sub_tabs_total,
      });

      // Build the body rows first: their height sets the panel height (x depends on panel width,
      // so it is applied after the panel is centered). The highlighted entry expands to its full
      // checklist; every other row keeps the single current-objective line.
      float body_height = 0.f;
      for (std::size_t i = 0; i < entries.size(); ++i) {
        const JournalEntry &entry = entries[i];
        widest = std::max(widest, cursor_w + r.measure_text(bold, entry.name, style.font_size_body));
        geometry.draw_rows.push_back(
            JournalDrawRow{.kind = JournalDrawRow::Kind::Option, .index = i, .y = body_height});
        body_height += geometry.body_line_h;

        const bool highlighted = std::cmp_equal(i, static_cast<std::size_t>(geometry.clamped_cursor));
        if (highlighted && !entry.objectives.empty()) {
          for (const JournalObjective &objective : entry.objectives) {
            const std::string_view mark = objective.done ? k_objective_done : k_objective_pending;
            const float objective_w = measure_objective(objective.text);
            widest = std::max(widest, geometry.objective_indent + r.measure_text(regular, mark, style.font_size_body) +
                                          objective_w);
            geometry.draw_rows.push_back(JournalDrawRow{
                .kind = JournalDrawRow::Kind::Checklist,
                .text = objective.text,
                .checked = objective.done,
                .y = body_height,
            });
            body_height += geometry.body_line_h;
          }
        } else if (!entry.objective.empty()) {
          const float objective_w = measure_objective(entry.objective);
          widest = std::max(widest, geometry.objective_indent + objective_w);
          geometry.draw_rows.push_back(
              JournalDrawRow{.kind = JournalDrawRow::Kind::Objective, .text = entry.objective, .y = body_height});
          body_height += geometry.body_line_h;
        }
      }

      geometry.panel_w = std::max(k_journal_min_w, widest + (k_journal_pad_x * 2.f));
      geometry.panel_h = (k_journal_pad_y * 2.f) + geometry.header_line_h + k_journal_gap + geometry.header_line_h +
                         k_journal_gap + body_height + k_journal_footer_gap + geometry.body_line_h;
      geometry.panel_x = (viewport.x - geometry.panel_w) * 0.5f;
      geometry.panel_y = (viewport.y - geometry.panel_h) * 0.5f;
      geometry.title_y = geometry.panel_y + k_journal_pad_y;
      geometry.sub_tab_y = geometry.title_y + geometry.header_line_h + k_journal_gap;
      geometry.body_top = geometry.sub_tab_y + geometry.header_line_h + k_journal_gap;
      geometry.row_x = geometry.panel_x + k_journal_pad_x;

      float x = geometry.panel_x + ((geometry.panel_w - sub_tabs_total) * 0.5f);
      for (std::size_t i = 0; i < k_journal_tabs.size(); ++i) {
        geometry.sub_tabs[i] = ui::RowRect{
            .pos = {.x = x, .y = geometry.sub_tab_y},
            .width = sub_tab_widths[i],
            .height = geometry.header_line_h,
        };
        x += sub_tab_widths[i] + k_journal_sub_tab_gap;
      }
      return geometry;
    }

  } // namespace

  std::string_view journal_tab_label(JournalTab tab) noexcept {
    switch (tab) {
      case JournalTab::Active:
        return "Active";
      case JournalTab::Completed:
        return "Completed";
      case JournalTab::Failed:
        return "Failed";
    }
    return "Unknown";
  }

  JournalTab cycle_journal_tab(JournalTab tab, int direction) noexcept {
    const auto count = static_cast<int>(k_journal_tabs.size());
    const int index = static_cast<int>(tab);
    const int next = (((index + direction) % count) + count) % count;
    return k_journal_tabs[static_cast<std::size_t>(next)];
  }

  std::vector<JournalEntry> build_journal_entries(const gameplay::quest::Registry &quests,
                                                  const world::FlagStore &flags, JournalTab tab,
                                                  std::string_view zone_id) {
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

  JournalLayout journal_panel_layout(const platform::Renderer &r, const ui::PanelStyle &style,
                                     const std::vector<JournalEntry> &entries, const JournalState &state,
                                     core::math::Vec2 viewport, input::InputDevice last_device) {
    const JournalGeometry geometry = compute_journal_geometry(r, style, entries, state, viewport, last_device);
    JournalLayout layout{};
    layout.panel_pos = {.x = geometry.panel_x, .y = geometry.panel_y};
    layout.panel_size = {.x = geometry.panel_w, .y = geometry.panel_h};
    layout.sub_tabs = geometry.sub_tabs;
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

  void journal_panel_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                            const std::vector<JournalEntry> &entries, const JournalState &state,
                            core::math::Vec2 viewport, input::InputDevice last_device) {
    const JournalGeometry geometry = compute_journal_geometry(r, style, entries, state, viewport, last_device);
    const ui::FontFamily &quest_fonts = style.family(ui::FontRole::Quest);
    const ui::FontFamily &ui_fonts = style.family(ui::FontRole::Ui);
    const std::uint32_t quest_regular = quest_fonts.get(ui::FontStyle::Regular);
    const std::uint32_t ui_regular = ui_fonts.get(ui::FontStyle::Regular);
    const std::uint32_t ui_bold = ui_fonts.get(ui::FontStyle::Bold);
    const float title_w = r.measure_text(ui_bold, k_journal_title, style.font_size_speaker);
    const float footer_w = r.measure_text(ui_regular, geometry.footer, style.font_size_body);

    ui::panel_chrome(r, style.bg, border, {.x = geometry.panel_x, .y = geometry.panel_y},
                     {.x = geometry.panel_w, .y = geometry.panel_h});

    r.draw(platform::DrawText{
        .font_id = ui_bold,
        .text = k_journal_title,
        .position = {.x = geometry.panel_x + ((geometry.panel_w - title_w) * 0.5f), .y = geometry.title_y},
        .char_size = style.font_size_speaker,
        .colour = style.speaker,
    });

    // Sub-tab strip: Active / Completed / Failed, the active one highlighted.
    for (std::size_t i = 0; i < k_journal_tabs.size(); ++i) {
      r.draw(platform::DrawText{
          .font_id = ui_regular,
          .text = journal_tab_label(k_journal_tabs[i]),
          .position = geometry.sub_tabs[i].pos,
          .char_size = style.font_size_speaker,
          .colour = k_journal_tabs[i] == state.tab ? style.speaker : style.choice,
      });
    }

    // Footer is bottom-anchored, so it draws before the body — the empty branch can return early.
    r.draw(platform::DrawText{
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

    if (entries.empty()) {
      const float empty_w = r.measure_text(ui_regular, k_journal_empty, style.font_size_body);
      r.draw(platform::DrawText{
          .font_id = ui_regular,
          .text = k_journal_empty,
          .position = {.x = geometry.panel_x + ((geometry.panel_w - empty_w) * 0.5f), .y = geometry.body_top},
          .char_size = style.font_size_body,
          .colour = style.choice,
      });
      return;
    }

    for (const JournalDrawRow &row : geometry.draw_rows) {
      switch (row.kind) {
        case JournalDrawRow::Kind::Option: {
          const JournalEntry &entry = entries[row.index];
          const std::string label = entry.tracked ? std::format("* {}", entry.name) : entry.name;
          ui::draw_option(r, style, ui::FontRole::Quest, ui::FontStyle::Bold, label,
                          {.x = geometry.row_x, .y = geometry.body_top + row.y},
                          std::cmp_equal(row.index, static_cast<std::size_t>(geometry.clamped_cursor)));
          break;
        }
        case JournalDrawRow::Kind::Objective:
          ui::draw_styled(r, quest_fonts, ui::parse_styled(row.text), style.font_size_body, style.choice,
                          {.x = geometry.row_x + geometry.objective_indent, .y = geometry.body_top + row.y});
          break;
        case JournalDrawRow::Kind::Checklist: {
          const std::string_view mark = row.checked ? k_objective_done : k_objective_pending;
          const core::math::Colour colour = row.checked ? style.body : style.choice;
          const float mark_x = geometry.row_x + geometry.objective_indent;
          r.draw(platform::DrawText{
              .font_id = quest_regular,
              .text = mark,
              .position = {.x = mark_x, .y = geometry.body_top + row.y},
              .char_size = style.font_size_body,
              .colour = colour,
          });
          ui::draw_styled(r, quest_fonts, ui::parse_styled(row.text), style.font_size_body, colour,
                          {.x = mark_x + r.measure_text(quest_regular, mark, style.font_size_body),
                           .y = geometry.body_top + row.y});
          break;
        }
      }
    }
  }

} // namespace corundum::gameplay::screens
