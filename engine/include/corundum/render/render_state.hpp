// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/game_config.hpp>
#include <corundum/core/math/isometric.hpp>
#include <corundum/core/math/vec.hpp>
#include <corundum/entities/entity.hpp>
#include <corundum/platform/resource_ids.hpp>
#include <corundum/render/chunk_window.hpp>
#include <corundum/render/layer_render_cache.hpp>
#include <corundum/sprites/sprite.hpp>
#include <corundum/ui/font_family.hpp>
#include <corundum/ui/panel_style.hpp>
#include <corundum/world/portals/portal.hpp>
#include <corundum/world/tilemap/loader.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <corundum/world/tilemap/walkability.hpp>
#include <corundum/world/tilemap/world_manifest.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace corundum::render {

/** Which kind of area is loaded. Derived from RenderState::area by mode(); never stored. */
enum class RenderMode : uint8_t { None, SingleMap, World };

/** Result of a sprite-id / anim-id / frame-index lookup.
 *
 *  Maps a logical sprite frame to its texture ID and source rectangle.
 *  Stores frame rectangles in a single contiguous buffer with flat offset
 *  and count tables indexed by (sprite_id * k_num_anim_ids + anim_id).
 *
 *  The tables are parallel and must agree: anim_offsets and anim_frame_counts share
 *  one index space and length, and each (offset, count) pair must address a valid
 *  window into frame_rects. get() verifies that window before dereferencing.
 *
 *  @performance O(1) lookup with no pointer chasing — all frame data in contiguous arrays.
 */
struct SpriteFrameIndex {
  std::vector<std::optional<platform::TextureId>> tex_by_sprite_id;
  std::vector<core::math::IntRect> frame_rects;

  /** walk_around_offset per sprite_id. */
  std::vector<float> walk_offsets;
  std::vector<uint32_t> anim_offsets;
  std::vector<uint8_t> anim_frame_counts;

  /** Lookup result: texture ID, source rect, and walk_around_offset. */
  struct Entry {
    platform::TextureId tex_id{platform::TextureId::Invalid};
    core::math::IntRect src{};
    float walk_offset = 0.f;
  };

  /** Look up the texture ID, source rect, and walk offset for a given frame.
   *  @param sprite_id   Interned sprite identifier.
   *  @param anim_id     Animation identifier.
   *  @param frame_index Zero-based frame within the animation.
   *  @return Entry with tex_id, src, walk_offset, or std::nullopt.
   */
  [[nodiscard]] std::optional<Entry> get(sprites::SpriteId sprite_id, sprites::AnimId anim_id,
                                         uint8_t frame_index) const noexcept;
};

/** A single pre-loaded tilemap (single-map mode). */
struct MapData {
  world::tilemap::Tilemap tilemap{};
  std::vector<platform::TextureId> tileset_texture_ids{};
  std::vector<int> above_z{};

  /** Parallel to tilemap.layers. */
  std::vector<LayerRenderCache> layer_caches{};
  std::vector<world::Portal> portals{};
};

/** Sorted draw-list entry for depth-ordered ground-layer rendering (tiles and entities). */
struct DepthEntry {
  platform::TextureId tex_id{platform::TextureId::Invalid};
  core::math::IntRect src{};
  float x = 0.f;
  float y = 0.f;
  float depth = 0.f;
  float scale = 1.f;
  bool flip_x = false;
  bool flip_y = false;
};

/** World-mode data derived from the active chunk window, in global world tile coordinates.
 *  Rebuilt by rebuild_world_aggregates() whenever the active set changes.
 */
struct WorldAggregates {
  world::tilemap::CollisionRects collisions{};

  /** Each chunk's portals offset into world tiles; MapView hands them out as a span. */
  std::vector<world::Portal> portals{};

  world::tilemap::CollisionTriangles triangles{};

  /** Spans the bounding box of the active chunks; col_origin/row_origin record the offset. */
  world::tilemap::WalkabilityGraph walkability{};
};

/** A loaded single map and the walkability graph built for it by load_map(). */
struct SingleMapArea {
  MapData map{};

  world::tilemap::WalkabilityGraph walkability{};
};

/** A streamed world: its manifest, the resident chunk window and the window's aggregates. */
struct WorldArea {
  /** Sorted, deduplicated z_index > 0 bands across the active chunks; render() rebuilds it when
   *  chunks.consume_dirty() reports a change.
   */
  std::vector<int> above_z_cache{};

  WorldAggregates aggregates{};

  /** Loads the window's chunks through the content source load_world() was given, keeping their
   *  shared tilesets until the world is replaced. No default: a world can't stream without a source.
   */
  world::tilemap::TilemapLoader chunk_loader;

  ChunkWindow chunks{};

  world::tilemap::WorldManifest manifest{};
};

/** The loaded area; std::monostate when none is loaded (RenderMode::None). */
using Area = std::variant<std::monostate, SingleMapArea, WorldArea>;

/** A world view, as Renderer::set_world_view takes it. */
struct WorldView {
  core::math::Vec2 camera{};

  core::math::Vec2 viewport_size{};

  float zoom{1.f};
};

/** Camera and entity tile positions at the start of the latest fixed step, for render
 *  interpolation. Written by snapshot_previous_step().
 */
struct StepSnapshot {
  core::math::Vec2 camera{};

  float zoom{1.f};

  /** Entity tile columns, indexed by EntityId::index (not dense slot) so a swap-and-pop
   *  removal never misattributes a snapshot. An entry is valid only where generation matches the
   *  entity's generation.
   */
  std::array<float, entities::k_max_entities> col{};

  /** Generation of the entity each col/row entry was captured from; 0 (never a live
   *  generation) marks an entry that was never written.
   */
  std::array<std::uint32_t, entities::k_max_entities> generation{};

  /** Entity tile rows. See col. */
  std::array<float, entities::k_max_entities> row{};
};

/** All mutable rendering state, grouped by lifetime tier. */
struct RenderState {
  /** Area tier: replaced whole by load_map()/load_world(), reset by unload_world(). */
  Area area{};

  /** Textures the loaded area holds one reference on each. Kept outside `area` so replacing the
   *  area can never drop references unreleased; see unload_world().
   */
  std::vector<platform::TextureId> area_texture_ids{};

  /** Per-role font families, indexed by ui::FontRole; loaded by load_fonts(). */
  std::array<ui::FontFamily, ui::k_font_role_count> fonts{};

  /** @return The family for `role`. */
  [[nodiscard]] const ui::FontFamily &family(ui::FontRole role) const noexcept {
    return fonts[static_cast<std::size_t>(role)];
  }

  ui::PanelSkin panel_skin{};

  SpriteFrameIndex sprite_index{};

  /** Dialogue text-reveal multiplier applied to k_base_reveal_chars_per_second; 0 = instant.
   *  Set from UserSettings::text_speed.
   */
  float text_speed{1.f};

  /** Font/margin scale applied by configure_panel_style(); 1 = the game.json sizes. Set from
   *  UserSettings::ui_scale.
   */
  float ui_scale{1.f};

  /** Frame scratch, reused across frames. */
  std::vector<DepthEntry> draw_list{};

  /** Indices into draw_list, sorted by depth each frame. Reused across frames
   *  (resized, never freed) so the depth sort touches no per-frame heap allocation.
   */
  std::vector<uint32_t> draw_order{};

  StepSnapshot previous_step{};

  /** The view the last render() drew the world with, interpolated by the loop's alpha. A screen
   *  that draws over the world uses this rather than Scene::camera, which is up to a step ahead.
   */
  WorldView world_view{};
};

/** The kind of area loaded. */
[[nodiscard]] inline RenderMode mode(const RenderState &state) noexcept {
  if (std::holds_alternative<WorldArea>(state.area))
    return RenderMode::World;
  if (std::holds_alternative<SingleMapArea>(state.area))
    return RenderMode::SingleMap;
  return RenderMode::None;
}

/** Combined collision views for debug rendering and collision queries.
 *
 *  Bundles rect and triangle collision data from the currently active render source.
 *  Abstracts away the World-vs-SingleMap distinction so consumers don't branch on mode.
 */
struct CollisionGeometry {
  world::tilemap::CollisionRectsView rects{};
  world::tilemap::CollisionTrianglesView tris{};
};

/** Collision geometry of the loaded area; empty views when none is loaded. */
[[nodiscard]] inline CollisionGeometry current_collisions(const RenderState &state) noexcept {
  if (const auto *world_area = std::get_if<WorldArea>(&state.area))
    return {.rects = world_area->aggregates.collisions.view(), .tris = world_area->aggregates.triangles.view()};
  if (const auto *single = std::get_if<SingleMapArea>(&state.area))
    return {.rects = single->map.tilemap.collisions.view(), .tris = single->map.tilemap.collision_triangles.view()};
  return {};
}

/** The single loaded tilemap, or nullptr in World mode (one tilemap per chunk) and before load. */
[[nodiscard]] inline const world::tilemap::Tilemap *active_tilemap(const RenderState &state) noexcept {
  const auto *single = std::get_if<SingleMapArea>(&state.area);
  return single != nullptr ? &single->map.tilemap : nullptr;
}

/** Isometric projection of a single map. */
[[nodiscard]] inline core::math::IsometricParams isometric_params(const SingleMapArea &area,
                                                                  const core::GameConfig &config) noexcept {
  const world::tilemap::Tilemap &tm = area.map.tilemap;
  return core::math::compute_isometric_params(tm.diamond_w(), tm.diamond_h(), tm.height, config.tile_scale,
                                              config.elevation_step_px);
}

/** Isometric projection of a streamed world: the first active chunk's diamond over the
 *  manifest's effective height.
 *  @pre area.chunks is non-empty, and every active chunk shares one diamond size.
 */
[[nodiscard]] inline core::math::IsometricParams isometric_params(const WorldArea &area,
                                                                  const core::GameConfig &config) noexcept {
  assert(!area.chunks.active_empty());
  const world::tilemap::Tilemap &first = area.chunks.active_at(0).tilemap;
  return core::math::compute_isometric_params(first.diamond_w(), first.diamond_h(),
                                              area.manifest.effective_tiles_tall(), config.tile_scale,
                                              config.elevation_step_px);
}

} // namespace corundum::render
