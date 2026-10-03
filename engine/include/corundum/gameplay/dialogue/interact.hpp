// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/game_config.hpp>
#include <corundum/gameplay/dialogue/registry.hpp>
#include <corundum/input/input_intent.hpp>
#include <corundum/world/scene.hpp>

namespace corundum::gameplay::quest {
  class Registry;
}

namespace corundum::gameplay::dialogue {

  /** @brief Advance the active dialogue conversation.
   *
   * Steps the dialogue Conversation, processes events, and restores NPC facing
   * and animation when the conversation ends.
   *
   *  @param[in,out] scene   All game-world state; steps scene.dialogue, mode, NPC state.
   *  @param[in]     intent  Abstract navigational intent for the current fixed step.
   *  @pre GameMode must be Dialogue.
   *  @post On dialogue end, scene.dialogue is reset and NPC facing/animation are
   *        restored from saved state.
   */
  void update_dialogue(corundum::world::Scene &scene, const corundum::input::InputIntent &intent);

  /** @brief Check for nearby NPCs and start a dialogue on the activate intent.
   *
   * Iterates all entities with dialogue references and checks proximity.
   * On success the scene transitions to Dialogue mode and NPC state is saved.
   *
   *  @param[in,out] scene   All game-world state; transitions to Dialogue mode on success.
   *  @param[in]     intent  Abstract navigational intent for the current fixed step.
   *  @param[in]     cfg     Game config (interact_radius, etc.).
   *  @param[in]     graphs  All loaded dialogue graphs for lookup by graph_id.
   *  @param[in,out] flags   Persistent game flags (graph default variables, visit counts).
   *  @param[in]     quests  Quest registry bound into the new Conversation for condition
   *                         evaluation; may be nullptr.
   *  @pre GameMode must be Exploring.
   *  @post If an NPC is within range, scene.mode → Dialogue and scene.dialogue holds a
   *        new Conversation.
   *  @post NPC facing/animation are saved before modification.
   *  @note O(n) over dialogue-ref entities; allocates the new Conversation on success.
   */
  void try_interact(corundum::world::Scene &scene, const corundum::input::InputIntent &intent,
                    const corundum::core::GameConfig &cfg, const corundum::gameplay::dialogue::Registry &graphs,
                    corundum::world::FlagStore &flags, const quest::Registry *quests = nullptr);

} // namespace corundum::gameplay::dialogue
