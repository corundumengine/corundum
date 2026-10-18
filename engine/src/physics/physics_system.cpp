// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include <corundum/core/math/isometric.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/entities/components.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/entities/tables/collision_table.hpp>
#include <corundum/input/actions.hpp>
#include <corundum/physics/collision.hpp>
#include <corundum/physics/physics_system.hpp>
#include <corundum/physics/walkability.hpp>
#include <corundum/world/map_view.hpp>
#include <corundum/world/pathfinding.hpp>
#include <corundum/world/picking.hpp>
#include <corundum/world/portals/portal.hpp>
#include <corundum/world/scene.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <corundum/world/tilemap/walkability.hpp>
#include <corundum/world/ui_stack.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace corundum::physics {

  namespace {
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
      return {
          .x = ((svx / iso.half_tw) + (svy / iso.half_th)) / 2.f,
          .y = ((svy / iso.half_th) - (svx / iso.half_tw)) / 2.f,
      };
    }

    /** @brief Borrow @p graph as a reference when present, or std::nullopt when null. */
    [[nodiscard]] std::optional<std::reference_wrapper<const corundum::world::tilemap::WalkabilityGraph>>
    borrow_walkability(const corundum::world::tilemap::WalkabilityGraph *graph) noexcept {
      return graph != nullptr ? std::make_optional(std::cref(*graph)) : std::nullopt;
    }

    /** @brief Test the player's post-move AABB against every portal in @p map.
     *
     * Both the AABB and the portal rects are tile-grid, so no iso↔cart conversion is needed. A
     * chunk-to-chunk portal teleports immediately (writing the spawn back through @p transforms);
     * a cross-map or return-to-world portal arms scene.transition_prompt, so the actual map swap
     * waits for the player to confirm. Clears a declined prompt once the player steps off its rect
     * — otherwise standing on a portal they cancelled would suppress every later re-prompt.
     * Elevation-gated via portal_elev_matches, so a player on a bridge does not trip a
     * ground-floor portal authored at the same cell.
     *
     * @pre @p player_slot is the dense index of the player in @p transforms.
     */
    void resolve_portals(corundum::entities::TransformTable &transforms, std::uint32_t player_slot,
                         corundum::entities::Position pos, float col_span, float row_span,
                         const corundum::world::MapView &map, corundum::world::Scene &scene, int player_elev,
                         int elev_tolerance) noexcept {
      if (map.portals.empty())
        return;

      // Same footprint the collision pass uses: a ground patch centred on the feet, so it
      // covers the tile the player stands on and does not reach into its neighbours.
      const corundum::entities::GridBox footprint =
          corundum::entities::footprint_of(pos.col, pos.row, col_span, row_span);
      const float col0 = footprint.col;
      const float col1 = footprint.col + footprint.col_span;
      const float row0 = footprint.row;
      const float row1 = footprint.row + footprint.row_span;

      if (scene.transition_prompt && scene.transition_prompt->declined() &&
          !scene.transition_prompt->overlaps(col0, col1, row0, row1))
        scene.transition_prompt.reset();

      for (const auto &portal : map.portals) {
        if (!corundum::world::portal_overlaps(portal, col0, col1, row0, row1))
          continue;
        if (!portal_elev_matches(map, portal, player_elev, elev_tolerance))
          continue;
        if (portal.target_chunk_col >= 0) {
          transforms.col[player_slot] = static_cast<float>(portal.spawn_col);
          transforms.row[player_slot] = static_cast<float>(portal.spawn_row);
          return;
        }
        // Suppress re-prompting while standing on a portal the player already declined.
        if (scene.transition_prompt && scene.transition_prompt->declined() && scene.transition_prompt->guards(portal))
          continue;
        scene.transition_prompt.emplace(portal);
        scene.ui.push(corundum::world::GameMode::Prompt);
        return;
      }
    }

    /// Queue a click-to-move path from the player's current cell to the hovered tile.
    ///
    /// Deliberately keyed on mouse_click_pressed, not Action::Select — Select is also raised by
    /// keyboard/gamepad confirm presses (which carry no click position), and using it here would
    /// spuriously queue a path toward wherever the mouse happens to hover any time the player
    /// presses Enter/Space/a gamepad button for an unrelated reason (e.g. confirming dialogue).
    void queue_click_path(corundum::world::Scene &scene, const corundum::world::MapView &map,
                          const corundum::input::InputState &input,
                          const corundum::entities::CollisionTable &collisions,
                          const corundum::entities::TransformTable &transforms, corundum::entities::EntityId player,
                          float prev_col, float prev_row) noexcept {
      if (!input.mouse_click_pressed || !scene.hovered_tile || map.walkability == nullptr)
        return;
      // std::floor (not truncate) so a fractionally-negative prev_col/prev_row selects the cell the
      // player is actually standing in — same convention as chunk_at_iso and picking. Latent today
      // (positions clamped >= 0), defensive against future knockback / camera-shake paths.
      const corundum::world::TileCoord start{
          .col = static_cast<int>(std::floor(prev_col)),
          .row = static_cast<int>(std::floor(prev_row)),
      };
      scene.path = corundum::world::find_path(map, start, *scene.hovered_tile, &collisions, &transforms, player);
    }

    /** @brief Snapshot of every non-player entity's collision AABB and rounded tile elevation. */
    struct NpcColliders {
      std::array<float, corundum::entities::k_max_entities> cols{};
      std::array<float, corundum::entities::k_max_entities> rows{};
      std::array<float, corundum::entities::k_max_entities> col_spans{};
      std::array<float, corundum::entities::k_max_entities> row_spans{};
      std::array<uint8_t, corundum::entities::k_max_entities> elevations{};
      uint16_t count{};

      [[nodiscard]] corundum::world::tilemap::CollisionRectsView view() const noexcept {
        return {
            .cols = std::span{cols.data(), count},
            .rows = std::span{rows.data(), count},
            .col_spans = std::span{col_spans.data(), count},
            .row_spans = std::span{row_spans.data(), count},
            .elevations = std::span{elevations.data(), count},
        };
      }
    };

    /// Fill @p out with every entity in @p collisions except @p exclude, converting each feet
    /// position to a collision AABB top-left and capturing its elevation so the player-vs-NPC pass
    /// can gate by elevation (same band as player-vs-world).
    void gather_npc_colliders(NpcColliders &out, corundum::entities::TransformTable &transforms,
                              const corundum::entities::CollisionTable &collisions,
                              corundum::entities::EntityId exclude, const corundum::world::MapView &map) noexcept {
      for (std::uint32_t i = 0; i < collisions.count; ++i) {
        const corundum::entities::EntityId eid = collisions.index.entities[i];
        if (eid == exclude)
          continue;
        if (!transforms.has(eid))
          continue;
        const auto &rect = collisions.rects[i];
        const auto slot = transforms.dense_index(eid);
        const float np_col = transforms.col[slot];
        const float np_row = transforms.row[slot];
        // NPC feet position to AABB top-left, per the shared CollisionTable convention.
        const corundum::entities::GridBox box =
            corundum::entities::footprint_of(np_col, np_row, rect.col_span, rect.row_span);
        out.cols[out.count] = box.col;
        out.rows[out.count] = box.row;
        out.col_spans[out.count] = box.col_span;
        out.row_spans[out.count] = box.row_span;
        out.elevations[out.count] =
            static_cast<uint8_t>(std::round(corundum::world::elevation_at_tile(map, np_col, np_row)));
        ++out.count;
      }
    }

    /// Integrate the player in ≤0.5-tile substeps, resolving tile rect/triangle collision and
    /// walkability each step, and return the resolved tile-grid position. Substepping is the
    /// tunneling guard: at high speed the resolver never has to clear more than one cell of
    /// geometry, so thin walls can't be skipped. The initial position is wherever the movement
    /// branch left the entity, so a follow_path snap is preserved.
    [[nodiscard]] corundum::entities::Position integrate_and_resolve_player(
        corundum::entities::TransformTable &transforms, corundum::entities::EntityId player, std::uint32_t player_slot,
        const corundum::world::MapView &map, const corundum::entities::CollisionTable::Rect &player_rect,
        ElevationGate elev_gate,
        std::optional<std::reference_wrapper<const corundum::world::tilemap::WalkabilityGraph>> walkability,
        float dt) noexcept {
      const float step_dist = std::hypot(transforms.dc[player_slot], transforms.dr[player_slot]) * dt;
      constexpr float k_substep_max_tile = 0.5f;
      const int substeps = std::max(1, static_cast<int>(std::ceil(step_dist / k_substep_max_tile)));
      const float sub_dt = dt / static_cast<float>(substeps);

      corundum::entities::Position p{.col = transforms.col[player_slot], .row = transforms.row[player_slot]};
      for (int s = 0; s < substeps; ++s) {
        const corundum::entities::Position sub_prev{.col = p.col, .row = p.row};
        integrate(transforms, player, sub_dt);
        p.col = transforms.col[player_slot];
        p.row = transforms.row[player_slot];

        if (map.half_tw > 0.f && map.half_th > 0.f) {
          const corundum::entities::GridBox box =
              corundum::entities::footprint_of(p.col, p.row, player_rect.col_span, player_rect.row_span);
          const corundum::entities::GridBox prev_box =
              corundum::entities::footprint_of(sub_prev.col, sub_prev.row, player_rect.col_span, player_rect.row_span);
          corundum::entities::Position pc{.col = box.col, .row = box.row};
          const corundum::entities::Position pcp{.col = prev_box.col, .row = prev_box.row};
          resolve_collisions(pc, pcp, box.col_span, box.row_span, map.collisions, 0.f, elev_gate);
          resolve_triangle_collisions(pc, pcp, box.col_span, box.row_span, map.collision_triangles, 0.f, elev_gate);
          p = corundum::entities::position_of(corundum::entities::GridBox{
              .col = pc.col,
              .row = pc.row,
              .col_span = box.col_span,
              .row_span = box.row_span,
          });
        }
        resolve_walkability(p, sub_prev, walkability);
        transforms.col[player_slot] = p.col;
        transforms.row[player_slot] = p.row;
      }
      return p;
    }

    /// Resolve the player's footprint against the gathered NPC boxes, anchored on the same point
    /// as the world pass, and return the corrected tile-grid position.
    [[nodiscard]] corundum::entities::Position
    resolve_npc_collisions(const corundum::entities::CollisionTable::Rect &player_rect,
                           corundum::world::tilemap::CollisionRectsView npc_view, corundum::entities::Position pos,
                           corundum::entities::Position prev_pos, ElevationGate elev_gate) noexcept {
      const corundum::entities::GridBox box =
          corundum::entities::footprint_of(pos.col, pos.row, player_rect.col_span, player_rect.row_span);
      const corundum::entities::GridBox prev_box =
          corundum::entities::footprint_of(prev_pos.col, prev_pos.row, player_rect.col_span, player_rect.row_span);
      corundum::entities::Position p_aabb{.col = box.col, .row = box.row};
      const corundum::entities::Position prev_aabb{.col = prev_box.col, .row = prev_box.row};
      resolve_collisions(p_aabb, prev_aabb, box.col_span, box.row_span, npc_view, 0.f, elev_gate);
      return corundum::entities::position_of(corundum::entities::GridBox{
          .col = p_aabb.col,
          .row = p_aabb.row,
          .col_span = box.col_span,
          .row_span = box.row_span,
      });
    }

    /// Clamp the stored position so the footprint never leaves the map.
    [[nodiscard]] corundum::entities::Position
    clamp_to_map_bounds(corundum::entities::Position pos, const corundum::entities::CollisionTable::Rect &player_rect,
                        const corundum::world::MapView &map) noexcept {
      // The footprint sits half a tile in from the stored position: the position may reach
      // map_w - 0.5 - col_span/2, not map_w - col_span. The lower bound stays 0 so the position
      // itself never leaves the map — stricter than the box needs, and harmless.
      const float col_limit =
          std::max(0.f, map.world_w_tiles - corundum::entities::k_entity_anchor_offset - (player_rect.col_span * 0.5f));
      const float row_limit =
          std::max(0.f, map.world_h_tiles - corundum::entities::k_entity_anchor_offset - (player_rect.row_span * 0.5f));
      pos.col = std::clamp(pos.col, 0.f, col_limit);
      pos.row = std::clamp(pos.row, 0.f, row_limit);
      return pos;
    }

  } // namespace

  ElevationGate compute_elevation_gate(const corundum::world::MapView &map, float col, float row) noexcept {
    using corundum::world::tilemap::elevation_at;
    using corundum::world::tilemap::ramp_axis_at;
    using corundum::world::tilemap::RampAxis;

    ElevationGate gate{};
    const float elev_f = corundum::world::elevation_at_tile(map, col, row);
    gate.player_elevation = static_cast<int>(std::round(elev_f));

    // World mode has no Tilemap to query — elevation_tolerance stays 0 (no ramps to widen for).
    if (map.elevation_map == nullptr)
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

  bool portal_elev_matches(const corundum::world::MapView &map, const corundum::world::Portal &portal, int player_elev,
                           int elev_tolerance) noexcept {
    // Use the portal's center cell — a multi-cell portal's intent is "the player is on
    // the floor that runs through here", so the center cell is the canonical reference.
    const float portal_elev_f =
        corundum::world::elevation_at_tile(map, portal.col + (portal.w * 0.5f), portal.row + (portal.h * 0.5f));
    const int portal_elev = static_cast<int>(std::round(portal_elev_f));
    return std::abs(portal_elev - player_elev) <= elev_tolerance;
  }

  void integrate(corundum::entities::TransformTable &transforms, corundum::entities::EntityId e, float dt) noexcept {
    const auto slot = transforms.dense_index(e);
    transforms.col[slot] += transforms.dc[slot] * dt;
    transforms.row[slot] += transforms.dr[slot] * dt;
  }

  void follow_path(corundum::entities::TransformTable &transforms, corundum::entities::EntityId player,
                   std::vector<corundum::world::TileCoord> &path, float player_speed,
                   corundum::core::math::IsometricParams iso, float dt) noexcept {
    if (!transforms.has(player)) [[unlikely]]
      return;
    const std::uint32_t slot = transforms.dense_index(player);

    while (!path.empty()) {
      // Waypoints are tile indices, which is exactly the entity's stored position — so
      // there is no half-tile offset here; the sprite's feet anchor (the tile centre) is
      // applied once, by footprint_of.
      const float target_col = static_cast<float>(path.front().col);
      const float target_row = static_cast<float>(path.front().row);
      const float dc = target_col - transforms.col[slot];
      const float dr = target_row - transforms.row[slot];

      if (iso.half_tw > 0.f && iso.half_th > 0.f) {
        const auto [svx, svy] = tile_to_screen_delta(dc, dr, iso);
        const float screen_dist = std::hypot(svx, svy);

        if (screen_dist > player_speed * dt) {
          const float scale = player_speed / screen_dist;
          const auto [tdc, tdr] = screen_to_tile_delta(svx * scale, svy * scale, iso);
          transforms.dc[slot] = tdc;
          transforms.dr[slot] = tdr;
          return;
        }
      } else {
        const float dist = std::hypot(dc, dr);

        if (dist > player_speed * dt) {
          const float inv_dist = player_speed / dist;
          transforms.dc[slot] = dc * inv_dist;
          transforms.dr[slot] = dr * inv_dist;
          return;
        }
      }

      // This frame's movement reaches the front waypoint: snap onto it and take the next.
      transforms.col[slot] = target_col;
      transforms.row[slot] = target_row;
      path.erase(path.begin());
    }

    transforms.dc[slot] = 0.f;
    transforms.dr[slot] = 0.f;
  }

  void apply_input(corundum::entities::TransformTable &transforms, corundum::entities::EntityId player,
                   const corundum::input::InputState &input, float player_speed,
                   corundum::core::math::IsometricParams iso) noexcept {
    if (!transforms.has(player)) [[unlikely]]
      return;

    const std::uint32_t slot = transforms.dense_index(player);
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

    const float len_sq = (dc * dc) + (dr * dr);
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

  void update_player(corundum::entities::TransformTable &transforms,
                     const corundum::entities::CollisionTable &collisions, corundum::entities::EntityId player,
                     const corundum::input::InputState &input, float player_speed, const corundum::world::MapView &map,
                     corundum::world::Scene &scene, float dt) noexcept {
    using corundum::core::math::IsometricParams;
    using corundum::entities::CollisionTable;
    using corundum::entities::EntityId;
    using corundum::entities::Position;

    const std::uint32_t p_slot = transforms.dense_index(player);
    const float prev_col = transforms.col[p_slot];
    const float prev_row = transforms.row[p_slot];

    // The floor the player is standing on at the start of the frame — computed from the
    // pre-move position so collision resolution doesn't depend on its own not-yet-resolved
    // result. Uses round-to-nearest + ramp-aware tolerance (see compute_elevation_gate)
    // so authored colliders at both ends of a ramp still block at mid-ramp.
    const auto elev_gate = compute_elevation_gate(map, prev_col, prev_row);
    const int player_elev = elev_gate.player_elevation;

    queue_click_path(scene, map, input, collisions, transforms, player, prev_col, prev_row);

    const bool manual_move =
        input.is_held(corundum::input::Action::MoveUp) || input.is_held(corundum::input::Action::MoveDown) ||
        input.is_held(corundum::input::Action::MoveLeft) || input.is_held(corundum::input::Action::MoveRight);
    const IsometricParams iso{.half_tw = map.half_tw, .half_th = map.half_th, .x_origin = 0.f, .elev_step = 0.f};
    if (manual_move) {
      scene.path.clear(); // manual input always overrides/cancels an active path
      apply_input(transforms, player, input, player_speed, iso);
    } else if (!scene.path.empty()) {
      follow_path(transforms, player, scene.path, player_speed, iso, dt);
    } else {
      apply_input(transforms, player, input, player_speed, iso); // zeroes dc/dr when nothing is held
    }

    const CollisionTable::Rect &player_rect = collisions.get_rect(player);
    const Position prev_pos{.col = prev_col, .row = prev_row};
    // Borrowed once per step; resolve_walkability takes no graph as std::nullopt so the
    // no-graph case is carried by the type rather than a null sentinel.
    const auto walkability = borrow_walkability(map.walkability);

    Position p = integrate_and_resolve_player(transforms, player, p_slot, map, player_rect, elev_gate, walkability, dt);

    NpcColliders npcs;
    gather_npc_colliders(npcs, transforms, collisions, player, map);
    p = resolve_npc_collisions(player_rect, npcs.view(), p, prev_pos, elev_gate);

    p = clamp_to_map_bounds(p, player_rect, map);
    transforms.col[p_slot] = p.col;
    transforms.row[p_slot] = p.row;

    resolve_portals(transforms, p_slot, p, player_rect.col_span, player_rect.row_span, map, scene, player_elev,
                    elev_gate.tolerance);
  }

} // namespace corundum::physics
