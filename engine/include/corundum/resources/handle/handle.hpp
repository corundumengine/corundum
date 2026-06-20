#pragma once
#include <corundum/core/containers/handle.hpp>

namespace corundum::resources {

  // Tag types for Handle<T> — incomplete types; no instances are created.
  struct Texture;
  struct Font;
  struct Sprite;
  struct Sheet;

} // namespace corundum::resources

namespace corundum::resources::handle {

  /** @brief Re-export of core::containers::Handle<T> under the resources:: layer.
   *
   * This alias anchors the concept at the correct architecture layer (resources::)
   * while the template itself lives in core::containers to avoid an upward dependency
   * from core::memory::Pool into resources::.
   *
   * Handle<T> is the required currency for all asset references crossing subsystem
   * boundaries. Never pass raw uint32_t IDs or T* pointers between subsystems.
   *
   * @see corundum::core::containers::Handle<T> for the full type documentation.
   * @see corundum::core::memory::Pool<T> for the canonical backing store.
   */
  template <typename T> using Handle = core::containers::Handle<T>;

  // ---------------------------------------------------------------------------
  // Canonical aliases — use these at every subsystem boundary.
  // ---------------------------------------------------------------------------

  /** @brief Handle to a GPU texture resource. */
  using TextureHandle = Handle<resources::Texture>;

  /** @brief Handle to a loaded font resource. */
  using FontHandle = Handle<resources::Font>;

  /** @brief Handle to a logical sprite definition. */
  using SpriteHandle = Handle<resources::Sprite>;

  /** @brief Handle to a sprite sheet atlas. */
  using SheetHandle = Handle<resources::Sheet>;

} // namespace corundum::resources::handle
