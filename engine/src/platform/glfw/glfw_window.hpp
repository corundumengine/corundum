// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/input/actions.hpp>
#include <corundum/platform/platform_events.hpp>
#include <corundum/platform/window.hpp>

#include <cstdint>
#include <expected>
#include <memory>
#include <string_view>
#include <utility>

struct GLFWwindow;

namespace corundum::platform::glfw {
  /// @brief Why GLFWWindow::create() failed.
  enum class WindowError : std::uint8_t {
    InitializationFailed, ///< glfwInit() could not bring GLFW up.
    CreationFailed,       ///< GLFW could not create the window.
  };

  /// @brief Human-readable form of @p error, for diagnostics.
  [[nodiscard]] std::string_view to_string(WindowError error) noexcept;

  class GLFWWindow final : public corundum::platform::Window {
  public:
    /// Factory method to safely create a window.
    [[nodiscard]] static std::expected<std::unique_ptr<GLFWWindow>, WindowError>
    create(unsigned width = 1920, unsigned height = 1080, std::string_view title = "Corundum");

    ~GLFWWindow() override;

    GLFWWindow(const GLFWWindow &) = delete;
    GLFWWindow &operator=(const GLFWWindow &) = delete;

    GLFWWindow(GLFWWindow &&) noexcept;
    GLFWWindow &operator=(GLFWWindow &&) noexcept = delete;

    [[nodiscard]] bool is_open() const override;

    void close() override;
    void poll_game_input(corundum::input::InputState &input, corundum::platform::PlatformEvents &events) override;

    /// @return {0, 0} once the underlying GLFW window no longer exists.
    [[nodiscard]] std::pair<int, int> size() const override;

    void set_vsync(bool enabled) override;

    [[nodiscard]] void *native_handle() const noexcept override;

  private:
    /// Private constructor to force usage of the factory method.
    GLFWWindow(unsigned width, unsigned height, std::string_view title);

    struct Impl;
    std::unique_ptr<Impl> impl_;
  };

} // namespace corundum::platform::glfw
