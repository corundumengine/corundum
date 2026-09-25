// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace corundum::platform {

  /** @brief OS lifecycle events reported alongside a window input poll.
   *
   * Set fields are edge events observed since the previous poll, not levels: a
   * backend clears each after reporting it. A backend that cannot observe an
   * event leaves its field false. The engine turns these into run-state and
   * quit decisions; game code observes them through Engine::on_platform_event.
   *
   * @note Distinct from corundum::input: these describe what the host OS did to
   *       the window, not what the player pressed.
   */
  struct PlatformEvents {
    /** @brief True when the drawable size, scale, or monitor changed this poll. */
    bool display_changed{};

    /** @brief True when the window regained input focus this poll. */
    bool focus_gained{};

    /** @brief True when the window lost input focus this poll. */
    bool focus_lost{};

    /** @brief True when the OS asked the window to close (close button, Alt+F4). */
    bool quit_requested{};
  };

  /** @brief OR @p source's one-shot flags into @p destination.
   *
   *  Lets a backend accumulate events set by OS callbacks with events queued by
   *  tests or the host, without either source clobbering the other.
   *
   *  @param[in,out] destination Events already set this poll.
   *  @param[in]     source      Events to fold in.
   */
  inline void merge_events(PlatformEvents &destination, const PlatformEvents &source) noexcept {
    destination.display_changed = destination.display_changed || source.display_changed;
    destination.focus_gained = destination.focus_gained || source.focus_gained;
    destination.focus_lost = destination.focus_lost || source.focus_lost;
    destination.quit_requested = destination.quit_requested || source.quit_requested;
  }

} // namespace corundum::platform
