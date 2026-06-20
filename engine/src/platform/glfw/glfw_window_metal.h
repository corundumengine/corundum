#pragma once

// Plain C interface — included by C++ and Objective-C++ files alike.
#ifdef __cplusplus
extern "C" {
#endif

struct GLFWwindow;

/// Attach a CAMetalLayer to the NSWindow backing @p win and return the layer as a void*.
/// The device is created via MTLCreateSystemDefaultDevice. drawableSize is left unset so
/// the layer auto-sizes from its backing view, which correctly handles Retina displays.
/// @pre win must be a valid GLFW window created with GLFW_CLIENT_API = GLFW_NO_API.
void *metal_setup_layer(struct GLFWwindow *win);

/// Return the MTLDevice owned by @p layer_ptr (a CAMetalLayer*) as a const void*.
const void *metal_device(void *layer_ptr);

/// Return the existing CAMetalLayer attached to the NSWindow backing @p win, as void*.
/// Does NOT retain. Caller must not hold the pointer beyond the lifetime of the window.
void *metal_get_layer(struct GLFWwindow *win);

/// Acquire the next CAMetalDrawable from @p layer_ptr (a CAMetalLayer*).
/// Returns a const void* bridged pointer to the drawable. Valid for one frame.
/// @return nullptr if the layer has no drawable ready (skip frame).
const void *metal_next_drawable(void *layer_ptr);

/// Enable or disable display sync on @p layer_ptr (a CAMetalLayer*).
void metal_set_display_sync(void *layer_ptr, int enabled);

#ifdef __cplusplus
}
#endif
