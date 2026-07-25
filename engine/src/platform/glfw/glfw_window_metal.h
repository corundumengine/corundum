#pragma once

// Plain C interface — included by C++ and Objective-C++ files alike.
#ifdef __cplusplus
extern "C" {
#endif

typedef struct metal_layer_t metal_layer_t;

struct GLFWwindow;

/// Attach a CAMetalLayer to the NSWindow backing @p win and return an owning handle.
/// The device is created via MTLCreateSystemDefaultDevice. drawableSize is left unset so
/// the layer auto-sizes from its backing view, which correctly handles Retina displays.
/// @pre win must be a valid GLFW window created with GLFW_CLIENT_API = GLFW_NO_API.
/// @return owning metal_layer_t*, or nullptr if the window is invalid or Metal is unavailable.
///         Call metal_teardown_layer() to release.
metal_layer_t *metal_setup_layer(struct GLFWwindow *win);

/// Return the MTLDevice owned by @p layer as a const void*.
/// @return nullptr if @p layer is null or has no device.
const void *metal_device(metal_layer_t *layer);

/// Acquire the next CAMetalDrawable from @p layer.
/// Returns a const void* bridged pointer to the drawable. Valid for one frame.
/// @return nullptr if the layer has no drawable ready (skip frame), or if @p layer is null.
const void *metal_next_drawable(metal_layer_t *layer);

/// Return a borrowed handle wrapping the existing CAMetalLayer attached to the NSWindow
/// backing @p win. The underlying CAMetalLayer is owned by the NSWindow contentView.
/// @return metal_layer_t* wrapping the existing layer, or nullptr if the window is invalid.
///         Call metal_teardown_layer() to free the wrapper (the CAMetalLayer is NOT released).
metal_layer_t *metal_get_layer(struct GLFWwindow *win);

/// Enable or disable display sync on @p layer.
/// No-op if @p layer is null.
void metal_set_display_sync(metal_layer_t *layer, int enabled);

/// Release all resources owned by @p layer and free the handle.
/// For a handle from metal_setup_layer(): CFReleases the CAMetalLayer, then frees the wrapper.
/// For a handle from metal_get_layer(): only frees the wrapper (CAMetalLayer owned by NSWindow).
/// Safe to call with nullptr (no-op).
void metal_teardown_layer(metal_layer_t *layer);

#ifdef __cplusplus
}
#endif
