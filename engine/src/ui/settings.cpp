// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/ui/settings.hpp>

#include <corundum/core/math/vec.hpp>
#include <corundum/core/window_mode.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/bindings.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/input_glyph.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/ui_draw.hpp>

#include <algorithm>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace corundum::ui {

  std::string_view settings_tab_label(SettingsTab tab) noexcept {
    switch (tab) {
      case SettingsTab::General:
        return "General";
      case SettingsTab::Controls:
        return "Controls";
    }
    return "";
  }

  std::string_view text_speed_label(float speed) noexcept {
    if (speed <= 0.f)
      return "Instant";
    if (speed <= 0.5f)
      return "Slow";
    if (speed <= 1.f)
      return "Normal";
    return "Fast";
  }

  namespace {

    int text_speed_index(float speed) noexcept {
      for (int i = 0; std::cmp_less(i, k_text_speed_presets.size()); ++i) {
        if (k_text_speed_presets[static_cast<std::size_t>(i)] == speed)
          return i;
      }
      return 2; // Normal — the default, and the fallback for an unrecognised value.
    }

  } // namespace

  float next_text_speed(float speed) noexcept {
    const int next = (text_speed_index(speed) + 1) % static_cast<int>(k_text_speed_presets.size());
    return k_text_speed_presets[static_cast<std::size_t>(next)];
  }

  float prev_text_speed(float speed) noexcept {
    const int size = static_cast<int>(k_text_speed_presets.size());
    const int prev = (text_speed_index(speed) - 1 + size) % size;
    return k_text_speed_presets[static_cast<std::size_t>(prev)];
  }

  std::string_view settings_general_row_label(SettingsGeneralRow row) noexcept {
    switch (row) {
      case SettingsGeneralRow::Volume:
        return "Master Volume";
      case SettingsGeneralRow::TextSpeed:
        return "Text Speed";
      case SettingsGeneralRow::UiScale:
        return "UI Scale";
      case SettingsGeneralRow::WindowMode:
        return "Window Mode";
      case SettingsGeneralRow::Count:
        break;
    }
    return "";
  }

  int settings_row_count(SettingsTab tab) noexcept {
    switch (tab) {
      case SettingsTab::General:
        return k_settings_general_row_count;
      case SettingsTab::Controls:
        return static_cast<int>(input::k_action_count);
    }
    return 0;
  }

  void settings_scroll_to_cursor(SettingsState &state, int row_count, int visible_rows) noexcept {
    if (visible_rows <= 0 || row_count <= visible_rows) {
      state.scroll = 0;
      return;
    }
    state.scroll = std::clamp(state.scroll, 0, row_count - visible_rows);
    if (state.cursor < state.scroll)
      state.scroll = state.cursor;
    else if (state.cursor >= state.scroll + visible_rows)
      state.scroll = state.cursor - visible_rows + 1;
    state.scroll = std::clamp(state.scroll, 0, row_count - visible_rows);
  }

  namespace {

    /// Every binding on @p action, joined with commas; "(unbound)" when it has none.
    std::string bindings_text(const input::Bindings &bindings, input::Action action) {
      std::string result;
      for (const input::PhysicalInput input : input::inputs_for(bindings, action)) {
        const std::string_view name = input::name_of(input);
        if (!name.empty()) {
          if (!result.empty())
            result += ", ";
          result += name;
        }
      }
      return result.empty() ? std::string{"(unbound)"} : result;
    }

    /// Value column text for one General row.
    std::string general_value_text(SettingsGeneralRow row, const SettingsValues &values) {
      switch (row) {
        case SettingsGeneralRow::Volume:
          return std::format("{:.0f}%", values.master_volume * 100.f);
        case SettingsGeneralRow::TextSpeed:
          return std::string{text_speed_label(values.text_speed)};
        case SettingsGeneralRow::UiScale:
          return std::format("{:.0f}%", values.ui_scale * 100.f);
        case SettingsGeneralRow::WindowMode:
          return std::string{core::window_mode_name(values.window_mode)};
        case SettingsGeneralRow::Count:
          break;
      }
      return {};
    }

    /// (label, value) for every row of @p tab, in draw order.
    std::vector<std::pair<std::string, std::string>>
    build_rows(const SettingsState &state, const SettingsValues &values, const input::Bindings &bindings) {
      std::vector<std::pair<std::string, std::string>> rows;
      if (state.tab == SettingsTab::General) {
        for (int i = 0; i < k_settings_general_row_count; ++i) {
          const auto row = static_cast<SettingsGeneralRow>(i);
          rows.emplace_back(std::string{settings_general_row_label(row)}, general_value_text(row, values));
        }
        return rows;
      }

      for (std::size_t i = 0; i < input::k_action_count; ++i) {
        const auto action = static_cast<input::Action>(i);
        const bool capturing = state.rebinding && std::cmp_equal(state.cursor, i);
        rows.emplace_back(std::string{input::action_name(action)},
                          capturing ? std::string{"(press a key)"} : bindings_text(bindings, action));
      }
      return rows;
    }

    constexpr float k_settings_min_w = 320.f;
    constexpr float k_settings_pad_x = 26.f;
    constexpr float k_settings_pad_y = 16.f;
    constexpr float k_settings_title_gap = 10.f;
    constexpr float k_settings_tab_gap = 26.f;
    constexpr float k_settings_value_gap = 24.f;
    constexpr float k_settings_footer_gap = 12.f;
    constexpr std::string_view k_settings_title = "Settings";

    /// Footer hint, using the last-used device's glyphs.
    std::string settings_footer(input::InputDevice last_device) {
      return std::format("{} / {} Tab   {} Select   {} Back", input_glyph(input::Action::TabPrev, last_device),
                         input_glyph(input::Action::TabNext, last_device),
                         input_glyph(input::Action::Activate, last_device),
                         input_glyph(input::Action::Cancel, last_device));
    }

    float settings_line_height(const PanelStyle &style) noexcept {
      return std::max(style.line_spacing, static_cast<float>(style.font_size_body) + 4.f);
    }

  } // namespace

  SettingsLayout settings_panel_layout(const platform::Renderer &r, const PanelStyle &style, const SettingsState &state,
                                       const SettingsValues &values, const input::Bindings &bindings,
                                       core::math::Vec2 viewport, input::InputDevice last_device) {
    const float line_h = settings_line_height(style);
    const float title_h = std::max(line_h, static_cast<float>(style.font_size_speaker) + 4.f);
    const float cursor_w = cursor_advance(r, style);

    const std::vector<std::pair<std::string, std::string>> rows = build_rows(state, values, bindings);
    const int row_count = static_cast<int>(rows.size());
    const int visible_rows = std::min(row_count, k_settings_max_visible_rows);
    const int first_row = std::clamp(state.scroll, 0, std::max(0, row_count - visible_rows));

    const std::string_view general = settings_tab_label(SettingsTab::General);
    const std::string_view controls = settings_tab_label(SettingsTab::Controls);
    const float general_w = r.measure_text(style.font_id, general, style.font_size_body);
    const float controls_w = r.measure_text(style.font_id, controls, style.font_size_body);

    float widest = std::max(r.measure_text(style.font_id, k_settings_title, style.font_size_speaker),
                            general_w + k_settings_tab_gap + controls_w);
    for (int i = first_row; i < first_row + visible_rows; ++i) {
      const auto &[label, value] = rows[static_cast<std::size_t>(i)];
      widest = std::max(widest, cursor_w + r.measure_text(style.font_id, label, style.font_size_body) +
                                    k_settings_value_gap + r.measure_text(style.font_id, value, style.font_size_body));
    }

    const std::string footer = settings_footer(last_device);
    widest = std::max(widest, r.measure_text(style.font_id, footer, style.font_size_body));

    const float panel_w = std::max(k_settings_min_w, widest + (k_settings_pad_x * 2.f));
    const float panel_h = (k_settings_pad_y * 2.f) + title_h + k_settings_title_gap + line_h + k_settings_title_gap +
                          (static_cast<float>(visible_rows) * line_h) + k_settings_footer_gap + line_h;
    const float panel_x = (viewport.x - panel_w) * 0.5f;
    const float panel_y = (viewport.y - panel_h) * 0.5f;

    SettingsLayout layout{};
    layout.panel_pos = {.x = panel_x, .y = panel_y};
    layout.panel_size = {.x = panel_w, .y = panel_h};

    const float tabs_w = general_w + k_settings_tab_gap + controls_w;
    const float tabs_x = panel_x + ((panel_w - tabs_w) * 0.5f);
    const float tabs_y = panel_y + k_settings_pad_y + title_h + k_settings_title_gap;
    layout.tabs[0] = ui::RowRect{.pos = {.x = tabs_x, .y = tabs_y}, .width = general_w, .height = line_h};
    layout.tabs[1] = ui::RowRect{
        .pos = {.x = tabs_x + general_w + k_settings_tab_gap, .y = tabs_y},
        .width = controls_w,
        .height = line_h,
    };

    layout.rows = ui::ListHit{
        .row_pos = {.x = panel_x + k_settings_pad_x, .y = tabs_y + line_h + k_settings_title_gap},
        .row_width = panel_w - (k_settings_pad_x * 2.f),
        .row_height = line_h,
        .first_row = first_row,
        .visible_rows = visible_rows,
    };
    return layout;
  }

  void settings_panel_render(platform::Renderer &r, const PanelStyle &style, const NinePatchBorder &border,
                             const SettingsState &state, const SettingsValues &values, const input::Bindings &bindings,
                             core::math::Vec2 viewport, input::InputDevice last_device) {
    const SettingsLayout layout = settings_panel_layout(r, style, state, values, bindings, viewport, last_device);
    const float line_h = layout.rows.row_height;

    const std::vector<std::pair<std::string, std::string>> rows = build_rows(state, values, bindings);
    const int row_count = static_cast<int>(rows.size());
    const int first_row = layout.rows.first_row;
    const int visible_rows = layout.rows.visible_rows;

    panel_chrome(r, style.bg, border, layout.panel_pos, layout.panel_size);

    const float title_w = r.measure_text(style.font_id, k_settings_title, style.font_size_speaker);
    float y = layout.panel_pos.y + k_settings_pad_y;
    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = k_settings_title,
        .position = {.x = layout.panel_pos.x + ((layout.panel_size.x - title_w) * 0.5f), .y = y},
        .char_size = style.font_size_speaker,
        .colour = style.speaker,
    });
    y += std::max(line_h, static_cast<float>(style.font_size_speaker) + 4.f) + k_settings_title_gap;

    // Tab header: active tab in the speaker colour, inactive dimmed. The tab rects double as
    // mouse hit targets for switch-on-click (see update_settings).
    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = settings_tab_label(SettingsTab::General),
        .position = layout.tabs[0].pos,
        .char_size = style.font_size_body,
        .colour = state.tab == SettingsTab::General ? style.speaker : style.choice,
    });
    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = settings_tab_label(SettingsTab::Controls),
        .position = layout.tabs[1].pos,
        .char_size = style.font_size_body,
        .colour = state.tab == SettingsTab::Controls ? style.speaker : style.choice,
    });
    y += line_h + k_settings_title_gap;

    const int clamped_cursor = std::clamp(state.cursor, 0, std::max(0, row_count - 1));
    for (int i = first_row; i < first_row + visible_rows; ++i) {
      const bool selected = i == clamped_cursor;
      const auto &[label, value] = rows[static_cast<std::size_t>(i)];
      draw_option(r, style, label, {.x = layout.rows.row_pos.x, .y = y}, selected);

      const float value_w = r.measure_text(style.font_id, value, style.font_size_body);
      r.draw(platform::DrawText{
          .font_id = style.font_id,
          .text = value,
          .position = {.x = layout.panel_pos.x + layout.panel_size.x - k_settings_pad_x - value_w, .y = y},
          .char_size = style.font_size_body,
          .colour = selected ? style.selected : style.choice,
      });
      y += line_h;
    }

    // A scroll affordance so a short page does not look like the whole list.
    if (first_row > 0 || first_row + visible_rows < row_count) {
      const std::string scroll = std::format("{}/{}", clamped_cursor + 1, row_count);
      r.draw(platform::DrawText{
          .font_id = style.font_id,
          .text = scroll,
          .position =
              {
                  .x = layout.panel_pos.x + layout.panel_size.x - k_settings_pad_x -
                       r.measure_text(style.font_id, scroll, style.font_size_prompt),
                  .y = layout.panel_pos.y + layout.panel_size.y - k_settings_pad_y - line_h,
              },
          .char_size = style.font_size_prompt,
          .colour = style.choice,
      });
    }

    const std::string footer = settings_footer(last_device);
    r.draw(platform::DrawText{
        .font_id = style.font_id,
        .text = footer,
        .position =
            {
                .x = layout.panel_pos.x + k_settings_pad_x,
                .y = layout.panel_pos.y + layout.panel_size.y - k_settings_pad_y - line_h,
            },
        .char_size = style.font_size_prompt,
        .colour = style.choice,
    });
  }

} // namespace corundum::ui
