// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstdint>

#include <corundum/world/portals/portal.hpp>

namespace corundum::input {
struct InputIntent;
} // namespace corundum::input

namespace corundum::world {

/** A portal transition the player has stepped into but not yet confirmed.
 *
 *  Owns the pending `MapTransition`, the arming `Portal` (whose tile-grid rect
 *  it tests against), the Yes/No highlight, and the "declined, walk away to clear"
 *  state. The physics system arms one when the player overlaps a cross-map / return
 *  portal; `step` advances it against player input each fixed tick.
 */
class TransitionPrompt {
public:
  /** Outcome of one `step` call. */
  enum class Step : std::uint8_t { Confirmed, Dismissed, Pending };

  /** Arm a fresh prompt (Yes highlighted) for the portal just entered.
   *  @pre `portal` is a cross-map or return-to-world portal.
   */
  explicit TransitionPrompt(const Portal &portal) noexcept;

  [[nodiscard]] bool confirm_selected() const noexcept { return confirm_selected_; }

  [[nodiscard]] bool declined() const noexcept { return declined_; }

  [[nodiscard]] const MapTransition &transition() const noexcept { return transition_; }

  /** True while a tile-grid AABB still overlaps the trigger rect.
   *  @param col0 Left edge of the AABB.
   *  @param col1 Right edge of the AABB.
   *  @param row0 Top edge of the AABB.
   *  @param row1 Bottom edge of the AABB.
   */
  [[nodiscard]] bool overlaps(float col0, float col1, float row0, float row1) const noexcept;

  /** True if `portal` is the exact trigger this prompt guards.
   *
   *  Compares the whole portal, not just its rect, so two triggers authored on the
   *  same cell at different elevations or targets are told apart.
   */
  [[nodiscard]] bool guards(const Portal &portal) const noexcept;

  /** Advance one fixed step. Left/Right (Up/Down aliased) move the
   *  highlight; Select commits it (Confirmed) or backs out (Dismissed);
   *  Cancel always Dismisses. Dismiss also latches `declined`.
   */
  [[nodiscard]] Step step(const corundum::input::InputIntent &intent) noexcept;

private:
  bool confirm_selected_ = true;
  bool declined_ = false;

  Portal portal_;
  MapTransition transition_;
};

} // namespace corundum::world
