// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace corundum::platform {

  /** @brief Backend-neutral id table shared by the texture caches.
   *
   * A cache hands the table a freshly created set of backend handles and gets
   * back the id it was published under; releasing an id hands the handles back
   * so the backend can tear down whatever GPU resources they name. Ids stay
   * valid for as long as their texture is live, and a released id goes to the
   * next texture that needs one, so the table never grows past its high-water
   * mark and no live texture is ever renumbered.
   *
   * The handles are never interpreted here — the table only stores and returns
   * them — so it compiles and unit-tests without any graphics backend.
   *
   * @tparam Payload  Backend handles belonging to one texture.
   *
   * @note Not thread-safe. Call only from the render thread.
   */
  template <typename Payload> class TextureSlotTable {
  public:
    /** @brief One published texture: its backend handles and its pixel extent. */
    struct Slot {
      /** @brief Texture height in pixels. */
      unsigned height{0};

      /** @brief Handles to release once the slot is no longer live. */
      Payload payload{};

      /** @brief Texture width in pixels. */
      unsigned width{0};
    };

    /** @brief Publish @p payload in the first free slot.
     *
     * @return The id it was published under; ids are 1-based.
     */
    uint32_t adopt(Payload payload, unsigned width, unsigned height) {
      const Slot slot{.height = height, .payload = payload, .width = width};

      for (std::size_t index = 0; index < slots_.size(); ++index) {
        if (!slots_[index]) {
          slots_[index] = slot;
          return static_cast<uint32_t>(index + 1);
        }
      }

      slots_.push_back(slot);
      return static_cast<uint32_t>(slots_.size());
    }

    /** @brief Retire @p id, freeing it for reuse.
     *
     * @param[in] id  Id returned by adopt().
     * @return The retired texture's handles, or std::nullopt if @p id is not live.
     */
    [[nodiscard]] std::optional<Payload> release(uint32_t id) {
      if (id == 0 || id > slots_.size())
        return std::nullopt;

      std::optional<Slot> &slot = slots_[static_cast<std::size_t>(id) - 1];
      if (!slot)
        return std::nullopt;

      const Payload payload = slot->payload;
      slot.reset();
      return payload;
    }

    /** @brief Number of ids the table has handed out — its high-water mark, not its live count.
     *
     * Every id in 1..slot_count() can be passed to release(); the ids that are
     * already retired return nothing.
     */
    [[nodiscard]] std::size_t slot_count() const {
      return slots_.size();
    }

    /** @brief The slot published under @p id, or std::nullopt if @p id is not live.
     *
     * @note Copies the slot, and with it the payload; use peek() to read a field
     *       or two without copying.
     */
    [[nodiscard]] std::optional<Slot> find(uint32_t id) const {
      const Slot *slot = peek(id);
      if (slot == nullptr)
        return std::nullopt;

      return *slot;
    }

    /** @brief The live slot under @p id, or nullptr if @p id is not live.
     *
     * @note The pointer is invalidated by the next adopt() or release().
     */
    [[nodiscard]] const Slot *peek(uint32_t id) const {
      if (id == 0 || id > slots_.size())
        return nullptr;

      const std::optional<Slot> &slot = slots_[static_cast<std::size_t>(id) - 1];
      return slot ? &*slot : nullptr;
    }

  private:
    std::vector<std::optional<Slot>> slots_;
  };

} // namespace corundum::platform
