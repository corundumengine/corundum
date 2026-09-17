// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "glfw_window.hpp"
#include "input_translator.hpp"

#include <corundum/input/actions.hpp>

#include <GLFW/glfw3.h>
#include <cstdio>
#include <expected>
#include <memory>
#include <string_view>
#include <utility>

#ifdef __APPLE__
#include "glfw_window_metal.h"
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
      if (s_glfw_refcount.fetch_add(1, std::memory_order_relaxed) == 0 && glfwInit() == GLFW_FALSE) {
        std::println(stderr, "[glfw] glfwInit() failed");
        std::terminate();
      }
    }

    void glfw_term_if_done() {
      if (s_glfw_refcount.fetch_sub(1, std::memory_order_relaxed) == 1)
        glfwTerminate();
    }

    struct WindowData {
      corundum::input::InputState input{};
      JoystickAxisState joystick_axis{};

#ifdef __APPLE__
      MetalLayer *metal_layer{nullptr};
#endif
    };

    void key_callback(GLFWwindow *win, int key, int /*scancode*/, int action, int /*mods*/) noexcept {
      auto *data = static_cast<WindowData *>(glfwGetWindowUserPointer(win));
      if (data != nullptr) {
        translate_key(key, action, data->input);
      }
    }

    void window_close_callback(GLFWwindow *win) noexcept {
      auto *data = static_cast<WindowData *>(glfwGetWindowUserPointer(win));
      if (data != nullptr) {
        data->input.held[static_cast<std::size_t>(corundum::input::Action::Quit)] = true;
      }
    }

    void mouse_button_callback(GLFWwindow *win, int button, int action, int /*mods*/) noexcept {
      auto *data = static_cast<WindowData *>(glfwGetWindowUserPointer(win));
      if (data != nullptr) {
        translate_mouse_button(button, action, data->input);
      }
    }

    void scroll_callback(GLFWwindow *win, double /*xoffset*/, double yoffset) noexcept {
      auto *data = static_cast<WindowData *>(glfwGetWindowUserPointer(win));
      if (data != nullptr) {
        translate_scroll(yoffset, data->input);
      }
    }

#ifdef __APPLE__
    void content_scale_callback(GLFWwindow *win, float /*xscale*/, float /*yscale*/) noexcept {
      const auto *data = static_cast<const WindowData *>(glfwGetWindowUserPointer(win));
      if (data != nullptr && data->metal_layer != nullptr) {
        metal_sync_contents_scale(data->metal_layer, win);
      }
    }
#endif
  } // namespace

  struct GLFWWindow::Impl {
    GLFWwindow *win{nullptr};
    WindowData data{};
#ifdef __APPLE__
    MetalLayer *metal_layer{nullptr};
#endif
  };

  std::expected<std::unique_ptr<GLFWWindow>, WindowError> GLFWWindow::create(unsigned width, unsigned height,
                                                                             std::string_view title) {
    glfw_init_if_needed();

    // If the constructor below throws, release the refcount it was created under; once the
    // window object exists its destructor owns the decrement.
    struct RefGuard {
      bool armed{true};

      RefGuard() = default;
      RefGuard(const RefGuard &) = delete;
      RefGuard &operator=(const RefGuard &) = delete;
      RefGuard(RefGuard &&) = delete;
      RefGuard &operator=(RefGuard &&) = delete;

      ~RefGuard() {
        if (armed)
          glfw_term_if_done();
      }

      void disarm() {
        armed = false;
      }
    } guard;

    // Use 'new' because the constructor is private.
    // The unique_ptr will take ownership immediately.
    auto window = std::unique_ptr<GLFWWindow>(new GLFWWindow(width, height, title));
    guard.disarm();

    if (window->impl_->win == nullptr) {
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

    if (impl_->win != nullptr) {
      glfwSetWindowUserPointer(impl_->win, &impl_->data);
      glfwSetKeyCallback(impl_->win, key_callback);
      glfwSetMouseButtonCallback(impl_->win, mouse_button_callback);
      glfwSetScrollCallback(impl_->win, scroll_callback);
      glfwSetWindowCloseCallback(impl_->win, window_close_callback);

#ifdef __APPLE__
      impl_->metal_layer = metal_setup_layer(impl_->win);
      impl_->data.metal_layer = impl_->metal_layer;
      glfwSetWindowContentScaleCallback(impl_->win, content_scale_callback);
#else
      glfwMakeContextCurrent(impl_->win);
#endif
    }
  }

  GLFWWindow::~GLFWWindow() {
    if (impl_) {
      if (impl_->win != nullptr) {
#ifdef __APPLE__
        if (impl_->metal_layer != nullptr) {
          // Destroying the window below can drive the content-scale callback, so stop it
          // reaching the handle before the handle is freed.
          impl_->data.metal_layer = nullptr;
          metal_teardown_layer(impl_->metal_layer);
        }
#endif
        glfwDestroyWindow(impl_->win);
      }
      glfw_term_if_done();
    }
  }

  GLFWWindow::GLFWWindow(GLFWWindow &&) noexcept = default;

  bool GLFWWindow::is_open() const {
    return impl_ && impl_->win != nullptr && glfwWindowShouldClose(impl_->win) == GLFW_FALSE;
  }

  void GLFWWindow::close() {
    if (impl_ && impl_->win != nullptr)
      glfwSetWindowShouldClose(impl_->win, GLFW_TRUE);
  }

  void GLFWWindow::poll_game_input(corundum::input::InputState &input) {
    corundum::input::clear_pressed(impl_->data.input);
    glfwPollEvents();
    poll_joystick(impl_->data.input, impl_->data.joystick_axis);
    double mx = 0.0;
    double my = 0.0;
    glfwGetCursorPos(impl_->win, &mx, &my);
    impl_->data.input.mouse_x = static_cast<float>(mx);
    impl_->data.input.mouse_y = static_cast<float>(my);
    corundum::input::accumulate_input(input, impl_->data.input);
  }

  std::pair<int, int> GLFWWindow::size() const {
    if (!impl_ || impl_->win == nullptr)
      return {0, 0};
    int w = 0;
    int h = 0;
    glfwGetWindowSize(impl_->win, &w, &h);
    return {w, h};
  }

  void GLFWWindow::set_vsync(bool enabled) {
    if (!impl_ || impl_->win == nullptr)
      return;
#ifdef __APPLE__
    if (impl_->metal_layer != nullptr)
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
