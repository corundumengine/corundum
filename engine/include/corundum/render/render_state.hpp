// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/sprites/sprite.hpp>
#include <corundum/ui/dialog_box.hpp>
#include <corundum/world/portals/portal.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <corundum/world/tilemap/walkability.hpp>
#include <corundum/world/tilemap/world_manifest.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
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

  /** @brief A single loaded world chunk (world mode only). */
  struct ChunkEntry {
    corundum::world::tilemap::ChunkCoord coord{};
    corundum::world::tilemap::Tilemap tilemap{};
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

  /** @brief Owns the World-mode streamed chunk window: the active/pending chunk
   *  sets, the last-synced center chunk, the offset→slot lookup used by
   *  elevation_under(), and the dirty flag that gates the above_z_cache rebuild in
   *  render(). Every mutator that changes the active set marks it dirty, and every
   *  mutator that moves the window or reshuffles the active set re-derives the
   *  offset→slot lookup — callers never touch either by hand.
   *
   *  active() is kept in the canonical back-to-front draw order (see sort_active)
   *  so streamed-in chunks always draw in the same order as the load_world bootstrap.
   */
  class ChunkWindow {
  public:
    /// Streaming radius in chunks: the resident window is a (2 * k_radius + 1) square.
    static constexpr int k_radius = 1;

    /// Chunks per side of the resident window.
    static constexpr int k_side = (2 * k_radius) + 1;

    /// Maximum chunks resident at once; the bound callers may reserve against.
    static constexpr std::size_t k_max_active_chunks = static_cast<std::size_t>(k_side) * k_side;

    [[nodiscard]] const std::vector<ChunkEntry> &active() const noexcept {
      return active_;
    }

    [[nodiscard]] bool active_empty() const noexcept {
      return active_.empty();
    }

    [[nodiscard]] std::size_t active_size() const noexcept {
      return active_.size();
    }

    [[nodiscard]] const ChunkEntry &active_at(std::size_t i) const noexcept {
      return active_[i];
    }

    /// @return true if the active set changed since the last call, clearing the flag.
    [[nodiscard]] bool consume_dirty() noexcept {
      const bool was_dirty = dirty_;
      dirty_ = false;
      return was_dirty;
    }

    [[nodiscard]] world::tilemap::ChunkCoord last_center() const noexcept {
      return last_center_;
    }

    /// Recenter the window and re-derive the offset→slot lookup.
    void set_last_center(world::tilemap::ChunkCoord c) noexcept {
      last_center_ = c;
      rebuild_slot_table();
    }

    /// @return slot index into active() for the chunk at (dx,dy) from last_center(), or -1.
    [[nodiscard]] int32_t slot_at_offset(int dx, int dy) const noexcept {
      if (dx < -k_radius || dx > k_radius || dy < -k_radius || dy > k_radius)
        return -1;
      return slot_by_offset_[offset_index(dx, dy)];
    }

    /// Add a freshly loaded chunk to the active set. Always marks dirty.
    void add_active(ChunkEntry entry) {
      active_.push_back(std::move(entry));
      sort_active();
      dirty_ = true;
    }

    /// Remove active chunks for which @p keep returns false, re-deriving the slot table.
    /// @return true if any chunk was removed.
    template <typename Keep> bool prune_active(Keep &&keep) {
      const std::size_t before = active_.size();
      std::erase_if(active_, [&](const ChunkEntry &e) { return !std::forward<Keep>(keep)(e); });
      if (active_.size() == before)
        return false;
      dirty_ = true;
      rebuild_slot_table();
      return true;
    }

    /// True if @p c is already active or already queued in pending.
    [[nodiscard]] bool has(world::tilemap::ChunkCoord c) const noexcept {
      return std::ranges::any_of(active_, [&](const ChunkEntry &e) { return e.coord == c; }) ||
             std::ranges::contains(pending_, c);
    }

    /// True if @p c is resident in the active window (ignores chunks still pending).
    [[nodiscard]] bool is_active(world::tilemap::ChunkCoord c) const noexcept {
      return std::ranges::any_of(active_, [&](const ChunkEntry &e) { return e.coord == c; });
    }

    /// Queue @p c for loading. Caller should check has(c) first to avoid duplicates
    /// (kept explicit rather than implicit to match the existing call-site logic).
    void enqueue_pending(world::tilemap::ChunkCoord c) {
      pending_.push_back(c);
    }

    /// Pop the front of the pending queue into @p out.
    /// @return false if pending is empty (out left unchanged).
    [[nodiscard]] bool pop_pending(world::tilemap::ChunkCoord &out) {
      if (pending_.empty())
        return false;
      out = pending_.front();
      pending_.erase(pending_.begin());
      return true;
    }

    /// Reset to the empty, never-loaded state (used by clean_up()).
    void clear() noexcept {
      active_.clear();
      pending_.clear();
      last_center_ = {};
      slot_by_offset_ = empty_slots();
      dirty_ = true;
    }

  private:
    /// Flatten a window offset into a slot_by_offset_ index. Callers bound-check first.
    [[nodiscard]] static constexpr std::size_t offset_index(int dx, int dy) noexcept {
      return (static_cast<std::size_t>(dy + k_radius) * k_side) + static_cast<std::size_t>(dx + k_radius);
    }

    /// All offsets absent — the value slot_by_offset_ holds before the first rebuild.
    [[nodiscard]] static constexpr std::array<int32_t, k_max_active_chunks> empty_slots() noexcept {
      std::array<int32_t, k_max_active_chunks> slots{};
      slots.fill(-1);
      return slots;
    }

    /// Re-derive the offset→slot lookup from the current active set and last_center().
    void rebuild_slot_table() noexcept {
      slot_by_offset_ = empty_slots();
      for (std::size_t i = 0; i < active_.size(); ++i) {
        const int dx = active_[i].coord.col - last_center_.col;
        const int dy = active_[i].coord.row - last_center_.row;
        if (dx >= -k_radius && dx <= k_radius && dy >= -k_radius && dy <= k_radius)
          slot_by_offset_[offset_index(dx, dy)] = static_cast<int32_t>(i);
      }
    }

    /// Restore the canonical back-to-front draw order, then re-derive the offset→slot table.
    ///
    /// The render pass draws chunks in active() order. Appending a streamed-in chunk (rather
    /// than ordering it) left it drawing ahead of the chunk in front of it after a walk
    /// away-and-back, so their overlapping art at the shared seam flipped — one chunk's tiles
    /// riding over the neighbour's. Row-major (row, then col) is the load_world() bootstrap
    /// order; within a window a greater row or col is nearer the camera, so it draws later.
    void sort_active() noexcept {
      std::ranges::sort(active_, {}, [](const ChunkEntry &e) noexcept { return std::pair{e.coord.row, e.coord.col}; });
      rebuild_slot_table();
    }

    std::vector<ChunkEntry> active_;
    std::vector<world::tilemap::ChunkCoord> pending_;
    world::tilemap::ChunkCoord last_center_{};
    std::array<int32_t, k_max_active_chunks> slot_by_offset_ = empty_slots();
    bool dirty_{true};
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
    corundum::ui::DialogBoxState dialog_box{};
    uint32_t font_id{0};
    corundum::world::tilemap::WorldManifest manifest{};
    MapData map_data{};
    /// Built once when a single map loads (load_map()); single-map mode only, same
    /// limitation as MapView::elevation_map — World mode leaves this default-empty.
    corundum::world::tilemap::WalkabilityGraph map_walkability{};
    RenderMode mode{RenderMode::None};
    SpriteFrameIndex sprite_index;

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
