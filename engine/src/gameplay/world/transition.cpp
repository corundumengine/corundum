#include <corundum/core/math/vec.hpp>
#include <corundum/engine.hpp>
#include <corundum/gameplay/ecs/components.hpp>
#include <corundum/gameplay/world/spawn.hpp>
#include <corundum/gameplay/world/transition.hpp>
#include <corundum/render/sys/render_sys.hpp>

#include <print>

namespace corundum::gameplay::world {

  void handle_map_transition(corundum::Engine &engine) noexcept {
    if (!engine.scene.pending_transition)
      return;
    const auto t = *engine.scene.pending_transition;
    engine.scene.pending_transition.reset();

    auto map_result = render::sys::load_map(*engine.renderer, engine.render, t.target_map, engine.cfg);
    if (!map_result) {
      std::println(stderr, "[engine] map transition failed: {}", map_result.error());
      request_quit(engine);
      engine.window->close();
      return;
    }

    const auto &new_tm = engine.render.map_data.tilemap;
    const float htw = static_cast<float>(new_tm.diamond_w()) * engine.cfg.tile_scale * 0.5f;
    const float hth = static_cast<float>(new_tm.diamond_h()) * engine.cfg.tile_scale * 0.5f;
    const float x_orig = static_cast<float>(new_tm.height) * htw;
    const auto spawn_pos = core::math::tile_to_world(t.spawn_col, t.spawn_row, 0, htw, hth, 0.f, x_orig);
    const gameplay::ecs::Position spawn{spawn_pos.x, spawn_pos.y};
    auto scene_result = gameplay::world::spawn_world(engine.cfg, engine.characters, new_tm, spawn);
    if (!scene_result) {
      std::println(stderr, "[engine] map transition failed: {}", scene_result.error());
      request_quit(engine);
      engine.window->close();
      return;
    }
    engine.scene = std::move(*scene_result);
    engine.scene.camera = {};
  }

} // namespace corundum::gameplay::world
