#pragma once
#include <corundum/core/game_config.hpp>
#include <corundum/gameplay/dialogue/registry.hpp>
#include <corundum/gameplay/world/scene.hpp>
#include <corundum/input/actions.hpp>

namespace corundum::gameplay::sys {

  /** @brief Advance the active dialogue state machine.
   *
   * Steps the dialogue graph, processes events, and restores NPC facing
   * and animation when the conversation ends.
   *
   *  @param[in,out] scene     All game-world state; reads/writes dialogue, mode, NPC state.
   *  @param[in]     actions   Player input actions for the current fixed step.
   *  @pre GameMode must be Dialogue.
   *  @post On dialogue end, NPC facing and animation are restored from saved state.
   *  @performance O(1) per step. No heap allocation.
   */
  void update_dialogue(corundum::gameplay::world::Scene &scene,
                       const corundum::input::PressedActions &actions) noexcept;

  /** @brief Check for nearby NPCs and start a dialogue on Select press.
   *
   * Iterates all entities with dialogue references and checks proximity
   * and facing direction. The player must face the NPC to interact.
   * On success the scene transitions to Dialogue mode and NPC state is saved.
   *
   *  @param[in,out] scene   All game-world state; transitions to Dialogue mode on success.
   *  @param[in]     input   Current frame input state.
   *  @param[in]     cfg     Game config (interact_radius, etc.).
   *  @param[in]     graphs  All loaded dialogue graphs for lookup by graph_id.
   *  @pre GameMode must be Exploring.
   *  @post If an NPC is within range and the player faces them, scene.mode → Dialogue.
   *  @post NPC facing/animation are saved before modification.
   *  @performance O(n) over dialogue-ref entities. No heap allocation.
   */
  void try_interact(corundum::gameplay::world::Scene &scene, const corundum::input::InputState &input,
                    const corundum::core::GameConfig &cfg,
                    const corundum::gameplay::dialogue::Registry &graphs) noexcept;

} // namespace corundum::gameplay::sys
