// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/core/math/vec.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/ui/dialog_box.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace corundum::ui {

  /** @brief Seconds a toast stays on screen before auto-dismissal. */
  inline constexpr float k_toast_ttl_seconds = 4.f;

  /** @brief Upper bound on simultaneously visible toasts; older entries are dropped first. */
  inline constexpr std::size_t k_toast_max_visible = 5;

  /** @brief Toast colour for neutral notifications (default). */
  inline constexpr core::math::Colour k_toast_default_colour{.r = 255, .g = 235, .b = 170, .a = 255};

  /** @brief Toast colour for an in-progress quest update. */
  inline constexpr core::math::Colour k_toast_updated_colour{.r = 200, .g = 220, .b = 255, .a = 255};

  /** @brief Toast colour for a completed quest. */
  inline constexpr core::math::Colour k_toast_complete_colour{.r = 170, .g = 230, .b = 170, .a = 255};

  /** @brief Toast colour for a failed quest. */
  inline constexpr core::math::Colour k_toast_failed_colour{.r = 240, .g = 150, .b = 150, .a = 255};

  /** @brief One transient notification: text, tint, and remaining lifetime in seconds. */
  struct Toast {
    std::string text{};
    core::math::Colour colour{};
    float remaining{};
  };

  /** @brief A bounded, automatically-expiring stack of on-screen notifications.
   *
   *  `notify()` pushes a message; `update(dt)` ages every toast and culls the expired ones;
   *  `render()` stacks the survivors bottom-left, newest nearest the bottom edge. Past
   *  k_toast_max_visible the oldest is dropped so a burst cannot cover the screen.
   */
  class ToastQueue {
  public:
    /** @brief Enqueue @p text, to be shown for k_toast_ttl_seconds.
     *  @param text   Message to display.
     *  @param colour Text colour; defaults to k_toast_default_colour. */
    void notify(std::string text, core::math::Colour colour = k_toast_default_colour);

    /** @brief Age every toast by @p dt seconds and drop those that reached zero.
     *  @param dt Elapsed time in seconds. */
    void update(float dt) noexcept;

    /** @brief Draw the live toasts stacked bottom-left.
     *  @param r        Platform renderer; receives a background DrawRect and a DrawText per toast.
     *  @param style    Dialog text style (font id/sizes/colours) for the message text.
     *  @param viewport Screen size in pixels; toasts anchor to its bottom-left corner.
     *  @pre The renderer's view is screen space. */
    void render(platform::Renderer &r, const DialogBoxStyle &style, core::math::Vec2 viewport) const;

    /** @brief True when no toast is live. */
    [[nodiscard]] bool empty() const noexcept {
      return toasts_.empty();
    }

    /** @brief Number of live toasts. */
    [[nodiscard]] std::size_t size() const noexcept {
      return toasts_.size();
    }

    /** @brief The toast at @p index, oldest first.
     *  @pre @p index < size(). */
    [[nodiscard]] const Toast &at(std::size_t index) const noexcept {
      return toasts_[index];
    }

    /** @brief Drop every live toast. */
    void clear() noexcept {
      toasts_.clear();
    }

  private:
    std::vector<Toast> toasts_;
  };

} // namespace corundum::ui
