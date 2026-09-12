// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/game_config.hpp>
#include <corundum/dialogue/registry.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/world/scene.hpp>

#include <string_view>

namespace corundum::quest {
  class Registry;
}

namespace corundum::dialogue {

  /** @brief Advance the active dialogue conversation.
   *
   * Steps the dialogue Conversation, processes events, and restores NPC facing
   * and animation when the conversation ends.
   *
   *  @param[in,out] scene   All game-world state; steps scene.dialogue, mode, NPC state.
   *  @param[in]     actions Player input actions for the current fixed step.
   *  @pre GameMode must be Dialogue.
   *  @post On dialogue end, scene.dialogue is reset and NPC facing/animation are
   *        restored from saved state.
   */
  void update_dialogue(corundum::world::Scene &scene, const corundum::input::PressedActions &actions) noexcept;

  /** @brief Check for nearby NPCs and start a dialogue on Select press.
   *
   * Iterates all entities with dialogue references and checks proximity.
   * On success the scene transitions to Dialogue mode and NPC state is saved.
   *
   *  @param[in,out] scene   All game-world state; transitions to Dialogue mode on success.
   *  @param[in]     input   Current frame input state.
   *  @param[in]     cfg     Game config (interact_radius, etc.).
   *  @param[in]     graphs  All loaded dialogue graphs for lookup by graph_id.
   *  @param[in,out] flags   Persistent game flags (graph default variables, visit counts).
   *  @param[in]     quests  Quest registry bound into the new Conversation for condition
   *                         evaluation; may be nullptr.
   *  @pre GameMode must be Exploring.
   *  @post If an NPC is within range, scene.mode → Dialogue and scene.dialogue holds a
   *        new Conversation.
   *  @post NPC facing/animation are saved before modification.
   *  @performance O(n) over dialogue-ref entities. No heap allocation.
   */
  void try_interact(corundum::world::Scene &scene, const corundum::input::InputState &input,
                    const corundum::core::GameConfig &cfg, const corundum::dialogue::Registry &graphs,
                    corundum::world::FlagStore &flags, const quest::Registry *quests = nullptr) noexcept;

} // namespace corundum::dialogue
