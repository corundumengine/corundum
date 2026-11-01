// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/entities/entity.hpp>
#include <corundum/gameplay/combat/encounter.hpp>
#include <corundum/gameplay/rules/rules.hpp>
#include <corundum/gameplay/screens/combat_hud.hpp>
#include <corundum/world/picking.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace corundum {
  struct Engine;
} // namespace corundum

// One fight played out in the scene. Encounter keeps the timeline; Battle maps its combatants onto
// entities, turns this step's click into an order and walks combatants into reach. Combatant
// k_protagonist is the protagonist and k_hostile the actor start_encounter named.
namespace corundum::gameplay::combat {

  /** @brief The fight start_encounter asked for: the hostile actor and the creature id its rules use. */
  struct EncounterRequest {
    std::string actor_id{};

    std::string creature_id{};
  };

  enum class Clock : std::uint8_t {
    Running, ///< Combat is on top: orders, the timeline and movement all run.
    Paused,  ///< CombatPaused is on top: only clicks on a hostile give orders.
  };

  /** @brief What one Battle step produced. */
  struct BattleStep {
    std::optional<Outcome> outcome{};

    std::vector<std::string> reports{};
  };

  /** @brief One encounter played out in the scene.
   *
   *  Screen-tier: Gameplay drops it on reset_session_state() and when the Combat layer leaves the
   *  UI stack. It holds entity ids, which name nothing once the scene it started in is replaced. */
  class Battle {
  public:
    static constexpr std::size_t k_protagonist = 0;

    static constexpr std::size_t k_hostile = 1;

    /** @brief Start a fight between the protagonist and the actor @p request names. The hostile is
     *  given a standing attack on the protagonist.
     *  @return The battle, or an error: no protagonist in the scene, no live actor with the id
     *          ("[gameplay] start_encounter: no actor '<id>' in this area"), or Encounter::start's. */
    [[nodiscard]] static std::expected<Battle, std::string> start(Engine &engine, const rules::CombatRules &rules,
                                                                  const EncounterRequest &request);

    /** @brief Run one fixed step: this step's click (and, while Running, held movement keys)
     *  become orders; while Running the timeline advances and hostiles walk; hud_rows() refreshes.
     *  @pre has_live_combatants(engine). */
    [[nodiscard]] BattleStep update(Engine &engine, Clock clock);

    /** @brief True while every combatant's entity is live with a transform. */
    [[nodiscard]] bool has_live_combatants(const Engine &engine) const;

    /** @brief Stop every combatant: clear the player's path and zero the hostiles' velocity.
     *  @pre has_live_combatants(engine). */
    void halt(Engine &engine) const;

    [[nodiscard]] const Encounter &encounter() const noexcept { return encounter_; }

    [[nodiscard]] entities::EntityId hostile_entity() const noexcept { return entities_[k_hostile]; }

    [[nodiscard]] std::string_view hostile_actor() const noexcept { return actor_id_; }

    /** @brief The HUD's rows, refreshed by start() and update(). */
    [[nodiscard]] std::span<const screens::CombatHudRow> hud_rows() const noexcept { return hud_rows_; }

  private:
    Battle(Encounter encounter, std::vector<entities::EntityId> entities, std::string actor_id);

    Encounter encounter_;

    std::vector<entities::EntityId> entities_;

    std::string actor_id_;

    std::vector<world::TileCoord> tiles_;

    std::vector<screens::CombatHudRow> hud_rows_;
  };

} // namespace corundum::gameplay::combat
