// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/ui/codex.hpp>

#include <corundum/codex/codex.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/dialog_box.hpp>
#include <corundum/ui/input_glyph.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <corundum/ui/word_wrap.hpp>
#include <corundum/world/flags.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace corundum::ui {

  namespace {

    std::string_view category_label(const codex::CodexEntry &entry) noexcept {
      return entry.category.empty() ? std::string_view{"Lore"} : std::string_view{entry.category};
    }

  } // namespace

  std::vector<codex::CodexEntry> build_codex_entries(const codex::Registry &registry, const world::FlagStore &flags) {
    std::vector<codex::CodexEntry> entries;
    for (const auto &[id, entry] : registry) {
      if (world::has_flag(flags, codex::flag_key(id)))
        entries.push_back(entry);
    }
    std::ranges::sort(entries, {}, [](const codex::CodexEntry &entry) {
      return std::tuple{std::string{category_label(entry)}, entry.title};
    });
    return entries;
  }

  void refresh_codex(CodexState &state, const codex::Registry &registry, const world::FlagStore &flags) {
    if (!state.dirty)
      return;
    state.entries = build_codex_entries(registry, flags);
    state.dirty = false;
    state.cursor = std::clamp(state.cursor, 0, std::max(0, static_cast<int>(state.entries.size()) - 1));
  }

  void codex_panel_render(platform::Renderer &r, const DialogBoxStyle &style, const NinePatchBorder &border,
                          const std::vector<codex::CodexEntry> &entries, int cursor, float scroll,
                          core::math::Vec2 viewport, input::InputDevice last_device) {
    constexpr float k_pad = 20.f;
    constexpr std::string_view k_title = "Codex";
    constexpr std::string_view k_empty = "(no entries)";
    constexpr float k_split_frac = 0.42f;

    const float line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
    const float header_h = std::max(line_h, static_cast<float>(style.font_size_speaker) + 4.f);

    const float panel_w = std::max(480.f, viewport.x * 0.8f);
    const float panel_h = std::max(280.f, viewport.y * 0.75f);
    const float panel_x = (viewport.x - panel_w) * 0.5f;
    const float panel_y = (viewport.y - panel_h) * 0.5f;

    panel_chrome(r, style.bg, border, {.x = panel_x, .y = panel_y}, {.x = panel_w, .y = panel_h});

    const float title_w = r.measure_text(style.font_id, k_title, style.font_size_speaker);
    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = k_title,
        .position = {.x = panel_x + ((panel_w - title_w) * 0.5f), .y = panel_y + k_pad},
        .char_size = style.font_size_speaker,
        .colour = style.speaker,
    });

    const std::string footer = std::format("{} Select   {} Close", input_glyph(input::Action::Activate, last_device),
                                           input_glyph(input::Action::Cancel, last_device));
    const float footer_w = r.measure_text(style.font_id, footer, style.font_size_body);
    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = footer,
        .position = {.x = panel_x + ((panel_w - footer_w) * 0.5f), .y = panel_y + panel_h - k_pad - line_h},
        .char_size = style.font_size_body,
        .colour = style.choice,
    });

    const float content_top = panel_y + k_pad + header_h + k_pad;
    const float content_bottom = panel_y + panel_h - k_pad - line_h - k_pad;
    const float list_w = panel_w * k_split_frac;
    const float list_x = panel_x + k_pad;
    const float body_x = panel_x + list_w + k_pad;
    const float body_w = panel_w - list_w - (k_pad * 2.f);

    if (entries.empty()) {
      r.draw(platform::DrawText{
          .font_id = style.font_id,
          .text = k_empty,
          .position = {.x = list_x, .y = content_top},
          .char_size = style.font_size_body,
          .colour = style.choice,
      });
      return;
    }

    const int clamped_cursor = std::clamp(cursor, 0, static_cast<int>(entries.size()) - 1);
    const int visible_rows = std::max(1, static_cast<int>((content_bottom - content_top) / line_h));

    // A flat row list of category headers and entry labels lets the list window scroll as one
    // sequence while the cursor still indexes entries.
    struct Row {
      bool header{false};
      std::string text{};
      int entry_index{};
    };

    std::vector<Row> rows;
    std::string_view last_category;
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
      const std::string_view category = category_label(entries[static_cast<std::size_t>(i)]);
      if (rows.empty() || category != last_category) {
        rows.push_back(Row{.header = true, .text = std::string(category)});
        last_category = category;
      }
      rows.push_back(Row{.header = false, .text = entries[static_cast<std::size_t>(i)].title, .entry_index = i});
    }

    const int cursor_row = static_cast<int>(
        std::ranges::find_if(
            rows, [clamped_cursor](const Row &row) { return !row.header && row.entry_index == clamped_cursor; }) -
        rows.begin());
    int first_row =
        std::clamp(cursor_row - visible_rows + 1, 0, std::max(0, static_cast<int>(rows.size()) - visible_rows));
    if (cursor_row < first_row)
      first_row = cursor_row;

    float y = content_top;
    for (int i = first_row; i < static_cast<int>(rows.size()) && i < first_row + visible_rows; ++i) {
      const Row &row = rows[static_cast<std::size_t>(i)];
      if (row.header) {
        r.draw(platform::DrawText{
            .font_id = style.font_id,
            .text = row.text,
            .position = {.x = list_x, .y = y},
            .char_size = style.font_size_speaker,
            .colour = style.speaker,
        });
      } else {
        draw_option(r, style, row.text, {.x = list_x, .y = y}, row.entry_index == clamped_cursor);
      }
      y += line_h;
    }

    const codex::CodexEntry &selected = entries[static_cast<std::size_t>(clamped_cursor)];
    const float body_line_h = line_h;
    const std::vector<std::string> body_lines = wrap_text(selected.body, body_w, [&](std::string_view text) {
      return r.measure_text(style.font_id, text, style.font_size_body);
    });

    const int visible_body = std::max(1, static_cast<int>((content_bottom - content_top) / body_line_h));
    const float max_scroll = std::max(0.f, static_cast<float>(body_lines.size()) - static_cast<float>(visible_body));
    const int first_body = std::clamp(static_cast<int>(scroll), 0, static_cast<int>(max_scroll));

    float body_y = content_top;
    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = selected.title,
        .position = {.x = body_x, .y = body_y},
        .char_size = style.font_size_speaker,
        .colour = style.speaker,
    });
    body_y += header_h;
    for (int i = first_body; i < static_cast<int>(body_lines.size()) && i < first_body + visible_body; ++i) {
      r.draw(platform::DrawText{
          .font_id = style.font_id,
          .text = body_lines[static_cast<std::size_t>(i)],
          .position = {.x = body_x, .y = body_y},
          .char_size = style.font_size_body,
          .colour = style.body,
      });
      body_y += body_line_h;
    }
  }

} // namespace corundum::ui
