#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/world/pathfinding.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/gameplay/world/update.hpp>
#include <corundum/physics/collision.hpp>
#include <corundum/physics/physics_sys.hpp>
#include <corundum/physics/walkability.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

namespace corundum::physics {

  namespace {
    constexpr float k_tile_center_offset = 0.5f;

    using corundum::core::math::IsometricParams;
    using corundum::core::math::tile_to_screen_delta;
    using corundum::core::math::Vec2;

    /** @brief Convert a screen-space velocity back to tile-grid velocity components.
     *
     * Inverse of the projection used by tile_to_screen_delta():
     *   dc = (svx / half_tw + svy / half_th) / 2
     *   dr = (svy / half_th - svx / half_tw) / 2
     *
     * @pre iso.half_tw > 0 and iso.half_th > 0.
     */
    [[nodiscard]] constexpr Vec2 screen_to_tile_delta(float svx, float svy, IsometricParams iso) noexcept {
      return {(svx / iso.half_tw + svy / iso.half_th) / 2.f, (svy / iso.half_th - svx / iso.half_tw) / 2.f};
    }
  } // namespace

  ElevationGate compute_elevation_gate(const corundum::gameplay::world::MapView &map, float col, float row) noexcept {
    using corundum::gameplay::world::tilemap::elevation_at;
    using corundum::gameplay::world::tilemap::ramp_axis_at;
    using corundum::gameplay::world::tilemap::RampAxis;

    ElevationGate gate{};
    const float elev_f = corundum::gameplay::world::elevation_at_tile(map, col, row);
    gate.player_elevation = static_cast<int>(std::round(elev_f));

    // World mode has no Tilemap to query — elevation_tolerance stays 0 (no ramps to widen for).
    if (!map.elevation_map)
      return gate;

    const int cell_col = static_cast<int>(std::floor(col));
    const int cell_row = static_cast<int>(std::floor(row));
    const std::optional<RampAxis> axis = ramp_axis_at(*map.elevation_map, cell_col, cell_row);
    if (!axis)
      return gate;

    // Δ across the ramp's two axis-neighbors (ramp's own integer elev doesn't matter —
    // interpolated_elevation_at uses the neighbors). ceil(Δ/2) widens tolerance so
    // both end elevations stay within range at the midpoint.
    const auto [dc, dr] = *axis == RampAxis::NorthSouth ? std::pair{0, 1} : std::pair{1, 0};
    const int elev_a = elevation_at(*map.elevation_map, cell_col - dc, cell_row - dr);
    const int elev_b = elevation_at(*map.elevation_map, cell_col + dc, cell_row + dr);
    const int ramp_dh = std::abs(elev_a - elev_b);
    gate.tolerance = (ramp_dh + 1) / 2;
    return gate;
  }

  bool portal_elev_matches(const corundum::gameplay::world::MapView &map,
                           const corundum::gameplay::world::Portal &portal, int player_elev,
                           int elev_tolerance) noexcept {
    // Use the portal's center cell — a multi-cell portal's intent is "the player is on
    // the floor that runs through here", so the center cell is the canonical reference.
    const float portal_elev_f =
        corundum::gameplay::world::elevation_at_tile(map, portal.col + portal.w * 0.5f, portal.row + portal.h * 0.5f);
    const int portal_elev = static_cast<int>(std::round(portal_elev_f));
    return std::abs(portal_elev - player_elev) <= elev_tolerance;
  }

  void integrate(corundum::ecs::TransformTable &transforms, corundum::ecs::EntityId e, float dt) noexcept {
    const auto slot = transforms.dense_idx(e);
    transforms.col[slot] += transforms.dc[slot] * dt;
    transforms.row[slot] += transforms.dr[slot] * dt;
  }

  void follow_path(corundum::ecs::TransformTable &transforms, corundum::ecs::EntityId player,
                   std::vector<corundum::gameplay::world::TileCoord> &path, float player_speed,
                   corundum::core::math::IsometricParams iso, float dt) noexcept {
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

  void apply_input(corundum::ecs::TransformTable &transforms, corundum::ecs::EntityId player,
                   const corundum::input::InputState &input, float player_speed,
                   corundum::core::math::IsometricParams iso) noexcept {
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

  void update_player(corundum::ecs::TransformTable &transforms, const corundum::ecs::CollisionTable &collisions,
                     corundum::ecs::EntityId player, const corundum::input::InputState &input, float player_speed,
                     const corundum::gameplay::world::MapView &map, corundum::gameplay::world::Scene &scene,
                     float dt) noexcept {
    using corundum::core::math::IsometricParams;
    using corundum::ecs::CollisionTable;
    using corundum::ecs::EntityId;
    using corundum::ecs::Position;

    const std::uint32_t p_slot = transforms.dense_idx(player);
    const float prev_col = transforms.col[p_slot];
    const float prev_row = transforms.row[p_slot];

    // The floor the player is standing on at the start of the frame — computed from the
    // pre-move position so collision resolution doesn't depend on its own not-yet-resolved
    // result. Uses round-to-nearest + ramp-aware tolerance (see compute_elevation_gate)
    // so authored colliders at both ends of a ramp still block at mid-ramp. Plan §3c.
    const auto elev_gate = compute_elevation_gate(map, prev_col, prev_row);
    const int player_elev = elev_gate.player_elevation;

    // A click queues a new path. Deliberately keyed on mouse_click_pressed, not
    // Action::Select — Select is also raised by keyboard/gamepad confirm presses (which
    // carry no click position), and using it here would spuriously queue a path toward
    // wherever the mouse happens to be hovering any time the player presses Enter/Space/
    // a gamepad button for an unrelated reason (e.g. confirming dialogue).
    if (input.mouse_click_pressed && scene.hovered_tile && map.walkability) {
      // std::floor (not truncate) so a fractionally-negative prev_col/prev_row selects
      // the cell the player is actually standing in — same convention as chunk_at_iso
      // and picking. Latent today (positions clamped >= 0), defensive against future
      // knockback / camera-shake paths.
      const corundum::gameplay::world::TileCoord start{static_cast<int>(std::floor(prev_col)),
                                                       static_cast<int>(std::floor(prev_row))};
      scene.path = corundum::gameplay::world::find_path(map, start, *scene.hovered_tile, &collisions, &transforms);
    }

    const bool manual_move =
        input.is_held(corundum::input::Action::MoveUp) || input.is_held(corundum::input::Action::MoveDown) ||
        input.is_held(corundum::input::Action::MoveLeft) || input.is_held(corundum::input::Action::MoveRight);
    const IsometricParams iso{map.half_tw, map.half_th, 0.f, 0.f};
    if (manual_move) {
      scene.path.clear(); // manual input always overrides/cancels an active path
      apply_input(transforms, player, input, player_speed, iso);
    } else if (!scene.path.empty()) {
      follow_path(transforms, player, scene.path, player_speed, iso, dt);
    } else {
      apply_input(transforms, player, input, player_speed, iso); // zeroes dc/dr when nothing is held
    }
    // Integration happens inside the substep loop below; this frame's integrate+resolve
    // is substepped against per-cell displacement so the resolver can't skip geometry
    // at high speed (see k_substep_max_tile / WalkabilityGraph::can_move denial below).

    const float map_w = map.world_w_tiles;
    const float map_h = map.world_h_tiles;

    const CollisionTable::Rect &player_rect = collisions.get_rect(player);

    // Tunneling guard: substep integrate+resolve when the frame's displacement exceeds
    // ~0.5 tile, so the resolver never has to clear more than one cell of geometry and
    // thin walls can't be skipped at high speed. Pair with WalkabilityGraph::can_move's
    // multi-cell denial (now false) so this loop is forced to take per-cell steps
    // against the graph. 1 substep when displacement is small — math identical to the
    // pre-substep path.
    const float step_dist = std::hypot(transforms.dc[p_slot], transforms.dr[p_slot]) * dt;
    constexpr float k_substep_max_tile = 0.5f;
    const int substeps = std::max(1, static_cast<int>(std::ceil(step_dist / k_substep_max_tile)));
    const float sub_dt = dt / static_cast<float>(substeps);

    Position p{prev_col, prev_row};
    transforms.col[p_slot] = p.col;
    transforms.row[p_slot] = p.row;
    for (int s = 0; s < substeps; ++s) {
      const Position sub_prev{p.col, p.row};
      integrate(transforms, player, sub_dt);
      p.col = transforms.col[p_slot];
      p.row = transforms.row[p_slot];

      if (map.half_tw > 0.f && map.half_th > 0.f) {
        const float half_cs = player_rect.col_span / 2.f;
        // AABB extends upward from the feet position.
        Position pc{p.col - half_cs, p.row - player_rect.row_span};
        const Position pcp{sub_prev.col - half_cs, sub_prev.row - player_rect.row_span};
        resolve_collisions(pc, pcp, player_rect.col_span, player_rect.row_span, map.collisions, 0.f, player_elev,
                           elev_gate.tolerance);
        resolve_triangle_collisions(pc, pcp, player_rect.col_span, player_rect.row_span, map.collision_triangles, 0.f,
                                    player_elev, elev_gate.tolerance);
        // Convert AABB top-left back to feet position.
        p.col = pc.col + half_cs;
        p.row = pc.row + player_rect.row_span;
      }
      resolve_walkability(p, sub_prev, map.walkability);
      // Write resolved position back so the next substep's integrate starts from here.
      transforms.col[p_slot] = p.col;
      transforms.row[p_slot] = p.row;
    }
    const Position prev_pos{prev_col, prev_row};

    std::array<float, corundum::ecs::k_max_entities> npc_cols{}, npc_rows{}, npc_cs{}, npc_rs{};
    // NPC elevations populated so resolve_collisions can gate player-vs-NPC by elevation
    // (same band as player-vs-world). Without this, an NPC under a bridge (elev 0) would
    // block a player on the bridge (elev 5). Plan §4a.
    std::array<uint8_t, corundum::ecs::k_max_entities> npc_elevations{};
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
      const float np_col = transforms.col[np_slot];
      const float np_row = transforms.row[np_slot];
      npc_cols[npc_count] = np_col - half_npc_cs;
      npc_rows[npc_count] = np_row - rect.row_span;
      npc_cs[npc_count] = rect.col_span;
      npc_rs[npc_count] = rect.row_span;
      npc_elevations[npc_count] =
          static_cast<uint8_t>(std::round(corundum::gameplay::world::elevation_at_tile(map, np_col, np_row)));
      ++npc_count;
    }
    const corundum::gameplay::world::tilemap::CollisionRectsView npc_view{
        std::span{npc_cols.data(), npc_count}, std::span{npc_rows.data(), npc_count},
        std::span{npc_cs.data(), npc_count}, std::span{npc_rs.data(), npc_count},
        std::span{npc_elevations.data(), npc_count}};
    // Convert player feet to AABB top-left for NPC collision.
    {
      const float half_cs = player_rect.col_span / 2.f;
      Position p_aabb{p.col - half_cs, p.row - player_rect.row_span};
      const Position prev_aabb{prev_pos.col - half_cs, prev_pos.row - player_rect.row_span};
      resolve_collisions(p_aabb, prev_aabb, player_rect.col_span, player_rect.row_span, npc_view, 0.f, player_elev,
                         elev_gate.tolerance);
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
      // Elevation gate: a player on a bridge (elev 5) crossing a ground-floor portal cell
      // (elev 0) must NOT trigger the portal — see portal_elev_matches / plan §4a.
      const float half_cs = player_rect.col_span / 2.f;
      const float col0 = p.col - half_cs;
      const float col1 = p.col + half_cs;
      const float row0 = p.row;
      const float row1 = p.row + player_rect.row_span;
      // Clear a declined prompt once the player walks off its rect — otherwise standing on a
      // portal they cancelled would suppress every subsequent re-prompt forever.
      if (scene.transition_prompt && scene.transition_prompt->declined() &&
          !scene.transition_prompt->overlaps(col0, col1, row0, row1))
        scene.transition_prompt.reset();
      for (const auto &portal : map.portals) {
        if (col1 > portal.col && col0 < portal.col + portal.w && row1 > portal.row && row0 < portal.row + portal.h) {
          if (!portal_elev_matches(map, portal, player_elev, elev_gate.tolerance))
            continue;
          if (portal.target_chunk_col >= 0) {
            p.col = static_cast<float>(portal.spawn_col);
            p.row = static_cast<float>(portal.spawn_row);
            transforms.col[p_slot] = p.col;
            transforms.row[p_slot] = p.row;
            return;
          }
          // Suppress re-prompting while standing on a portal the player already declined.
          if (scene.transition_prompt && scene.transition_prompt->declined() && scene.transition_prompt->guards(portal))
            continue;
          scene.transition_prompt.emplace(portal);
          scene.mode = corundum::gameplay::world::GameMode::Prompt;
          return;
        }
      }
    }
  }

} // namespace corundum::physics
