#include <corundum/gameplay/world/update.hpp>

#include <corundum/anim/sys/anim_sys.hpp>
#include <corundum/gameplay/component/components.hpp>
#include <corundum/gameplay/entity/world.hpp>
#include <corundum/gameplay/sys/camera_system.hpp>
#include <corundum/gameplay/sys/dialogue_system.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/physics/sys/physics_sys.hpp>
#include <corundum/resources/sprite.hpp>

#include <array>

namespace {

  void update_exploring(corundum::gameplay::world::Scene &scene, const corundum::input::InputState &input,
                        const corundum::gameplay::world::MapView &map, const corundum::core::GameConfig &cfg,
                        float dt) {
    using corundum::gameplay::entity::EntityId;

    auto &world = scene.world;
    const EntityId player = scene.player;

    corundum::physics::sys::update_player(world.transforms, world.collisions, player, input, cfg.player_speed, map,
                                          scene, dt);
    corundum::anim::sys::animate(world.sprites, world.transforms, world.animations, world.facings, world.motion_sprites,
                                 dt);

    const auto p_slot = world.transforms.dense_idx(player);
    const float pc = world.transforms.col[p_slot];
    const float pr = world.transforms.row[p_slot];
    const float iso_x = (pc - pr) * map.half_tw + map.x_origin;
    const float iso_y = (pc + pr) * map.half_th;
    corundum::gameplay::sys::follow_player(scene.camera, iso_x, iso_y, map, cfg.win_w, cfg.win_h);
  }

} // namespace

namespace corundum::gameplay::world {

  void update(corundum::gameplay::world::Scene &scene, const corundum::core::GameConfig &cfg,
              const corundum::gameplay::dialogue::Registry &graphs, const corundum::input::InputState &input,
              const MapView &map, float dt, const quest::Registry *quests) {
    const auto actions = corundum::input::pressed_actions(input);

    if (scene.mode == corundum::gameplay::world::GameMode::Dialogue) [[unlikely]] {
      corundum::gameplay::sys::update_dialogue(scene, actions, quests);
    } else [[likely]] {
      update_exploring(scene, input, map, cfg, dt);
      corundum::gameplay::sys::try_interact(scene, input, cfg, graphs);
    }
  }

} // namespace corundum::gameplay::world
