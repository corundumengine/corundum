#include <corundum/gameplay/component/components.hpp>
#include <corundum/gameplay/dialogue/system.hpp>
#include <corundum/gameplay/sys/dialogue_system.hpp>
#include <corundum/resources/sprite.hpp>

#include <cmath>
#include <cstdint>
#include <utility>

namespace corundum::gameplay::sys {

  namespace {

    using corundum::gameplay::component::FacingDir;
    using corundum::resources::AnimId;

    /** @brief Ratio above which the dominant axis is considered "cardinal"
     *  rather than diagonal when computing facing direction. */
    inline constexpr float k_cardinal_dominance_ratio = 2.f;

    /// Map facing direction to the corresponding directional AnimId.
    inline constexpr std::array<AnimId, 8> k_dir_to_anim = {
        AnimId::South,     AnimId::North,     AnimId::East,      AnimId::West,
        AnimId::NorthEast, AnimId::SouthEast, AnimId::SouthWest, AnimId::NorthWest,
    };

    [[nodiscard]] constexpr AnimId dir_to_anim(FacingDir dir) noexcept {
      return k_dir_to_anim[std::to_underlying(dir)];
    }

    /// Classify a tile-grid displacement (dx=Δcol, dy=Δrow) into the nearest screen-space FacingDir.
    /// The isometric projection rotates the grid axes 45° relative to screen space,
    /// so tile-cardinal directions (pure dc/dr) map to screen-intercardinal and vice versa:
    ///   Tile SE (+,+) → screen South,  Tile NW (-,-) → screen North,
    ///   Tile NE (+,-) → screen East,   Tile SW (-,+) → screen West.
    [[nodiscard]] FacingDir dir_from_delta(float dx, float dy) noexcept {
      const float ax = std::abs(dx);
      const float ay = std::abs(dy);
      if (ay > k_cardinal_dominance_ratio * ax)
        return dy > 0.f ? FacingDir::SouthWest : FacingDir::NorthEast;
      if (ax > k_cardinal_dominance_ratio * ay)
        return dx > 0.f ? FacingDir::SouthEast : FacingDir::NorthWest;
      if (dx > 0.f)
        return dy > 0.f ? FacingDir::South : FacingDir::East;
      return dy > 0.f ? FacingDir::West : FacingDir::North;
    }

  } // namespace

  void update_dialogue(corundum::gameplay::world::Scene &scene, const corundum::input::PressedActions &actions,
                       const quest::Registry *quests) noexcept {
    using corundum::gameplay::entity::EntityId;
    using corundum::gameplay::entity::World;

    scene.pending_dialogue_events = corundum::gameplay::dialogue::system(scene.dialogue, actions, scene.flags, quests);
    if (!scene.dialogue.active) {
      if (scene.dialogue_npc) {
        World &world = scene.world;
        const EntityId npc = *scene.dialogue_npc;
        if (scene.dialogue_npc_saved_facing && world.facings.has(npc))
          world.facings.dir_ref(npc) = *scene.dialogue_npc_saved_facing;
        if (scene.dialogue_npc_saved_anim && world.sprites.has(npc)) {
          world.sprites.anim_id_ref(npc) = *scene.dialogue_npc_saved_anim;
          world.sprites.frame_index_ref(npc) = 0;
        }
      }
      scene.dialogue_npc.reset();
      scene.dialogue_npc_saved_facing.reset();
      scene.dialogue_npc_saved_anim.reset();
      scene.mode = corundum::gameplay::world::GameMode::Exploring;
    }
  }

  void try_interact(corundum::gameplay::world::Scene &scene, const corundum::input::InputState &input,
                    const corundum::core::GameConfig &cfg,
                    const corundum::gameplay::dialogue::Registry &graphs) noexcept {
    using corundum::gameplay::component::distance;
    using corundum::gameplay::component::Position;
    using corundum::gameplay::dialogue::Graph;
    using corundum::gameplay::entity::EntityId;
    using corundum::gameplay::entity::World;

    if (!input.is_pressed(corundum::input::Action::Select))
      return;

    World &world = scene.world;
    const std::uint32_t p_slot = world.transforms.dense_idx(scene.player);
    const float player_col = world.transforms.col[p_slot];
    const float player_row = world.transforms.row[p_slot];

    for (EntityId eid : world.dialogue_refs.active_entities()) {
      if (!world.transforms.has(eid))
        continue;

      const std::uint32_t n_slot = world.transforms.dense_idx(eid);
      const float npc_col = world.transforms.col[n_slot];
      const float npc_row = world.transforms.row[n_slot];

      if (distance(Position{player_col, player_row}, Position{npc_col, npc_row}) > cfg.interact_radius)
        continue;

      const Graph *const graph = graphs.find(world.dialogue_refs.get_graph_id(eid));
      if (!graph)
        continue;

      const FacingDir toward_npc = dir_from_delta(npc_col - player_col, npc_row - player_row);

      if (world.facings.has(eid)) {
        scene.dialogue_npc_saved_facing = world.facings.dir_of(eid);
        const FacingDir face_player = corundum::gameplay::component::opposite(toward_npc);
        world.facings.dir_ref(eid) = face_player;
        if (world.sprites.has(eid) && world.animations.has(eid)) {
          scene.dialogue_npc_saved_anim = world.sprites.anim_id_ref(eid);
          const AnimId dir_anim = dir_to_anim(face_player);
          const bool has_dir_anim = world.animations.frame_count(eid, dir_anim) > 0;
          world.sprites.anim_id_ref(eid) = has_dir_anim ? dir_anim : AnimId::Default;
          world.sprites.frame_index_ref(eid) = 0;
        }
      }

      scene.dialogue_npc = eid;
      corundum::gameplay::dialogue::start(scene.dialogue, *graph, scene.flags);
      scene.mode = corundum::gameplay::world::GameMode::Dialogue;
      break;
    }
  }

} // namespace corundum::gameplay::sys
