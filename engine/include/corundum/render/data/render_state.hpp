#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/gameplay/ui/dialog_box.hpp>
#include <corundum/gameplay/world/portals/portal.hpp>
#include <corundum/gameplay/world/tilemap/tilemap.hpp>
#include <corundum/gameplay/world/tilemap/world_manifest.hpp>
#include <corundum/resources/sprite.hpp>

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
    std::vector<uint32_t> anim_offsets;
    std::vector<uint8_t> anim_frame_counts;

    /** @brief Look up the texture ID and source rect for a given frame.
     *  @param[in] sprite_id   Interned sprite identifier.
     *  @param[in] anim_id     Animation identifier (Idle, WalkSouth, …).
     *  @param[in] frame_index Zero-based frame within the animation.
     *  @return Pair of {texture_id, source_rect}, or std::nullopt if not found.
     */
    [[nodiscard]] std::optional<std::pair<uint32_t, corundum::core::math::IntRect>>
    get(corundum::resources::SpriteId sprite_id, corundum::resources::AnimId anim_id,
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
  };

  /** @brief Sorted draw-list entry for depth-ordered entity rendering. */
  struct DepthEntry {
    uint32_t tex_id;
    corundum::core::math::IntRect src;
    float x;
    float y;
    float depth;
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
    corundum::gameplay::world::tilemap::ChunkCoord last_center_chunk{};
    corundum::gameplay::world::tilemap::CollisionRects agg_collisions{};
    corundum::gameplay::world::tilemap::CollisionTriangles agg_triangles{};
    corundum::gameplay::ui::DialogBoxState dialog_box{};
    uint32_t font_id{0};
    RenderMode mode{RenderMode::None};

    std::vector<int> above_z_cache{};
    bool chunks_dirty{true};
    std::vector<DepthEntry> draw_list{};

    /** @brief Interpolation alpha for render smoothing (0 = no interpolation). */
    float interpolation_alpha{0.f};
    /** @brief Previous-frame entity x positions for render interpolation. */
    std::vector<float> prev_x{};
    /** @brief Previous-frame entity y positions for render interpolation. */
    std::vector<float> prev_y{};
    /** @brief Previous-frame camera x for render interpolation. */
    float prev_cam_x{0.f};
    /** @brief Previous-frame camera y for render interpolation. */
    float prev_cam_y{0.f};
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
