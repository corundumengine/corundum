#include <corundum/engine.hpp>
#include <corundum/gameplay/component/components.hpp>
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

    const auto &new_tm = *active_tilemap(engine);
    const gameplay::component::Position spawn{static_cast<float>(t.spawn_col), static_cast<float>(t.spawn_row)};
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
