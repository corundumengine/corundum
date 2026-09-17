// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <memory>

namespace corundum::platform {

  /** @brief Deleter that calls a destruction function supplied by the backend that
   *  created the object.
   *
   *  Stored by value in the handle. A named class (rather than a bare function
   *  pointer) so the handle stays default-constructible — unique_ptr disables its
   *  default constructor when the deleter is itself a pointer type.
   *
   *  @note An empty handle is safe to destroy.
   */
  template <typename T> struct BackendDeleter {
    /** @brief Destruction function for the handle's object, or null if none was supplied. */
    void (*destroy)(T *) = nullptr;

    void operator()(T *pointer) const noexcept {
      if (destroy != nullptr)
        destroy(pointer);
    }
  };

  /** @brief Owning handle to a platform object whose destruction is performed by a
   *  function supplied by the backend that created it.
   *
   *  The object is destroyed on scope exit. Because the function is owned by the
   *  backend that built the object, the engine core never references the interface's
   *  destructor symbol. This is what lets the engine library link against the
   *  abstract interfaces without pulling in the rendering backend.
   */
  template <typename T> using Handle = std::unique_ptr<T, BackendDeleter<T>>;

} // namespace corundum::platform
