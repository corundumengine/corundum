// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "glfw_window_metal.h"

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <cstdint>
#include <memory>
#include <new>

struct MetalLayer {
  /// Whether teardown releases the CAMetalLayer or only frees this wrapper.
  enum class Ownership : std::uint8_t {
    Borrowed, ///< The window's content view owns the layer.
    Owned,    ///< This handle holds the layer's sole CF retain.
  };

  CAMetalLayer *layer{nullptr};

  Ownership ownership{Ownership::Borrowed};
};

MetalLayer *metal_setup_layer(GLFWwindow *win) {
  @autoreleasepool {
    if (win == nullptr)
      return nullptr;

    NSWindow *const ns_window = glfwGetCocoaWindow(win);
    if (ns_window == nullptr || ns_window.contentView == nullptr)
      return nullptr;

    // Allocated before the Metal objects, so a failure below has only the handle to unwind.
    std::unique_ptr<MetalLayer> handle{new (std::nothrow) MetalLayer{}};
    if (handle == nullptr)
      return nullptr;

    id<MTLDevice> const device = MTLCreateSystemDefaultDevice();
    if (device == nullptr)
      return nullptr;

    CAMetalLayer *const layer = [CAMetalLayer layer];
    layer.device = device;
    [device release]; // layer.device retains it
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.contentsScale = ns_window.backingScaleFactor;
    layer.magnificationFilter = kCAFilterNearest;
    layer.minificationFilter = kCAFilterNearest;

    ns_window.contentView.layer = layer;
    ns_window.contentView.wantsLayer = YES;

    CFRetain((__bridge CFTypeRef)layer);

    handle->layer = layer;
    handle->ownership = MetalLayer::Ownership::Owned;
    return handle.release();
  }
}

const void *metal_device(const MetalLayer *layer) {
  if (layer == nullptr || layer->layer == nullptr)
    return nullptr;

  return (__bridge const void *)layer->layer.device;
}

const void *metal_next_drawable(const MetalLayer *layer) {
  @autoreleasepool {
    if (layer == nullptr || layer->layer == nullptr)
      return nullptr;

    // Retain so the drawable outlives this function's pool; the caller releases it once the
    // frame has been submitted.
    id<CAMetalDrawable> const drawable = [layer->layer nextDrawable];
    if (drawable == nil)
      return nullptr;

    CFRetain((__bridge CFTypeRef)drawable);
    return (__bridge const void *)drawable;
  }
}

void metal_release_drawable(const void *drawable) {
  if (drawable != nullptr)
    CFRelease((__bridge CFTypeRef)drawable);
}

MetalLayer *metal_get_layer(GLFWwindow *win) {
  @autoreleasepool {
    if (win == nullptr)
      return nullptr;

    NSWindow *const ns_window = glfwGetCocoaWindow(win);
    if (ns_window == nullptr || ns_window.contentView == nullptr)
      return nullptr;

    CALayer *const view_layer = ns_window.contentView.layer;
    if (![view_layer isKindOfClass:[CAMetalLayer class]])
      return nullptr;

    std::unique_ptr<MetalLayer> handle{new (std::nothrow) MetalLayer{}};
    if (handle == nullptr)
      return nullptr;

    handle->layer = (__bridge CAMetalLayer *)view_layer;
    handle->ownership = MetalLayer::Ownership::Borrowed;
    return handle.release();
  }
}

void metal_set_display_sync(const MetalLayer *layer, int enabled) {
  if (layer == nullptr || layer->layer == nullptr)
    return;

  layer->layer.displaySyncEnabled = (enabled != 0) ? YES : NO;
}

void metal_sync_contents_scale(const MetalLayer *layer, GLFWwindow *win) {
  if (layer == nullptr || layer->layer == nullptr || win == nullptr)
    return;

  NSWindow *const ns_window = glfwGetCocoaWindow(win);
  if (ns_window == nullptr)
    return;

  layer->layer.contentsScale = ns_window.backingScaleFactor;
}

void metal_set_drawable_size(const MetalLayer *layer, int width, int height) {
  if (layer == nullptr || layer->layer == nullptr || width <= 0 || height <= 0)
    return;

  const CGSize current = layer->layer.drawableSize;
  if (current.width == static_cast<CGFloat>(width) && current.height == static_cast<CGFloat>(height))
    return;

  layer->layer.drawableSize = CGSizeMake(width, height);
}

// NOLINTNEXTLINE(misc-const-correctness): consumes @p layer and frees it.
void metal_teardown_layer(MetalLayer *layer) {
  @autoreleasepool {
    if (layer == nullptr)
      return;

    if (layer->ownership == MetalLayer::Ownership::Owned && layer->layer != nullptr)
      CFRelease((__bridge CFTypeRef)layer->layer);

    delete layer; // NOLINT(cppcoreguidelines-owning-memory): C API boundary; the caller owns the handle.
  }
}
