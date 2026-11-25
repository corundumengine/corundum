// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/title.hpp>

#include "gameplay/screens/framing_menu.hpp"
#include "gameplay/screens/screen_handlers.hpp"
#include "gameplay/screens/screen_input.hpp"

#include <corundum/core/game_config.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/input_glyph.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/settings.hpp>
#include <corundum/ui/ui_draw.hpp>
#include <corundum/world/ui_stack.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>

namespace corundum::gameplay::screens {

namespace {

constexpr std::array<TitleRow, k_title_row_count> k_title_rows{
    TitleRow::Continue, TitleRow::NewGame, TitleRow::Load, TitleRow::Settings, TitleRow::Credits, TitleRow::Quit,
};

/** Credits is hidden when the game configures none, so Quit shifts down one index. */
constexpr std::array<TitleRow, k_title_row_count - 1> k_title_rows_without_credits{
    TitleRow::Continue, TitleRow::NewGame, TitleRow::Load, TitleRow::Settings, TitleRow::Quit,
};

std::string title_footer(input::InputDevice last_device) {
  return std::format("{} Select   {} Close", ui::input_glyph(input::Action::Activate, last_device),
                     ui::input_glyph(input::Action::Cancel, last_device));
}

/** The labels of the visible rows, packed to the front of the array; the returned span
 *  covers exactly title_visible_row_count entries.
 */
std::span<const std::string_view> title_labels(const TitleState &state,
                                               std::array<std::string_view, k_title_row_count> &storage) {
  const int count = title_visible_row_count(state.credits_available);
  for (int row = 0; row < count; ++row)
    storage[static_cast<std::size_t>(row)] = title_row_label(title_row_at(row, state.credits_available));
  return {storage.data(), static_cast<std::size_t>(count)};
}

} // namespace

std::string_view title_row_label(TitleRow row) noexcept {
  switch (row) {
    case TitleRow::Continue: return "Continue";
    case TitleRow::NewGame: return "New Game";
    case TitleRow::Load: return "Load";
    case TitleRow::Settings: return "Settings";
    case TitleRow::Credits: return "Credits";
    case TitleRow::Quit: return "Quit";
  }
  return "";
}

TitleRow title_row_at(int row, bool credits_available) noexcept {
  if (row < 0)
    return TitleRow::Continue;
  const auto index = static_cast<std::size_t>(row);
  if (credits_available)
    return index < k_title_rows.size() ? k_title_rows[index] : TitleRow::Continue;
  return index < k_title_rows_without_credits.size() ? k_title_rows_without_credits[index] : TitleRow::Continue;
}

bool title_row_enabled(TitleRow row, bool continue_available) noexcept {
  return row != TitleRow::Continue || continue_available;
}

TitleLayout title_panel_layout(const platform::Renderer &renderer, const ui::PanelStyle &style, const TitleState &state,
                               std::string_view title, core::math::Vec2 viewport, input::InputDevice last_device) {
  std::array<std::string_view, k_title_row_count> storage{};
  const std::span<const std::string_view> labels = title_labels(state, storage);
  const detail::FramingMenuLayout framing =
      detail::framing_menu_layout(renderer, style, viewport, labels, title, title_footer(last_device));
  return TitleLayout{
      .panel_pos = framing.panel_pos,
      .panel_size = framing.panel_size,
      .rows = framing.rows,
  };
}

void title_panel_render(platform::Renderer &renderer, const ui::PanelStyle &style, const ui::NinePatchBorder &border,
                        const TitleState &state, std::string_view title, core::math::Vec2 viewport,
                        input::InputDevice last_device) {
  std::array<std::string_view, k_title_row_count> storage{};
  const std::span<const std::string_view> labels = title_labels(state, storage);
  std::array<bool, k_title_row_count> enabled{};
  const int row_count = static_cast<int>(labels.size());
  for (int row = 0; row < row_count; ++row) {
    enabled[static_cast<std::size_t>(row)] =
        title_row_enabled(title_row_at(row, state.credits_available), state.continue_available);
  }

  const detail::FramingMenuLayout framing =
      detail::framing_menu_layout(renderer, style, viewport, labels, title, title_footer(last_device));
  detail::framing_menu_render(renderer, style, border, framing, labels,
                              std::span<const bool>(enabled).first(labels.size()), state.cursor, title,
                              title_footer(last_device), viewport);
}

namespace {

/** Move the title cursor by `delta`, wrapping and skipping disabled rows (Continue when no
 *  save exists). No-op when every row is disabled, which cannot happen today. The row count
 *  follows the Credits row's visibility, so the cursor never lands on a hidden row.
 */
void move_title_cursor(TitleState &state, int delta) noexcept {
  const int rows = title_visible_row_count(state.credits_available);
  int row = std::clamp(state.cursor, 0, rows - 1);
  for (int step = 0; step < rows; ++step) {
    row = wrap_cursor(row, delta, rows);
    if (title_row_enabled(title_row_at(row, state.credits_available), state.continue_available)) {
      state.cursor = row;
      return;
    }
  }
}

/** Apply mouse intent to the title: a hovered enabled row takes focus and the wheel moves the cursor. */
void apply_title_pointer(const Engine &engine, TitleState &state, const input::InputIntent &intent) {
  if (!pointer_active(intent))
    return;
  const TitleLayout layout =
      title_panel_layout(engine.renderer(), engine.render.panel_skin.style, state, core::game_title(engine.config),
                         screen_viewport(engine), engine.input_mapper.last_device());
  const core::math::Vec2 cursor = intent_cursor(intent);
  if (pointer_focus(intent)) {
    if (const int hovered = ui::hovered_row(cursor, layout.rows);
        hovered >= 0 && title_row_enabled(title_row_at(hovered, state.credits_available), state.continue_available))
      state.cursor = hovered;
  }
  if (const int steps = ui::scroll_row_delta(intent.scroll_y); steps != 0)
    move_title_cursor(state, steps);
}

/** Run the title row the player activated. */
void run_title_row(Engine &engine, Gameplay &gameplay, TitleRow row) {
  switch (row) {
    case TitleRow::Continue:
      if (gameplay.title_screen.continue_available)
        gameplay.begin_load([](Gameplay &g) { return g.continue_game(); });
      break;
    case TitleRow::NewGame: gameplay.begin_load([](Gameplay &g) { return g.new_game(); }); break;
    case TitleRow::Load: gameplay.open_save_load(SaveLoadIntent::Load); break;
    case TitleRow::Settings:
      engine.settings_screen = {.presentation = ui::SettingsPresentation::Framing};
      engine.scene.ui.push(world::GameMode::Settings);
      break;
    case TitleRow::Credits: gameplay.open_credits(); break;
    case TitleRow::Quit: engine.request_quit(); break;
  }
}

} // namespace

void update_title(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent) {
  TitleState &state = gameplay.title_screen;
  apply_title_pointer(engine, state, intent);

  if (intent.navigate_y != 0)
    move_title_cursor(state, intent.navigate_y);
  if (!intent.activate)
    return;

  const int rows = title_visible_row_count(state.credits_available);
  const TitleRow row = title_row_at(std::clamp(state.cursor, 0, rows - 1), state.credits_available);
  run_title_row(engine, gameplay, row);
}

void render_title(const Engine &engine, const TitleState &state, platform::Renderer &renderer,
                  core::math::Vec2 viewport) {
  if (engine.scene.mode() != Title)
    return;
  title_panel_render(renderer, engine.render.panel_skin.style, engine.render.panel_skin.border, state,
                     core::game_title(engine.config), viewport, engine.input_mapper.last_device());
}

} // namespace corundum::gameplay::screens
