// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace corundum::platform {

  /** @brief Reference counts for a renderer's append-only texture ids.
   *
   *  Bookkeeping owned by a renderer backend, never by game code: only the backend's
   *  load_texture() calls acquire(), once per call (a fresh load and a path-dedup hit alike),
   *  and holders give references back through Renderer::release_texture(). Releasing never
   *  destroys: ids whose count reached zero are handed out by take_unreferenced(), which the
   *  renderer calls at an area boundary, so a texture released by the old area and re-acquired by
   *  the new one in the same load survives without a reload. Id 0 is the invalid id and is
   *  ignored throughout. Ids are never reused, so a stale id can never alias a newer texture, and
   *  a purged id can never be acquired again (a cached stale id cannot resurrect a destroyed
   *  texture). The trade-off: the backend's texture table keeps one tombstone per purged id, so
   *  it grows with the number of distinct textures ever loaded in the process.
   *
   *  @note Not thread-safe. Call only from the render thread.
   */
  class TextureRefCounts {
  public:
    /** @brief Add one reference to @p id. Id 0 and a purged id are ignored. */
    void acquire(std::uint32_t id) {
      if (id == 0)
        return;
      if (id >= counts_.size())
        counts_.resize(static_cast<std::size_t>(id) + 1);
      Entry &entry = counts_[id];
      if (entry.state == State::Purged)
        return;
      entry.state = State::Live;
      ++entry.count;
    }

    /** @brief Drop one reference to @p id. Id 0, an untracked id or an id already at zero is
     *  ignored. */
    void release(std::uint32_t id) {
      if (id == 0 || id >= counts_.size())
        return;
      Entry &entry = counts_[id];
      if (entry.state != State::Live || entry.count == 0)
        return;
      --entry.count;
    }

    /** @brief Current reference count of @p id; 0 for an untracked or purged id. */
    [[nodiscard]] std::uint32_t count(std::uint32_t id) const {
      if (id >= counts_.size() || counts_[id].state != State::Live)
        return 0;
      return counts_[id].count;
    }

    /** @brief Every id whose count is zero, each returned once; they become purged for good. */
    [[nodiscard]] std::vector<std::uint32_t> take_unreferenced() {
      std::vector<std::uint32_t> ids;
      for (std::size_t id{1}; id < counts_.size(); ++id) {
        Entry &entry = counts_[id];
        if (entry.state == State::Live && entry.count == 0) {
          ids.push_back(static_cast<std::uint32_t>(id));
          entry.state = State::Purged;
        }
      }
      return ids;
    }

  private:
    enum class State : std::uint8_t { Untracked, Live, Purged };

    struct Entry {
      std::uint32_t count{0};

      State state{State::Untracked};
    };

    std::vector<Entry> counts_;
  };

} // namespace corundum::platform
