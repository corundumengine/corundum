#include <corundum/core/math/vec.hpp>
#include <corundum/debug/debug_overlay.hpp>
#include <corundum/ecs/component/components.hpp>
#include <corundum/ecs/world.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/render/render_sys.hpp>

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
    constexpr core::math::Colour k_player_col{0, 255, 0, 220};

    constexpr float k_y = 10.f;
    constexpr uint32_t k_font_sz = 16;
    constexpr float k_line_h = 20.f;
    constexpr float k_pad = 8.f;
    constexpr float k_box_w = 360.f;

    constexpr float k_fps_ema_alpha = 0.05f;
    constexpr float k_marker_hw = 5.f;
    constexpr float k_marker_hh = 3.f;
    constexpr float k_line_thickness = 2.f;

    [[nodiscard]] constexpr std::string_view facing_name(ecs::FacingDir d) noexcept {
      using ecs::FacingDir;
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

  } // namespace

  core::math::IsometricParams HudOverlay::resolve_isometric(const render::RenderState &render,
                                                            const core::GameConfig &cfg) noexcept {
    if (render.mode == render::RenderMode::World && !render.chunks.active_empty()) {
      const world::tilemap::Tilemap &first_tm = render.chunks.active_at(0).tilemap;
      const int total_h = render.manifest.tiles_tall > 0 ? render.manifest.tiles_tall
                                                         : render.manifest.chunks_tall * render.manifest.chunk_size;
      return core::math::compute_isometric_params(first_tm.diamond_w(), first_tm.diamond_h(), total_h, cfg.tile_scale,
                                                  cfg.elevation_step_px);
    }
    if (render.mode == render::RenderMode::SingleMap && !render.map_data.tilemap.tilesets.empty()) {
      const world::tilemap::Tilemap &tm = render.map_data.tilemap;
      return core::math::compute_isometric_params(tm.diamond_w(), tm.diamond_h(), tm.height, cfg.tile_scale,
                                                  cfg.elevation_step_px);
    }
    return {};
  }

  void HudOverlay::draw_collision(platform::Renderer &r, core::math::Vec2 camera, core::math::Vec2 viewport,
                                  world::tilemap::CollisionRectsView rects, world::tilemap::CollisionTrianglesView tris,
                                  core::math::IsometricParams iso, float zoom) const noexcept {
    r.set_world_view(camera, viewport, zoom);

    if (iso.half_tw > 0.f && iso.half_th > 0.f) {
      // Convert a tile-grid rect's four corners to isometric and draw as a diamond.
      // Diamond corners are at the cell's top-vertex projection (matching the cell
      // diamond's outline), so the diamond aligns with where tile art draws its
      // top vertex — no offset needed (the old `-iso.half_th` shift drew the
      // collision a full diamond height below the visible tile).
      auto draw_tile_rect = [&r, iso](float col, float row, float col_span, float row_span, core::math::Colour colour) {
        const core::math::Vec2 a = core::math::tile_to_world(col, row, 0, iso);
        const core::math::Vec2 b = core::math::tile_to_world(col + col_span, row, 0, iso);
        const core::math::Vec2 c = core::math::tile_to_world(col + col_span, row + row_span, 0, iso);
        const core::math::Vec2 d = core::math::tile_to_world(col, row + row_span, 0, iso);
        r.draw(platform::DrawLine{.start = a, .end = b, .colour = colour, .thickness = k_line_thickness});
        r.draw(platform::DrawLine{.start = b, .end = c, .colour = colour, .thickness = k_line_thickness});
        r.draw(platform::DrawLine{.start = c, .end = d, .colour = colour, .thickness = k_line_thickness});
        r.draw(platform::DrawLine{.start = d, .end = a, .colour = colour, .thickness = k_line_thickness});
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

  void HudOverlay::draw_player_marker(platform::Renderer &r, core::math::Vec2 camera, core::math::Vec2 viewport,
                                      float zoom, const render::RenderState &render, const ecs::World &w,
                                      ecs::EntityId player, core::math::IsometricParams iso) const noexcept {
    if (iso.half_tw <= 0.f || iso.half_th <= 0.f || !w.transforms.has(player) || !w.collisions.has(player))
      return;

    r.set_world_view(camera, viewport, zoom);
    const auto slot = w.transforms.dense_idx(player);
    const float col = w.transforms.col[slot];
    const float row = w.transforms.row[slot];

    // Feet position (entity anchor) in isometric space — cell center, matching the
    // entity sprite anchor so the marker sits on the character's feet, not above them.
    // Elevation must match render_sys.cpp's entity anchor calc (elevation_under), or the
    // marker desyncs from the drawn sprite on any non-flat tile.
    const float marker_elev = corundum::render::elevation_under(render, col, row);
    const auto [mx, my] = core::math::tile_to_world_center(col, row, marker_elev, iso);
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

  void HudOverlay::draw_text_panel(platform::Renderer &r, const render::RenderState &render,
                                   const core::GameConfig &cfg, const world::Scene &scene) const noexcept {
    const float x = cfg.win_w - k_box_w - k_pad;

    const ecs::World &w = scene.world;
    const ecs::EntityId p = scene.player;

    std::string grid_str{"(none)"};
    float player_dc = 0.f;
    float player_dr = 0.f;
    if (w.transforms.has(p)) {
      grid_str = std::format("col ({:7.1f}), row ({:7.1f})", w.transforms.pos_col(p), w.transforms.pos_row(p));
      const auto di = w.transforms.dense_idx(p);
      player_dc = w.transforms.dc[di];
      player_dr = w.transforms.dr[di];
    }

    std::string velocity_str = std::format("dc ({:7.1f}), dr ({:7.1f})", player_dc, player_dr);
    if (w.facings.has(p))
      velocity_str += std::format("  {}", facing_name(w.facings.dir_of(p)));

    const render::CollisionGeometry geo = render::current_collisions(render);
    const int collision_rects = static_cast<int>(geo.rects.size());
    const int collision_tris = static_cast<int>(geo.tris.size());

    std::string map_name;
    if (render.mode == render::RenderMode::SingleMap && !render.map_data.tilemap.path.empty()) {
      map_name = render.map_data.tilemap.path;
    } else if (render.mode == render::RenderMode::World && !render.chunks.active_empty()) {
      const int cs = render.manifest.chunk_size;
      if (cs > 0 && w.transforms.has(p)) {
        const world::tilemap::ChunkCoord c{static_cast<int>(w.transforms.pos_col(p)) / cs,
                                           static_cast<int>(w.transforms.pos_row(p)) / cs};
        for (const render::ChunkEntry &entry : render.chunks.active()) {
          if (entry.coord == c) {
            map_name = entry.tilemap.path;
            break;
          }
        }
      }
    }

    std::string hover_str;
    if (scene.hovered_tile)
      hover_str = std::format("Hover:  col ({}), row ({})", scene.hovered_tile->col, scene.hovered_tile->row);
    else
      hover_str = "Hover:  none";

    std::array<std::string, 8> lines{
        std::format("FPS:  sim {:3.0f} / render {:3.0f}", static_cast<float>(cfg.framerate), smoothed_fps),
        std::format("Grid:  {}", grid_str),
        std::format("Velocity:  {}", velocity_str),
        std::format("Camera:  x ({:7.1f}), y ({:7.1f})", scene.camera.x, scene.camera.y),
        std::format("Stats:  chunk:{}  col rect:{}  col tri:{}  ent:{}", static_cast<int>(render.chunks.active_size()),
                    collision_rects, collision_tris, static_cast<int>(w.entities.alive())),
        std::format("Map:  {}", map_name),
        std::format("Draw:  calls {}  quads {}{}", r.stats().draw_calls, r.stats().quads,
                    r.stats().dropped_quads ? std::format("  DROPPED {}", r.stats().dropped_quads) : std::string{}),
        hover_str,
    };

    r.draw(platform::DrawRect{
        .position = {x - k_pad, k_y - k_pad},
        .size = {k_box_w + 2.f * k_pad, static_cast<float>(lines.size()) * k_line_h + k_pad * 2.f},
        .colour = k_hud_bg,
    });

    float y = k_y;
    for (const std::string &text : lines) {
      r.draw(platform::DrawText{
          .font_id = render.font_id,
          .text = text,
          .position = {x, y},
          .char_size = k_font_sz,
          .colour = k_hud_text,
      });
      y += k_line_h;
    }
  }

  void HudOverlay::render(platform::Renderer &r, const OverlayInput &input) noexcept {
    const render::RenderState &render = input.render_state;
    const core::GameConfig &cfg = input.cfg;
    const world::Scene &scene = input.scene;
    const core::time::LoopTimer &timer = input.timer;

    const core::math::Vec2 viewport{cfg.win_w, cfg.win_h};
    const core::math::Vec2 camera{scene.camera.x, scene.camera.y};

    const core::math::IsometricParams iso = resolve_isometric(render, cfg);

    const render::CollisionGeometry geo = render::current_collisions(render);
    draw_collision(r, camera, viewport, geo.rects, geo.tris, iso, scene.camera.zoom);
    draw_player_marker(r, camera, viewport, scene.camera.zoom, render, scene.world, scene.player, iso);

    const float raw_fps = timer.last_frame_dt > 0.f ? 1.f / timer.last_frame_dt : 0.f;
    smoothed_fps += k_fps_ema_alpha * (raw_fps - smoothed_fps);

    draw_text_panel(r, render, cfg, scene);
  }

} // namespace corundum::debug
