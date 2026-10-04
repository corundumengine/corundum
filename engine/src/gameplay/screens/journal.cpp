// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/journal.hpp>

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
#include <corundum/ui/ui_draw.hpp>
#include <corundum/world/flags.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace corundum::gameplay::screens {

  namespace {

    /// Section display order: Active, then Completed, then Failed. NotStarted is unreachable
    /// here because build_journal_entries() only sees started quests.
    int lifecycle_rank(gameplay::quest::Lifecycle lifecycle) noexcept {
      switch (lifecycle) {
        case gameplay::quest::Lifecycle::Active:
          return 0;
        case gameplay::quest::Lifecycle::Completed:
          return 1;
        case gameplay::quest::Lifecycle::Failed:
          return 2;
        case gameplay::quest::Lifecycle::NotStarted:
          return 3;
      }
      return 3;
    }

    std::string_view lifecycle_header(gameplay::quest::Lifecycle lifecycle) noexcept {
      switch (lifecycle) {
        case gameplay::quest::Lifecycle::Active:
          return "Active";
        case gameplay::quest::Lifecycle::Completed:
          return "Completed";
        case gameplay::quest::Lifecycle::Failed:
          return "Failed";
        case gameplay::quest::Lifecycle::NotStarted:
          return "Unknown";
      }
      return "Unknown";
    }

    /// First not-yet-done objective of the current stage, falling back to the first objective,
    /// or empty when the stage declares none.
    std::string current_objective(const gameplay::quest::Quest &quest, const world::FlagStore &flags,
                                  const gameplay::quest::Registry &quests, std::string_view zone_id) {
      const std::vector<gameplay::quest::ObjectiveView> views =
          gameplay::quest::objectives(quest, flags, &quests, zone_id);
      if (views.empty())
        return {};
      for (const gameplay::quest::ObjectiveView &view : views) {
        if (!view.done)
          return std::string(view.text);
      }
      return std::string(views.front().text);
    }

    constexpr float k_journal_min_w = 260.f;
    constexpr float k_journal_pad_x = 24.f;
    constexpr float k_journal_pad_y = 16.f;
    constexpr float k_journal_header_gap = 10.f;
    constexpr float k_journal_group_gap = 8.f;
    constexpr float k_journal_footer_gap = 12.f;
    constexpr std::string_view k_journal_title = "Journal";
    constexpr std::string_view k_journal_empty = "(no quests)";

    /// One line of the panel's vertical draw sequence.
    struct JournalDrawRow {
      enum class Kind : std::uint8_t { Header, Option, Objective };

      Kind kind{Kind::Option};

      std::string_view text{}; ///< Header/objective text; options read entries[index].name.

      std::size_t index{};

      float x{};

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
      float body_top{};
      float row_x{};
      float objective_indent{};
      int clamped_cursor{};
      std::string footer{};
      std::vector<JournalDrawRow> draw_rows;
    };

    JournalGeometry compute_journal_geometry(const platform::Renderer &r, const ui::PanelStyle &style,
                                             const std::vector<JournalEntry> &entries, int cursor,
                                             core::math::Vec2 viewport, input::InputDevice last_device) {
      JournalGeometry geometry{};
      geometry.body_line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
      geometry.header_line_h = std::max(geometry.body_line_h, static_cast<float>(style.font_size_speaker) + 4.f);
      const float cursor_w = ui::cursor_advance(r, style);
      geometry.objective_indent = cursor_w + 8.f;
      const float title_w = r.measure_text(style.font_id, k_journal_title, style.font_size_speaker);
      geometry.footer = std::format("{} Close", ui::input_glyph(input::Action::Cancel, last_device));
      const float footer_w = r.measure_text(style.font_id, geometry.footer, style.font_size_body);

      std::size_t group_count = 0;
      std::size_t objective_rows = 0;
      float widest =
          std::max({title_w, r.measure_text(style.font_id, k_journal_empty, style.font_size_body), footer_w});
      gameplay::quest::Lifecycle last_section = gameplay::quest::Lifecycle::NotStarted;
      for (std::size_t i = 0; i < entries.size(); ++i) {
        const JournalEntry &entry = entries[i];
        if (i == 0 || entry.lifecycle != last_section) {
          ++group_count;
          last_section = entry.lifecycle;
        }
        widest = std::max(widest, cursor_w + r.measure_text(style.font_id, entry.name, style.font_size_body));
        if (!entry.objective.empty()) {
          ++objective_rows;
          widest = std::max(widest, geometry.objective_indent +
                                        r.measure_text(style.font_id, entry.objective, style.font_size_body));
        }
      }

      geometry.panel_w = std::max(k_journal_min_w, widest + (k_journal_pad_x * 2.f));
      const float body_rows = static_cast<float>(entries.size() + objective_rows);
      const float group_gaps = group_count > 0 ? static_cast<float>(group_count - 1) * k_journal_group_gap : 0.f;
      geometry.panel_h = (k_journal_pad_y * 2.f) + geometry.header_line_h + k_journal_header_gap +
                         (body_rows * geometry.body_line_h) +
                         (static_cast<float>(group_count) * geometry.header_line_h) + group_gaps +
                         k_journal_footer_gap + geometry.body_line_h;
      geometry.panel_x = (viewport.x - geometry.panel_w) * 0.5f;
      geometry.panel_y = (viewport.y - geometry.panel_h) * 0.5f;
      geometry.title_y = geometry.panel_y + k_journal_pad_y;
      geometry.body_top = geometry.title_y + geometry.header_line_h + k_journal_header_gap;
      geometry.row_x = geometry.panel_x + k_journal_pad_x;
      geometry.clamped_cursor = std::clamp(cursor, 0, std::max(0, static_cast<int>(entries.size()) - 1));

      float y = geometry.body_top;
      for (std::size_t i = 0; i < entries.size();) {
        const gameplay::quest::Lifecycle section = entries[i].lifecycle;
        geometry.draw_rows.push_back(JournalDrawRow{
            .kind = JournalDrawRow::Kind::Header,
            .text = lifecycle_header(section),
            .y = y,
        });
        y += geometry.header_line_h;

        while (i < entries.size() && entries[i].lifecycle == section) {
          geometry.draw_rows.push_back(JournalDrawRow{
              .kind = JournalDrawRow::Kind::Option,
              .index = i,
              .x = geometry.row_x,
              .y = y,
          });
          y += geometry.body_line_h;
          if (!entries[i].objective.empty()) {
            geometry.draw_rows.push_back(JournalDrawRow{
                .kind = JournalDrawRow::Kind::Objective,
                .text = entries[i].objective,
                .x = geometry.row_x + geometry.objective_indent,
                .y = y,
            });
            y += geometry.body_line_h;
          }
          ++i;
        }

        if (i < entries.size())
          y += k_journal_group_gap;
      }
      return geometry;
    }

  } // namespace

  std::vector<JournalEntry> build_journal_entries(const gameplay::quest::Registry &quests,
                                                  const world::FlagStore &flags, std::string_view zone_id) {
    std::vector<JournalEntry> entries;
    for (const gameplay::quest::Quest *quest : gameplay::quest::started_quests(quests, flags)) {
      if (quest == nullptr)
        continue;
      entries.push_back(JournalEntry{
          .name = quest->name,
          .objective = current_objective(*quest, flags, quests, zone_id),
          .lifecycle = gameplay::quest::lifecycle(*quest, flags),
      });
    }
    std::ranges::sort(
        entries, {}, [](const JournalEntry &entry) { return std::tuple{lifecycle_rank(entry.lifecycle), entry.name}; });
    return entries;
  }

  JournalLayout journal_panel_layout(const platform::Renderer &r, const ui::PanelStyle &style,
                                     const std::vector<JournalEntry> &entries, int cursor, core::math::Vec2 viewport,
                                     input::InputDevice last_device) {
    const JournalGeometry geometry = compute_journal_geometry(r, style, entries, cursor, viewport, last_device);
    JournalLayout layout{};
    layout.panel_pos = {.x = geometry.panel_x, .y = geometry.panel_y};
    layout.panel_size = {.x = geometry.panel_w, .y = geometry.panel_h};
    for (const JournalDrawRow &row : geometry.draw_rows) {
      if (row.kind != JournalDrawRow::Kind::Option)
        continue;
      layout.rows.push_back(ui::RowRect{
          .pos = {.x = geometry.row_x, .y = row.y},
          .width = geometry.panel_w - (k_journal_pad_x * 2.f),
          .height = geometry.body_line_h,
      });
    }
    return layout;
  }

  void journal_panel_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                            const std::vector<JournalEntry> &entries, int cursor, core::math::Vec2 viewport,
                            input::InputDevice last_device) {
    const JournalGeometry geometry = compute_journal_geometry(r, style, entries, cursor, viewport, last_device);
    const float title_w = r.measure_text(style.font_id, k_journal_title, style.font_size_speaker);
    const float footer_w = r.measure_text(style.font_id, geometry.footer, style.font_size_body);

    ui::panel_chrome(r, style.bg, border, {.x = geometry.panel_x, .y = geometry.panel_y},
                     {.x = geometry.panel_w, .y = geometry.panel_h});

    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = k_journal_title,
        .position = {.x = geometry.panel_x + ((geometry.panel_w - title_w) * 0.5f), .y = geometry.title_y},
        .char_size = style.font_size_speaker,
        .colour = style.speaker,
    });

    // Footer is bottom-anchored, so it draws before the body — the empty branch can return early.
    r.draw(platform::DrawText{
        .font_id = style.font_id,
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
      const float empty_w = r.measure_text(style.font_id, k_journal_empty, style.font_size_body);
      r.draw(platform::DrawText{
          .font_id = style.font_id,
          .text = k_journal_empty,
          .position =
              {
                  .x = geometry.panel_x + ((geometry.panel_w - empty_w) * 0.5f),
                  .y = geometry.body_top,
              },
          .char_size = style.font_size_body,
          .colour = style.choice,
      });
      return;
    }

    for (const JournalDrawRow &row : geometry.draw_rows) {
      switch (row.kind) {
        case JournalDrawRow::Kind::Header: {
          const float header_w = r.measure_text(style.font_id, row.text, style.font_size_speaker);
          r.draw(platform::DrawText{
              .font_id = style.font_id,
              .text = row.text,
              .position = {.x = geometry.panel_x + ((geometry.panel_w - header_w) * 0.5f), .y = row.y},
              .char_size = style.font_size_speaker,
              .colour = style.speaker,
          });
          break;
        }
        case JournalDrawRow::Kind::Option:
          ui::draw_option(r, style, entries[row.index].name, {.x = row.x, .y = row.y},
                          std::cmp_equal(row.index, static_cast<std::size_t>(geometry.clamped_cursor)));
          break;
        case JournalDrawRow::Kind::Objective:
          r.draw(platform::DrawText{
              .font_id = style.font_id,
              .text = row.text,
              .position = {.x = row.x, .y = row.y},
              .char_size = style.font_size_body,
              .colour = style.choice,
          });
          break;
      }
    }
  }

} // namespace corundum::gameplay::screens
