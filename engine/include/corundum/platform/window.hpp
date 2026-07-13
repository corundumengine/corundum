#pragma once
#include <corundum/input/actions.hpp>

#include <utility>

namespace corundum::platform {

  /// @brief Abstract OS window. Concrete implementations live in the platform layer.
  /// @note Not thread-safe. Call only from the main thread.
  class Window {
  public:
    virtual ~Window() = default;

    [[nodiscard]] virtual bool is_open() const = 0;

    virtual void close() = 0;

    /// Poll platform events, translate to engine input, and write into @p input.
    /// Clears pressed flags before writing new ones each frame.
    /// @pre This window must be open.
    virtual void poll_game_input(corundum::input::InputState &input) = 0;

    /// Resize the OS window to the given dimensions in pixels.
    virtual void resize(unsigned width, unsigned height) = 0;

    /// @brief Query the current OS window dimensions in screen pixels.
    /// @return {width, height} in pixels.
    [[nodiscard]] virtual std::pair<int, int> size() const = 0;

    /// Enable or disable vertical synchronisation.
    virtual void set_vsync(bool enabled) = 0;

    /// @brief Return the platform-native window handle (e.g. GLFWwindow*).
    /// The caller is responsible for correct interpretation of the void*.
    [[nodiscard]] virtual void *native_handle() const = 0;
  };

} // namespace corundum::platform
