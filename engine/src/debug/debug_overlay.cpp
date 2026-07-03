#include <corundum/core/math/vec.hpp>
#include <corundum/debug/debug_overlay.hpp>
#include <corundum/gameplay/entity/world.hpp>
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

    [[nodiscard]] constexpr std::string_view facing_name(gameplay::component::FacingDir d) noexcept {
      using gameplay::component::FacingDir;
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

    constexpr float k_fps_ema_alpha = 0.05f;

  } // namespace

  void draw_collision(platform::Renderer &r, core::math::Vec2 camera, core::math::Vec2 viewport,
                      gameplay::world::tilemap::CollisionRectsView rects,
                      gameplay::world::tilemap::CollisionTrianglesView tris, core::math::IsoParams iso) noexcept {
    r.set_world_view(camera, viewport);

    if (iso.half_tw > 0.f && iso.half_th > 0.f) {
      constexpr float k_line_thickness = 2.f;

      // Convert a tile-grid rect's four corners to isometric and draw as a diamond.
      auto draw_tile_rect = [&r, iso](float col, float row, float col_span, float row_span, core::math::Colour colour) {
        const float x0 = (col - row) * iso.half_tw + iso.x_origin;
        const float y0 = (col + row) * iso.half_th;
        const float x1 = ((col + col_span) - row) * iso.half_tw + iso.x_origin;
        const float y1 = ((col + col_span) + row) * iso.half_th;
        const float x2 = ((col + col_span) - (row + row_span)) * iso.half_tw + iso.x_origin;
        const float y2 = ((col + col_span) + (row + row_span)) * iso.half_th;
        const float x3 = (col - (row + row_span)) * iso.half_tw + iso.x_origin;
        const float y3 = (col + (row + row_span)) * iso.half_th;
        const float offset = -iso.half_th;
        r.draw(platform::DrawLine{
            .start = {x0, y0 + offset}, .end = {x1, y1 + offset}, .colour = colour, .thickness = k_line_thickness});
        r.draw(platform::DrawLine{
            .start = {x1, y1 + offset}, .end = {x2, y2 + offset}, .colour = colour, .thickness = k_line_thickness});
        r.draw(platform::DrawLine{
            .start = {x2, y2 + offset}, .end = {x3, y3 + offset}, .colour = colour, .thickness = k_line_thickness});
        r.draw(platform::DrawLine{
            .start = {x3, y3 + offset}, .end = {x0, y0 + offset}, .colour = colour, .thickness = k_line_thickness});
      };

      // Full-tile collisions — the red diamonds you see in keystone.
      for (std::size_t i = 0; i < rects.size(); ++i)
        draw_tile_rect(rects.cols[i], rects.rows[i], rects.col_spans[i], rects.row_spans[i], k_rect_col);

      // Half-tile diagonal collisions (not used in keystone).
      for (std::size_t i = 0; i < tris.size(); ++i)
        draw_tile_rect(tris.cols[i], tris.rows[i], tris.col_spans[i], tris.row_spans[i], k_tri_col);
    } else {
      for (std::size_t i = 0; i < rects.size(); ++i) {
        r.draw(platform::DrawRect{
            .position = {rects.cols[i], rects.rows[i]},
            .size = {rects.col_spans[i], rects.row_spans[i]},
            .colour = k_rect_col,
        });
      }

      for (std::size_t i = 0; i < tris.size(); ++i) {
        r.draw(platform::DrawRect{
            .position = {tris.cols[i], tris.rows[i]},
            .size = {tris.col_spans[i], tris.row_spans[i]},
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
        std::format("Grid:  col ({:7.1f}), row ({:7.1f})", d.player_col, d.player_row),
        [&d] {
          std::string vel = std::format("Velocity: dc ({:7.1f}), dr ({:7.1f})", d.player_dc, d.player_dr);
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

    core::math::IsoParams iso{};
    if (render.mode == render::data::RenderMode::World && !render.active_chunks.empty()) {
      const gameplay::world::tilemap::Tilemap &first_tm = render.active_chunks[0].tilemap;
      const int total_h = render.manifest.tiles_tall > 0 ? render.manifest.tiles_tall
                                                         : render.manifest.chunks_tall * render.manifest.chunk_size;
      iso = core::math::compute_iso_params(first_tm.diamond_w(), first_tm.diamond_h(), total_h, cfg.tile_scale);
    } else if (render.mode == render::data::RenderMode::SingleMap && !render.map_data.tilemap.tilesets.empty()) {
      const gameplay::world::tilemap::Tilemap &tm = render.map_data.tilemap;
      iso = core::math::compute_iso_params(tm.diamond_w(), tm.diamond_h(), tm.height, cfg.tile_scale);
    }
    const render::data::CollisionGeometry geo = render::data::current_collisions(render);
    draw_collision(r, camera, viewport, geo.rects, geo.tris, iso);

    // Draw player collision bounding box in isometric space
    const gameplay::entity::World &w = scene.world;
    const gameplay::entity::EntityId p = scene.player;
    if (iso.half_tw > 0.f && iso.half_th > 0.f && w.transforms.has(p) && w.collisions.has(p)) {
      r.set_world_view(camera, viewport);
      const std::uint32_t slot = w.transforms.dense_idx(p);
      const float col = w.transforms.col[slot];
      const float row = w.transforms.row[slot];

      // Feet position (entity anchor) in isometric space.
      const float mx = (col - row) * iso.half_tw + iso.x_origin;
      const float my = (col + row) * iso.half_th;
      constexpr float k_marker_hw = 5.f;
      constexpr float k_marker_hh = 3.f;
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
      r.reset_screen_view();
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
      hud.player_col = w.transforms.pos_col(p);
      hud.player_row = w.transforms.pos_row(p);
      const std::uint32_t di = w.transforms.dense_idx(p);
      hud.player_dc = w.transforms.dc[di];
      hud.player_dr = w.transforms.dr[di];
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
