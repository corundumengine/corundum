// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/ui/journal.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/quest/quest.hpp>
#include <corundum/quest/status.hpp>
#include <corundum/quest/system.hpp>
#include <corundum/ui/dialog_box.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <corundum/world/flags.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace corundum::ui {

  namespace {

    /// Section display order: Active, then Completed, then Failed. NotStarted is unreachable
    /// here because build_journal_entries() only sees started quests.
    int lifecycle_rank(quest::Lifecycle lifecycle) noexcept {
      switch (lifecycle) {
        case quest::Lifecycle::Active:
          return 0;
        case quest::Lifecycle::Completed:
          return 1;
        case quest::Lifecycle::Failed:
          return 2;
        case quest::Lifecycle::NotStarted:
          return 3;
      }
      return 3;
    }

    std::string_view lifecycle_header(quest::Lifecycle lifecycle) noexcept {
      switch (lifecycle) {
        case quest::Lifecycle::Active:
          return "Active";
        case quest::Lifecycle::Completed:
          return "Completed";
        case quest::Lifecycle::Failed:
          return "Failed";
        case quest::Lifecycle::NotStarted:
          return "Unknown";
      }
      return "Unknown";
    }

    /// First not-yet-done objective of the current stage, falling back to the first objective,
    /// or empty when the stage declares none.
    std::string current_objective(const quest::Quest &quest, const world::FlagStore &flags,
                                  const quest::Registry &quests, std::string_view zone_id) {
      const std::vector<quest::ObjectiveView> views = quest::objectives(quest, flags, &quests, zone_id);
      if (views.empty())
        return {};
      for (const quest::ObjectiveView &view : views) {
        if (!view.done)
          return std::string(view.text);
      }
      return std::string(views.front().text);
    }

  } // namespace

  std::vector<JournalEntry> build_journal_entries(const quest::Registry &quests, const world::FlagStore &flags,
                                                  std::string_view zone_id) {
    std::vector<JournalEntry> entries;
    for (const quest::Quest *quest : quest::started_quests(quests, flags)) {
      if (quest == nullptr)
        continue;
      entries.push_back(JournalEntry{
          .name = quest->name,
          .objective = current_objective(*quest, flags, quests, zone_id),
          .lifecycle = quest::lifecycle(*quest, flags),
      });
    }
    std::ranges::sort(
        entries, {}, [](const JournalEntry &entry) { return std::tuple{lifecycle_rank(entry.lifecycle), entry.name}; });
    return entries;
  }

  void journal_panel_render(platform::Renderer &r, const DialogBoxStyle &style, const NinePatchBorder &border,
                            const std::vector<JournalEntry> &entries, int cursor, core::math::Vec2 viewport) {
    constexpr float k_min_w = 260.f;
    constexpr float k_pad_x = 24.f;
    constexpr float k_pad_y = 16.f;
    constexpr float k_header_gap = 10.f;
    constexpr float k_group_gap = 8.f;
    constexpr std::string_view k_title = "Journal";
    constexpr std::string_view k_empty = "(no quests)";

    const float body_line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
    const float header_line_h = std::max(body_line_h, static_cast<float>(style.font_size_speaker) + 4.f);
    const float cursor_w = cursor_advance(r, style);
    const float objective_indent = cursor_w + 8.f;
    const float title_w = r.measure_text(style.font_id, k_title, style.font_size_speaker);

    // Walk the (already lifecycle-sorted) entries once to size the panel: group count, total
    // height, and the widest body line.
    std::size_t group_count = 0;
    std::size_t objective_rows = 0;
    float widest = std::max(title_w, r.measure_text(style.font_id, k_empty, style.font_size_body));
    quest::Lifecycle last_section = quest::Lifecycle::NotStarted;
    for (std::size_t i = 0; i < entries.size(); ++i) {
      const JournalEntry &entry = entries[i];
      if (i == 0 || entry.lifecycle != last_section) {
        ++group_count;
        last_section = entry.lifecycle;
      }
      widest = std::max(widest, cursor_w + r.measure_text(style.font_id, entry.name, style.font_size_body));
      if (!entry.objective.empty()) {
        ++objective_rows;
        widest =
            std::max(widest, objective_indent + r.measure_text(style.font_id, entry.objective, style.font_size_body));
      }
    }

    const float panel_w = std::max(k_min_w, widest + (k_pad_x * 2.f));
    const float body_rows = static_cast<float>(entries.size() + objective_rows);
    const float group_gaps = group_count > 0 ? static_cast<float>(group_count - 1) * k_group_gap : 0.f;
    const float panel_h = (k_pad_y * 2.f) + header_line_h + k_header_gap + (body_rows * body_line_h) +
                          (static_cast<float>(group_count) * header_line_h) + group_gaps;
    const float panel_x = (viewport.x - panel_w) * 0.5f;
    const float panel_y = (viewport.y - panel_h) * 0.5f;

    panel_chrome(r, style.bg, border, {.x = panel_x, .y = panel_y}, {.x = panel_w, .y = panel_h});

    const float title_x = panel_x + ((panel_w - title_w) * 0.5f);
    const float title_y = panel_y + k_pad_y;
    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = k_title,
        .position = {.x = title_x, .y = title_y},
        .char_size = style.font_size_speaker,
        .colour = style.speaker,
    });

    float y = title_y + header_line_h + k_header_gap;
    if (entries.empty()) {
      const float empty_w = r.measure_text(style.font_id, k_empty, style.font_size_body);
      r.draw(platform::DrawText{
          .font_id = style.font_id,
          .text = k_empty,
          .position = {.x = panel_x + ((panel_w - empty_w) * 0.5f), .y = y},
          .char_size = style.font_size_body,
          .colour = style.choice,
      });
      return;
    }

    const int clamped_cursor = std::clamp(cursor, 0, static_cast<int>(entries.size()) - 1);
    const float row_x = panel_x + k_pad_x;
    for (std::size_t i = 0; i < entries.size();) {
      const quest::Lifecycle section = entries[i].lifecycle;
      const std::string_view header = lifecycle_header(section);
      const float header_w = r.measure_text(style.font_id, header, style.font_size_speaker);
      r.draw(platform::DrawText{
          .font_id = style.font_id,
          .text = header,
          .position = {.x = panel_x + ((panel_w - header_w) * 0.5f), .y = y},
          .char_size = style.font_size_speaker,
          .colour = style.speaker,
      });
      y += header_line_h;

      while (i < entries.size() && entries[i].lifecycle == section) {
        const JournalEntry &entry = entries[i];
        draw_option(r, style, entry.name, {.x = row_x, .y = y}, std::cmp_equal(i, clamped_cursor));
        y += body_line_h;
        if (!entry.objective.empty()) {
          r.draw(platform::DrawText{
              .font_id = style.font_id,
              .text = entry.objective,
              .position = {.x = row_x + objective_indent, .y = y},
              .char_size = style.font_size_body,
              .colour = style.choice,
          });
          y += body_line_h;
        }
        ++i;
      }

      if (i < entries.size())
        y += k_group_gap;
    }
  }

} // namespace corundum::ui
