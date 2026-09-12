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
   * Leaves @c Engine::gpu null (matching production behaviour when no audio
   * backend is present: nothing about the platform is required to be non-null
   * except Window and Renderer — the main loop never touches @c Engine::gpu).
   *
   * adopt_null_platform() moves the bundle's members into the Engine, leaving
   * the bundle empty; it does not need to outlive the Engine afterwards.
   */
  struct NullPlatform {
    Handle<platform::Window> window;
    Handle<platform::Renderer> renderer;
  };

  /** @brief Construct a NullPlatform bundle with a window of the given size.
   *
   *  @param[in] w  Initial window width in pixels (stored for `size()`).
   *  @param[in] h  Initial window height in pixels.
   *  @return A bundle whose Window/Renderer are ready to adopt.
   */
  [[nodiscard]] inline NullPlatform make_null_platform(unsigned w, unsigned h) {
    NullPlatform p{};
    p.window =
        Handle<platform::Window>{new NullWindow(w, h), BackendDeleter<platform::Window>{&detail::destroy_window}};
    p.renderer =
        Handle<platform::Renderer>{new NullRenderer(), BackendDeleter<platform::Renderer>{&detail::destroy_renderer}};
    return p;
  }

  /** @brief Move the NullPlatform's Window/Renderer into @p engine.
   *
   *  Audio backend is left null (initialise() downgrades audio init to WARN
   *  when no backend is present); Engine::gpu is also left null. The platform
   *  members of @p platform are released; the bundle itself can be destroyed
   *  once the engine is.
   *
   *  @param[in,out] engine Uninitialised Engine.
   *  @param[in,out] platform The bundle; its members are moved out.
   */
  inline void adopt_null_platform(corundum::Engine &engine, NullPlatform &platform) {
    engine.adopt_window(std::move(platform.window));
    engine.adopt_renderer(std::move(platform.renderer));
  }

} // namespace corundum::platform::null
