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

    std::vector<std::string> reports{}; ///< Toasts: each command that resolved, and each order refused.

    bool should_pause{}; ///< A hostile started a wind-up the rules' auto-pause names; see pause_note().
  };

  /** @brief One encounter played out in the scene.
   *
   *  Screen-tier: Gameplay drops it on reset_session_state() and when the Combat layer leaves the
   *  UI stack. It holds entity ids, which name nothing once the scene it started in is replaced. */
  class Battle {
  public:
    static constexpr std::size_t k_protagonist = 0;

    static constexpr std::size_t k_hostile = 1;

    /** @brief Start a fight between the protagonist and the actor @p request names. Each side is
     *  given a standing attack on the other, so the protagonist fights without a click; a move order
     *  drops the protagonist's. The command bar is Attack on CombatCommand1, then the first two of
     *  the protagonist's commands on CombatCommand2 and CombatCommand3.
     *  @return The battle, or an error: no protagonist in the scene, no live actor with the id
     *          ("[gameplay] start_encounter: no actor '<id>' in this area"), or Encounter::start's. */
    [[nodiscard]] static std::expected<Battle, std::string> start(Engine &engine, const rules::CombatRules &rules,
                                                                  const EncounterRequest &request);

    /** @brief Run one fixed step:
     *  - this step's input becomes orders: a click, the command keys, and while Running held
     *    movement keys;
     *  - while Running the timeline advances, and a hostile's wind-up may call for an auto-pause;
     *  - a combatant with an attack order out of reach paths to a tile within reach of its target
     *    and walks there, and one in reach or winding up stands;
     *  - hud() refreshes.
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

    /** @brief The HUD's rows and command bar, refreshed by start() and update(). */
    [[nodiscard]] const screens::CombatHud &hud() const noexcept { return hud_; }

    /** @brief Why the fight last paused on its own, such as "Brann is winding up Crushing Blow";
     *  empty when it didn't, and cleared once it runs again. */
    [[nodiscard]] std::string_view pause_note() const noexcept { return pause_note_; }

  private:
    Battle(Encounter encounter, std::vector<entities::EntityId> entities, std::string actor_id,
           std::optional<rules::AutoPause> auto_pause);

    /// Turn this step's input into the protagonist's orders; returns the refusals to toast.
    [[nodiscard]] std::vector<std::string> read_orders(const Engine &engine, const rules::CombatContext &context,
                                                       Clock clock);

    /// Give the protagonist a standing attack on the hostile, re-planning its approach.
    void engage(const rules::CombatContext &context);

    /// The command key for bar slot @p slot (1 or 2): unqueue it when queued, refuse it when
    /// unavailable (returning why), otherwise queue it, engaging the hostile for an Enemy command.
    [[nodiscard]] std::optional<std::string> press_slot(const rules::CombatContext &context, std::size_t slot);

    /// Whether a wind-up in @p started calls for an auto-pause; sets pause_note_ when one does.
    [[nodiscard]] bool check_auto_pause(std::span<const std::size_t> started);

    /// Plan each ordered combatant's movement: stand when in reach or winding up, else approach.
    void plan_movement(Engine &engine, const world::MapView &map, const rules::CombatContext &context);

    /// Path combatant @p mover to the nearest tile within reach of combatant @p target_index.
    void approach(Engine &engine, const world::MapView &map, std::size_t mover, std::size_t target_index);

    /// Drive each hostile that can act along its planned path.
    void walk_hostiles(Engine &engine, const world::MapView &map, const rules::CombatContext &context);

    Encounter encounter_;

    std::vector<entities::EntityId> entities_;

    std::string actor_id_;

    std::optional<rules::AutoPause> auto_pause_;

    std::vector<world::TileCoord> tiles_;

    screens::CombatHud hud_;

    std::vector<std::vector<world::TileCoord>> paths_; ///< Each hostile's path; the protagonist uses Scene::path.

    std::vector<std::optional<world::TileCoord>> planned_for_; ///< The target tile each path was planned for.

    std::string pause_note_;
  };

} // namespace corundum::gameplay::combat
