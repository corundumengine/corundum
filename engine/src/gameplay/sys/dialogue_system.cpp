#include <corundum/gameplay/dialogue/system.hpp>
#include <corundum/gameplay/ecs/components.hpp>
#include <corundum/gameplay/sys/dialogue_system.hpp>
#include <corundum/resources/sprite.hpp>

#include <cmath>
#include <utility>

namespace corundum::gameplay::sys {

  namespace {

    /// Map facing direction to the corresponding directional AnimId.
    inline constexpr std::array<corundum::resources::AnimId, 8> k_dir_to_anim = {
        corundum::resources::AnimId::South,     corundum::resources::AnimId::North,
        corundum::resources::AnimId::East,      corundum::resources::AnimId::West,
        corundum::resources::AnimId::NorthEast, corundum::resources::AnimId::SouthEast,
        corundum::resources::AnimId::SouthWest, corundum::resources::AnimId::NorthWest,
    };

    [[nodiscard]] constexpr corundum::resources::AnimId dir_to_anim(corundum::gameplay::ecs::FacingDir dir) noexcept {
      return k_dir_to_anim[std::to_underlying(dir)];
    }

  } // namespace

  void update_dialogue(corundum::gameplay::world::Scene &scene,
                       const corundum::input::PressedActions &actions) noexcept {
    using corundum::gameplay::ecs::EntityId;

    static_cast<void>(corundum::gameplay::dialogue::system(scene.dialogue, actions, scene.flags));
    if (!scene.dialogue.active) {
      if (scene.dialogue_npc) {
        auto &world = scene.ecs_world;
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
    using corundum::gameplay::ecs::distance;
    using corundum::gameplay::ecs::EntityId;
    using corundum::gameplay::ecs::Position;

    if (!input.is_pressed(corundum::input::Action::Select))
      return;

    const auto p_slot = scene.ecs_world.transforms.dense_idx(scene.player);
    const float player_x = scene.ecs_world.transforms.x[p_slot];
    const float player_y = scene.ecs_world.transforms.y[p_slot];

    for (EntityId eid : scene.ecs_world.dialogue_refs.active_entities()) {
      if (!scene.ecs_world.transforms.has(eid))
        continue;

      const auto n_slot = scene.ecs_world.transforms.dense_idx(eid);
      const float npc_x = scene.ecs_world.transforms.x[n_slot];
      const float npc_y = scene.ecs_world.transforms.y[n_slot];

      const Position player_pos{player_x, player_y};
      const Position npc_pos{npc_x, npc_y};
      if (distance(player_pos, npc_pos) > cfg.interact_radius)
        continue;

      auto &world = scene.ecs_world;
      const EntityId player = scene.player;
      corundum::gameplay::ecs::FacingDir toward_npc = corundum::gameplay::ecs::FacingDir::South;
      if (world.facings.has(player)) {
        const float fdx = npc_x - player_x;
        const float fdy = npc_y - player_y;
        const float abs_fdx = std::abs(fdx);
        const float abs_fdy = std::abs(fdy);
        using FD = corundum::gameplay::ecs::FacingDir;
        if (abs_fdy > 2.f * abs_fdx)
          toward_npc = fdy > 0.f ? FD::South : FD::North;
        else if (abs_fdx > 2.f * abs_fdy)
          toward_npc = fdx > 0.f ? FD::East : FD::West;
        else if (fdx > 0.f)
          toward_npc = fdy > 0.f ? FD::SouthEast : FD::NorthEast;
        else
          toward_npc = fdy > 0.f ? FD::SouthWest : FD::NorthWest;
        if (world.facings.dir_of(player) != toward_npc)
          continue;
      }

      const auto *graph = graphs.find(scene.ecs_world.dialogue_refs.get_graph_id(eid));
      if (!graph)
        continue;

      if (world.facings.has(eid)) {
        scene.dialogue_npc_saved_facing = world.facings.dir_of(eid);
        const corundum::gameplay::ecs::FacingDir new_dir = corundum::gameplay::ecs::opposite(toward_npc);
        world.facings.dir_ref(eid) = new_dir;
        if (world.sprites.has(eid) && world.animations.has(eid)) {
          scene.dialogue_npc_saved_anim = world.sprites.anim_id_ref(eid);
          const corundum::resources::AnimId dir_anim = dir_to_anim(new_dir);
          const bool has_dir_frames = world.animations.frame_count(eid, dir_anim) > 0;
          world.sprites.anim_id_ref(eid) = has_dir_frames ? dir_anim : corundum::resources::AnimId::Default;
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
