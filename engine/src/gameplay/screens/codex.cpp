// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/codex.hpp>
#include <corundum/gameplay/screens/hub_tabs.hpp>
#include <corundum/ui/font_family.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/codex/codex.hpp>
#include <corundum/gameplay/codex/registry.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/input_glyph.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/styled_text.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <corundum/ui/word_wrap_styled.hpp>
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

    std::string_view category_label(const gameplay::codex::CodexEntry &entry) noexcept {
      return entry.category.empty() ? std::string_view{"Lore"} : std::string_view{entry.category};
    }

    /// One flattened list row: a category header or a selectable entry.
    struct CodexRow {
      bool header{false};

      std::string_view text{};

      int entry_index{};
    };

    struct CodexGeometry {
      float line_h{};
      float header_h{};
      float panel_x{};
      float panel_y{};
      float panel_w{};
      float panel_h{};
      float content_top{};
      float content_bottom{};
      float list_x{};
      float list_w{};
      float body_x{};
      float body_w{};
      int first_row{};
      int visible_rows{};
      int clamped_cursor{};
      std::vector<CodexRow> rows;
    };

    /// Flatten entries into header/entry rows and scroll the window to keep @p cursor visible.
    std::vector<CodexRow> build_codex_rows(const std::vector<gameplay::codex::CodexEntry> &entries) {
      std::vector<CodexRow> rows;
      std::string_view last_category;
      for (int i = 0; std::cmp_less(i, entries.size()); ++i) {
        const std::string_view category = category_label(entries[static_cast<std::size_t>(i)]);
        if (rows.empty() || category != last_category) {
          rows.push_back(CodexRow{.header = true, .text = category, .entry_index = -1});
          last_category = category;
        }
        rows.push_back(CodexRow{.header = false, .text = entries[static_cast<std::size_t>(i)].title, .entry_index = i});
      }
      return rows;
    }

    /// Whether @p index is a currently-drawn row: inside both the scroll window and the row list.
    bool codex_row_visible(const CodexGeometry &geometry, int index) noexcept {
      return index < geometry.first_row + geometry.visible_rows && std::cmp_less(index, geometry.rows.size());
    }

    /// Screen y of drawn row @p index.
    float codex_row_y(const CodexGeometry &geometry, int index) noexcept {
      return geometry.content_top + (static_cast<float>(index - geometry.first_row) * geometry.line_h);
    }

    CodexGeometry compute_codex_geometry(const ui::PanelStyle &style,
                                         const std::vector<gameplay::codex::CodexEntry> &entries, int cursor,
                                         core::math::Vec2 viewport) {
      constexpr float k_pad = 20.f;
      constexpr float k_split_frac = 0.42f;

      CodexGeometry geometry{};
      geometry.line_h = std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
      geometry.header_h = std::max(geometry.line_h, static_cast<float>(style.font_size_speaker) + 4.f);
      const ui::PanelRect panel = ui::screen_panel_rect(viewport, style, hub_panel_top_inset(style));
      geometry.panel_w = panel.size.x;
      geometry.panel_h = panel.size.y;
      geometry.panel_x = panel.pos.x;
      geometry.panel_y = panel.pos.y;
      geometry.content_top = geometry.panel_y + k_pad + geometry.header_h + k_pad;
      geometry.content_bottom = geometry.panel_y + geometry.panel_h - k_pad - geometry.line_h - k_pad;
      geometry.list_w = geometry.panel_w * k_split_frac;
      geometry.list_x = geometry.panel_x + k_pad;
      geometry.body_x = geometry.panel_x + geometry.list_w + k_pad;
      geometry.body_w = geometry.panel_w - geometry.list_w - (k_pad * 2.f);

      geometry.clamped_cursor = std::clamp(cursor, 0, std::max(0, static_cast<int>(entries.size()) - 1));
      geometry.rows = build_codex_rows(entries);
      if (geometry.rows.empty())
        return geometry;

      const int cursor_row =
          static_cast<int>(std::ranges::find_if(geometry.rows,
                                                [&](const CodexRow &row) {
                                                  return !row.header && row.entry_index == geometry.clamped_cursor;
                                                }) -
                           geometry.rows.begin());
      geometry.visible_rows =
          std::max(1, static_cast<int>((geometry.content_bottom - geometry.content_top) / geometry.line_h));
      geometry.first_row = std::clamp(cursor_row - geometry.visible_rows + 1, 0,
                                      std::max(0, static_cast<int>(geometry.rows.size()) - geometry.visible_rows));
      if (cursor_row < geometry.first_row)
        geometry.first_row = std::min(cursor_row, geometry.first_row);
      return geometry;
    }

  } // namespace

  std::vector<gameplay::codex::CodexEntry> build_codex_entries(const gameplay::codex::Registry &registry,
                                                               const world::FlagStore &flags) {
    std::vector<gameplay::codex::CodexEntry> entries;
    for (const auto &[id, entry] : registry) {
      if (world::has_flag(flags, gameplay::codex::flag_key(id)))
        entries.push_back(entry);
    }
    std::ranges::sort(entries, {}, [](const gameplay::codex::CodexEntry &entry) {
      return std::tuple{std::string{category_label(entry)}, entry.title};
    });
    return entries;
  }

  void refresh_codex(CodexState &state, const gameplay::codex::Registry &registry, const world::FlagStore &flags) {
    if (!state.dirty)
      return;
    state.entries = build_codex_entries(registry, flags);
    state.dirty = false;
    state.cursor = std::clamp(state.cursor, 0, std::max(0, static_cast<int>(state.entries.size()) - 1));
  }

  CodexLayout codex_panel_layout(const platform::Renderer & /*r*/, const ui::PanelStyle &style,
                                 const std::vector<gameplay::codex::CodexEntry> &entries, int cursor,
                                 core::math::Vec2 viewport, input::InputDevice /*last_device*/) {
    const CodexGeometry geometry = compute_codex_geometry(style, entries, cursor, viewport);

    CodexLayout layout{};
    layout.panel_pos = {.x = geometry.panel_x, .y = geometry.panel_y};
    layout.panel_size = {.x = geometry.panel_w, .y = geometry.panel_h};
    layout.body_rect = ui::RowRect{
        .pos = {.x = geometry.body_x, .y = geometry.content_top},
        .width = geometry.body_w,
        .height = geometry.content_bottom - geometry.content_top,
    };
    for (int i = geometry.first_row; codex_row_visible(geometry, i); ++i) {
      const CodexRow &row = geometry.rows[static_cast<std::size_t>(i)];
      layout.list_rows.push_back(CodexListRow{
          .rect =
              ui::RowRect{
                  .pos = {.x = geometry.list_x, .y = codex_row_y(geometry, i)},
                  .width = geometry.list_w,
                  .height = geometry.line_h,
              },
          .entry_index = row.header ? -1 : row.entry_index,
      });
    }
    return layout;
  }

  void codex_panel_render(platform::Renderer &r, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                          const std::vector<gameplay::codex::CodexEntry> &entries, int cursor, float scroll,
                          core::math::Vec2 viewport, input::InputDevice last_device) {
    constexpr float k_pad = 20.f;
    constexpr std::string_view k_title = "Codex";
    constexpr std::string_view k_empty = "(no entries)";

    const CodexGeometry geometry = compute_codex_geometry(style, entries, cursor, viewport);

    ui::screen_backdrop(r, style, viewport);
    ui::panel_chrome(r, style.bg, border, {.x = geometry.panel_x, .y = geometry.panel_y},
                     {.x = geometry.panel_w, .y = geometry.panel_h});

    const ui::FontFamily &quest_fonts = style.family(ui::FontRole::Quest);
    const ui::FontFamily &ui_fonts = style.family(ui::FontRole::Ui);
    const std::uint32_t quest_bold = quest_fonts.get(ui::FontStyle::Bold);
    const std::uint32_t ui_regular = ui_fonts.get(ui::FontStyle::Regular);
    const std::uint32_t ui_bold = ui_fonts.get(ui::FontStyle::Bold);
    const float title_w = r.measure_text(ui_bold, k_title, style.font_size_speaker);
    r.draw(platform::DrawText{
        .font_id = ui_bold,
        .text = k_title,
        .position = {.x = geometry.panel_x + ((geometry.panel_w - title_w) * 0.5f), .y = geometry.panel_y + k_pad},
        .char_size = style.font_size_speaker,
        .colour = style.speaker,
    });

    const std::string footer =
        std::format("{} Select   {} Close", ui::input_glyph(input::Action::Activate, last_device),
                    ui::input_glyph(input::Action::Cancel, last_device));
    const float footer_w = r.measure_text(ui_regular, footer, style.font_size_body);
    r.draw(platform::DrawText{
        .font_id = ui_regular,
        .text = footer,
        .position =
            {
                .x = geometry.panel_x + ((geometry.panel_w - footer_w) * 0.5f),
                .y = geometry.panel_y + geometry.panel_h - k_pad - geometry.line_h,
            },
        .char_size = style.font_size_body,
        .colour = style.choice,
    });

    if (entries.empty()) {
      r.draw(platform::DrawText{
          .font_id = ui_regular,
          .text = k_empty,
          .position = {.x = geometry.list_x, .y = geometry.content_top},
          .char_size = style.font_size_body,
          .colour = style.choice,
      });
      return;
    }

    for (int i = geometry.first_row; codex_row_visible(geometry, i); ++i) {
      const CodexRow &row = geometry.rows[static_cast<std::size_t>(i)];
      const float y = codex_row_y(geometry, i);
      if (row.header) {
        r.draw(platform::DrawText{
            .font_id = ui_bold,
            .text = row.text,
            .position = {.x = geometry.list_x, .y = y},
            .char_size = style.font_size_speaker,
            .colour = style.speaker,
        });
      } else {
        ui::draw_option(r, style, ui::FontRole::Quest, ui::FontStyle::Bold, row.text, {.x = geometry.list_x, .y = y},
                        row.entry_index == geometry.clamped_cursor);
      }
    }

    const gameplay::codex::CodexEntry &selected = entries[static_cast<std::size_t>(geometry.clamped_cursor)];
    const auto measure = [&](std::string_view text, ui::FontStyle font_style) -> float {
      return r.measure_text(quest_fonts.get(font_style), text, style.font_size_body);
    };
    const std::vector<ui::StyledLine> body_lines =
        ui::wrap_styled(ui::parse_styled(selected.body), geometry.body_w, measure);

    const int visible_body = geometry.visible_rows;
    const float max_scroll = std::max(0.f, static_cast<float>(body_lines.size()) - static_cast<float>(visible_body));
    const int first_body = std::clamp(static_cast<int>(scroll), 0, static_cast<int>(max_scroll));

    float body_y = geometry.content_top;
    r.draw(platform::DrawText{
        .font_id = quest_bold,
        .text = selected.title,
        .position = {.x = geometry.body_x, .y = body_y},
        .char_size = style.font_size_speaker,
        .colour = style.speaker,
    });
    body_y += geometry.header_h;
    for (int i = first_body; std::cmp_less(i, body_lines.size()) && i < first_body + visible_body; ++i) {
      ui::draw_styled(r, quest_fonts, body_lines[static_cast<std::size_t>(i)].segments, style.font_size_body,
                      style.body, {.x = geometry.body_x, .y = body_y});
      body_y += geometry.line_h;
    }
  }

} // namespace corundum::gameplay::screens
