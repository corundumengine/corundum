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
   *  @note A default-constructed deleter is inert (null function); destruction is
   *        a no-op unless a function was supplied, so an empty handle is safe.
   */
  template <typename T>
  struct BackendDeleter {
    void (*destroy)(T *) = nullptr;

    void operator()(T *pointer) const noexcept {
      if (destroy != nullptr)
        destroy(pointer);
    }
  };

  /** @brief Owning handle to a platform object whose destruction is performed by a
   *  function supplied by the backend that created it.
   *
   *  The object is destroyed on scope exit. Because the function lives in the backend
   *  translation unit (real or null), the engine core never references the
   *  interface's destructor symbol — only the backend that built the object does.
   *  This is what lets the engine library link against the abstract interfaces
   *  without pulling in GLFW/sokol.
   */
  template <typename T>
  using Handle = std::unique_ptr<T, BackendDeleter<T>>;

} // namespace corundum::platform
