#pragma once
#include <corundum/engine.hpp>
#include <corundum/platform/renderer.hpp>
#include <corundum/platform/window.hpp>

#include <corundum/platform/null/null_renderer.hpp>
#include <corundum/platform/null/null_window.hpp>

#include <memory>

namespace corundum::platform::null {

  /** @brief Owned bundle of no-op Window + Renderer for tests.
   *
   * Leaves @c Engine::gpu null (matching production behaviour when no audio
   * backend is present: nothing about the platform is required to be non-null
   * except Window and Renderer — the main loop never touches @c Engine::gpu).
   *
   * adopt_null_platform() moves the bundle's members into the Engine, leaving
   * the bundle empty; it does not need to outlive the Engine afterwards. The
   * Engine holds the objects through no-op deleters (see detail::PlatformDeleter),
   * so their memory is reclaimed by the OS at process exit.
   */
  struct NullPlatform {
    std::unique_ptr<NullWindow> window;
    std::unique_ptr<NullRenderer> renderer;
  };

  /** @brief Construct a NullPlatform bundle with a window of the given size.
   *
   *  @param[in] w  Initial window width in pixels (stored for `size()`).
   *  @param[in] h  Initial window height in pixels.
   *  @return A bundle whose Window/Renderer are ready to adopt.
   */
  [[nodiscard]] inline NullPlatform make_null_platform(unsigned w, unsigned h) {
    NullPlatform p{};
    p.window = std::make_unique<NullWindow>(w, h);
    p.renderer = std::make_unique<NullRenderer>();
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
    // reset(release()), not std::move: Engine's unique_ptrs use a no-op deleter,
    // so they can't be move-assigned from a default_delete.
    engine.window.reset(platform.window.release());
    engine.renderer.reset(platform.renderer.release());
  }

} // namespace corundum::platform::null
