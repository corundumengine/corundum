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
// k_protagonist is the protagonist, and the hostiles follow in the order start_encounter named
// them, from k_hostile.
namespace corundum::gameplay::combat {

  /** @brief One hostile in a fight: its actor and the creature id its rules use. */
  struct EncounterHostile {
    std::string actor_id{};

    std::string creature_id{};
  };

  /** @brief The fight start_encounter asked for: its hostiles, in the order named. */
  struct EncounterRequest {
    std::vector<EncounterHostile> hostiles{};
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

    static constexpr std::size_t k_hostile = 1; ///< The first hostile named.

    /** @brief Start a fight between the protagonist and the actors @p request names. Each hostile
     *  is given a standing attack on the protagonist, and the protagonist one on the first hostile,
     *  so it fights without a click; a move order drops the protagonist's. The command bar is
     *  Attack, then the protagonist's commands, then its toggles, on CombatCommand1–9.
     *  @return The battle, or an error: no protagonist in the scene, an empty request
     *          ("[gameplay] start_encounter: it names no hostiles"), no live actor with an id
     *          ("[gameplay] start_encounter: no actor '<id>' in this area"), one actor named twice
     *          ("[gameplay] start_encounter: actor '<id>' is named twice"), more slots than the bar
     *          holds ("[gameplay] the command bar holds 9 slots; the rules list <n>"), or
     *          Encounter::start's. */
    [[nodiscard]] static std::expected<Battle, std::string> start(Engine &engine, const rules::CombatRules &rules,
                                                                  const EncounterRequest &request);

    /** @brief Run one fixed step:
     *  - this step's input becomes orders: a click, the slot keys (on a gamepad, A/Y/B press the
     *    shown page's slots, and L1/R1 change the page), and while Running held movement keys;
     *  - while Running the timeline advances, and a hostile's wind-up may call for an auto-pause;
     *  - a combatant with an attack order out of reach paths to a tile within reach of its target
     *    and walks there, and one in reach or winding up stands. One its status says can't move
     *    stands too, and the protagonist then takes no move order and Scene::is_player_held is set;
     *  - hud() refreshes.
     *  @pre has_live_combatants(engine). */
    [[nodiscard]] BattleStep update(Engine &engine, Clock clock);

    /** @brief True while every combatant still in the fight has a live entity with a transform. */
    [[nodiscard]] bool has_live_combatants(const Engine &engine) const;

    /** @brief Stop every combatant: clear the player's path and zero the hostiles' velocity.
     *  @pre has_live_combatants(engine). */
    void halt(Engine &engine) const;

    [[nodiscard]] const Encounter &encounter() const noexcept { return encounter_; }

    /** @brief The actor named first; encounter_won and encounter_lost name it. */
    [[nodiscard]] std::string_view first_actor() const noexcept { return first_actor_; }

    /** @brief The HUD's rows and command bar, refreshed by start() and update(). */
    [[nodiscard]] const screens::CombatHud &hud() const noexcept { return hud_; }

    /** @brief Why the fight last paused on its own, such as "Brann is winding up Crushing Blow";
     *  empty when it didn't, and cleared once it runs again. */
    [[nodiscard]] std::string_view pause_note() const noexcept { return pause_note_; }

  private:
    Battle(Encounter encounter, std::vector<std::optional<entities::EntityId>> entities, std::string first_actor,
           std::optional<rules::AutoPause> auto_pause);

    /// How one approach ended.
    enum class ApproachOutcome : std::uint8_t {
      Settled, ///< A path was found, or none can be until the target moves.
      Blocked, ///< Claimed tiles stood in the way; try again next step.
    };

    /// Turn this step's input into the protagonist's orders; returns the refusals to toast.
    [[nodiscard]] std::vector<std::string> read_orders(const Engine &engine, const rules::CombatContext &context,
                                                       Clock clock);

    /// Move the gamepad's page on CombatPageNext/CombatPagePrev, wrapping within the pages.
    void read_pages(const Engine &engine);

    /// Press the slot each pressed key names on the current page; returns the refusals to toast.
    [[nodiscard]] std::vector<std::string> read_slot_presses(const Engine &engine, const rules::CombatContext &context);

    /// Turn held movement keys and a click into a move or attack order, appending any refusals.
    void read_move_orders(const Engine &engine, const rules::CombatContext &context, Clock clock,
                          std::vector<std::string> &refusals);

    /// Give the protagonist a standing attack on the hostile, re-planning its approach.
    void engage(const rules::CombatContext &context);

    /// The command key index for bar slot @p slot: 0 is Attack, then the commands, then the toggles.
    /// Attack renews the attack and drops a queued command; a command unqueues when queued, is
    /// refused when unavailable (returning why), and otherwise queues, engaging the hostile for an
    /// Enemy command; a toggle turns off when on, is refused when unavailable, and otherwise turns
    /// on.
    [[nodiscard]] std::optional<std::string> press_slot(const rules::CombatContext &context, std::size_t slot);

    /// Whether a wind-up in @p started calls for an auto-pause; sets pause_note_ when one does.
    [[nodiscard]] bool check_auto_pause(std::span<const std::size_t> started);

    /// Plan each ordered combatant's movement: stand when in reach or winding up, else approach.
    void plan_movement(Engine &engine, const world::MapView &map, const rules::CombatContext &context);

    /// Plan combatant @p index's movement: stand when in reach or winding up, else approach.
    void plan_combatant(Engine &engine, const world::MapView &map, const rules::CombatContext &context,
                        std::size_t index);

    /// Whether combatant @p index is still in the fight: in the scene, and able to act.
    [[nodiscard]] bool is_fighting(const rules::CombatContext &context, std::size_t index) const;

    /// Whether a combatant other than @p mover stands on @p tile or is walking to it.
    [[nodiscard]] bool is_claimed(const Engine &engine, world::TileCoord tile, std::size_t mover) const;

    /// Remove every hostile that can no longer act from the scene.
    void remove_fallen(Engine &engine, const rules::CombatContext &context);

    /// Stop combatant @p index: clear its path and forget what it was planned for.
    void stand_still(Engine &engine, std::size_t index);

    /// Path combatant @p mover to the nearest tile within reach of combatant @p target_index that
    /// nobody else has claimed.
    [[nodiscard]] ApproachOutcome approach(Engine &engine, const world::MapView &map, std::size_t mover,
                                           std::size_t target_index);

    /// Drive each hostile that can act along its planned path.
    void walk_hostiles(Engine &engine, const world::MapView &map, const rules::CombatContext &context);

    Encounter encounter_;

    std::vector<std::optional<entities::EntityId>> entities_; ///< nullopt once a hostile has fallen and left the scene.

    std::string first_actor_;

    std::optional<rules::AutoPause> auto_pause_;

    std::vector<world::TileCoord> tiles_;

    screens::CombatHud hud_;

    std::vector<std::vector<world::TileCoord>> paths_; ///< Each hostile's path; the protagonist uses Scene::path.

    std::vector<std::optional<world::TileCoord>> planned_for_; ///< The target tile each path was planned for.

    std::string pause_note_;

    int page_{}; ///< The command-bar page a gamepad's A/Y/B press; below hud_.page.count.
  };

} // namespace corundum::gameplay::combat
