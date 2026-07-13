#include "glfw_window.hpp"
#include "input_translator.hpp"

#include <corundum/input/actions.hpp>

#include <GLFW/glfw3.h>

#ifdef __APPLE__
#include "glfw_window_metal.h"
#include <CoreFoundation/CoreFoundation.h>
#endif

#include <atomic>
#include <cstdlib>
#include <exception>
#include <print>
#include <string>

namespace corundum::platform::glfw {

  namespace {
    std::atomic<int> s_glfw_refcount{0};

    void glfw_init_if_needed() {
      if (s_glfw_refcount.fetch_add(1, std::memory_order_relaxed) == 0) {
        if (!glfwInit()) {
          std::println(stderr, "[glfw] glfwInit() failed");
          std::terminate();
        }
      }
    }

    void glfw_term_if_done() {
      if (s_glfw_refcount.fetch_sub(1, std::memory_order_relaxed) == 1)
        glfwTerminate();
    }

    struct WindowData {
      corundum::input::InputState input{};
      JoystickAxisState joystick_axis{};
    };

    void key_callback(GLFWwindow *win, int key, int /*scancode*/, int action, int /*mods*/) noexcept {
      auto *data = static_cast<WindowData *>(glfwGetWindowUserPointer(win));
      if (data) {
        translate_key(key, action, data->input);
      }
    }

    void window_close_callback(GLFWwindow *win) noexcept {
      auto *data = static_cast<WindowData *>(glfwGetWindowUserPointer(win));
      if (data) {
        data->input.held[static_cast<std::size_t>(corundum::input::Action::Quit)] = true;
      }
    }

    void mouse_button_callback(GLFWwindow *win, int button, int action, int /*mods*/) noexcept {
      auto *data = static_cast<WindowData *>(glfwGetWindowUserPointer(win));
      if (data) {
        translate_mouse_button(button, action, data->input);
      }
    }

    void scroll_callback(GLFWwindow *win, double /*xoffset*/, double yoffset) noexcept {
      auto *data = static_cast<WindowData *>(glfwGetWindowUserPointer(win));
      if (data) {
        translate_scroll(yoffset, data->input);
      }
    }
  } // namespace

  struct GLFWWindow::Impl {
    GLFWwindow *win{nullptr};
    WindowData data{};
#ifdef __APPLE__
    void *metal_layer{nullptr};
#endif
  };

  std::expected<std::unique_ptr<GLFWWindow>, WindowError> GLFWWindow::create(unsigned width, unsigned height,
                                                                             std::string_view title) {
    glfw_init_if_needed();

    // Use 'new' because the constructor is private.
    // The unique_ptr will take ownership immediately.
    auto window = std::unique_ptr<GLFWWindow>(new GLFWWindow(width, height, title));

    if (!window->impl_->win) {
      return std::unexpected(WindowError::CreationFailed);
    }

    return std::move(window);
  }

  GLFWWindow::GLFWWindow(unsigned width, unsigned height, std::string_view title) : impl_{std::make_unique<Impl>()} {

#ifdef __APPLE__
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#endif

    impl_->win = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), std::string{title}.c_str(),
                                  nullptr, nullptr);

    if (impl_->win) {
      glfwSetWindowUserPointer(impl_->win, &impl_->data);
      glfwSetKeyCallback(impl_->win, key_callback);
      glfwSetMouseButtonCallback(impl_->win, mouse_button_callback);
      glfwSetScrollCallback(impl_->win, scroll_callback);
      glfwSetWindowCloseCallback(impl_->win, window_close_callback);

#ifdef __APPLE__
      impl_->metal_layer = metal_setup_layer(impl_->win);
#else
      glfwMakeContextCurrent(impl_->win);
#endif
    }
  }

  GLFWWindow::~GLFWWindow() {
    if (impl_) {
      if (impl_->win) {
#ifdef __APPLE__
        if (impl_->metal_layer) {
          CFRelease(impl_->metal_layer);
        }
#endif
        glfwDestroyWindow(impl_->win);
      }
      glfw_term_if_done();
    }
  }

  GLFWWindow::GLFWWindow(GLFWWindow &&) noexcept = default;
  GLFWWindow &GLFWWindow::operator=(GLFWWindow &&) noexcept = default;

  bool GLFWWindow::is_open() const {
    return impl_ && impl_->win && !glfwWindowShouldClose(impl_->win);
  }

  void GLFWWindow::close() {
    if (impl_ && impl_->win)
      glfwSetWindowShouldClose(impl_->win, GLFW_TRUE);
  }

  void GLFWWindow::poll_game_input(corundum::input::InputState &input) {
    corundum::input::clear_pressed(impl_->data.input);
    glfwPollEvents();
    poll_joystick(impl_->data.input, impl_->data.joystick_axis);
    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(impl_->win, &mx, &my);
    impl_->data.input.mouse_x = static_cast<float>(mx);
    impl_->data.input.mouse_y = static_cast<float>(my);
    corundum::input::accumulate_input(input, impl_->data.input);
  }

  void GLFWWindow::resize(unsigned width, unsigned height) {
    if (impl_ && impl_->win)
      glfwSetWindowSize(impl_->win, static_cast<int>(width), static_cast<int>(height));
  }

  std::pair<int, int> GLFWWindow::size() const {
    if (!impl_ || !impl_->win)
      return {0, 0};
    int w = 0, h = 0;
    glfwGetWindowSize(impl_->win, &w, &h);
    return {w, h};
  }

  void GLFWWindow::set_vsync(bool enabled) {
    if (!impl_ || !impl_->win)
      return;
#ifdef __APPLE__
    if (impl_->metal_layer)
      metal_set_display_sync(impl_->metal_layer, enabled ? 1 : 0);
#else
    glfwMakeContextCurrent(impl_->win);
    glfwSwapInterval(enabled ? 1 : 0);
#endif
  }

  void *GLFWWindow::native_handle() const noexcept {
    return impl_ ? impl_->win : nullptr;
  }

  ::GLFWwindow *GLFWWindow::glfw_window() const noexcept {
    return impl_ ? impl_->win : nullptr;
  }

} // namespace corundum::platform::glfw
