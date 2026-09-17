// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Plain C interface — included by C++ and Objective-C++ files alike.
#ifdef __cplusplus
extern "C" {
#endif

typedef struct MetalLayer MetalLayer;

struct GLFWwindow;

/** @brief Attach a CAMetalLayer to the NSWindow backing @p win and return an owning handle.
 *
 * Drawables follow the backing view's size, so they track the view across resizes. The
 * contentsScale captured here is not GLFW's to maintain: call metal_sync_contents_scale()
 * from the window's content-scale callback, or drawables stop matching the framebuffer on a
 * display with a different scale.
 *
 * @pre @p win must be a valid GLFW window created with GLFW_CLIENT_API = GLFW_NO_API.
 * @return Owning handle, or nullptr if @p win has no content view or Metal is unavailable.
 *         Release with metal_teardown_layer().
 */
MetalLayer *metal_setup_layer(struct GLFWwindow *win);

/** @brief The MTLDevice backing @p layer.
 *
 * @return nullptr if @p layer is null or has no device.
 */
const void *metal_device(const MetalLayer *layer);

/** @brief The next drawable @p layer has for this frame.
 *
 * @note Borrowed, not retained: the layer holds the drawable until the frame is presented.
 * @return nullptr if @p layer is null or has no drawable ready, in which case the frame
 *         should be skipped.
 */
const void *metal_next_drawable(MetalLayer *layer);

/** @brief Wrap the CAMetalLayer already attached to the NSWindow backing @p win.
 *
 * The wrapped layer belongs to the window's content view, so the handle only borrows it.
 *
 * @return Borrowed handle, or nullptr if @p win is invalid or its content view carries no
 *         layer. metal_teardown_layer() frees the wrapper alone.
 */
MetalLayer *metal_get_layer(struct GLFWwindow *win);

/** @brief Enable or disable display sync on @p layer; no-op if @p layer is null. */
void metal_set_display_sync(MetalLayer *layer, int enabled);

/** @brief Match @p layer's contentsScale to the backing scale factor of @p win.
 *
 * No-op if @p layer is null or @p win has no NSWindow.
 */
void metal_sync_contents_scale(MetalLayer *layer, struct GLFWwindow *win);

/** @brief Release the resources @p layer owns and free the handle; no-op if @p layer is null.
 *
 * A handle from metal_setup_layer() releases the CAMetalLayer; one from metal_get_layer()
 * frees the wrapper alone, leaving the window's layer attached.
 */
void metal_teardown_layer(MetalLayer *layer);

#ifdef __cplusplus
}
#endif
