// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/render/chunk_window.hpp>
#include <corundum/sprites/sprite.hpp>
#include <corundum/ui/font_family.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/world/portals/portal.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <corundum/world/tilemap/walkability.hpp>
#include <corundum/world/tilemap/world_manifest.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace corundum::render {

  /** @brief Tracks whether the renderer is in map or world mode. */
  enum class RenderMode : uint8_t { None, SingleMap, World };

  /** @brief Result of a sprite-id / anim-id / frame-index lookup.
   *
   * Maps a logical sprite frame to its texture ID and source rectangle.
   * Stores frame rectangles in a single contiguous buffer with flat offset
   * and count tables indexed by (sprite_id * k_num_anim_ids + anim_id).
   *
   * The tables are parallel and must agree: anim_offsets and anim_frame_counts share
   * one index space and length, and each (offset, count) pair must address a valid
   * window into frame_rects. get() verifies that window before dereferencing.
   *
   * @performance O(1) lookup with no pointer chasing — all frame data in contiguous arrays.
   */
  struct SpriteFrameIndex {
    std::vector<std::optional<uint32_t>> tex_by_sprite_id;
    std::vector<corundum::core::math::IntRect> frame_rects;
    std::vector<float> walk_offsets; ///< walk_around_offset per sprite_id.
    std::vector<uint32_t> anim_offsets;
    std::vector<uint8_t> anim_frame_counts;

    /// Lookup result: texture ID, source rect, and walk_around_offset.
    struct Entry {
      uint32_t tex_id = 0;
      corundum::core::math::IntRect src{};
      float walk_offset = 0.f;
    };

    /** @brief Look up the texture ID, source rect, and walk offset for a given frame.
     *  @param[in] sprite_id   Interned sprite identifier.
     *  @param[in] anim_id     Animation identifier.
     *  @param[in] frame_index Zero-based frame within the animation.
     *  @return Entry with tex_id, src, walk_offset, or std::nullopt.
     */
    [[nodiscard]] std::optional<Entry> get(corundum::sprites::SpriteId sprite_id, corundum::sprites::AnimId anim_id,
                                           uint8_t frame_index) const noexcept;
  };

  /** @brief A single pre-loaded tilemap (map mode, not world mode). */
  struct MapData {
    corundum::world::tilemap::Tilemap tilemap;
    std::vector<uint32_t> tileset_texture_ids;
    std::vector<int> above_z;
    std::vector<corundum::world::Portal> portals;
  };

  /** @brief Sorted draw-list entry for depth-ordered ground-layer rendering (tiles and entities). */
  struct DepthEntry {
    uint32_t tex_id = 0;
    corundum::core::math::IntRect src{};
    float x = 0.f;
    float y = 0.f;
    float depth = 0.f;
    float scale = 1.f;
    bool flip_x = false;
    bool flip_y = false;
  };

  /** @brief All mutable rendering state for the active map or world.
   *
   * Mostly passive data: free functions in namespace corundum::render own the logic.
   * ChunkWindow is the one member that keeps behaviour, encapsulating its own
   * streaming invariants (draw order, dirty flag, offset→slot lookup) so callers
   * never have to uphold them by hand.
   */
  struct RenderState {
    corundum::world::tilemap::CollisionRects agg_collisions{};
    /// Aggregated portal buffer for world mode — rebuilt by rebuild_world_aggregates()
    /// alongside agg_collisions/agg_walkability, then returned as a span via MapView.
    /// Single-map mode passes map_data.portals directly.
    std::vector<corundum::world::Portal> agg_portals;
    corundum::world::tilemap::CollisionTriangles agg_triangles{};
    /// Aggregated walkability graph spanning the active chunk window (world mode only).
    /// Rebuilt by rebuild_world_walkability() alongside agg_collisions; stays empty in
    /// single-map mode, which uses map_walkability instead.
    corundum::world::tilemap::WalkabilityGraph agg_walkability{};
    ChunkWindow chunks{};
    corundum::ui::PanelSkin panel_skin{};
    /// Per-role font families, indexed by ui::FontRole; loaded by load_fonts().
    std::array<corundum::ui::FontFamily, corundum::ui::k_font_role_count> fonts{};

    /// @return The family for @p role.
    [[nodiscard]] const corundum::ui::FontFamily &family(corundum::ui::FontRole role) const noexcept {
      return fonts[static_cast<std::size_t>(role)];
    }

    corundum::world::tilemap::WorldManifest manifest{};
    MapData map_data{};
    /// Built once when a single map loads (load_map()); single-map mode only, same
    /// limitation as MapView::elevation_map — World mode leaves this default-empty.
    corundum::world::tilemap::WalkabilityGraph map_walkability{};
    /// Textures the loaded area (single map or streamed world) holds one reference on each.
    /// Released when the area is replaced or unloaded; see render::unload_world().
    std::vector<uint32_t> area_texture_ids;
    RenderMode mode{RenderMode::None};
    SpriteFrameIndex sprite_index;

    /// Font/margin scale applied by configure_panel_style(); 1 = the game.json sizes. Set from
    /// UserSettings::ui_scale.
    float ui_scale{1.f};

    /// Dialogue text-reveal multiplier applied to k_base_reveal_chars_per_second; 0 = instant.
    /// Set from UserSettings::text_speed.
    float text_speed{1.f};
    std::vector<int> above_z_cache;
    std::vector<DepthEntry> draw_list;
    /** @brief Indices into draw_list, sorted by depth each frame. Reused across frames
     *  (resized, never freed) so the depth sort touches no per-frame heap allocation. */
    std::vector<uint32_t> draw_order;

    /** @brief Camera x at the start of the latest fixed step, for render interpolation. */
    float prev_cam_x{0.f};
    /** @brief Camera y at the start of the latest fixed step, for render interpolation. */
    float prev_cam_y{0.f};
    /** @brief Entity tile columns at the start of the latest fixed step, indexed by
     *  EntityId::index (not dense slot) so a swap-and-pop removal never misattributes a
     *  snapshot. An entry is valid only where prev_generation matches the entity's generation. */
    std::array<float, corundum::entities::k_max_entities> prev_col{};
    /** @brief Generation of the entity each prev_col/prev_row entry was captured from; 0 (never
     *  a live generation) marks an entry that was never written. */
    std::array<std::uint32_t, corundum::entities::k_max_entities> prev_generation{};
    /** @brief Entity tile rows at the start of the latest fixed step. See prev_col. */
    std::array<float, corundum::entities::k_max_entities> prev_row{};
    /** @brief Camera zoom at the start of the latest fixed step, for render interpolation. */
    float prev_zoom{1.f};
  };

  /** @brief Combined collision views for debug rendering and collision queries.
   *
   * Bundles rect and triangle collision data from the currently active render source.
   * Abstracts away the World-vs-SingleMap distinction so consumers don't branch on mode.
   */
  struct CollisionGeometry {
    world::tilemap::CollisionRectsView rects{};
    world::tilemap::CollisionTrianglesView tris{};
  };

  /** @brief Return the collision geometry for the currently active render mode.
   *  @param[in] rs  Initialised render state.
   *  @return CollisionGeometry with valid (possibly empty) views.
   */
  [[nodiscard]] inline CollisionGeometry current_collisions(const RenderState &rs) noexcept {
    if (rs.mode == RenderMode::World) {
      return {.rects = rs.agg_collisions.view(), .tris = rs.agg_triangles.view()};
    }
    if (rs.mode == RenderMode::SingleMap) {
      return {.rects = rs.map_data.tilemap.collisions.view(), .tris = rs.map_data.tilemap.collision_triangles.view()};
    }
    return {};
  }

  /** @brief The single loaded tilemap, if the engine is in single-map mode.
   *  @param[in] rs  Initialised render state.
   *  @return Pointer to the active tilemap, or nullptr in World mode (which streams
   *          one tilemap per chunk — see RenderState::chunks) and before load. */
  [[nodiscard]] inline const world::tilemap::Tilemap *active_tilemap(const RenderState &rs) noexcept {
    return rs.mode == RenderMode::SingleMap ? &rs.map_data.tilemap : nullptr;
  }

} // namespace corundum::render
