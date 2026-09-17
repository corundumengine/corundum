// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <corundum/engine.hpp>
#include <corundum/platform/handle.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/window.hpp>

#include <corundum/platform/null/null_renderer.hpp>
#include <corundum/platform/null/null_window.hpp>

#include <memory>
#include <utility>

namespace corundum::platform::null {

  namespace detail {

    // Backend-side destruction for the null bundle: the handle's function pointer
    // deletes the concrete NullWindow/NullRenderer through the interface's virtual
    // destructor, so the engine core never needs those destructors.
    inline void destroy_window(platform::Window *window) noexcept {
      std::default_delete<platform::Window>{}(window);
    }

    inline void destroy_renderer(platform::Renderer *renderer) noexcept {
      std::default_delete<platform::Renderer>{}(renderer);
    }

  } // namespace detail

  /** @brief Owned bundle of no-op Window + Renderer for tests.
   *
   * Carries no @c GpuContext: this backend provides none, and initialize()
   * permits a null @c Engine::gpu because the main loop never touches it.
   *
   * adopt_null_platform() moves the members into the Engine, leaving the bundle
   * empty and safe to destroy at any point afterwards.
   */
  struct NullPlatform {
    /** @brief No-op renderer; every asset load returns the shared dummy handle. */
    Handle<platform::Renderer> renderer;

    /** @brief No-op window of the requested size. */
    Handle<platform::Window> window;
  };

  /** @brief Construct a NullPlatform bundle with a window of the given size.
   *
   *  @param[in] width   Initial window width in pixels (stored for `size()`).
   *  @param[in] height  Initial window height in pixels.
   *  @return A bundle whose Window/Renderer are ready to adopt.
   */
  [[nodiscard]] inline NullPlatform make_null_platform(unsigned width, unsigned height) {
    NullPlatform bundle{};
    bundle.renderer =
        Handle<platform::Renderer>{new NullRenderer(), BackendDeleter<platform::Renderer>{&detail::destroy_renderer}};
    bundle.window = Handle<platform::Window>{new NullWindow(width, height),
                                             BackendDeleter<platform::Window>{&detail::destroy_window}};
    return bundle;
  }

  /** @brief Move the NullPlatform's Window/Renderer into @p engine.
   *
   *  @p platform is left empty and may be destroyed immediately; nothing in the
   *  engine refers back to it. The audio backend and @c Engine::gpu stay null
   *  (initialize() downgrades audio init to a WARN when no backend is present).
   *
   *  @param[in,out] engine Uninitialised Engine.
   *  @param[in,out] platform The bundle; its members are moved out.
   */
  inline void adopt_null_platform(corundum::Engine &engine, NullPlatform &platform) {
    engine.adopt_window(std::move(platform.window));
    engine.adopt_renderer(std::move(platform.renderer));
  }

} // namespace corundum::platform::null
