// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/entities/entity.hpp>
#include <corundum/entities/tables/clip_table.hpp>
#include <corundum/entities/world.hpp>
#include <corundum/gameplay/combat/encounter.hpp>
#include <corundum/gameplay/rules/rules.hpp>
#include <corundum/gameplay/screens/combat_hud.hpp>
#include <corundum/world/picking.hpp>

#include <cassert>
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

  /** @brief The clips one combatant's sheets offer; a sheet it doesn't have leaves its clip empty. */
  struct CombatantClips {
    std::optional<entities::Clip> attack{};

    std::optional<entities::Clip> die{};

    std::optional<entities::Clip> hurt{};
  };

  enum class Clock : std::uint8_t {
    Running, ///< Combat is on top: orders, the timeline and movement all run.
    Paused,  ///< CombatPaused is on top: commands, the next-target key and clicks on a hostile give orders.
  };

  /** @brief What one Battle step produced. */
  struct BattleStep {
    std::optional<Outcome> outcome{}; ///< A lost fight reports this only once the protagonist's death clip ends.

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
     *  is given a standing attack on the protagonist, and the protagonist one on the nearest (ties to
     *  the first named), so it fights without a click; a move order drops the protagonist's. The
     *  command bar is Attack, then the protagonist's commands, then its toggles, on CombatCommand1–9.
     *  @return The battle, or an error: no protagonist in the scene, an empty request
     *          ("[gameplay] start_encounter: it names no hostiles"), no live actor with an id
     *          ("[gameplay] start_encounter: no actor '<id>' in this area"), an actor with no
     *          position ("[gameplay] start_encounter: actor '<id>' has no position in this area"),
     *          one actor named twice ("[gameplay] start_encounter: actor '<id>' is named twice"),
     *          no hostile that can act ("[gameplay] start_encounter: no hostile in this fight can
     *          act"), more slots than the bar holds ("[gameplay] the command bar holds 9 slots; the
     *          rules list <n>"), or Encounter::start's. */
    [[nodiscard]] static std::expected<Battle, std::string> start(Engine &engine, const rules::CombatRules &rules,
                                                                  const EncounterRequest &request);

    /** @brief Run one fixed step:
     *  - this step's input becomes orders: a click, the slot keys (on a gamepad, A/Y/B press the
     *    shown page's slots, and L1/R1 change the page), the next-target key, and while Running
     *    held movement keys;
     *  - while Running the timeline advances, and a hostile's wind-up may call for an auto-pause;
     *  - a combatant with an attack order out of reach paths to a tile within reach of its target
     *    and walks there, and one in reach or winding up stands. One its status says can't move
     *    stands too, and the protagonist then takes no move order and Scene::is_player_held is set;
     *  - hud() refreshes;
     *  - a wind-up at an enemy plays the combatant's attack clip, facing its target, a fall in Health
     *    its hurt clip, and a fall its death clip; a character missing a sheet skips that clip;
     *  - from the step the protagonist falls, no orders are read and everyone stands until its death
     *    clip ends.
     *  While Paused nobody walks: no path is planned, and the protagonist's is cleared, so a paused
     *  fight freezes every combatant.
     *  Every combatant with a ready sheet stands on it from start until stand_down.
     *  @pre has_live_combatants(engine). */
    [[nodiscard]] BattleStep update(Engine &engine, Clock clock);

    /** @brief True while every combatant still in the fight has a live entity with a transform. */
    [[nodiscard]] bool has_live_combatants(const Engine &engine) const;

    /** @brief Stop every combatant: clear the player's path and zero the hostiles' velocity.
     *  @pre has_live_combatants(engine). */
    void halt(Engine &engine) const;

    /** @brief Take every combatant out of its combat stance.
     *  @pre The fight's scene is still loaded: an area change replaces it, and the ids name nothing.
     *  @note A combatant that has left the scene is skipped, so this needs no has_live_combatants(). */
    void stand_down(Engine &engine) const;

    [[nodiscard]] const Encounter &encounter() const noexcept { return encounter_; }

    /** @brief The actor named first; encounter_won and encounter_lost name it. */
    [[nodiscard]] std::string_view first_actor() const noexcept { return first_actor_; }

    /** @brief The fight's chosen hostile, by combatant index. It outlives an attack order, so a
     *  protagonist who walks off keeps it; when it falls, it moves to the nearest hostile still
     *  fighting. */
    [[nodiscard]] std::size_t target() const noexcept { return target_; }

    /** @brief The tile target() stands on, as of the last update; nullopt once it has fallen, which
     *  only outlasts the step it fell on when the protagonist fell too and its death clip plays. */
    [[nodiscard]] std::optional<world::TileCoord> target_tile() const {
      if (!entities_[target_])
        return std::nullopt;
      return tiles_[target_];
    }

    /** @brief The HUD's rows and command bar, refreshed by start() and update(). */
    [[nodiscard]] const screens::CombatHud &hud() const noexcept { return hud_; }

    /** @brief Why the fight last paused on its own, such as "Brann is winding up Crushing Blow";
     *  empty when it didn't, and cleared once it runs again. */
    [[nodiscard]] std::string_view pause_note() const noexcept { return pause_note_; }

  private:
    Battle(Encounter encounter, std::vector<std::optional<entities::EntityId>> entities,
           std::vector<CombatantClips> clips, std::string first_actor, std::optional<rules::AutoPause> auto_pause);

    /// How one approach ended.
    enum class ApproachOutcome : std::uint8_t {
      Settled, ///< A path was found, or none can be until the target moves.
      Blocked, ///< Claimed tiles stood in the way; try again next step.
      NoMap,   ///< There is no walkable map yet; try again next step.
    };

    /// Whether the protagonist had an attack order before a step's timeline ran. Read it first:
    /// the timeline drops an order whose target falls.
    enum class AttackOrder : std::uint8_t {
      Standing,
      None,
    };

    /// Turn this step's input into the protagonist's orders, appending the refusals to toast.
    void read_orders(const Engine &engine, const rules::CombatContext &context, Clock clock,
                     std::vector<std::string> &reports);

    /// Move the gamepad's page on CombatPageNext/CombatPagePrev, wrapping within the pages.
    void read_pages(const Engine &engine);

    /// Read every combatant still in the scene's tile into tiles_.
    void refresh_tiles(const Engine &engine);

    /// Give each side its standing attack on the other, point the fight at @p target and refresh the
    /// HUD. @pre refresh_tiles() has run, and @p target is a hostile that start() found able to act.
    void open_fight(Engine &engine, std::size_t target);

    /// Press the slot each pressed key names on the current page, appending the refusals to toast.
    void read_slot_presses(const Engine &engine, const rules::CombatContext &context,
                           std::vector<std::string> &refusals);

    /// Turn held movement keys and a click into a move or attack order, appending any refusals.
    void read_move_orders(const Engine &engine, const rules::CombatContext &context, Clock clock,
                          std::vector<std::string> &refusals);

    /// Give the protagonist a standing attack on target(), re-planning its approach.
    void engage(const rules::CombatContext &context);

    /// Target @p hostile and attack it.
    void engage(const rules::CombatContext &context, std::size_t hostile);

    /// On CombatTargetNext, attack the next hostile still fighting after target(), wrapping.
    void read_next_target(const Engine &engine, const rules::CombatContext &context);

    /// The hostile still fighting that's nearest the protagonist, ties to the first named; nullopt
    /// when none is.
    [[nodiscard]] std::optional<std::size_t> nearest_hostile(const rules::CombatContext &context) const;

    /// The hostile still fighting that a click on @p clicked means: one standing there, else the
    /// first whose sprite covers it; nullopt for none.
    [[nodiscard]] std::optional<std::size_t> hostile_at(world::TileCoord clicked,
                                                        const rules::CombatContext &context) const;

    /// When target() has fallen, move it to the nearest hostile, attacking that one when
    /// @p before is AttackOrder::Standing.
    void follow_fallen_target(const rules::CombatContext &context, AttackOrder before);

    /// The command key index for bar slot @p slot: 0 is Attack, then the commands, then the toggles.
    /// The bar's layout and the rules' lists keep one length for the fight, so a slot names one
    /// option. Attack renews the attack and drops a queued command; a command unqueues when queued,
    /// is refused when unavailable (returning why), and otherwise queues, engaging the hostile for
    /// an Enemy command; a toggle turns off when on, is refused when unavailable, and otherwise
    /// turns on.
    [[nodiscard]] std::optional<std::string> press_slot(const rules::CombatContext &context, std::size_t slot);

    /// Whether a wind-up in @p started calls for an auto-pause; sets pause_note_ when one does.
    [[nodiscard]] bool check_auto_pause(std::span<const std::size_t> started);

    /// Plan each ordered combatant's movement: stand when in reach or winding up, else approach.
    void plan_movement(Engine &engine, const world::MapView &map, const rules::CombatContext &context);

    /// Plan combatant @p index's movement: stand when in reach or winding up, else approach.
    void plan_combatant(Engine &engine, const world::MapView &map, const rules::CombatContext &context,
                        std::size_t index);

    /// Combatant @p index's entity while it is still in the fight: in the scene, and able to act.
    [[nodiscard]] std::optional<entities::EntityId> fighting_entity(const rules::CombatContext &context,
                                                                    std::size_t index) const;

    /// Whether combatant @p index is still in the fight: in the scene, and able to act.
    [[nodiscard]] bool is_fighting(const rules::CombatContext &context, std::size_t index) const;

    /// Whether a combatant other than @p mover stands on @p tile or is walking to it.
    [[nodiscard]] bool is_claimed(const Engine &engine, world::TileCoord tile, std::size_t mover) const;

    /// Remove every hostile that can no longer act from the scene.
    void remove_fallen(Engine &engine, const rules::CombatContext &context);

    /// Play each clip this step calls for: attacks for the wind-ups in @p windups_started, then hurts.
    void play_step_clips(Engine &engine, const rules::CombatContext &context,
                         std::span<const std::size_t> windups_started);

    /// Turn combatant @p index toward its target and play its attack clip, timed so the clip's middle
    /// meets the landing. Does nothing for a Self command or a character with no attack sheet.
    void play_attack_clip(entities::World &world, std::size_t index);

    /// Play the hurt clip of each combatant whose Health fell since the last call and who can show
    /// it: still fighting, and not winding up.
    void play_hurt_clips(entities::World &world, const rules::CombatContext &context);

    /// When the protagonist has fallen, play its death clip.
    void play_protagonist_death(Engine &engine, const rules::CombatContext &context);

    /// The outcome to report: none yet while a lost fight's protagonist plays its death clip.
    [[nodiscard]] std::optional<Outcome> finished_outcome(const Engine &engine) const;

    /// Stop everyone and keep the protagonist from moving: the fight is lost.
    void hold_for_defeat(Engine &engine) const;

    /// Stop combatant @p index: clear its path and forget what it was planned for.
    void stand_still(Engine &engine, std::size_t index);

    /// Path combatant @p mover to the nearest tile within reach of combatant @p target_index that
    /// nobody else has claimed.
    [[nodiscard]] ApproachOutcome approach(Engine &engine, const world::MapView &map, std::size_t mover,
                                           entities::EntityId mover_entity, std::size_t target_index);

    /// Drive each hostile that can act along its planned path.
    void walk_hostiles(Engine &engine, const world::MapView &map, const rules::CombatContext &context);

    Encounter encounter_;

    std::vector<std::optional<entities::EntityId>> entities_; ///< nullopt once a hostile has fallen and left the scene.

    std::vector<CombatantClips> clips_;

    std::vector<int> healths_; ///< Each combatant's Health at the last play_hurt_clips.

    std::string first_actor_;

    std::optional<rules::AutoPause> auto_pause_;

    std::vector<world::TileCoord> tiles_;

    screens::CombatHud hud_;

    std::vector<std::vector<world::TileCoord>> paths_; ///< Each hostile's path; the protagonist uses Scene::path.

    std::vector<std::optional<world::TileCoord>> planned_for_; ///< The target tile each path was planned for.

    std::string pause_note_;

    std::size_t target_{k_hostile};

    std::size_t page_{}; ///< The command-bar page a gamepad's A/Y/B press; below hud_.page.count.
  };

} // namespace corundum::gameplay::combat
