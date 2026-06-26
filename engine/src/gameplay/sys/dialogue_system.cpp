#include <corundum/gameplay/dialogue/system.hpp>
#include <corundum/gameplay/ecs/components.hpp>
#include <corundum/gameplay/sys/dialogue_system.hpp>
#include <corundum/resources/sprite.hpp>

#include <cmath>
#include <cstdint>
#include <utility>

namespace corundum::gameplay::sys {

  namespace {

    using corundum::gameplay::ecs::FacingDir;
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

    /// Classify a world-space displacement (dx, dy) into the nearest FacingDir.
    [[nodiscard]] FacingDir dir_from_delta(float dx, float dy) noexcept {
      const float ax = std::abs(dx);
      const float ay = std::abs(dy);
      if (ay > k_cardinal_dominance_ratio * ax)
        return dy > 0.f ? FacingDir::South : FacingDir::North;
      if (ax > k_cardinal_dominance_ratio * ay)
        return dx > 0.f ? FacingDir::East : FacingDir::West;
      if (dx > 0.f)
        return dy > 0.f ? FacingDir::SouthEast : FacingDir::NorthEast;
      return dy > 0.f ? FacingDir::SouthWest : FacingDir::NorthWest;
    }

  } // namespace

  void update_dialogue(corundum::gameplay::world::Scene &scene,
                       const corundum::input::PressedActions &actions) noexcept {
    using corundum::gameplay::ecs::EntityId;
    using corundum::gameplay::ecs::World;

    scene.pending_dialogue_events = corundum::gameplay::dialogue::system(scene.dialogue, actions, scene.flags);
    if (!scene.dialogue.active) {
      if (scene.dialogue_npc) {
        World &world = scene.ecs_world;
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
    using corundum::gameplay::dialogue::Graph;
    using corundum::gameplay::ecs::distance;
    using corundum::gameplay::ecs::EntityId;
    using corundum::gameplay::ecs::Position;
    using corundum::gameplay::ecs::World;

    if (!input.is_pressed(corundum::input::Action::Select))
      return;

    World &world = scene.ecs_world;
    const std::uint32_t p_slot = world.transforms.dense_idx(scene.player);
    const float player_x = world.transforms.x[p_slot];
    const float player_y = world.transforms.y[p_slot];

    for (EntityId eid : world.dialogue_refs.active_entities()) {
      if (!world.transforms.has(eid))
        continue;

      const std::uint32_t n_slot = world.transforms.dense_idx(eid);
      const float npc_x = world.transforms.x[n_slot];
      const float npc_y = world.transforms.y[n_slot];

      if (distance(Position{player_x, player_y}, Position{npc_x, npc_y}) > cfg.interact_radius)
        continue;

      const Graph *const graph = graphs.find(world.dialogue_refs.get_graph_id(eid));
      if (!graph)
        continue;

      const FacingDir toward_npc = dir_from_delta(npc_x - player_x, npc_y - player_y);

      if (world.facings.has(eid)) {
        scene.dialogue_npc_saved_facing = world.facings.dir_of(eid);
        const FacingDir face_player = corundum::gameplay::ecs::opposite(toward_npc);
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
