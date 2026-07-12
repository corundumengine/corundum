#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/entity/entity.hpp>
#include <corundum/gameplay/ui/dialog_box.hpp>
#include <corundum/gameplay/world/portals/portal.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/gameplay/world/tilemap/walkability.hpp>
#include <corundum/gameplay/world/tilemap/world_manifest.hpp>
#include <corundum/resources/sprite.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace corundum::render::data {

  /** @brief Tracks whether the renderer is in map or world mode. */
  enum class RenderMode { None, SingleMap, World };

  /** @brief Result of a sprite-id / anim-id / frame-index lookup.
   *
   * Maps a logical sprite frame to its texture ID and source rectangle.
   * Stores frame rectangles in a single contiguous buffer with flat offset
   * and count tables indexed by (sprite_id * k_num_anim_ids + anim_id).
   *
   * @performance O(1) lookup with no pointer chasing — all frame data in contiguous arrays.
   */
  struct SpriteFrameIndex {
    std::vector<std::optional<uint32_t>> tex_by_sprite_id;
    std::vector<corundum::core::math::IntRect> frame_rects;
    std::vector<float> walk_offsets; ///< walk_around_offset per sprite_id.
    std::vector<uint32_t> anim_offsets;
    std::vector<uint8_t> anim_frame_counts;

    /// Lookup result: texture id, source rect, and walk_around_offset.
    struct Entry {
      uint32_t tex_id;
      corundum::core::math::IntRect src;
      float walk_offset;
    };

    /** @brief Look up the texture ID, source rect, and walk offset for a given frame.
     *  @param[in] sprite_id   Interned sprite identifier.
     *  @param[in] anim_id     Animation identifier.
     *  @param[in] frame_index Zero-based frame within the animation.
     *  @return Entry with texture_id, source_rect, walk_offset, or std::nullopt.
     */
    [[nodiscard]] std::optional<Entry> get(corundum::resources::SpriteId sprite_id, corundum::resources::AnimId anim_id,
                                           uint8_t frame_index) const noexcept;
  };

  /** @brief A single pre-loaded tilemap (map mode, not world mode). */
  struct MapData {
    corundum::gameplay::world::tilemap::Tilemap tilemap;
    std::vector<uint32_t> tileset_texture_ids;
    std::vector<int> above_z;
    std::vector<corundum::gameplay::world::Portal> portals;
  };

  /** @brief A single loaded world chunk (world mode only). */
  struct ChunkEntry {
    corundum::gameplay::world::tilemap::ChunkCoord coord{};
    corundum::gameplay::world::tilemap::Tilemap tilemap{};
    std::vector<uint32_t> tileset_texture_ids{};
    std::vector<int> above_z{};
    std::vector<corundum::gameplay::world::Portal> portals{};
  };

  /** @brief Sorted draw-list entry for depth-ordered ground-layer rendering (tiles and entities). */
  struct DepthEntry {
    uint32_t tex_id;
    corundum::core::math::IntRect src;
    float x;
    float y;
    float depth;
    float scale = 1.f;
    bool flip_x = false;
    bool flip_y = false;
  };

  /** @brief All mutable rendering state — pure data with no behaviour.
   *
   * Operated on by free functions in namespace corundum::render::sys.
   * Separates data from logic per DOD: functions own no state, state holds no behaviour.
   */
  struct RenderState {
    SpriteFrameIndex sprite_index;
    MapData map_data{};
    corundum::gameplay::world::tilemap::WorldManifest manifest{};
    std::vector<ChunkEntry> active_chunks{};
    /// Chunks discovered by sync_active_chunks that need loading; drained between frames
    /// so the I/O does not hitch the render pass.
    std::vector<corundum::gameplay::world::tilemap::ChunkCoord> pending_chunks{};
    corundum::gameplay::world::tilemap::ChunkCoord last_center_chunk{};
    /// O(1) lookup from a chunk coord's offset relative to last_center_chunk (the fixed 3×3
    /// streaming window — see sync_active_chunks) to its index in active_chunks, or -1 if that
    /// slot isn't loaded. Indexed by (dy+1)*3+(dx+1) for dx,dy in [-1,1]. Rebuilt by
    /// sync_active_chunks each time active_chunks or last_center_chunk changes; used by
    /// elevation_under() to avoid a linear find_if over active_chunks per entity per frame.
    std::array<int32_t, 9> chunk_slot_by_offset{-1, -1, -1, -1, -1, -1, -1, -1, -1};
    corundum::gameplay::world::tilemap::CollisionRects agg_collisions{};
    corundum::gameplay::world::tilemap::CollisionTriangles agg_triangles{};
    /// Aggregated portal buffer for world mode — cleared and repopulated by build_map_view
    /// then returned as a span via MapView. Single-map mode passes map_data.portals directly.
    std::vector<corundum::gameplay::world::Portal> agg_portals{};
    /// Built once when a single map loads (load_map()); single-map mode only, same
    /// limitation as MapView::elevation_map — World mode leaves this default-empty.
    corundum::gameplay::world::tilemap::WalkabilityGraph map_walkability{};
    corundum::gameplay::ui::DialogBoxState dialog_box{};
    uint32_t font_id{0};
    RenderMode mode{RenderMode::None};

    std::vector<int> above_z_cache{};
    bool chunks_dirty{true};
    std::vector<DepthEntry> draw_list{};
    /** @brief Indices into draw_list, sorted by depth each frame. Reused across frames
     *  (resized, never freed) so the depth sort touches no per-frame heap allocation. */
    std::vector<uint32_t> draw_order{};

    /** @brief Interpolation alpha for render smoothing (0 = no interpolation). */
    float interpolation_alpha{0.f};
    /** @brief Previous-frame entity tile columns for render interpolation. Fixed-size:
     *  bounded by k_max_entities, so no heap growth mid-frame. Only the first
     *  prev_count entries hold a valid snapshot from before this frame. */
    std::array<float, corundum::gameplay::entity::k_max_entities> prev_col{};
    /** @brief Previous-frame entity tile rows for render interpolation. See prev_col. */
    std::array<float, corundum::gameplay::entity::k_max_entities> prev_row{};
    /** @brief Number of valid entries in prev_col/prev_row (entities that existed at the
     *  start of this frame, before any mid-frame spawns). */
    std::uint32_t prev_count{0};
    /** @brief Previous-frame camera x for render interpolation. */
    float prev_cam_x{0.f};
    /** @brief Previous-frame camera y for render interpolation. */
    float prev_cam_y{0.f};
    /** @brief Previous-frame camera zoom for render interpolation. */
    float prev_zoom{1.f};
  };

  /** @brief Combined collision views for debug rendering and collision queries.
   *
   * Bundles rect and triangle collision data from the currently active render source.
   * Abstracts away the World-vs-SingleMap distinction so consumers don't branch on mode.
   */
  struct CollisionGeometry {
    gameplay::world::tilemap::CollisionRectsView rects;
    gameplay::world::tilemap::CollisionTrianglesView tris;
  };

  /** @brief Return the collision geometry for the currently active render mode.
   *  @param[in] rs  Initialised render state.
   *  @return CollisionGeometry with valid (possibly empty) views.
   */
  [[nodiscard]] inline CollisionGeometry current_collisions(const RenderState &rs) noexcept {
    if (rs.mode == RenderMode::World) {
      return {rs.agg_collisions.view(), rs.agg_triangles.view()};
    }
    if (rs.mode == RenderMode::SingleMap) {
      return {rs.map_data.tilemap.collisions.view(), rs.map_data.tilemap.collision_triangles.view()};
    }
    return {};
  }

} // namespace corundum::render::data
