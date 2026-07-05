#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/gameplay/world/update.hpp>
#include <corundum/physics/collision.hpp>
#include <corundum/physics/sys/physics_sys.hpp>
#include <corundum/physics/walkability.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace corundum::physics::sys {

  void integrate(corundum::gameplay::component::TransformTable &transforms, float dt) noexcept {
    [[assume(transforms.count <= std::remove_reference_t<decltype(transforms)>::k_max)]];
    float *pcol = std::assume_aligned<16>(transforms.col.data());
    float *prow = std::assume_aligned<16>(transforms.row.data());
    const float *pdc = std::assume_aligned<16>(transforms.dc.data());
    const float *pdr = std::assume_aligned<16>(transforms.dr.data());
    for (uint16_t i = 0; i < transforms.count; ++i) {
      pcol[i] += pdc[i] * dt;
      prow[i] += pdr[i] * dt;
    }
  }

  void apply_input(corundum::gameplay::component::TransformTable &transforms,
                   corundum::gameplay::entity::EntityId player, const corundum::input::InputState &input,
                   float speed) noexcept {
    if (!transforms.has(player)) [[unlikely]]
      return;

    const auto slot = transforms.dense_idx(player);
    transforms.dc[slot] = 0.f;
    transforms.dr[slot] = 0.f;

    float dc = 0.f;
    float dr = 0.f;
    // Map screen directions to tile-grid axes.
    // Screen up    = NW = both col and row decrease.
    // Screen down  = SE = both col and row increase.
    // Screen left  = SW = col decreases, row increases.
    // Screen right = NE = col increases, row decreases.
    if (input.is_held(corundum::input::Action::MoveUp)) {
      dc -= 1.f;
      dr -= 1.f;
    }
    if (input.is_held(corundum::input::Action::MoveDown)) {
      dc += 1.f;
      dr += 1.f;
    }
    if (input.is_held(corundum::input::Action::MoveLeft)) {
      dc -= 1.f;
      dr += 1.f;
    }
    if (input.is_held(corundum::input::Action::MoveRight)) {
      dc += 1.f;
      dr -= 1.f;
    }

    const float len_sq = dc * dc + dr * dr;
    if (len_sq > 0.f) {
      const float inv_len = speed / std::sqrt(len_sq);
      transforms.dc[slot] = dc * inv_len;
      transforms.dr[slot] = dr * inv_len;
    }
  }

  void update_player(corundum::gameplay::component::TransformTable &transforms,
                     const corundum::gameplay::component::CollisionTable &collisions,
                     corundum::gameplay::entity::EntityId player, const corundum::input::InputState &input,
                     float player_speed, const corundum::gameplay::world::MapView &map,
                     corundum::gameplay::world::Scene &scene, float dt) noexcept {
    using corundum::gameplay::component::Position;
    using corundum::gameplay::entity::EntityId;

    const auto p_slot = transforms.dense_idx(player);
    const float prev_col = transforms.col[p_slot];
    const float prev_row = transforms.row[p_slot];

    // The floor the player is standing on at the start of the frame — computed from the
    // pre-move position so collision resolution doesn't depend on its own not-yet-resolved
    // result. Null elevation_map (chunked World mode) means "no elevation data": treat as 0.
    constexpr int k_elevation_tolerance = 0;
    const int player_elev = map.elevation_map
                                ? corundum::gameplay::world::tilemap::elevation_at(
                                      *map.elevation_map, static_cast<int>(prev_col), static_cast<int>(prev_row))
                                : 0;

    // Convert player_speed from isometric pixels/sec to tiles/sec.
    // For NW/SE movement: Δiso_y = (dc+dr)*half_th, where |dc|=|dr|=speed/√2.
    // Solving for speed such that Δiso_y/sec = player_speed:
    //   speed = player_speed / (√2 * half_th)
    const float tile_speed = player_speed / (1.41421356f * map.half_th);
    apply_input(transforms, player, input, tile_speed);
    integrate(transforms, dt);

    const float map_w = map.world_w_px;
    const float map_h = map.world_h_px;

    const auto &player_rect = collisions.get_rect(player);

    Position p{transforms.col[p_slot], transforms.row[p_slot]};
    const Position prev_pos{prev_col, prev_row};

    if (map.half_tw > 0.f && map.half_th > 0.f) {
      const float half_cs = player_rect.col_span / 2.f;
      // AABB extends upward from the feet position.
      Position pc{p.col - half_cs, p.row - player_rect.row_span};
      const Position pcp{prev_pos.col - half_cs, prev_pos.row - player_rect.row_span};
      resolve_collisions(pc, pcp, player_rect.col_span, player_rect.row_span, map.collisions, 0.f, player_elev,
                         k_elevation_tolerance);
      resolve_triangle_collisions(pc, pcp, player_rect.col_span, player_rect.row_span, map.collision_triangles, 0.f,
                                  player_elev, k_elevation_tolerance);
      // Convert AABB top-left back to feet position.
      p.col = pc.col + half_cs;
      p.row = pc.row + player_rect.row_span;
      resolve_walkability(p, prev_pos, map.walkability);
    }

    std::array<float, corundum::gameplay::entity::k_max_entities> npc_cols{}, npc_rows{}, npc_cs{}, npc_rs{};
    uint16_t npc_count = 0;
    for (uint16_t i = 0; i < collisions.count; ++i) {
      const EntityId eid = collisions.entities[i];
      if (eid == player)
        continue;
      if (!transforms.has(eid))
        continue;
      const auto &rect = collisions.rects[i];
      const auto np_slot = transforms.dense_idx(eid);
      // Convert NPC feet position to AABB top-left.
      const float half_npc_cs = rect.col_span / 2.f;
      npc_cols[npc_count] = transforms.col[np_slot] - half_npc_cs;
      npc_rows[npc_count] = transforms.row[np_slot] - rect.row_span;
      npc_cs[npc_count] = rect.col_span;
      npc_rs[npc_count] = rect.row_span;
      ++npc_count;
    }
    const corundum::gameplay::world::tilemap::CollisionRectsView npc_view{
        std::span{npc_cols.data(), npc_count}, std::span{npc_rows.data(), npc_count},
        std::span{npc_cs.data(), npc_count}, std::span{npc_rs.data(), npc_count}};
    // Convert player feet to AABB top-left for NPC collision.
    {
      const float half_cs = player_rect.col_span / 2.f;
      Position p_aabb{p.col - half_cs, p.row - player_rect.row_span};
      const Position prev_aabb{prev_pos.col - half_cs, prev_pos.row - player_rect.row_span};
      resolve_collisions(p_aabb, prev_aabb, player_rect.col_span, player_rect.row_span, npc_view, 0.f);
      p.col = p_aabb.col + half_cs;
      p.row = p_aabb.row + player_rect.row_span;
    }

    p.col = std::clamp(p.col, 0.f, std::max(0.f, map_w - player_rect.col_span));
    p.row = std::clamp(p.row, 0.f, std::max(0.f, map_h - player_rect.row_span));

    transforms.col[p_slot] = p.col;
    transforms.row[p_slot] = p.row;

    if (map.half_tw > 0.f && map.half_th > 0.f && !map.portals.empty()) {
      const float tile_w = map.half_tw * 2.f;
      const float px = (p.col - p.row) * map.half_tw + map.x_origin;
      const float py = (p.col + p.row) * map.half_th;
      const auto p_cart = corundum::core::math::iso_to_cart({px, py}, map.half_tw, map.half_th, map.x_origin);
      const float px0 = p_cart.x - player_rect.col_span * tile_w / 2.f;
      const float py0 = p_cart.y;
      const float px1 = p_cart.x + player_rect.col_span * tile_w / 2.f;
      const float py1 = p_cart.y + player_rect.row_span * tile_w;
      for (const auto &portal : map.portals) {
        if (px1 > portal.x && px0 < portal.x + portal.w && py1 > portal.y && py0 < portal.y + portal.h) {
          scene.pending_transition = {portal.target_map, portal.spawn_col, portal.spawn_row};
          return;
        }
      }
    }
  }

} // namespace corundum::physics::sys
