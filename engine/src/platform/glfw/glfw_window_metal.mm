// SPDX-FileCopyrightText: 2026 Gentle Lion Studios, Inc.
// SPDX-License-Identifier: Apache-2.0

#include "glfw_window_metal.h"

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <memory>
#include <new>

struct MetalLayer {
  CAMetalLayer *layer{nullptr};

  bool owns{false};
};

MetalLayer *metal_setup_layer(GLFWwindow *win) {
  NSWindow *const ns_window = glfwGetCocoaWindow(win);
  if (ns_window == nullptr || ns_window.contentView == nullptr)
    return nullptr;

  // Taken before the Metal objects, so a failure below has nothing to unwind.
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

  ns_window.contentView.layer = layer;
  ns_window.contentView.wantsLayer = YES;

  CFRetain((__bridge CFTypeRef)layer);

  handle->layer = layer;
  handle->owns = true;
  return handle.release();
}

const void *metal_device(const MetalLayer *layer) {
  if (layer == nullptr || layer->layer == nullptr)
    return nullptr;

  return (__bridge const void *)layer->layer.device;
}

const void *metal_next_drawable(MetalLayer *layer) {
  if (layer == nullptr || layer->layer == nullptr)
    return nullptr;

  id<CAMetalDrawable> const drawable = [layer->layer nextDrawable];
  return (__bridge const void *)drawable;
}

MetalLayer *metal_get_layer(GLFWwindow *win) {
  NSWindow *const ns_window = glfwGetCocoaWindow(win);
  if (ns_window == nullptr || ns_window.contentView == nullptr || ns_window.contentView.layer == nullptr)
    return nullptr;

  std::unique_ptr<MetalLayer> handle{new (std::nothrow) MetalLayer{}};
  if (handle == nullptr)
    return nullptr;

  handle->layer = (__bridge CAMetalLayer *)ns_window.contentView.layer;
  handle->owns = false;
  return handle.release();
}

void metal_set_display_sync(MetalLayer *layer, int enabled) {
  if (layer == nullptr || layer->layer == nullptr)
    return;

  layer->layer.displaySyncEnabled = (enabled != 0) ? YES : NO;
}

void metal_sync_contents_scale(MetalLayer *layer, GLFWwindow *win) {
  if (layer == nullptr || layer->layer == nullptr)
    return;

  NSWindow *const ns_window = glfwGetCocoaWindow(win);
  if (ns_window == nullptr)
    return;

  layer->layer.contentsScale = ns_window.backingScaleFactor;
}

void metal_teardown_layer(MetalLayer *layer) {
  if (layer == nullptr)
    return;

  if (layer->owns && layer->layer != nullptr)
    CFRelease((__bridge CFTypeRef)layer->layer);

  delete layer;
}
