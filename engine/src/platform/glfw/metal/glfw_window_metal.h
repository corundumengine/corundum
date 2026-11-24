// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Plain C interface — included by C++ and Objective-C++ files alike.
#ifdef __cplusplus
extern "C" {
#endif

// This header is a plain C interface, so the opaque handles must stay typedefs: `using` is
// not valid C. NOLINTNEXTLINE(modernize-use-using)
typedef struct MetalLayer MetalLayer;

struct GLFWwindow;

/** Attach a CAMetalLayer to the NSWindow backing `win` and return an owning handle.
 *
 *  The layer's drawable size is not tracked automatically: once CAMetalLayer has vended a
 *  drawable it keeps the size it resolved, so pass the framebuffer size to
 *  metal_set_drawable_size() before each frame or resizes render into a stale drawable. The
 *  contentsScale captured here is likewise not GLFW's to maintain: call
 *  metal_sync_contents_scale() from the window's content-scale callback.
 *
 *  @pre `win` must be a valid GLFW window created with GLFW_CLIENT_API = GLFW_NO_API.
 *  @return Owning handle, or nullptr if `win` has no content view or Metal is unavailable.
 *          Release with metal_teardown_layer().
 */
MetalLayer *metal_setup_layer(struct GLFWwindow *win);

/** The MTLDevice backing `layer`.
 *
 *  @return nullptr if `layer` is null or has no device.
 */
const void *metal_device(const MetalLayer *layer);

/** The next drawable `layer` has for this frame.
 *
 *  @post The returned drawable must be released with metal_release_drawable() once the frame
 *        has been submitted.
 *  @return Retained drawable, or nullptr if `layer` is null or has no drawable ready, in
 *          which case the frame should be skipped.
 */
const void *metal_next_drawable(const MetalLayer *layer);

/** Release a drawable from metal_next_drawable(); no-op if `drawable` is null. */
void metal_release_drawable(const void *drawable);

/** Wrap the CAMetalLayer already attached to the NSWindow backing `win`.
 *
 *  The wrapped layer belongs to the window's content view, so the handle only borrows it.
 *
 *  @return Borrowed handle, or nullptr if `win` is invalid or its content view carries no
 *          CAMetalLayer. metal_teardown_layer() frees the wrapper alone.
 */
MetalLayer *metal_get_layer(struct GLFWwindow *win);

/** Enable or disable display sync on `layer`; no-op if `layer` is null. */
void metal_set_display_sync(const MetalLayer *layer, int enabled);

/** Match `layer`'s contentsScale to the backing scale factor of `win`.
 *
 *  No-op if `layer` or `win` is null, or `win` has no NSWindow.
 */
void metal_sync_contents_scale(const MetalLayer *layer, struct GLFWwindow *win);

/** Size `layer`'s drawables in pixels.
 *
 *  Call with the framebuffer size before requesting a drawable each frame; CAMetalLayer does
 *  not keep drawableSize in step with the view on its own. No-op if `layer` is null or either
 *  dimension is <= 0.
 */
void metal_set_drawable_size(const MetalLayer *layer, int width, int height);

/** Tag `layer`'s drawables as sRGB for macOS color matching.
 *
 *  A drawable with no colorspace tag has its values taken as already being in the display's native
 *  space, so a wide-gamut Display P3 panel misreads the engine's sRGB output as P3 and oversaturates
 *  it. No-op if `layer` is null.
 */
void metal_set_colorspace_srgb(const MetalLayer *layer);

/** Release the resources `layer` owns and free the handle; no-op if `layer` is null.
 *
 *  A handle from metal_setup_layer() releases the CAMetalLayer; one from metal_get_layer()
 *  frees the wrapper alone, leaving the window's layer attached.
 */
void metal_teardown_layer(MetalLayer *layer);

#ifdef __cplusplus
}
#endif
