// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "glfw_window.hpp"
#include "input_translator.hpp"
#include "window_graphics.hpp"

#include <corundum/core/window_mode.hpp>
#include <corundum/input/input_mapper.hpp>
#include <corundum/input/physical_input.hpp>
#include <corundum/platform/platform_events.hpp>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace corundum::platform::glfw {

  namespace {
    /// Main-thread-only refcount: the first window brings GLFW up, the last tears it down.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): TU-local, must stay mutable.
    int s_glfw_refcount = 0;

    [[nodiscard]] bool glfw_init_if_needed() {
      if (s_glfw_refcount == 0 && glfwInit() == GLFW_FALSE)
        return false;
      ++s_glfw_refcount;
      return true;
    }

    void glfw_term_if_done() {
      if (--s_glfw_refcount == 0)
        glfwTerminate();
    }

    /// A window's position and size in screen coordinates.
    struct WindowRect {
      int height{};

      int width{};

      int x{};

      int y{};
    };

    struct WindowData {
      /// Engine input sink, set only for the duration of poll_game_input. Callbacks fired by another
      /// event pump (a tool's own glfwPollEvents) find it null and drop the event.
      corundum::input::InputMapper *mapper{nullptr};

      /// Joystick id whose state drives input, or -1 while no gamepad is connected.
      int active_gamepad{-1};

      /// Bit j is set while joystick j is a mapped gamepad; diffed each poll to report hot-plug.
      std::uint32_t gamepads_present{};

      /// Windowed placement saved on entering fullscreen, restored on leaving it.
      WindowRect windowed_rect{};

      corundum::platform::PlatformEvents events{};

      /// Backend graphics seam for this window; null when the backend needs no separate object.
      window_graphics::Context *context{nullptr};
    };

    void key_callback(GLFWwindow *win, int key, int /*scancode*/, int action, int /*mods*/) noexcept {
      const auto *data = static_cast<const WindowData *>(glfwGetWindowUserPointer(win));
      if (data == nullptr || data->mapper == nullptr || action == GLFW_REPEAT)
        return;
      if (const auto engine_key = to_key(key))
        data->mapper->key(*engine_key, action == GLFW_PRESS);
    }

    void window_close_callback(GLFWwindow *win) noexcept {
      auto *data = static_cast<WindowData *>(glfwGetWindowUserPointer(win));
      if (data != nullptr) {
        data->events.quit_requested = true;
      }
    }

    void window_focus_callback(GLFWwindow *win, int focused) noexcept {
      auto *data = static_cast<WindowData *>(glfwGetWindowUserPointer(win));
      if (data == nullptr)
        return;
      if (focused == GLFW_TRUE)
        data->events.focus_gained = true;
      else
        data->events.focus_lost = true;
    }

    void window_iconify_callback(GLFWwindow *win, int iconified) noexcept {
      auto *data = static_cast<WindowData *>(glfwGetWindowUserPointer(win));
      if (data == nullptr)
        return;
      // An iconified window has effectively lost focus; restoring it brings it back.
      if (iconified == GLFW_TRUE)
        data->events.focus_lost = true;
      else
        data->events.focus_gained = true;
    }

    void framebuffer_size_callback(GLFWwindow *win, int /*width*/, int /*height*/) noexcept {
      auto *data = static_cast<WindowData *>(glfwGetWindowUserPointer(win));
      if (data != nullptr) {
        data->events.display_changed = true;
      }
    }

    void mouse_button_callback(GLFWwindow *win, int button, int action, int /*mods*/) noexcept {
      const auto *data = static_cast<const WindowData *>(glfwGetWindowUserPointer(win));
      if (data == nullptr || data->mapper == nullptr)
        return;
      if (const auto engine_button = to_mouse_button(button))
        data->mapper->mouse_button(*engine_button, action == GLFW_PRESS);
    }

    void scroll_callback(GLFWwindow *win, double /*xoffset*/, double yoffset) noexcept {
      const auto *data = static_cast<const WindowData *>(glfwGetWindowUserPointer(win));
      if (data != nullptr && data->mapper != nullptr)
        data->mapper->scroll(static_cast<float>(yoffset));
    }

    /// The bit for @p joystick in a gamepad-presence mask.
    constexpr std::uint32_t joystick_bit(int joystick) noexcept {
      return 1u << static_cast<unsigned>(joystick);
    }

    /// Bitmask of the joystick ids that are currently mapped gamepads.
    std::uint32_t connected_gamepads() noexcept {
      std::uint32_t mask{};
      for (int joystick = GLFW_JOYSTICK_1; joystick <= GLFW_JOYSTICK_LAST; ++joystick) {
        if (glfwJoystickIsGamepad(joystick) == GLFW_TRUE)
          mask |= joystick_bit(joystick);
      }
      return mask;
    }

    /// Report gamepad hot-plug edges and feed the active pad to @p mapper. The active pad stays
    /// active while connected; when it goes, the lowest-numbered remaining pad takes over. Only
    /// mapped gamepads count: an unmapped joystick has no portable button layout to bind.
    void poll_gamepads(WindowData &data, corundum::input::InputMapper &mapper) noexcept {
      std::uint32_t present{connected_gamepads()};
      GLFWgamepadstate state{};
      bool read{false};

      // A pad that vanishes between the presence scan and its state read counts as gone: its
      // disconnect is reported, and the next remaining pad takes over this same poll, so input never
      // drops out while another pad is connected. Each failed read clears a bit, so this terminates.
      while (!read && present != 0) {
        if (data.active_gamepad < 0 || (present & joystick_bit(data.active_gamepad)) == 0)
          data.active_gamepad = std::countr_zero(present);
        read = glfwGetGamepadState(data.active_gamepad, &state) == GLFW_TRUE;
        if (!read)
          present &= ~joystick_bit(data.active_gamepad);
      }
      if (present == 0)
        data.active_gamepad = -1;

      if ((present & ~data.gamepads_present) != 0)
        data.events.controller_connected = true;
      if ((data.gamepads_present & ~present) != 0)
        data.events.controller_disconnected = true;
      data.gamepads_present = present;

      if (!read) {
        mapper.gamepad_absent();
        return;
      }
      mapper.gamepad(to_gamepad_state(state));
    }

    /// The monitor overlapping most of @p win, falling back to the primary monitor.
    GLFWmonitor *monitor_for_window(GLFWwindow *win) noexcept {
      int window_x{};
      int window_y{};
      int window_w{};
      int window_h{};
      glfwGetWindowPos(win, &window_x, &window_y);
      glfwGetWindowSize(win, &window_w, &window_h);

      int count{};
      GLFWmonitor **monitors = glfwGetMonitors(&count);
      GLFWmonitor *best = glfwGetPrimaryMonitor();
      int best_area{0};
      for (GLFWmonitor *monitor : std::span(monitors, static_cast<std::size_t>(count))) {
        const GLFWvidmode *video = glfwGetVideoMode(monitor);
        if (video == nullptr)
          continue;
        int monitor_x{};
        int monitor_y{};
        glfwGetMonitorPos(monitor, &monitor_x, &monitor_y);
        const int overlap_w =
            std::max(0, std::min(window_x + window_w, monitor_x + video->width) - std::max(window_x, monitor_x));
        const int overlap_h =
            std::max(0, std::min(window_y + window_h, monitor_y + video->height) - std::max(window_y, monitor_y));
        if (overlap_w * overlap_h > best_area) {
          best_area = overlap_w * overlap_h;
          best = monitor;
        }
      }
      return best;
    }

    void content_scale_callback(GLFWwindow *win, float /*xscale*/, float /*yscale*/) noexcept {
      const auto *data = static_cast<const WindowData *>(glfwGetWindowUserPointer(win));
      if (data != nullptr)
        window_graphics::sync_content_scale(data->context, win);
    }
  } // namespace

  struct GLFWWindow::Impl {
    GLFWwindow *win{nullptr};
    WindowData data{};
  };

  std::string_view to_string(WindowError error) noexcept {
    switch (error) {
      case WindowError::InitializationFailed:
        return "GLFW initialization failed";
      case WindowError::CreationFailed:
        return "GLFW window creation failed";
    }
    return "unknown GLFW window error";
  }

  std::expected<std::unique_ptr<GLFWWindow>, WindowError> GLFWWindow::create(unsigned width, unsigned height,
                                                                             std::string_view title) {
    if (!glfw_init_if_needed())
      return std::unexpected(WindowError::InitializationFailed);

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
    glfwDefaultWindowHints();

    // Start hidden so the OS never composites an unpainted surface while assets load; a backend
    // whose platform cannot present a hidden window starts visible instead (see window_graphics).
    glfwWindowHint(GLFW_VISIBLE, window_graphics::can_present_hidden() ? GLFW_FALSE : GLFW_TRUE);
    window_graphics::apply_creation_hints();

    impl_->win = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), std::string{title}.c_str(),
                                  nullptr, nullptr);

    if (impl_->win != nullptr) {
      glfwSetWindowUserPointer(impl_->win, &impl_->data);
      glfwSetKeyCallback(impl_->win, key_callback);
      glfwSetMouseButtonCallback(impl_->win, mouse_button_callback);
      glfwSetScrollCallback(impl_->win, scroll_callback);
      glfwSetWindowCloseCallback(impl_->win, window_close_callback);
      glfwSetWindowFocusCallback(impl_->win, window_focus_callback);
      glfwSetWindowIconifyCallback(impl_->win, window_iconify_callback);
      glfwSetFramebufferSizeCallback(impl_->win, framebuffer_size_callback);
      // Pads already connected at startup are not hot-plug events.
      impl_->data.gamepads_present = connected_gamepads();
      // Borderless fullscreen should stay visible behind other windows on alt-tab, not minimise.
      glfwSetWindowAttrib(impl_->win, GLFW_AUTO_ICONIFY, GLFW_FALSE);

      impl_->data.context = window_graphics::create(impl_->win);
      glfwSetWindowContentScaleCallback(impl_->win, content_scale_callback);
    }
  }

  GLFWWindow::~GLFWWindow() {
    if (impl_) {
      if (impl_->win != nullptr) {
        // Destroying the window below can drive the content-scale callback, so drop the context
        // reference before the context is freed.
        window_graphics::Context *context = impl_->data.context;
        impl_->data.context = nullptr;
        window_graphics::destroy(context);
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

  void GLFWWindow::show() {
    if (impl_ && impl_->win != nullptr) {
      glfwShowWindow(impl_->win);
      // Take focus on first reveal so the game is frontmost. An inactive window is throttled by the
      // OS, which shows up as judder.
      glfwFocusWindow(impl_->win);
    }
  }

  void GLFWWindow::poll_game_input(corundum::input::InputMapper &mapper, corundum::platform::PlatformEvents &events) {
    if (!impl_ || impl_->win == nullptr)
      return;

    impl_->data.mapper = &mapper;
    glfwPollEvents();
    impl_->data.mapper = nullptr;

    poll_gamepads(impl_->data, mapper);

    double cursor_x{};
    double cursor_y{};
    glfwGetCursorPos(impl_->win, &cursor_x, &cursor_y);
    mapper.cursor(static_cast<float>(cursor_x), static_cast<float>(cursor_y));

    corundum::platform::merge_events(events, impl_->data.events);
    impl_->data.events = {};
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
    window_graphics::set_vsync(impl_->data.context, impl_->win, enabled);
  }

  std::string GLFWWindow::input_label(corundum::input::PhysicalInput input) const {
    if (input.device == corundum::input::InputDevice::Keyboard) {
      // Layout-aware for printable keys only; GLFW returns null for the rest.
      if (const char *name = glfwGetKeyName(static_cast<int>(input.code), 0); name != nullptr) {
        std::string label{name};
        if (label.size() == 1 && label[0] >= 'a' && label[0] <= 'z')
          label[0] = static_cast<char>(label[0] - 'a' + 'A');
        return label;
      }
    }
    return std::string{corundum::input::name_of(input)};
  }

  void GLFWWindow::set_window_mode(corundum::core::WindowMode mode) {
    if (!impl_ || impl_->win == nullptr || mode == window_mode())
      return;

    GLFWwindow *win = impl_->win;
    WindowRect &rect = impl_->data.windowed_rect;
    if (mode == corundum::core::WindowMode::Fullscreen) {
      GLFWmonitor *monitor = monitor_for_window(win);
      const GLFWvidmode *video = monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
      if (video == nullptr)
        return;
      glfwGetWindowPos(win, &rect.x, &rect.y);
      glfwGetWindowSize(win, &rect.width, &rect.height);
      // Borderless: request the monitor's current mode exactly, refresh rate included, so GLFW keeps
      // it. GLFW_DONT_CARE for the rate would let GLFW pick the closest mode by size, which can be a
      // different rate, and that is a mode switch.
      glfwSetWindowMonitor(win, monitor, 0, 0, video->width, video->height, video->refreshRate);
    } else {
      glfwSetWindowMonitor(win, nullptr, rect.x, rect.y, rect.width, rect.height, GLFW_DONT_CARE);
    }
    impl_->data.events.display_changed = true;
  }

  corundum::core::WindowMode GLFWWindow::window_mode() const {
    if (!impl_ || impl_->win == nullptr)
      return corundum::core::WindowMode::Windowed;
    return glfwGetWindowMonitor(impl_->win) != nullptr ? corundum::core::WindowMode::Fullscreen
                                                       : corundum::core::WindowMode::Windowed;
  }

  void *GLFWWindow::native_handle() const noexcept {
    return impl_ ? impl_->win : nullptr;
  }

} // namespace corundum::platform::glfw
