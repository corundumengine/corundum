#include "glfw_window_metal.h"

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <cstdlib>

struct metal_layer_t {
  CAMetalLayer *layer;
  bool owns;
};

metal_layer_t *metal_setup_layer(GLFWwindow *win) {
  NSWindow *nswin = glfwGetCocoaWindow(win);
  if (!nswin || !nswin.contentView)
    return nullptr;

  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  if (!device)
    return nullptr;

  CAMetalLayer *layer = [CAMetalLayer layer];
  layer.device = device;
  layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  layer.contentsScale = nswin.backingScaleFactor;

  nswin.contentView.layer = layer;
  nswin.contentView.wantsLayer = YES;

  CFRetain((__bridge CFTypeRef)layer);

  metal_layer_t *handle = (metal_layer_t *)std::malloc(sizeof(metal_layer_t));
  handle->layer = layer;
  handle->owns = true;
  return handle;
}

const void *metal_device(metal_layer_t *layer) {
  if (!layer || !layer->layer)
    return nullptr;
  return (__bridge const void *)layer->layer.device;
}

const void *metal_next_drawable(metal_layer_t *layer) {
  if (!layer || !layer->layer)
    return nullptr;
  id<CAMetalDrawable> drawable = [layer->layer nextDrawable];
  return (__bridge const void *)drawable;
}

metal_layer_t *metal_get_layer(GLFWwindow *win) {
  NSWindow *nswin = glfwGetCocoaWindow(win);
  if (!nswin || !nswin.contentView || !nswin.contentView.layer)
    return nullptr;

  metal_layer_t *handle = (metal_layer_t *)std::malloc(sizeof(metal_layer_t));
  handle->layer = (__bridge CAMetalLayer *)nswin.contentView.layer;
  handle->owns = false;
  return handle;
}

void metal_set_display_sync(metal_layer_t *layer, int enabled) {
  if (!layer || !layer->layer)
    return;
  layer->layer.displaySyncEnabled = enabled ? YES : NO;
}

void metal_teardown_layer(metal_layer_t *layer) {
  if (!layer)
    return;
  if (layer->owns && layer->layer)
    CFRelease((__bridge CFTypeRef)layer->layer);
  std::free(layer);
}
