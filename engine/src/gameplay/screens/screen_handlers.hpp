// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
// The update and render handlers gameplay.cpp registers for each gameplay screen and render
// layer. Each is defined in its screen's .cpp, after that screen's layout and drawing code.

#include <corundum/core/math/vec.hpp>
#include <corundum/input/physical_input.hpp>

#include <string>
#include <string_view>

namespace corundum {

struct Engine;

} // namespace corundum

namespace corundum::input {

struct InputIntent;

} // namespace corundum::input

namespace corundum::platform {

class Renderer;

} // namespace corundum::platform

namespace corundum::gameplay {

class Gameplay;

} // namespace corundum::gameplay

namespace corundum::gameplay::combat {

class Battle;

} // namespace corundum::gameplay::combat

namespace corundum::gameplay::screens {

// Menu hub (screens/hub_tabs.cpp)
bool handle_hub_input(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent);

void render_hub_strip(const Engine &engine, platform::Renderer &renderer, core::math::Vec2 viewport);

// HUD strip (screens/hud_strip.cpp)
void render_hud(const Engine &engine, const Gameplay &gameplay, platform::Renderer &renderer,
                core::math::Vec2 /*viewport*/);

// Dialogue box (screens/dialog_box.cpp)
void render_dialogue(const Engine &engine, Gameplay &gameplay, platform::Renderer &renderer, core::math::Vec2 viewport);

// Combat (screens/combat_hud.cpp)
void render_combat(const Engine &engine, const combat::Battle &battle, platform::Renderer &renderer,
                   core::math::Vec2 viewport, std::string_view banner);

std::string paused_banner(input::InputDevice device);

// Inventory (screens/inventory_panel.cpp)
void update_inventory(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent);

void render_inventory(const Engine &engine, const Gameplay &gameplay, platform::Renderer &renderer,
                      core::math::Vec2 viewport);

// Character sheet (screens/character_sheet.cpp)
bool handle_character_input(Engine &engine, Gameplay &gameplay);

void update_character(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent);

void render_character(const Engine &engine, const Gameplay &gameplay, platform::Renderer &renderer,
                      core::math::Vec2 viewport);

// Journal (screens/journal.cpp)
void update_journal(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent);

void render_journal(const Engine &engine, const Gameplay &gameplay, platform::Renderer &renderer,
                    core::math::Vec2 viewport);

// Codex (screens/codex.cpp)
void update_codex(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent);

void render_codex(const Engine &engine, const Gameplay &gameplay, platform::Renderer &renderer,
                  core::math::Vec2 viewport);

// Map (screens/map.cpp)
void update_map(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent);

void render_map(const Engine &engine, const Gameplay &gameplay, platform::Renderer &renderer,
                core::math::Vec2 viewport);

// Loot (screens/loot.cpp)
void update_loot(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent);

void render_loot(const Engine &engine, const Gameplay &gameplay, platform::Renderer &renderer,
                 core::math::Vec2 viewport);

// Barter (screens/barter.cpp)
void update_barter(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent);

void render_barter(const Engine &engine, const Gameplay &gameplay, platform::Renderer &renderer,
                   core::math::Vec2 viewport);

// Confirm (screens/confirm.cpp)
void update_confirm(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent);

void render_confirm(const Engine &engine, const Gameplay &gameplay, platform::Renderer &renderer,
                    core::math::Vec2 viewport);

// Splash (screens/splash.cpp)
void update_splash(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent);

void render_splash(const Engine &engine, const Gameplay &gameplay, platform::Renderer &renderer,
                   core::math::Vec2 viewport);

// Title (screens/title.cpp)
void update_title(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent);

void render_title(const Engine &engine, const Gameplay &gameplay, platform::Renderer &renderer,
                  core::math::Vec2 viewport);

// Save/Load (screens/save_load.cpp)
void update_save_load(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent);

void render_save_load(const Engine &engine, const Gameplay &gameplay, platform::Renderer &renderer,
                      core::math::Vec2 viewport);

// Game over (screens/game_over.cpp)
void update_game_over(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent);

void render_game_over(const Engine &engine, const Gameplay &gameplay, platform::Renderer &renderer,
                      core::math::Vec2 viewport);

// Loading (screens/loading.cpp)
void update_loading(Gameplay &gameplay);

void render_loading(const Engine &engine, Gameplay &gameplay, platform::Renderer &renderer, core::math::Vec2 viewport);

// Credits (screens/credits.cpp)
void update_credits(Engine &engine, Gameplay &gameplay, const input::InputIntent &intent);

void render_credits(const Engine &engine, const Gameplay &gameplay, platform::Renderer &renderer,
                    core::math::Vec2 viewport);

} // namespace corundum::gameplay::screens
