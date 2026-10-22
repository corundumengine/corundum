// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/world/portals/portal.hpp>
#include <corundum/world/tilemap/tilemap.hpp>
#include <corundum/world/tilemap/world_manifest.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace corundum::render {

  /** @brief A single loaded world chunk (world mode only). */
  struct ChunkEntry {
    corundum::world::tilemap::ChunkCoord coord{};
    corundum::world::tilemap::Tilemap tilemap{};
    std::vector<uint32_t> tileset_texture_ids;
    std::vector<int> above_z;
    std::vector<corundum::world::Portal> portals;
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

    [[nodiscard]] const std::vector<ChunkEntry> &active() const noexcept { return active_; }

    [[nodiscard]] bool active_empty() const noexcept { return active_.empty(); }

    [[nodiscard]] std::size_t active_size() const noexcept { return active_.size(); }

    [[nodiscard]] const ChunkEntry &active_at(std::size_t i) const noexcept { return active_[i]; }

    /// @return true if the active set changed since the last call, clearing the flag.
    [[nodiscard]] bool consume_dirty() noexcept {
      const bool was_dirty = dirty_;
      dirty_ = false;
      return was_dirty;
    }

    [[nodiscard]] world::tilemap::ChunkCoord last_center() const noexcept { return last_center_; }

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

    /// True if @p c is active, queued in pending, or failed to load (see mark_failed()).
    [[nodiscard]] bool has(world::tilemap::ChunkCoord c) const noexcept {
      return std::ranges::any_of(active_, [&](const ChunkEntry &e) { return e.coord == c; }) ||
             std::ranges::contains(pending_, c) || std::ranges::contains(failed_, c);
    }

    /// True if @p c is resident in the active window (ignores chunks still pending).
    [[nodiscard]] bool is_active(world::tilemap::ChunkCoord c) const noexcept {
      return std::ranges::any_of(active_, [&](const ChunkEntry &e) { return e.coord == c; });
    }

    /// Queue @p c for loading. Caller should check has(c) first to avoid duplicates
    /// (kept explicit rather than implicit to match the existing call-site logic).
    void enqueue_pending(world::tilemap::ChunkCoord c) { pending_.push_back(c); }

    /// Drop pending coords for which @p keep returns false — chunks the window moved away from
    /// before they were loaded.
    template <typename Keep> void prune_pending(Keep &&keep) {
      std::erase_if(pending_, [&](world::tilemap::ChunkCoord c) { return !std::forward<Keep>(keep)(c); });
    }

    /// Record that @p c failed to load, so has() reports it and it is never re-queued. Failed
    /// chunks stay failed until clear() (the next load_world()).
    void mark_failed(world::tilemap::ChunkCoord c) { failed_.push_back(c); }

    /// Pop the front of the pending queue into @p out.
    /// @return false if pending is empty (out left unchanged).
    [[nodiscard]] bool pop_pending(world::tilemap::ChunkCoord &out) {
      if (pending_.empty())
        return false;
      out = pending_.front();
      pending_.erase(pending_.begin());
      return true;
    }

    /// Reset to the empty, never-loaded state (used by load_map(), load_world() and
    /// unload_world()).
    void clear() noexcept {
      active_.clear();
      pending_.clear();
      failed_.clear();
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
    std::vector<world::tilemap::ChunkCoord> failed_;
    world::tilemap::ChunkCoord last_center_{};
    std::array<int32_t, k_max_active_chunks> slot_by_offset_ = empty_slots();
    bool dirty_{true};
  };

} // namespace corundum::render
