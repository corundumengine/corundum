#include <corundum/core/math/vec.hpp>
#include <corundum/debug/debug_overlay.hpp>
#include <corundum/gameplay/ecs/world.hpp>
#include <corundum/platform/renderer.hpp>

#include <array>
#include <cstddef>
#include <format>
#include <string>
#include <utility>

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

    [[nodiscard]] constexpr std::string_view facing_name(gameplay::ecs::FacingDir d) noexcept {
      using gameplay::ecs::FacingDir;
      switch (d) {
      case FacingDir::South:
        return "South";
      case FacingDir::North:
        return "North";
      case FacingDir::East:
        return "East";
      case FacingDir::West:
        return "West";
      case FacingDir::NorthEast:
        return "NE";
      case FacingDir::SouthEast:
        return "SE";
      case FacingDir::SouthWest:
        return "SW";
      case FacingDir::NorthWest:
        return "NW";
      }
      std::unreachable();
    }

    constexpr float k_tile_edge_eps = 0.001f;
    constexpr float k_fps_ema_alpha = 0.05f;

  } // namespace

  void draw_collision(platform::Renderer &r, core::math::Vec2 camera, core::math::Vec2 viewport,
                      gameplay::world::tilemap::CollisionRectsView rects,
                      gameplay::world::tilemap::CollisionTrianglesView tris, IsoParams iso) noexcept {
    r.set_world_view(camera, viewport);

    if (iso.half_tw > 0.f && iso.half_th > 0.f) {
      constexpr float k_line_thickness = 2.f;

      auto draw_cart_rect = [&r, iso](float cart_x, float cart_y, float cart_w, float cart_h,
                                      core::math::Colour tile_colour) {
        const float dw = iso.half_tw * 2.f;
        const int col_start = static_cast<int>(cart_x / dw);
        const int col_end = static_cast<int>((cart_x + cart_w - k_tile_edge_eps) / dw);
        const int row_start = static_cast<int>(cart_y / dw);
        const int row_end = static_cast<int>((cart_y + cart_h - k_tile_edge_eps) / dw);

        for (int row = row_start; row <= row_end; ++row) {
          for (int col = col_start; col <= col_end; ++col) {
            const float tx = static_cast<float>(col) * dw;
            const float ty = static_cast<float>(row) * dw;
            const core::math::Vec2 v0 = core::math::cart_to_iso({tx, ty}, iso.half_tw, iso.half_th, iso.x_origin);
            const core::math::Vec2 v1 = core::math::cart_to_iso({tx + dw, ty}, iso.half_tw, iso.half_th, iso.x_origin);
            const core::math::Vec2 v2 =
                core::math::cart_to_iso({tx + dw, ty + dw}, iso.half_tw, iso.half_th, iso.x_origin);
            const core::math::Vec2 v3 = core::math::cart_to_iso({tx, ty + dw}, iso.half_tw, iso.half_th, iso.x_origin);
            // Offset by -half_th so diamonds render at grid-anchor (above tile sprites),
            // matching the Tilesmith editor convention.
            const float yo = -iso.half_th;
            r.draw(platform::DrawLine{.start = {v0.x, v0.y + yo},
                                      .end = {v1.x, v1.y + yo},
                                      .colour = tile_colour,
                                      .thickness = k_line_thickness});
            r.draw(platform::DrawLine{.start = {v1.x, v1.y + yo},
                                      .end = {v2.x, v2.y + yo},
                                      .colour = tile_colour,
                                      .thickness = k_line_thickness});
            r.draw(platform::DrawLine{.start = {v2.x, v2.y + yo},
                                      .end = {v3.x, v3.y + yo},
                                      .colour = tile_colour,
                                      .thickness = k_line_thickness});
            r.draw(platform::DrawLine{.start = {v3.x, v3.y + yo},
                                      .end = {v0.x, v0.y + yo},
                                      .colour = tile_colour,
                                      .thickness = k_line_thickness});
          }
        }
      };

      for (std::size_t i = 0; i < rects.size(); ++i)
        draw_cart_rect(rects.xs[i], rects.ys[i], rects.ws[i], rects.hs[i], k_rect_col);

      for (std::size_t i = 0; i < tris.size(); ++i)
        draw_cart_rect(tris.xs[i], tris.ys[i], tris.ws[i], tris.hs[i], k_tri_col);
    } else {
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
    }

    r.reset_screen_view();
  }

  void draw_hud(platform::Renderer &r, const HudData &d) noexcept {
    const float x = d.win_w - k_box_w - k_pad;

    std::array<std::string, 5> lines{
        std::format("FPS:  sim {:3.0f} / render {:3.0f}", d.sim_fps, d.render_fps),
        std::format("Position:  x ({:7.1f}), y ({:7.1f})", d.player_x, d.player_y),
        [&d] {
          std::string vel = std::format("Velocity:  dx ({:7.1f}), dy ({:7.1f})", d.player_dx, d.player_dy);
          if (d.player_has_facing)
            vel += std::format("  {}", facing_name(d.player_facing));
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
    for (const std::string &text : lines) {
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

  void draw_overlays(platform::Renderer &r, const OverlayInput &input, float &smoothed_fps) noexcept {
    const render::data::RenderState &render = input.render_state;
    const core::GameConfig &cfg = input.cfg;
    const gameplay::world::Scene &scene = input.scene;

    const core::math::Vec2 viewport{cfg.win_w, cfg.win_h};
    const core::math::Vec2 camera{scene.camera.x, scene.camera.y};

    float half_tw = 0.f;
    float half_th = 0.f;
    float x_origin = 0.f;
    if (render.mode == render::data::RenderMode::World && !render.active_chunks.empty()) {
      const gameplay::world::tilemap::Tilemap &first_tm = render.active_chunks[0].tilemap;
      half_tw = static_cast<float>(first_tm.diamond_w()) * cfg.tile_scale * 0.5f;
      half_th = static_cast<float>(first_tm.diamond_h()) * cfg.tile_scale * 0.5f;
      const int total_h = render.manifest.tiles_tall > 0 ? render.manifest.tiles_tall
                                                         : render.manifest.chunks_tall * render.manifest.chunk_size;
      x_origin = static_cast<float>(total_h - 1) * half_tw;
    } else if (render.mode == render::data::RenderMode::SingleMap && !render.map_data.tilemap.tilesets.empty()) {
      const gameplay::world::tilemap::Tilemap &tm = render.map_data.tilemap;
      half_tw = static_cast<float>(tm.diamond_w()) * cfg.tile_scale * 0.5f;
      half_th = static_cast<float>(tm.diamond_h()) * cfg.tile_scale * 0.5f;
      x_origin = static_cast<float>(tm.height - 1) * half_tw;
    }

    const IsoParams iso{.half_tw = half_tw, .half_th = half_th, .x_origin = x_origin};
    const render::data::CollisionGeometry geo = render::data::current_collisions(render);
    draw_collision(r, camera, viewport, geo.rects, geo.tris, iso);

    // Draw player collision bounding box in isometric space
    const gameplay::ecs::World &w = scene.ecs_world;
    const gameplay::ecs::EntityId p = scene.player;
    if (iso.half_tw > 0.f && iso.half_th > 0.f && w.transforms.has(p) && w.collisions.has(p)) {
      const gameplay::ecs::CollisionTable::Rect &bb = w.collisions.get_rect(p);
      const std::uint32_t slot = w.transforms.dense_idx(p);
      const float px = w.transforms.x[slot];
      const float py = w.transforms.y[slot];
      const float scale_ratio = static_cast<float>(cfg.sprite_scale) / static_cast<float>(cfg.tile_scale);
      const float feet_y = py + bb.yo * scale_ratio;

      // Draw a small diamond at the feet position — the collision anchor point
      constexpr float k_marker_hw = 5.f;
      constexpr float k_marker_hh = 3.f;
      const float mx = px;
      const float my = feet_y;
      constexpr core::math::Colour k_player_col{0, 255, 0, 220};
      constexpr float k_line_thickness = 2.f;
      r.draw(platform::DrawLine{.start = {mx, my - k_marker_hh},
                                .end = {mx + k_marker_hw, my},
                                .colour = k_player_col,
                                .thickness = k_line_thickness});
      r.draw(platform::DrawLine{.start = {mx + k_marker_hw, my},
                                .end = {mx, my + k_marker_hh},
                                .colour = k_player_col,
                                .thickness = k_line_thickness});
      r.draw(platform::DrawLine{.start = {mx, my + k_marker_hh},
                                .end = {mx - k_marker_hw, my},
                                .colour = k_player_col,
                                .thickness = k_line_thickness});
      r.draw(platform::DrawLine{.start = {mx - k_marker_hw, my},
                                .end = {mx, my - k_marker_hh},
                                .colour = k_player_col,
                                .thickness = k_line_thickness});
    }

    const float raw_fps = input.timer.last_frame_dt > 0.f ? 1.f / input.timer.last_frame_dt : 0.f;
    smoothed_fps += k_fps_ema_alpha * (raw_fps - smoothed_fps);
    HudData hud{
        .font_id = render.font_id,
        .win_w = cfg.win_w,
        .render_fps = smoothed_fps,
        .sim_fps = static_cast<float>(cfg.framerate),
    };
    if (w.transforms.has(p)) {
      hud.player_x = w.transforms.pos_x(p);
      hud.player_y = w.transforms.pos_y(p);
      const std::uint32_t di = w.transforms.dense_idx(p);
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
