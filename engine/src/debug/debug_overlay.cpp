#include <corundum/debug/debug_overlay.hpp>
#include <corundum/gameplay/ecs/world.hpp>
#include <corundum/platform/renderer.hpp>

#include <array>
#include <cstddef>
#include <format>
#include <string>

namespace corundum::debug {

  namespace {

    constexpr core::math::Colour k_rect_col{220, 60, 60, 80};
    constexpr core::math::Colour k_tri_col{60, 100, 220, 80};
    constexpr core::math::Colour k_hud_bg{0, 0, 0, 75};
    constexpr core::math::Colour k_hud_text{220, 220, 200, 255};

    constexpr float k_y = 10.f;
    constexpr uint32_t k_font_sz = 16;
    constexpr float k_line_h = 20.f;
    constexpr float k_pad = 8.f;
    constexpr float k_box_w = 275.f;

    constexpr std::array<std::string_view, 8> k_facing_names{"South", "North", "East", "West", "NE", "SE", "SW", "NW"};

  } // namespace

  void draw_collision(platform::Renderer &r, core::math::Vec2 camera, core::math::Vec2 viewport,
                      gameplay::world::tilemap::CollisionRectsView rects,
                      gameplay::world::tilemap::CollisionTrianglesView tris) noexcept {
    r.set_world_view(camera, viewport);

    for (std::size_t i = 0; i < rects.size(); ++i) {
      r.draw(platform::DrawRect{
          .position = {rects.xs[i], rects.ys[i]},
          .size = {rects.ws[i], rects.hs[i]},
          .colour = k_rect_col,
      });
    }

    for (std::size_t i = 0; i < tris.size(); ++i) {
      r.draw(platform::DrawRect{
          .position = {tris.xs[i], tris.ys[i]},
          .size = {tris.ws[i], tris.hs[i]},
          .colour = k_tri_col,
      });
    }

    r.reset_screen_view();
  }

  void draw_hud(platform::Renderer &r, const HudData &d) noexcept {
    const float x = d.win_w - k_box_w - k_pad;

    std::array<std::string, 5> lines{
        std::format("FPS:  sim {:3.0f} / render {:3.0f}", d.sim_fps, d.render_fps),
        std::format("Position:  x ({:7.1f}), y ({:7.1f})", d.player_x, d.player_y),
        [&] {
          std::string vel = std::format("Velocity:  dx ({:7.1f}), dy ({:7.1f})", d.player_dx, d.player_dy);
          if (d.player_has_facing)
            vel += std::format("  {}", k_facing_names[std::to_underlying(d.player_facing)]);
          return vel;
        }(),
        std::format("Camera:  x ({:7.1f}), y ({:7.1f})", d.camera_x, d.camera_y),
        std::format("Stats:  chunk:{}  col rect:{}  col tri:{}  ent:{}", d.active_chunks, d.collision_rects,
                    d.collision_tris, d.entity_count),
    };

    r.draw(platform::DrawRect{
        .position = {x - k_pad, k_y - k_pad},
        .size = {k_box_w + 2.f * k_pad, static_cast<float>(lines.size()) * k_line_h + k_pad * 2.f},
        .colour = k_hud_bg,
    });

    float y = k_y;
    for (const auto &text : lines) {
      r.draw(platform::DrawText{
          .font_id = d.font_id,
          .text = text,
          .position = {x, y},
          .char_size = k_font_sz,
          .colour = k_hud_text,
      });
      y += k_line_h;
    }
  }

  void draw_overlays(platform::Renderer &r, OverlayInput &input) noexcept {
    const auto &render = input.render_state;
    const auto &cfg = input.cfg;
    const auto &scene = input.scene;

    const core::math::Vec2 viewport{cfg.win_w, cfg.win_h};
    const core::math::Vec2 camera{scene.camera.x, scene.camera.y};

    const auto geo = render::data::current_collisions(render);
    draw_collision(r, camera, viewport, geo.rects, geo.tris);

    const auto &w = scene.ecs_world;
    const auto p = scene.player;
    const float raw_fps = input.timer.last_frame_dt > 0.f ? 1.f / input.timer.last_frame_dt : 0.f;
    input.smoothed_fps += 0.05f * (raw_fps - input.smoothed_fps);
    HudData hud{
        .font_id = render.font_id,
        .win_w = cfg.win_w,
        .render_fps = input.smoothed_fps,
        .sim_fps = static_cast<float>(cfg.framerate),
    };
    if (w.transforms.has(p)) {
      hud.player_x = w.transforms.pos_x(p);
      hud.player_y = w.transforms.pos_y(p);
      const auto di = w.transforms.dense_idx(p);
      hud.player_dx = w.transforms.dx[di];
      hud.player_dy = w.transforms.dy[di];
    }
    if (w.facings.has(p)) {
      hud.player_has_facing = true;
      hud.player_facing = w.facings.dir_of(p);
    }
    hud.camera_x = camera.x;
    hud.camera_y = camera.y;
    hud.active_chunks = static_cast<int>(render.active_chunks.size());
    hud.collision_rects = static_cast<int>(geo.rects.size());
    hud.collision_tris = static_cast<int>(geo.tris.size());
    hud.entity_count = static_cast<int>(w.entities.alive());
    draw_hud(r, hud);
  }

} // namespace corundum::debug
