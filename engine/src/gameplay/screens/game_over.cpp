// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/gameplay/screens/game_over.hpp>

#include "gameplay/screens/framing_menu.hpp"
#include "gameplay/screens/screen_handlers.hpp"
#include "gameplay/screens/screen_input.hpp"

#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/input_glyph.hpp>
#include <corundum/ui/nine_patch.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/ui/toast.hpp>
#include <corundum/ui/ui_draw.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>

namespace corundum::gameplay::screens {

namespace {

constexpr std::string_view k_game_over_heading = "Game Over";

std::string game_over_footer(input::InputDevice last_device) {
  return std::format("{} Select   {} Close", ui::input_glyph(input::Action::Activate, last_device),
                     ui::input_glyph(input::Action::Cancel, last_device));
}

std::array<std::string_view, k_game_over_row_count> game_over_labels() {
  std::array<std::string_view, k_game_over_row_count> labels{};
  for (int row = 0; row < k_game_over_row_count; ++row)
    labels[static_cast<std::size_t>(row)] = game_over_row_label(game_over_row_at(row));
  return labels;
}

} // namespace

std::string_view game_over_row_label(GameOverRow row) noexcept {
  switch (row) {
    case GameOverRow::Reload: return "Reload";
    case GameOverRow::Load: return "Load";
    case GameOverRow::ReturnToTitle: return "Return to Title";
  }
  return "";
}

GameOverRow game_over_row_at(int row) noexcept {
  switch (row) {
    case 0: return GameOverRow::Reload;
    case 1: return GameOverRow::Load;
    case 2: return GameOverRow::ReturnToTitle;
    default: return GameOverRow::Reload;
  }
}

GameOverLayout game_over_panel_layout(const platform::Renderer &renderer, const ui::PanelStyle &style,
                                      core::math::Vec2 viewport, input::InputDevice last_device) {
  const std::array<std::string_view, k_game_over_row_count> labels = game_over_labels();
  const detail::FramingMenuLayout framing = detail::framing_menu_layout(
      renderer, style, viewport, labels, k_game_over_heading, game_over_footer(last_device));
  return GameOverLayout{
      .panel_pos = framing.panel_pos,
      .panel_size = framing.panel_size,
      .rows = framing.rows,
  };
}

void game_over_panel_render(platform::Renderer &renderer, const ui::PanelStyle &style,
                            const ui::NinePatchBorder &border, const GameOverState &state, core::math::Vec2 viewport,
                            input::InputDevice last_device) {
  const std::array<std::string_view, k_game_over_row_count> labels = game_over_labels();
  std::array<bool, k_game_over_row_count> enabled{};
  for (int row = 0; row < k_game_over_row_count; ++row) {
    const GameOverRow command = game_over_row_at(row);
    enabled[static_cast<std::size_t>(row)] = command != GameOverRow::Reload || state.reload_available;
  }

  const detail::FramingMenuLayout framing = detail::framing_menu_layout(
      renderer, style, viewport, labels, k_game_over_heading, game_over_footer(last_device));
  detail::framing_menu_render(renderer, style, border, framing, labels, enabled, state.cursor, k_game_over_heading,
                              game_over_footer(last_device), viewport);
}

namespace {

/** Apply mouse intent to the game-over screen: a hovered enabled row takes focus and the wheel moves it. */
void apply_game_over_pointer(const Engine &engine, GameOverState &state, const input::InputIntent &intent) {
  if (!pointer_active(intent))
    return;
  const GameOverLayout layout = game_over_panel_layout(engine.renderer(), engine.render.panel_skin.style,
                                                       screen_viewport(engine), engine.input_mapper.last_device());
  const core::math::Vec2 cursor = intent_cursor(intent);
  if (pointer_focus(intent)) {
    if (const int hovered = ui::hovered_row(cursor, layout.rows); hovered >= 0) {
      const GameOverRow row = game_over_row_at(hovered);
      if (row != GameOverRow::Reload || state.reload_available)
        state.cursor = hovered;
    }
  }
  if (const int steps = ui::scroll_row_delta(intent.scroll_y); steps != 0)
    state.cursor = wrap_cursor(state.cursor, steps, k_game_over_row_count);
}

/** Run the game-over row the player activated. */
void run_game_over_row(Engine &engine, Gameplay &gameplay, GameOverRow row) {
  switch (row) {
    case GameOverRow::Reload:
      if (!gameplay.game_over_screen.reload_available)
        break;
      if (const auto result = gameplay.load_from_slot(gameplay.playthrough.last_slot); !result)
        engine.notify(std::format("Load failed: {}", result.error()), ui::k_toast_failed_colour);
      break;
    case GameOverRow::Load: gameplay.open_save_load(SaveLoadIntent::Load); break;
    case GameOverRow::ReturnToTitle: gameplay.return_to_title(); break;
  }
}

} // namespace

void update_game_over(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent) {
  GameOverState &state = gameplay.game_over_screen;
  apply_game_over_pointer(engine, state, intent);

  if (intent.navigate_y != 0)
    state.cursor = wrap_cursor(state.cursor, intent.navigate_y, k_game_over_row_count);
  if (!intent.activate)
    return;

  run_game_over_row(engine, gameplay, game_over_row_at(std::clamp(state.cursor, 0, k_game_over_row_count - 1)));
}

void render_game_over(const Engine &engine, const GameOverState &state, platform::Renderer &renderer,
                      core::math::Vec2 viewport) {
  if (engine.scene.mode() != GameOver)
    return;
  game_over_panel_render(renderer, engine.render.panel_skin.style, engine.render.panel_skin.border, state, viewport,
                         engine.input_mapper.last_device());
}

} // namespace corundum::gameplay::screens
