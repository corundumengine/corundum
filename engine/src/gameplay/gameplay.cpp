// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/audio/audio_backend.hpp>
#include <corundum/audio/audio_system.hpp>
#include <corundum/core/direction.hpp>
#include <corundum/core/game_config.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/engine.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/entities/world.hpp>
#include <corundum/gameplay/codex/registry.hpp>
#include <corundum/gameplay/combat/battle.hpp>
#include <corundum/gameplay/credits/credits.hpp>
#include <corundum/gameplay/credits/loader.hpp>
#include <corundum/gameplay/dialogue/action.hpp>
#include <corundum/gameplay/dialogue/conversation.hpp>
#include <corundum/gameplay/dialogue/dialogue.hpp>
#include <corundum/gameplay/dialogue/dialogue_npc.hpp>
#include <corundum/gameplay/dialogue/validate_refs.hpp>
#include <corundum/gameplay/gameplay.hpp>
#include <corundum/gameplay/item/registry.hpp>
#include <corundum/gameplay/location/location.hpp>
#include <corundum/gameplay/quest/runner.hpp>
#include <corundum/gameplay/quest/status.hpp>
#include <corundum/gameplay/quest/system.hpp>
#include <corundum/gameplay/rules/rules.hpp>
#include <corundum/gameplay/screens/barter.hpp>
#include <corundum/gameplay/screens/character_sheet.hpp>
#include <corundum/gameplay/screens/codex.hpp>
#include <corundum/gameplay/screens/confirm.hpp>
#include <corundum/gameplay/screens/credits.hpp>
#include <corundum/gameplay/screens/dialog_box.hpp>
#include <corundum/gameplay/screens/game_over.hpp>
#include <corundum/gameplay/screens/journal.hpp>
#include <corundum/gameplay/screens/loot.hpp>
#include <corundum/gameplay/screens/map.hpp>
#include <corundum/gameplay/screens/modes.hpp>
#include <corundum/gameplay/screens/save_load.hpp>
#include <corundum/gameplay/screens/splash.hpp>
#include <corundum/gameplay/screens/title.hpp>
#include <corundum/gameplay/shop/shop.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/input/input_intent.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/resource_ids.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/save/save.hpp>
#include <corundum/screen_registry.hpp>
#include <corundum/sprites/sprite.hpp>
#include <corundum/ui/screen_transition.hpp>
#include <corundum/ui/toast.hpp>
#include <corundum/world/flags.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/transition.hpp>
#include <corundum/world/ui_stack.hpp>

#include "core/warn_log.hpp"
#include "gameplay/dialogue_events.hpp"
#include "gameplay/screens/screen_handlers.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace corundum::gameplay {

namespace {

using detail::warn_log;

/** Drop the fight and let the player walk again. Every path that ends or abandons a fight goes
 *  through here, so a protagonist the fight held never stays held.
 */
void drop_battle(Engine &engine, Gameplay &gameplay) noexcept {
  engine.scene.is_player_held = false;
  gameplay.battle.reset();
}

void validate_quest_references(const dialogue::Registry &graphs, const quest::Registry &quests,
                               const item::Registry &items) {
  for (const auto &[id, graph] : graphs) {
    for (const auto &err : dialogue::validate_quest_refs(graph, quests, &items, &graphs))
      warn_log("[gameplay] WARN: dialogue '{}' {}", id, err);
    for (const auto &err : dialogue::validate_condition_quest_refs(graph, quests))
      warn_log("[gameplay] WARN: dialogue '{}' {}", id, err);
  }
}

/** Seconds the credits hold still after opening before the roll begins to move. */
constexpr float k_credits_hold_seconds = 1.f;

/** Seconds the title music takes to fade in at boot and when the Title reopens. */
constexpr float k_title_music_fade_in_seconds = 1.f;

/** Seconds the title music takes to fade out once a world loads: just longer than the 0.4 s
 *  screen fade, so the new area appears under the tail of the music.
 */
constexpr float k_title_music_fade_out_seconds = 0.5f;

void load_content(Gameplay &gameplay, Engine &engine) {
  int dialogue_loaded{0};
  if (!engine.config.paths.dialogue_dir.empty())
    dialogue_loaded = gameplay.graphs.load_all(engine.content_source(), engine.config.paths.dialogue_dir);
  detail::info_log("[gameplay] Loaded {} dialogue graphs from '{}'", dialogue_loaded, engine.config.paths.dialogue_dir);

  int quest_loaded{0};
  if (!engine.config.paths.quests_dir.empty())
    quest_loaded = gameplay.quests.load_all(engine.content_source(), engine.config.paths.quests_dir);
  detail::info_log("[gameplay] Loaded {} quests from '{}'", quest_loaded, engine.config.paths.quests_dir);

  int item_loaded{0};
  if (!engine.config.paths.items_dir.empty())
    item_loaded = gameplay.items.load_all(engine.content_source(), engine.config.paths.items_dir);
  detail::info_log("[gameplay] Loaded {} items from '{}'", item_loaded, engine.config.paths.items_dir);

  int codex_loaded{0};
  if (!engine.config.paths.codex_dir.empty())
    codex_loaded = gameplay.codex.load_all(engine.content_source(), engine.config.paths.codex_dir);
  detail::info_log("[gameplay] Loaded {} codex entries from '{}'", codex_loaded, engine.config.paths.codex_dir);

  int location_loaded{0};
  if (!engine.config.paths.locations_dir.empty())
    location_loaded = gameplay.locations.load_all(engine.content_source(), engine.config.paths.locations_dir);
  detail::info_log("[gameplay] Loaded {} locations from '{}'", location_loaded, engine.config.paths.locations_dir);

  int shop_loaded{0};
  if (!engine.config.paths.shops_dir.empty())
    shop_loaded = gameplay.shops.load_all(engine.content_source(), engine.config.paths.shops_dir);
  detail::info_log("[gameplay] Loaded {} shops from '{}'", shop_loaded, engine.config.paths.shops_dir);

  validate_quest_references(gameplay.graphs, gameplay.quests, gameplay.items);
  for (const auto &[shop_id, shop] : gameplay.shops) {
    for (const shop::StockEntry &entry : shop.stock) {
      if (gameplay.items.find(entry.item) == nullptr)
        warn_log("[gameplay] WARN: shop '{}' stocks unknown item '{}'", shop_id, entry.item);
    }
  }
}

/** Pop the combat layers off the top of `ui`: CombatPaused, then Combat. */
void close_combat_layers(world::UIStack &ui) noexcept {
  if (ui.top() == screens::CombatPaused)
    ui.pop();
  if (ui.top() == screens::Combat)
    ui.pop();
}

/** Toggle the fight between running and paused on the Pause action. */
void toggle_combat_pause(world::UIStack &ui, const input::InputState &input) {
  if (!input.is_pressed(input::Action::Pause))
    return;
  if (ui.top() == screens::Combat)
    ui.push(screens::CombatPaused);
  else if (ui.top() == screens::CombatPaused)
    ui.pop();
}

/** End the fight with `outcome`: stop everyone, close the combat layers, and raise
 *  encounter_won/encounter_lost for the game's on_event hook.
 */
void end_battle(Engine &engine, Gameplay &gameplay, const combat::Battle &battle, rules::Outcome outcome) {
  battle.halt(engine);
  battle.stand_down(engine);
  close_combat_layers(engine.scene.ui);
  dialogue::EventAction ended{
      .name = outcome == rules::Outcome::Won ? "encounter_won" : "encounter_lost",
      .args = {std::string{battle.first_actor()}},
  };
  gameplay.pending_dialogue_events.push_back(std::move(ended));
  drop_battle(engine, gameplay);
}

/** Run the fight for one fixed step: drop it when its scene is gone, toggle the pause, then step
 *  it and toast its reports.
 */
void update_battle(Engine &engine, Gameplay &gameplay) {
  if (!gameplay.battle)
    return;

  world::UIStack &ui = engine.scene.ui;
  // An area change replaces the scene and its UI stack, so the Combat layer is gone and the
  // battle's entity ids name nothing: leaving ends the fight with no outcome.
  if (!ui.contains(screens::Combat)) {
    drop_battle(engine, gameplay);
    return;
  }
  // The liveness check runs only while the layer is there, so it never reads a replaced scene's ids.
  if (!gameplay.battle->has_live_combatants(engine)) {
    gameplay.battle->stand_down(engine);
    close_combat_layers(ui);
    drop_battle(engine, gameplay);
    return;
  }

  toggle_combat_pause(ui, engine.input_state);

  // A screen over the fight (the portal prompt) holds it.
  const world::GameMode top = ui.top();
  if (top != screens::Combat && top != screens::CombatPaused)
    return;

  const combat::Clock clock = top == screens::Combat ? combat::Clock::Running : combat::Clock::Paused;
  combat::BattleStep step = gameplay.battle->update(engine, clock);
  for (std::string &report : step.reports)
    engine.notify(std::move(report));
  if (step.outcome) {
    end_battle(engine, gameplay, *gameplay.battle, *step.outcome);
    return;
  }
}

/** Register the hub tab screens and the in-world screens: dialogue, combat, inventory, character,
 *  journal, codex and map.
 */
void register_hub_screens(Gameplay &gameplay, Engine &engine) {
  engine.screens.add(screens::Dialogue, ScreenSpec{.owns_step = false});
  engine.screens.add(screens::Combat,
                     ScreenSpec{
                         .handles_back = false,
                         .owns_step = false,
                         .world_step = world::WorldStep::Runs,
                         .render =
                             [&gameplay](Engine &e, platform::Renderer &renderer, core::math::Vec2 viewport) {
                               if (gameplay.battle)
                                 screens::render_combat(e, *gameplay.battle, renderer, viewport, "");
                             },
                     });
  engine.screens.add(screens::CombatPaused,
                     ScreenSpec{
                         .handles_back = false,
                         .owns_step = false,
                         .render =
                             [&gameplay](Engine &e, platform::Renderer &renderer, core::math::Vec2 viewport) {
                               if (!gameplay.battle)
                                 return;
                               const std::string banner = screens::paused_banner(e.input_mapper.last_device());
                               screens::render_combat(e, *gameplay.battle, renderer, viewport, banner);
                             },
                     });
  engine.screens.add(
      screens::Inventory,
      ScreenSpec{
          .owns_step = true,
          .update = [&gameplay](Engine &e,
                                const input::InputIntent &intent) { screens::update_inventory(e, gameplay, intent); },
          .render =
              [&gameplay](Engine &e, platform::Renderer &renderer, core::math::Vec2 viewport) {
                screens::render_inventory(e, gameplay, renderer, viewport);
              },
      });
  engine.screens.add(
      screens::Character,
      ScreenSpec{
          .owns_step = true,
          .update = [&gameplay](Engine &e,
                                const input::InputIntent &intent) { screens::update_character(e, gameplay, intent); },
          .render =
              [&gameplay](Engine &e, platform::Renderer &renderer, core::math::Vec2 viewport) {
                screens::render_character(e, gameplay.character_sheet_state, gameplay.character_sheet_info, renderer,
                                          viewport);
              },
      });
  engine.screens.add(screens::Journal,
                     ScreenSpec{
                         .owns_step = true,
                         .update =
                             [&gameplay](Engine &e, const input::InputIntent &intent) {
                               screens::update_journal(e, gameplay.journal_screen, gameplay.quests, intent);
                             },
                         .render =
                             [&gameplay](Engine &e, platform::Renderer &renderer, core::math::Vec2 viewport) {
                               screens::render_journal(e, gameplay.journal_screen, gameplay.quests, renderer, viewport);
                             },
                     });
  engine.screens.add(screens::Codex, ScreenSpec{
                                         .owns_step = true,
                                         .update =
                                             [&gameplay](Engine &e, const input::InputIntent &intent) {
                                               screens::update_codex(e, gameplay.codex_screen, gameplay.codex, intent);
                                             },
                                     });
  engine.screens.add(screens::Map, ScreenSpec{
                                       .owns_step = true,
                                       .update =
                                           [&gameplay](Engine &e, const input::InputIntent &intent) {
                                             screens::update_map(e, gameplay.map_screen, gameplay.locations, intent);
                                           },
                                   });
}

/** Register the modal screens: loot, barter and confirm. */
void register_modal_screens(Gameplay &gameplay, Engine &engine) {
  engine.screens.add(screens::Loot, ScreenSpec{
                                        .owns_step = true,
                                        .update =
                                            [&gameplay](Engine &e, const input::InputIntent &intent) {
                                              screens::update_loot(e, gameplay, intent);
                                            },
                                    });
  engine.screens.add(screens::Barter, ScreenSpec{
                                          .owns_step = true,
                                          .update =
                                              [&gameplay](Engine &e, const input::InputIntent &intent) {
                                                screens::update_barter(e, gameplay, intent);
                                              },
                                      });
  engine.screens.add(
      screens::Confirm,
      ScreenSpec{
          .owns_step = true,
          .update = [&gameplay](Engine &e,
                                const input::InputIntent &intent) { screens::update_confirm(e, gameplay, intent); },
          .render =
              [&gameplay](Engine &e, platform::Renderer &renderer, core::math::Vec2 viewport) {
                screens::render_confirm(e, gameplay.confirm, renderer, viewport);
              },
      });
}

/** Register the framing screens: splash, title, save/load, game over, loading and credits. */
void register_framing_screens(Gameplay &gameplay, Engine &engine) {
  engine.screens.add(
      screens::Splash,
      ScreenSpec{
          .owns_step = true,
          .update = [&gameplay](Engine &e,
                                const input::InputIntent &intent) { screens::update_splash(e, gameplay, intent); },
          .render =
              [&gameplay](Engine &e, platform::Renderer &renderer, core::math::Vec2 viewport) {
                screens::render_splash(e, gameplay.splash_screen, renderer, viewport);
              },
      });
  engine.screens.add(
      screens::Title,
      ScreenSpec{
          .owns_step = true,
          .update = [&gameplay](Engine &e,
                                const input::InputIntent &intent) { screens::update_title(e, gameplay, intent); },
          .render =
              [&gameplay](Engine &e, platform::Renderer &renderer, core::math::Vec2 viewport) {
                screens::render_title(e, gameplay.title_screen, renderer, viewport);
              },
      });
  engine.screens.add(
      screens::SaveLoad,
      ScreenSpec{
          .owns_step = true,
          .update = [&gameplay](Engine &e,
                                const input::InputIntent &intent) { screens::update_save_load(e, gameplay, intent); },
          .render =
              [&gameplay](Engine &e, platform::Renderer &renderer, core::math::Vec2 viewport) {
                screens::render_save_load(e, gameplay.save_load_screen, renderer, viewport);
              },
      });
  engine.screens.add(
      screens::GameOver,
      ScreenSpec{
          .owns_step = true,
          .update = [&gameplay](Engine &e,
                                const input::InputIntent &intent) { screens::update_game_over(e, gameplay, intent); },
          .render =
              [&gameplay](Engine &e, platform::Renderer &renderer, core::math::Vec2 viewport) {
                screens::render_game_over(e, gameplay.game_over_screen, renderer, viewport);
              },
      });
  engine.screens.add(screens::Loading,
                     ScreenSpec{
                         .owns_step = true,
                         .update = [&gameplay](Engine &, const input::InputIntent &) { gameplay.advance_loading(); },
                         .render =
                             [&gameplay](Engine &e, platform::Renderer &renderer, core::math::Vec2 viewport) {
                               screens::render_loading(e, gameplay, renderer, viewport);
                             },
                     });
  engine.screens.add(screens::Credits,
                     ScreenSpec{
                         .owns_step = true,
                         .update =
                             [&gameplay](Engine &e, const input::InputIntent &intent) {
                               screens::update_credits(e, gameplay.credits_screen, intent);
                             },
                         .render =
                             [&gameplay](Engine &e, platform::Renderer &renderer, core::math::Vec2 viewport) {
                               screens::render_credits(e, gameplay.credits_screen, renderer, viewport);
                             },
                     });
}

/** Install the engine hooks and the fixed-step system that drive the framework. */
void register_hooks(Gameplay &gameplay, Engine &engine) {
  engine.on_menu_quit = [&gameplay](Engine &) {
    gameplay.open_confirm("Quit to Title?", [](Gameplay &g) { g.return_to_title(); });
  };
  engine.on_menu_save_load = [&gameplay](Engine &, SaveLoadIntent intent) { gameplay.open_save_load(intent); };
  // The framing screens replace the world; the pause menu must not open over any of them.
  engine.blocks_pause_menu = [](world::GameMode mode) {
    return mode == screens::Splash || mode == screens::Title || mode == screens::GameOver || mode == screens::Loading ||
           mode == screens::Credits;
  };

  engine.fixed_step_systems.emplace_back([&gameplay](Engine &, float dt) { gameplay.fixed_step(dt); });

  engine.screen_input_hooks.emplace_back([&gameplay](Engine &e, const input::InputIntent &intent) {
    return screens::handle_hub_input(e, gameplay, intent);
  });
  engine.screen_input_hooks.emplace_back(
      [&gameplay](Engine &e, const input::InputIntent &) { return screens::handle_character_input(e, gameplay); });
}

/** Register the gameplay render layers. */
void register_render_layers(Gameplay &gameplay, Engine &engine) {
  engine.screens.add_layer(RenderLayer::Hud,
                           [&gameplay](Engine &e, platform::Renderer &renderer, core::math::Vec2 /*viewport*/) {
                             screens::render_hud(e, gameplay.dialogue, gameplay.quests, renderer);
                           });
  engine.screens.add_layer(RenderLayer::Modal,
                           [&gameplay](Engine &e, platform::Renderer &renderer, core::math::Vec2 v) {
                             screens::render_dialogue(e, gameplay.dialog_box, gameplay.dialogue, renderer, v);
                           });
  engine.screens.add_layer(RenderLayer::HubPanel,
                           [&gameplay](Engine &e, platform::Renderer &renderer, core::math::Vec2 v) {
                             screens::render_codex(e, gameplay.codex_screen, renderer, v);
                             screens::render_map(e, gameplay.map_screen, gameplay.locations, renderer, v);
                             screens::render_loot(e, gameplay, renderer, v);
                             screens::render_barter(e, gameplay, renderer, v);
                           });
  engine.screens.add_layer(RenderLayer::HubStrip, [](Engine &e, platform::Renderer &renderer, core::math::Vec2 v) {
    screens::render_hub_strip(e, renderer, v);
  });
}

/** Register the gameplay framework with `engine`: its fixed-step system, hub input hook,
 *  screens and render layers. Every closure captures `gameplay`, which outlives the engine
 *  under the Runtime member-order contract.
 */
void register_gameplay(Gameplay &gameplay, Engine &engine) {
  register_hub_screens(gameplay, engine);
  register_modal_screens(gameplay, engine);
  register_framing_screens(gameplay, engine);
  register_hooks(gameplay, engine);
  register_render_layers(gameplay, engine);
}

} // namespace

Gameplay::Gameplay(Engine &engine) : engine_(&engine) {
  load_content(*this, engine);
  register_gameplay(*this, engine);
  // Title music starts at boot so it plays under the splash. The splash never touches audio, so
  // skipping its images cannot interrupt the music.
  play_title_music();
  // A splash defers open_first_screen() until its last image fades out (see advance_splash()).
  if (!open_splash())
    open_first_screen();
}

Gameplay::~Gameplay() = default;

void Gameplay::process_events() noexcept {
  // A hook can end the session (clearing pending_dialogue_events) or queue new events, so swap
  // the batch out first: the loop must never walk a vector that changes under it. Vector move
  // assignment doesn't allocate, so this stays noexcept-safe.
  const std::vector<dialogue::EventAction> events = std::exchange(pending_dialogue_events, {});
  const std::uint64_t batch_generation = session_generation_;
  quest::Runner quest_runner{quests, engine_->flags};
  for (const dialogue::EventAction &event : events) {
    // Events after a session reset belong to the session that just ended.
    if (session_generation_ != batch_generation)
      break;
    dispatch_dialogue_event(*this, *engine_, quest_runner, event);
  }
}

void Gameplay::fixed_step(float dt) {
  Engine &engine = *engine_;
  const input::InputIntent intent = input::make_input_intent(engine.input_state, engine.input_mapper.last_device());

  // Quick-save/quick-load (F5/F9) are the framework's dev shortcuts; handle them on a step the
  // world owns, so a step-owning screen cannot trigger a save mid-menu.
  if (engine.input_state.is_pressed(input::Action::QuickSave))
    std::ignore = quick_save();
  if (engine.input_state.is_pressed(input::Action::QuickLoad))
    std::ignore = quick_load();

  playthrough.playtime_seconds += static_cast<double>(dt);

  update_battle(engine, *this);

  // Interaction is a one-frame pulse: read and clear it unconditionally so an unconsumed value
  // (no target in range, wrong mode) never persists into the next step.
  const std::optional<entities::EntityId> interaction = engine.scene.pending_interaction;
  engine.scene.pending_interaction.reset();

  if (engine.scene.mode() == screens::Dialogue) {
    update_dialogue(intent);
  } else if (engine.scene.mode() == world::GameMode::Exploring && interaction) {
    try_interact(*interaction);
  }

  process_events();

  // After process_events, so a start_encounter whose dialogue closed this step starts now; the
  // fight reads no input until the next step.
  start_pending_encounter(engine, *this);

  if (dialogue)
    screens::dialog_box_advance(dialog_box, *dialogue, dt, engine.render.text_speed);

  quest::tick_quests(quests, engine.flags, engine.scene.zone_id);
}

std::string Gameplay::current_location_name() const {
  if (const location::Location *loc = locations.find(engine_->scene.zone_id); loc != nullptr && !loc->name.empty())
    return loc->name;
  return engine_->scene.zone_id;
}

std::expected<void, std::string> Gameplay::save_to_slot(std::string_view slot_id) {
  // A fight's state lives only in the battle, which a save doesn't hold.
  if (battle)
    return std::unexpected(std::string{"can't save during a fight"});

  if (!engine_->user_storage)
    return std::unexpected(engine_->user_storage.error());

  const auto result =
      save::save_game(*engine_, **engine_->user_storage, slot_id, current_location_name(),
                      static_cast<std::int64_t>(playthrough.playtime_seconds), std::chrono::system_clock::now());
  if (result)
    playthrough.last_slot = std::string{slot_id};
  return result;
}

std::expected<void, std::string> Gameplay::load_from_slot(std::string_view slot_id) {
  if (!engine_->user_storage)
    return std::unexpected(engine_->user_storage.error());

  const std::expected<save::SaveMeta, std::string> loaded = save::load_game(*engine_, **engine_->user_storage, slot_id);
  if (!loaded) {
    // load_game unloads the world when the scene rebuild itself failed, which also empties the UI
    // stack; with nothing left to show, fall back to the title. A refusal before the rebuild
    // (missing file, wrong game) keeps the current world or screen.
    if (render::mode(engine_->render) == render::RenderMode::None && engine_->scene.ui.empty())
      return_to_title();
    return std::unexpected(loaded.error());
  }

  // Build the new record before assigning: slot_id may view the old playthrough.last_slot.
  playthrough = Playthrough{
      .playtime_seconds = static_cast<double>(loaded->playtime_seconds),
      .last_slot = std::string{slot_id},
  };
  // load_game replaced the Scene (which clears scene.ui) but left every screen member pointing
  // at the old world. Clear them so the loaded scene starts clean.
  reset_session_state();
  // A loaded world ends the title music. A quick-load mid-game has none playing, so this is a no-op.
  stop_title_music();
  // Reveal the loaded area from black; this covers quicksave and Game-over reloads as well as
  // the Title's Continue/Load, which arrive here through begin_load().
  engine_->screen_transition.begin_fade_in();
  return {};
}

std::expected<void, std::string> Gameplay::quick_save() {
  std::expected<void, std::string> result = save_to_slot(save::k_quicksave_slot);
  if (result)
    engine_->notify("Game saved");
  else
    engine_->notify(std::format("Save failed: {}", result.error()), ui::k_toast_failed_colour);
  return result;
}

std::expected<void, std::string> Gameplay::quick_load() {
  std::expected<void, std::string> result = load_from_slot(save::k_quicksave_slot);
  if (result)
    engine_->notify("Game loaded");
  else
    engine_->notify(std::format("Load failed: {}", result.error()), ui::k_toast_failed_colour);
  return result;
}

std::expected<void, std::string> Gameplay::autosave() {
  return save_to_slot(save::k_autosave_slot);
}

void Gameplay::open_confirm(std::string question, std::function<void(Gameplay &)> on_yes) {
  confirm = screens::ConfirmState{
      .question = std::move(question),
      .on_yes = std::move(on_yes),
      .yes_selected = true,
  };
  engine_->scene.ui.push(screens::Confirm);
}

void Gameplay::open_title() {
  title_screen = {};
  if (engine_->user_storage) {
    const std::vector<save::SaveSlotInfo> slots = save::list_saves(**engine_->user_storage, engine_->config.game_id);
    title_screen.continue_available = screens::newest_valid_slot(slots).has_value();
  }
  refresh_credits();
  title_screen.cursor =
      screens::title_row_enabled(screens::TitleRow::Continue, title_screen.continue_available) ? 0 : 1;
  engine_->scene.ui.push(screens::Title);
  play_title_music();
}

bool Gameplay::open_splash() {
  splash_screen = {};
  platform::Renderer &renderer = engine_->renderer();
  for (const std::string &path : engine_->config.paths.splash_images) {
    const std::expected<platform::TextureId, std::string> texture = renderer.load_texture(path);
    if (!texture) {
      warn_log("[splash] cannot load '{}': {} (image skipped)", path, texture.error());
      continue;
    }
    splash_screen.images.push_back(screens::SplashImage{
        .size = renderer.texture_size(*texture),
        .texture = *texture,
    });
  }
  if (splash_screen.images.empty())
    return false;
  splash_screen.initial_delay_seconds = screens::k_splash_initial_delay_seconds;
  engine_->screen_transition.begin_fade_in(screens::k_splash_fade_seconds);
  engine_->scene.ui.push(screens::Splash);
  return true;
}

void Gameplay::advance_splash() {
  splash_screen.shown_seconds = 0.f;
  ++splash_screen.index;
  // The previous image faded out to black, and nothing else clears the overlay; reveal what comes
  // next: the following splash image at the splash's pace, or the first screen at the standard pace.
  if (splash_screen.index < splash_screen.images.size()) {
    engine_->screen_transition.begin_fade_in(screens::k_splash_fade_seconds);
    return;
  }
  // Splash art is screen-tier: give every reference back now; the first area load purges it.
  for (const screens::SplashImage &image : splash_screen.images)
    engine_->renderer().release_texture(image.texture);
  splash_screen = {};
  engine_->scene.ui.pop();
  open_first_screen();
  engine_->screen_transition.begin_fade_in();
}

void Gameplay::open_first_screen() {
  if (engine_->config.show_title) {
    open_title();
    return;
  }
  // No title: start the playthrough at once, without the toast New Game shows.
  if (const std::expected<void, std::string> result = begin_new_playthrough(); !result) {
    warn_log("[gameplay] WARN: could not start the game: {} (showing the title)", result.error());
    return_to_title();
  }
}

void Gameplay::play_title_music() {
  const core::TitleMusicConfig &music = engine_->config.title_music;
  if (music.track.empty())
    return;
  const audio::MusicPlayback playback{
      .fade_in_seconds = k_title_music_fade_in_seconds,
      .loop_delay_seconds = music.loop_delay_seconds,
  };
  if (const std::expected<void, std::string> started = engine_->audio.play_music(music.track, playback); !started)
    warn_log("[gameplay] WARN: title music '{}' did not start: {}", music.track, started.error());
}

void Gameplay::stop_title_music() noexcept {
  engine_->audio.stop_music(k_title_music_fade_out_seconds);
}

void Gameplay::open_character_sheet() {
  character_sheet_info = screens::build_character_info(engine_->flags, items, ruleset);
  character_sheet_state = {};
  engine_->scene.ui.push(screens::Character);
}

void Gameplay::refresh_credits() {
  // open_title() refreshes the credits on every visit; hand back the previous visit's reference
  // so the background's count stays at one. Credits art is process-lifetime: only area unloads
  // purge, and the reference is re-taken below before any purge.
  engine_->renderer().release_texture(credits_screen.background_texture);
  credits_screen = {};
  const std::string &path = engine_->config.paths.credits_file;
  if (path.empty())
    return;
  std::expected<credits::CreditsFile, std::string> loaded =
      credits::load_credits_file(engine_->content_source(), std::filesystem::path{path});
  if (!loaded) {
    warn_log("[credits] cannot load '{}': {} (Credits hidden)", path, loaded.error());
    return;
  }
  credits_screen.title = std::move(loaded->title);
  credits_screen.sections = std::move(loaded->sections);
  if (!loaded->background.empty()) {
    platform::Renderer &renderer = engine_->renderer();
    const std::expected<platform::TextureId, std::string> texture = renderer.load_texture(loaded->background);
    if (texture) {
      credits_screen.background_texture = *texture;
      credits_screen.background_size = renderer.texture_size(*texture);
    } else {
      warn_log("[credits] cannot load background '{}': {} (flat backdrop used)", loaded->background, texture.error());
    }
  }
  title_screen.credits_available = !credits_screen.sections.empty();
}

void Gameplay::open_credits() {
  credits_screen.scroll = 0.f;
  credits_screen.hold = k_credits_hold_seconds;
  engine_->scene.ui.push(screens::Credits);
}

void Gameplay::begin_load(std::function<std::expected<void, std::string>(Gameplay &)> work) {
  pending_load_ = std::move(work);
  loading_rendered_ = false;
  // Fade toward black while the Loading overlay covers the scene swap; the queued work
  // replaces the scene and fades the new one in.
  engine_->screen_transition.begin_fade_out();
  engine_->scene.ui.push(screens::Loading);
}

void Gameplay::notify_loading_rendered() noexcept {
  loading_rendered_ = true;
}

void Gameplay::advance_loading() {
  if (!loading_rendered_ || !pending_load_)
    return;

  const std::function<std::expected<void, std::string>(Gameplay &)> work = std::move(pending_load_);
  pending_load_ = nullptr;
  engine_->scene.ui.pop();
  if (const std::expected<void, std::string> result = work(*this); !result) {
    // The load began a fade-out; reveal whatever is showing now (the title) again.
    engine_->screen_transition.begin_fade_in();
    engine_->notify(std::format("Load failed: {}", result.error()), ui::k_toast_failed_colour);
  }
}

void Gameplay::open_game_over() {
  game_over_screen = {};
  game_over_screen.reload_available = !playthrough.last_slot.empty();
  game_over_screen.cursor = game_over_screen.reload_available ? 0 : 1;
  engine_->scene.ui.push(screens::GameOver);
}

void Gameplay::open_save_load(SaveLoadIntent intent) {
  save_load_screen = {};
  save_load_screen.saving = intent == SaveLoadIntent::Save;
  if (engine_->user_storage) {
    save_load_screen.slots = screens::build_slot_rows(**engine_->user_storage, engine_->config.game_id);
  } else {
    engine_->notify(std::format("Cannot open saves: {}", engine_->user_storage.error()), ui::k_toast_failed_colour);
  }
  engine_->scene.ui.push(screens::SaveLoad);
}

std::expected<void, std::string> Gameplay::begin_new_playthrough() {
  Engine &engine = *engine_;
  engine.flags.clear();
  for (const auto &[key, value] : engine.config.starting_flags)
    engine.flags[key] = value;
  engine.entered_from_world = false;
  playthrough = {};

  // World mode enters the overworld at the manifest default spawn; single-map mode applies the
  // per-map spawn point / game.json player / built-in precedence. apply_spawn would instead pin
  // the spawn to game.json's tile and skip a per-map spawn point.
  const std::expected<void, std::string> result = world::load_initial_scene(engine);

  // load_initial_scene replaces the Scene (clearing scene.ui); clear every screen member too, so
  // a conversation or cursor from the abandoned playthrough cannot outlive it.
  reset_session_state();
  // Title music belongs to the world-less framing screens; a world that loaded ends it.
  if (result)
    stop_title_music();
  return result;
}

std::expected<void, std::string> Gameplay::new_game() {
  if (const std::expected<void, std::string> result = begin_new_playthrough(); !result) {
    // The failed build may have left a half-loaded world; drop it and fall back to the title.
    // result still carries the original scene-build error for the caller and tests.
    return_to_title();
    engine_->notify(std::format("New game failed: {}", result.error()), ui::k_toast_failed_colour);
    return result;
  }
  // Reveal the new game from black, whichever path drove it (Title, Game over, tests).
  engine_->screen_transition.begin_fade_in();
  engine_->notify("New game started");
  return {};
}

void Gameplay::return_to_title() {
  // reset_session_state() clears screen-tier members and the UI stack; unload_world() then
  // replaces the Scene (which also clears its UI stack). The double UI clear is harmless, and
  // keeping reset_session_state() first ensures every screen member is torn down even if a
  // future change makes unload_world() keep some scene state.
  reset_session_state();
  world::unload_world(*engine_);
  engine_->flags.clear();
  playthrough = {};
  open_title();
}

std::expected<void, std::string> Gameplay::continue_game() {
  if (!engine_->user_storage) {
    engine_->notify(std::format("Load failed: {}", engine_->user_storage.error()), ui::k_toast_failed_colour);
    return std::unexpected(engine_->user_storage.error());
  }
  const std::vector<save::SaveSlotInfo> slots = save::list_saves(**engine_->user_storage, engine_->config.game_id);
  const std::optional<std::string> newest = screens::newest_valid_slot(slots);
  if (!newest) {
    engine_->notify("No saved game to continue");
    return std::unexpected("no saved game");
  }
  const std::expected<void, std::string> result = load_from_slot(*newest);
  if (result)
    engine_->notify("Game loaded");
  else
    engine_->notify(std::format("Load failed: {}", result.error()), ui::k_toast_failed_colour);
  return result;
}

void Gameplay::reset_session_state() {
  ++session_generation_;
  dialogue.reset();
  dialogue_npc.reset();
  pending_dialogue_events.clear();
  drop_battle(*engine_, *this);
  pending_encounter.reset();
  active_container_id.clear();
  active_shop_id.clear();
  inventory_cursor = 0;
  inventory_scroll = 0;
  inventory_lines.clear();
  inventory_equipment.clear();
  character_sheet_info = {};
  character_sheet_state = {};
  journal_screen = {};
  codex_screen = {};
  map_screen = {};
  loot_screen = {};
  barter_screen = {};
  confirm = {};
  title_screen = {};
  credits_screen.scroll = 0.f;
  credits_screen.hold = 0.f;
  game_over_screen = {};
  save_load_screen = {};
  pending_load_ = nullptr;
  loading_rendered_ = false;
  last_hub_mode = screens::Inventory;
  engine_->scene.ui.clear();
}

void Gameplay::update_dialogue(const input::InputIntent &intent) {
  if (!dialogue)
    return;

  pending_dialogue_events = dialogue->update(intent);
  if (dialogue->is_active())
    return;

  if (dialogue_npc) {
    entities::World &world = engine_->scene.world;
    const dialogue::DialogueNpc &npc = *dialogue_npc;
    if (npc.saved_facing && world.facings.has(npc.entity))
      world.facings.dir_ref(npc.entity) = *npc.saved_facing;
    if (npc.saved_anim && world.sprites.has(npc.entity)) {
      world.sprites.anim_id_ref(npc.entity) = *npc.saved_anim;
      world.sprites.frame_index_ref(npc.entity) = 0;
    }
  }
  dialogue_npc.reset();
  dialogue.reset();
  engine_->scene.ui.pop();
}

void Gameplay::try_interact(entities::EntityId target) {
  using core::Direction;
  using sprites::AnimId;
  using sprites::to_anim;

  world::Scene &scene = engine_->scene;
  entities::World &world = scene.world;
  if (!world::player_present(scene))
    return;
  if (!world.dialogue_refs.has(target) || !world.transforms.has(target))
    return;

  const dialogue::Graph *const graph = graphs.find(world.dialogue_refs.get_graph_id(target));
  if (graph == nullptr)
    return;

  const std::uint32_t player_slot = world.transforms.dense_index(scene.player);
  const float player_col = world.transforms.col[player_slot];
  const float player_row = world.transforms.row[player_slot];
  const std::uint32_t npc_slot = world.transforms.dense_index(target);
  const float npc_col = world.transforms.col[npc_slot];
  const float npc_row = world.transforms.row[npc_slot];

  const Direction toward_npc = core::direction_from_tile_delta(npc_col - player_col, npc_row - player_row);

  dialogue::DialogueNpc npc{.entity = target};
  if (world.facings.has(target)) {
    npc.saved_facing = world.facings.dir_of(target);
    const Direction face_player = core::opposite(toward_npc);
    world.facings.dir_ref(target) = face_player;
    if (world.sprites.has(target) && world.animations.has(target)) {
      npc.saved_anim = world.sprites.anim_id_ref(target);
      const AnimId dir_anim = to_anim(face_player);
      const bool has_dir_anim = world.animations.frame_count(target, dir_anim) > 0;
      world.sprites.anim_id_ref(target) = has_dir_anim ? dir_anim : AnimId::Default;
      world.sprites.frame_index_ref(target) = 0;
    }
  }

  dialogue_npc = npc;
  dialogue.emplace(*graph, engine_->flags, &quests, &graphs, scene.zone_id);
  scene.ui.push(screens::Dialogue);
  // Defensive: a click that both queued a path AND was close enough to trigger interact (same
  // frame) would otherwise leave that path to silently resume once the conversation ends,
  // walking the player toward wherever they clicked to start it.
  scene.path.clear();
}

} // namespace corundum::gameplay
