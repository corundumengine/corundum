#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/sys/pathfinding.hpp>
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

  namespace {
    constexpr float k_tile_center_offset = 0.5f;

    using corundum::core::math::IsoParams;
    using corundum::core::math::Vec2;

    /** @brief Convert a tile-grid offset (dc, dr) to the equivalent screen-space displacement.
     *
     * The isometric projection maps tile coordinates to screen via:
     *   screen_x = (col - row) * half_tw
     *   screen_y = (col + row) * half_th
     *
     * This function applies the same Jacobian to a delta vector, so that normalising
     * in screen space produces equal perceived speed in all directions rather than
     * equal tile-grid speed (which the isometric projection distorts).
     */
    [[nodiscard]] constexpr Vec2 tile_to_screen_delta(float dc, float dr, IsoParams iso) noexcept {
      return {(dc - dr) * iso.half_tw, (dc + dr) * iso.half_th};
    }

    /** @brief Convert a screen-space velocity back to tile-grid velocity components.
     *
     * Inverse of the projection used by tile_to_screen_delta():
     *   dc = (svx / half_tw + svy / half_th) / 2
     *   dr = (svy / half_th - svx / half_tw) / 2
     *
     * @pre iso.half_tw > 0 and iso.half_th > 0.
     */
    [[nodiscard]] constexpr Vec2 screen_to_tile_delta(float svx, float svy, IsoParams iso) noexcept {
      return {(svx / iso.half_tw + svy / iso.half_th) / 2.f, (svy / iso.half_th - svx / iso.half_tw) / 2.f};
    }
  } // namespace

  void integrate(corundum::gameplay::component::TransformTable &transforms, corundum::gameplay::entity::EntityId e,
                 float dt) noexcept {
    const auto slot = transforms.dense_idx(e);
    transforms.col[slot] += transforms.dc[slot] * dt;
    transforms.row[slot] += transforms.dr[slot] * dt;
  }

  void follow_path(corundum::gameplay::component::TransformTable &transforms,
                   corundum::gameplay::entity::EntityId player, std::vector<corundum::gameplay::sys::TileCoord> &path,
                   float player_speed, corundum::core::math::IsoParams iso, float dt) noexcept {
    if (!transforms.has(player)) [[unlikely]]
      return;
    const std::uint32_t slot = transforms.dense_idx(player);

    if (path.empty()) {
      transforms.dc[slot] = 0.f;
      transforms.dr[slot] = 0.f;
      return;
    }

    const float target_col = static_cast<float>(path.front().col) + k_tile_center_offset;
    const float target_row = static_cast<float>(path.front().row) + k_tile_center_offset;
    const float dc = target_col - transforms.col[slot];
    const float dr = target_row - transforms.row[slot];

    if (iso.half_tw > 0.f && iso.half_th > 0.f) {
      const auto [svx, svy] = tile_to_screen_delta(dc, dr, iso);
      const float screen_dist = std::hypot(svx, svy);

      if (screen_dist <= player_speed * dt) {
        transforms.col[slot] = target_col;
        transforms.row[slot] = target_row;
        path.erase(path.begin());
        follow_path(transforms, player, path, player_speed, iso, dt);
        return;
      }
      const float scale = player_speed / screen_dist;
      const auto [tdc, tdr] = screen_to_tile_delta(svx * scale, svy * scale, iso);
      transforms.dc[slot] = tdc;
      transforms.dr[slot] = tdr;
    } else {
      const float dist = std::hypot(dc, dr);
      if (dist <= player_speed * dt) {
        transforms.col[slot] = target_col;
        transforms.row[slot] = target_row;
        path.erase(path.begin());
        follow_path(transforms, player, path, player_speed, iso, dt);
        return;
      }
      const float inv_dist = player_speed / dist;
      transforms.dc[slot] = dc * inv_dist;
      transforms.dr[slot] = dr * inv_dist;
    }
  }

  void apply_input(corundum::gameplay::component::TransformTable &transforms,
                   corundum::gameplay::entity::EntityId player, const corundum::input::InputState &input,
                   float player_speed, corundum::core::math::IsoParams iso) noexcept {
    if (!transforms.has(player)) [[unlikely]]
      return;

    const std::uint32_t slot = transforms.dense_idx(player);
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
    if (len_sq > 0.f && iso.half_tw > 0.f && iso.half_th > 0.f) {
      // Normalise in screen space so that east/west and north/south movement
      // feel equally fast (isometric projection distorts tile-grid distances).
      const auto [svx, svy] = tile_to_screen_delta(dc, dr, iso);
      const float screen_len = std::hypot(svx, svy);
      const float scale = player_speed / screen_len;
      const auto [tdc, tdr] = screen_to_tile_delta(svx * scale, svy * scale, iso);
      transforms.dc[slot] = tdc;
      transforms.dr[slot] = tdr;
    } else if (len_sq > 0.f) {
      const float inv_len = player_speed / std::sqrt(len_sq);
      transforms.dc[slot] = dc * inv_len;
      transforms.dr[slot] = dr * inv_len;
    }
  }

  void update_player(corundum::gameplay::component::TransformTable &transforms,
                     const corundum::gameplay::component::CollisionTable &collisions,
                     corundum::gameplay::entity::EntityId player, const corundum::input::InputState &input,
                     float player_speed, const corundum::gameplay::world::MapView &map,
                     corundum::gameplay::world::Scene &scene, float dt) noexcept {
    using corundum::core::math::IsoParams;
    using corundum::gameplay::component::CollisionTable;
    using corundum::gameplay::component::Position;
    using corundum::gameplay::entity::EntityId;

    const std::uint32_t p_slot = transforms.dense_idx(player);
    const float prev_col = transforms.col[p_slot];
    const float prev_row = transforms.row[p_slot];

    // The floor the player is standing on at the start of the frame — computed from the
    // pre-move position so collision resolution doesn't depend on its own not-yet-resolved
    // result. Null elevation_map (chunked World mode) means "no elevation data": treat as 0.
    constexpr int k_elevation_tolerance = 0;
    const int player_elev = static_cast<int>(corundum::gameplay::world::elevation_at_tile(map, prev_col, prev_row));

    // A click queues a new path. Deliberately keyed on mouse_click_pressed, not
    // Action::Select — Select is also raised by keyboard/gamepad confirm presses (which
    // carry no click position), and using it here would spuriously queue a path toward
    // wherever the mouse happens to be hovering any time the player presses Enter/Space/
    // a gamepad button for an unrelated reason (e.g. confirming dialogue).
    if (input.mouse_click_pressed && scene.hovered_tile && map.walkability) {
      const corundum::gameplay::sys::TileCoord start{static_cast<int>(prev_col), static_cast<int>(prev_row)};
      scene.path = corundum::gameplay::sys::find_path(map, start, *scene.hovered_tile);
    }

    const bool manual_move =
        input.is_held(corundum::input::Action::MoveUp) || input.is_held(corundum::input::Action::MoveDown) ||
        input.is_held(corundum::input::Action::MoveLeft) || input.is_held(corundum::input::Action::MoveRight);
    const IsoParams iso{map.half_tw, map.half_th};
    if (manual_move) {
      scene.path.clear(); // manual input always overrides/cancels an active path
      apply_input(transforms, player, input, player_speed, iso);
    } else if (!scene.path.empty()) {
      follow_path(transforms, player, scene.path, player_speed, iso, dt);
    } else {
      apply_input(transforms, player, input, player_speed, iso); // zeroes dc/dr when nothing is held
    }
    integrate(transforms, player, dt);

    const float map_w = map.world_w_tiles;
    const float map_h = map.world_h_tiles;

    const CollisionTable::Rect &player_rect = collisions.get_rect(player);

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
      const EntityId eid = collisions.idx.entities[i];
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

    if (!map.portals.empty()) {
      // Player AABB in tile-grid space (same convention as collision rects), tested directly
      // against portal rects — both already live in tile-grid units, so no iso<->cart conversion.
      const float half_cs = player_rect.col_span / 2.f;
      const float col0 = p.col - half_cs;
      const float col1 = p.col + half_cs;
      const float row0 = p.row;
      const float row1 = p.row + player_rect.row_span;
      for (const auto &portal : map.portals) {
        if (col1 > portal.col && col0 < portal.col + portal.w && row1 > portal.row && row0 < portal.row + portal.h) {
          if (portal.target_chunk_x >= 0) {
            p.col = static_cast<float>(portal.spawn_col);
            p.row = static_cast<float>(portal.spawn_row);
            transforms.col[p_slot] = p.col;
            transforms.row[p_slot] = p.row;
            return;
          }
          scene.pending_transition = {portal.target_map, portal.spawn_col, portal.spawn_row};
          return;
        }
      }
    }
  }

} // namespace corundum::physics::sys
