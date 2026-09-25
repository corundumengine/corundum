// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/input/actions.hpp>
#include <corundum/platform/platform_events.hpp>
#include <corundum/platform/window.hpp>

#include <utility>

namespace corundum::platform::null {

  /** @brief No-op Window for headless lifecycle tests.
   *
   * `open_` toggles between `is_open()` and `close()`. Stores dimensions so
   * `size()` matches what the test sets. `poll_game_input` is a no-op for input,
   * but emits whatever `scripted_events` a test has queued.
   */
  class NullWindow final : public corundum::platform::Window {
  public:
    explicit NullWindow(unsigned width, unsigned height) : width_{width}, height_{height} {}

    [[nodiscard]] bool is_open() const override {
      return open_;
    }

    void close() override {
      open_ = false;
    }

    void poll_game_input(corundum::input::InputState & /*input*/, PlatformEvents &events) override {
      merge_events(events, scripted_events);
      scripted_events = {};
    }

    [[nodiscard]] std::pair<int, int> size() const override {
      return {static_cast<int>(width_), static_cast<int>(height_)};
    }

    void set_vsync(bool /*enabled*/) override {}

    [[nodiscard]] void *native_handle() const override {
      return nullptr;
    }

    /** @brief Events emitted on the next poll_game_input(), then cleared.
     *
     *  Tests set these to script focus, quit, and display changes the engine
     *  would otherwise receive from a real host OS.
     */
    PlatformEvents scripted_events{};

  private:
    bool open_ = true;
    unsigned width_;
    unsigned height_;
  };

} // namespace corundum::platform::null
