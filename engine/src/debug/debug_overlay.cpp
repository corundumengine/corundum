// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/direction.hpp>
#include <corundum/core/game_config.hpp>
#include <corundum/core/math/isometric.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/core/time/loop_timer.hpp>
#include <corundum/debug/debug_overlay.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/entities/world.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/render/render_state.hpp>
#include <corundum/render/render_sys.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <corundum/world/tilemap/world_manifest.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <string>
#include <string_view>

namespace corundum::debug {

  namespace {

    constexpr core::math::Colour k_rect_col{.r = 220, .g = 60, .b = 60, .a = 80};
    constexpr core::math::Colour k_tri_col{.r = 60, .g = 100, .b = 220, .a = 80};
    constexpr core::math::Colour k_hud_bg{.r = 0, .g = 0, .b = 0, .a = 75};
    constexpr core::math::Colour k_hud_text{.r = 220, .g = 220, .b = 200, .a = 255};
    constexpr core::math::Colour k_player_col{.r = 0, .g = 255, .b = 0, .a = 220};

    constexpr float k_y = 10.f;
    constexpr uint32_t k_font_sz = 16;
    constexpr float k_line_h = 20.f;
    constexpr float k_pad = 8.f;
    constexpr float k_box_w = 360.f;

    constexpr float k_fps_ema_alpha = 0.05f;
    constexpr float k_marker_hw = 5.f;
    constexpr float k_marker_hh = 3.f;
    constexpr float k_line_thickness = 2.f;

    template <typename View>
    void draw_tile_collisions(platform::Renderer &r, const View &view, core::math::IsometricParams iso,
                              core::math::Colour colour) noexcept {
      for (std::size_t i = 0; i < view.size(); ++i) {
        const float col = view.cols[i];
        const float row = view.rows[i];
        const float col_end = col + view.col_spans[i];
        const float row_end = row + view.row_spans[i];
        const core::math::Vec2 a = core::math::tile_to_world(col, row, 0, iso);
        const core::math::Vec2 b = core::math::tile_to_world(col_end, row, 0, iso);
        const core::math::Vec2 c = core::math::tile_to_world(col_end, row_end, 0, iso);
        const core::math::Vec2 d = core::math::tile_to_world(col, row_end, 0, iso);
        r.draw(platform::DrawLine{.start = a, .end = b, .colour = colour, .thickness = k_line_thickness});
        r.draw(platform::DrawLine{.start = b, .end = c, .colour = colour, .thickness = k_line_thickness});
        r.draw(platform::DrawLine{.start = c, .end = d, .colour = colour, .thickness = k_line_thickness});
        r.draw(platform::DrawLine{.start = d, .end = a, .colour = colour, .thickness = k_line_thickness});
      }
    }

    template <typename View>
    void draw_flat_collisions(platform::Renderer &r, const View &view, core::math::Colour colour) noexcept {
      for (std::size_t i = 0; i < view.size(); ++i) {
        r.draw(platform::DrawRect{
            .position = {view.cols[i], view.rows[i]},
            .size = {view.col_spans[i], view.row_spans[i]},
            .colour = colour,
        });
      }
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
                                  core::math::IsometricParams iso, float zoom) noexcept {
    r.set_world_view(camera, viewport, zoom);

    if (iso.half_tw > 0.f && iso.half_th > 0.f) {
      // Diamond corners project from the cell's top vertex, matching the tile art's
      // outline, so no vertical offset is needed for the outline to sit on the tile.
      draw_tile_collisions(r, rects, iso, k_rect_col);
      draw_tile_collisions(r, tris, iso, k_tri_col);
    } else {
      draw_flat_collisions(r, rects, k_rect_col);
      draw_flat_collisions(r, tris, k_tri_col);
    }

    r.reset_screen_view();
  }

  void HudOverlay::draw_player_marker(platform::Renderer &r, core::math::Vec2 camera, core::math::Vec2 viewport,
                                      float zoom, const render::RenderState &render, const entities::World &w,
                                      entities::EntityId player, core::math::IsometricParams iso) noexcept {
    if (iso.half_tw <= 0.f || iso.half_th <= 0.f || !w.transforms.has(player) || !w.collisions.has(player))
      return;

    r.set_world_view(camera, viewport, zoom);
    const auto slot = w.transforms.dense_index(player);
    const float col = w.transforms.col[slot];
    const float row = w.transforms.row[slot];

    // Feet position (entity anchor) in isometric space — cell center, matching the
    // entity sprite anchor so the marker sits on the character's feet, not above them.
    // Elevation must match render_sys.cpp's entity anchor calc (elevation_under), or the
    // marker desyncs from the drawn sprite on any non-flat tile.
    const float marker_elev = corundum::render::elevation_under(render, col, row);
    const auto [mx, my] = core::math::tile_to_world_center(col, row, marker_elev, iso);
    r.draw(platform::DrawLine{
        .start = {.x = mx, .y = my - k_marker_hh},
        .end = {.x = mx + k_marker_hw, .y = my},
        .colour = k_player_col,
        .thickness = k_line_thickness,
    });
    r.draw(platform::DrawLine{
        .start = {.x = mx + k_marker_hw, .y = my},
        .end = {.x = mx, .y = my + k_marker_hh},
        .colour = k_player_col,
        .thickness = k_line_thickness,
    });
    r.draw(platform::DrawLine{
        .start = {.x = mx, .y = my + k_marker_hh},
        .end = {.x = mx - k_marker_hw, .y = my},
        .colour = k_player_col,
        .thickness = k_line_thickness,
    });
    r.draw(platform::DrawLine{
        .start = {.x = mx - k_marker_hw, .y = my},
        .end = {.x = mx, .y = my - k_marker_hh},
        .colour = k_player_col,
        .thickness = k_line_thickness,
    });
    r.reset_screen_view();
  }

  void HudOverlay::draw_text_panel(platform::Renderer &r, const render::RenderState &render,
                                   const core::GameConfig &cfg, const world::Scene &scene) const {
    const float x = cfg.win_w - k_box_w - k_pad;

    const entities::World &w = scene.world;
    const entities::EntityId p = scene.player;

    float player_dc = 0.f;
    float player_dr = 0.f;
    if (w.transforms.has(p)) {
      const auto di = w.transforms.dense_index(p);
      player_dc = w.transforms.dc[di];
      player_dr = w.transforms.dr[di];
    }

    const render::CollisionGeometry geo = render::current_collisions(render);
    const int collision_rects = static_cast<int>(geo.rects.size());
    const int collision_tris = static_cast<int>(geo.tris.size());

    const platform::RendererStats stats = r.stats();

    std::string map_name;
    if (render.mode == render::RenderMode::SingleMap && !render.map_data.tilemap.path.empty()) {
      map_name = render.map_data.tilemap.path;
    } else if (render.mode == render::RenderMode::World && !render.chunks.active_empty()) {
      const int cs = render.manifest.chunk_size;
      if (cs > 0 && w.transforms.has(p)) {
        const world::tilemap::ChunkCoord c{
            .col = static_cast<int>(w.transforms.pos_col(p)) / cs,
            .row = static_cast<int>(w.transforms.pos_row(p)) / cs,
        };
        for (const render::ChunkEntry &entry : render.chunks.active()) {
          if (entry.coord == c) {
            map_name = entry.tilemap.path;
            break;
          }
        }
      }
    }

    // Format every line into one buffer, recording each line's start offset. The
    // buffer may reallocate as it grows, so offsets (not views) are kept and sliced
    // at draw time — DrawText takes a string_view, so no per-line allocation.
    constexpr std::size_t k_line_count = 8;
    std::array<std::size_t, k_line_count> line_offsets{};
    std::string panel;
    panel.reserve(512);

    const auto begin_line = [&panel, &line_offsets](std::size_t index) -> std::string & {
      line_offsets[index] = panel.size();
      return panel;
    };

    std::format_to(std::back_inserter(begin_line(0)), "FPS:  sim {:3.0f} / render {:3.0f}",
                   static_cast<float>(cfg.simulation_fps), smoothed_fps);
    if (shed_frames > 0)
      std::format_to(std::back_inserter(panel), "  SHED {}", shed_frames);

    if (w.transforms.has(p))
      std::format_to(std::back_inserter(begin_line(1)), "Grid:  col ({:7.1f}), row ({:7.1f})", w.transforms.pos_col(p),
                     w.transforms.pos_row(p));
    else
      std::format_to(std::back_inserter(begin_line(1)), "Grid:  (none)");

    std::format_to(std::back_inserter(begin_line(2)), "Velocity:  dc ({:7.1f}), dr ({:7.1f})", player_dc, player_dr);
    if (w.facings.has(p))
      std::format_to(std::back_inserter(panel), "  {}", core::direction_name(w.facings.dir_of(p)));

    std::format_to(std::back_inserter(begin_line(3)), "Camera:  x ({:7.1f}), y ({:7.1f})", scene.camera.x,
                   scene.camera.y);

    std::format_to(std::back_inserter(begin_line(4)), "Stats:  chunk:{}  col rect:{}  col tri:{}  ent:{}",
                   static_cast<int>(render.chunks.active_size()), collision_rects, collision_tris,
                   static_cast<int>(w.entities.alive()));

    std::format_to(std::back_inserter(begin_line(5)), "Map:  {}", map_name);

    std::format_to(std::back_inserter(begin_line(6)), "Draw:  calls {}  quads {}", stats.draw_calls, stats.quads);
    if (stats.dropped_quads > 0u)
      std::format_to(std::back_inserter(panel), "  DROPPED {}", stats.dropped_quads);

    if (scene.hovered_tile)
      std::format_to(std::back_inserter(begin_line(7)), "Hover:  col ({}), row ({})", scene.hovered_tile->col,
                     scene.hovered_tile->row);
    else
      std::format_to(std::back_inserter(begin_line(7)), "Hover:  none");

    r.draw(platform::DrawRect{
        .position = {.x = x - k_pad, .y = k_y - k_pad},
        .size = {.x = k_box_w + (2.f * k_pad), .y = (static_cast<float>(k_line_count) * k_line_h) + (k_pad * 2.f)},
        .colour = k_hud_bg,
    });

    const std::string_view panel_view{panel};
    float y = k_y;
    for (std::size_t i = 0; i < k_line_count; ++i) {
      const std::size_t end = (i + 1 < k_line_count) ? line_offsets[i + 1] : panel.size();
      r.draw(platform::DrawText{
          .font_id = render.font_id,
          .text = panel_view.substr(line_offsets[i], end - line_offsets[i]),
          .position = {.x = x, .y = y},
          .char_size = k_font_sz,
          .colour = k_hud_text,
      });
      y += k_line_h;
    }
  }

  void HudOverlay::render(platform::Renderer &r, const OverlayInput &input) {
    const core::time::LoopTimer &timer = *input.timer;

    const float raw_fps = timer.last_frame_dt > 0.f ? 1.f / timer.last_frame_dt : 0.f;
    smoothed_fps += k_fps_ema_alpha * (raw_fps - smoothed_fps);

    if (enabled && input.step_budget_exhausted)
      ++shed_frames;

    if (!enabled)
      return;

    const render::RenderState &render = *input.render_state;
    const core::GameConfig &cfg = *input.cfg;
    const world::Scene &scene = *input.scene;

    const core::math::Vec2 viewport{.x = cfg.win_w, .y = cfg.win_h};
    const core::math::Vec2 camera{.x = scene.camera.x, .y = scene.camera.y};

    const core::math::IsometricParams iso = resolve_isometric(render, cfg);

    const render::CollisionGeometry geo = render::current_collisions(render);
    draw_collision(r, camera, viewport, geo.rects, geo.tris, iso, scene.camera.zoom);
    draw_player_marker(r, camera, viewport, scene.camera.zoom, render, scene.world, scene.player, iso);

    draw_text_panel(r, render, cfg, scene);
  }

} // namespace corundum::debug
