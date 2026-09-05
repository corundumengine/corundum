#pragma once
#include <corundum/platform/window.hpp>

namespace corundum::platform::null {

  /** @brief No-op Window for headless lifecycle tests.
   *
   * `open_` toggles between `is_open()` and `close()`. Stores dimensions so
   * `size()` matches what the test sets. `poll_game_input` is a no-op.
   */
  class NullWindow final : public corundum::platform::Window {
  public:
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    explicit NullWindow(unsigned width, unsigned height) : width_{width}, height_{height} {}

    [[nodiscard]] bool is_open() const override {
      return open_;
    }

    void close() override {
      open_ = false;
    }

    void poll_game_input(corundum::input::InputState &/*input*/) override {}

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void resize(unsigned width, unsigned height) override {
      width_ = width;
      height_ = height;
    }

    [[nodiscard]] std::pair<int, int> size() const override {
      return {static_cast<int>(width_), static_cast<int>(height_)};
    }

    void set_vsync(bool /*enabled*/) override {}

    [[nodiscard]] void *native_handle() const override {
      return nullptr;
    }

  private:
    bool open_ = true;
    unsigned width_;
    unsigned height_;
  };

} // namespace corundum::platform::null
