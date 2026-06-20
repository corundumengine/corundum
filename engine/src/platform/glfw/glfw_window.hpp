#pragma once
#include <corundum/platform/window.hpp>

#include <expected>
#include <memory>
#include <string_view>

struct GLFWwindow;

namespace corundum::platform::glfw {
  enum class WindowError { CreationFailed };

  class GLFWWindow final : public corundum::platform::Window {
  public:
    /// Factory method to safely create a window.
    [[nodiscard]] static std::expected<std::unique_ptr<GLFWWindow>, WindowError>
    create(unsigned width = 1920, unsigned height = 1080, std::string_view title = "Project Keystone");

    ~GLFWWindow() override;

    GLFWWindow(const GLFWWindow &) = delete;
    GLFWWindow &operator=(const GLFWWindow &) = delete;

    GLFWWindow(GLFWWindow &&) noexcept;
    GLFWWindow &operator=(GLFWWindow &&) noexcept;

    bool is_open() const override;

    void close() override;
    void poll_game_input(corundum::input::InputState &input) override;
    void resize(unsigned width, unsigned height) override;
    void set_vsync(bool enabled) override;

    [[nodiscard]] ::GLFWwindow *glfw_window() const noexcept;

  private:
    /// Private constructor to force usage of the factory method.
    GLFWWindow(unsigned width, unsigned height, std::string_view title);

    struct Impl;
    std::unique_ptr<Impl> impl_;
  };

} // namespace corundum::platform::glfw
