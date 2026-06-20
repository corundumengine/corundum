#include <corundum/gameplay/sys/camera_system.hpp>
#include <corundum/gameplay/world/update.hpp>

#include <algorithm>

namespace corundum::gameplay::sys {

  void follow_player(corundum::gameplay::world::Camera &camera, float player_x, float player_y,
                     const corundum::gameplay::world::MapView &map, float win_w, float win_h) noexcept {
    constexpr float k_horiz_margin = 200.0f;
    constexpr float k_vert_margin = 150.0f;

    if (map.world_w_px <= win_w) {
      camera.x = (map.world_w_px - win_w) * 0.5f;
    } else {
      const float player_screen_x = player_x - camera.x;
      if (player_screen_x < k_horiz_margin)
        camera.x = player_x - k_horiz_margin;
      else if (player_screen_x > win_w - k_horiz_margin)
        camera.x = player_x - (win_w - k_horiz_margin);
      camera.x = std::clamp(camera.x, 0.f, map.world_w_px - win_w);
    }

    if (map.world_h_px <= win_h) {
      camera.y = (map.world_h_px - win_h) * 0.5f;
    } else {
      const float player_screen_y = player_y - camera.y;
      if (player_screen_y < k_vert_margin)
        camera.y = player_y - k_vert_margin;
      else if (player_screen_y > win_h - k_vert_margin)
        camera.y = player_y - (win_h - k_vert_margin);
      camera.y = std::clamp(camera.y, 0.f, map.world_h_px - win_h);
    }
  }

} // namespace corundum::gameplay::sys
